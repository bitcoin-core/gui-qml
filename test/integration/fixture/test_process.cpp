// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/integration/fixture/test_process.h>
#include <test/integration/runner/test_cases.h>
#include <util/subprocess.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QMessageLogger>
#include <QSet>
#include <QTemporaryDir>
#include <QXmlStreamReader>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
QByteArray ReadFile(const QString& path)
{
    QFile file{path};
    if (!file.open(QIODevice::ReadOnly)) {
        throw std::runtime_error{"Cannot read " + path.toStdString() + ": " + file.errorString().toStdString()};
    }
    return file.readAll();
}

IntegrationTestResult ReadResult(const QString& path, const QString& selector)
{
    QXmlStreamReader xml{ReadFile(path)};
    IntegrationTestResult result;
    const auto function = selector.section(':', 0, 0);
    const auto expected_tag = selector.section(':', 1);
    bool suite{false};
    bool test{false};
    bool failed{false};
    bool passed_test{false};
    bool skipped{false};
    while (!xml.atEnd()) {
        xml.readNext();
        if (!xml.isStartElement()) continue;
        if (xml.name() == u"TestCase") {
            suite = true;
        } else if (xml.name() == u"TestFunction") {
            test = xml.attributes().value(QStringLiteral("name")) == function;
        } else if (xml.name() == u"Incident" || xml.name() == u"Message") {
            const bool message = xml.name() == u"Message";
            const auto type = xml.attributes().value(QStringLiteral("type")).toString();
            const auto file = xml.attributes().value(QStringLiteral("file")).toString();
            const int line = xml.attributes().value(QStringLiteral("line")).toInt();
            QString tag;
            QString description;
            while (xml.readNextStartElement()) {
                if (xml.name() == u"DataTag") {
                    tag = xml.readElementText();
                } else if (xml.name() == u"Description") {
                    description = xml.readElementText();
                } else {
                    xml.skipCurrentElement();
                }
            }
            if (test && !message && type == "pass" && tag != expected_tag) {
                failed = true;
                result.details += "Unexpected child data row: " + tag + '\n';
            }
            if (test && type == "pass") passed_test = true;
            const bool failure = type == "fail" || type == "xpass";
            const bool skip = type == "skip";
            if (failure || skip) {
                if (result.source_file.isEmpty() || (failure && !failed)) {
                    result.source_file = file;
                    result.source_line = line;
                }
                failed |= failure;
                skipped |= skip;
                result.details += description + '\n';
            } else if (message || type == "xfail") {
                result.messages.push_back({type, description, file, line});
            }
        }
    }
    if (xml.hasError() || !suite) {
        result.details += "Missing or invalid child test result: " + xml.errorString() + '\n';
    } else if (!failed && passed_test) {
        result.status = IntegrationTestStatus::Passed;
    } else if (!failed && skipped) {
        result.status = IntegrationTestStatus::Skipped;
    } else if (result.details.isEmpty()) {
        result.details = "The child did not report a completed test\n";
    }
    return result;
}

void AppendCapturedOutput(const QString& path, const std::vector<char>& buffer, size_t length)
{
    if (length == 0) return;
    QFile file{path};
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append) ||
        file.write(buffer.data(), static_cast<qint64>(length)) != static_cast<qint64>(length)) {
        throw std::runtime_error{"Cannot write test diagnostics: " + path.toStdString()};
    }
}

IntegrationTestResult RunChild(const IntegrationTestModule& module, WalletMode mode,
                               const QString& selector, const QString& profile_dir,
                               const QStringList& options, bool discover,
                               const std::function<IntegrationTestResult(const QDir&)>& read_result)
{
    const QDir profiles{profile_dir.isEmpty() ? QDir::tempPath() : profile_dir};
    if (!profiles.mkpath(".")) return {IntegrationTestStatus::Failed, "Cannot create profile directory: " + profiles.absolutePath()};
    // Data tags may contain slashes or characters forbidden in Windows paths.
    const QString name = QString::fromUtf8(module.name) + "." + WalletModeName(mode) + "." +
                         (discover ? "discovery" : selector.section(':', 0, 0));
    QTemporaryDir profile{profiles.absoluteFilePath("bitcoinqml-" + name + "-XXXXXX")};
    if (!profile.isValid()) return {IntegrationTestStatus::Failed, "Cannot create profile: " + profile.errorString()};
    profile.setAutoRemove(false);
    IntegrationTestResult result;
    int exit_status{EXIT_SUCCESS};
    try {
        std::vector<std::string> arguments{
            QCoreApplication::applicationFilePath().toStdString(),
            std::string{"--module="} + module.name,
            std::string{"--wallet="} + WalletModeName(mode),
            "--test-profile=" + profile.path().toStdString(),
        };
        if (discover) {
            arguments.emplace_back("--test-discover");
        } else {
            arguments.push_back("--test=" + selector.toStdString());
            for (const auto& option : options) arguments.push_back(option.toStdString());
        }
        subprocess::Popen process{arguments, subprocess::input{subprocess::PIPE},
                                  subprocess::output{subprocess::PIPE}, subprocess::error{subprocess::PIPE}};
        const auto [output, error] = process.communicate();
        exit_status = process.retcode();
        // Preserve loader/argument errors emitted before the child redirects its
        // streams. Normal child logs already exist and must not be overwritten.
        AppendCapturedOutput(profile.filePath("stdout.txt"), output.buf, output.length);
        AppendCapturedOutput(profile.filePath("stderr.txt"), error.buf, error.length);
        result = read_result(QDir{profile.path()});
    } catch (const std::exception& error) {
        result.status = IntegrationTestStatus::Failed;
        result.details += QString::fromUtf8(error.what());
    }
    if (exit_status != EXIT_SUCCESS) {
        result.status = IntegrationTestStatus::Failed;
        result.details.prepend(QString{"Child exited with status %1\n"}.arg(exit_status));
    }
    for (const auto* filename : {"stdout.txt", "stderr.txt"}) {
        QFile file{profile.filePath(filename)};
        if (!file.exists()) continue;
        if (!file.open(QIODevice::ReadOnly)) {
            result.status = IntegrationTestStatus::Failed;
            result.details += "\nCannot read diagnostics: " + file.fileName();
            continue;
        }
        const auto text = QString::fromUtf8(file.readAll());
        if (text.isEmpty()) continue;
        const QString diagnostic = QString::fromLatin1(filename) + ":\n" + text;
        if (result.status != IntegrationTestStatus::Failed) {
            result.messages.push_back({"qinfo", diagnostic, {}, 0});
        } else {
            result.details += '\n' + diagnostic;
        }
    }
    profile.setAutoRemove(result.status != IntegrationTestStatus::Failed);
    if (result.status == IntegrationTestStatus::Failed) result.details += "\nRetained failed profile: " + profile.path();
    return result;
}

bool RedirectStream(const QString& path, FILE* stream)
{
#ifdef Q_OS_WIN
    if (!_wfreopen(path.toStdWString().c_str(), L"w", stream)) return false;
#else
    if (!std::freopen(QFile::encodeName(path).constData(), "w", stream)) return false;
#endif
    return std::setvbuf(stream, nullptr, _IONBF, 0) == 0;
}
} // namespace

bool PrepareIntegrationChild(const QString& profile, bool discover)
{
    const QDir directory{profile};
    if (!RedirectStream(directory.filePath("stderr.txt"), stderr)) return false;
    if (!RedirectStream(directory.filePath(discover ? "test-cases.txt" : "stdout.txt"), stdout)) return false;
    // Qt 6.2's -o parser cannot handle commas in a filename. Relative result
    // filenames also work when the profile's absolute path contains commas.
    return QDir::setCurrent(directory.absolutePath());
}

IntegrationTestDiscovery DiscoverIntegrationTests(const IntegrationTestModule& module, WalletMode mode,
                                                  const QString& class_name, const QStringList& functions,
                                                  const QString& profile_dir)
{
    IntegrationTestDiscovery discovery;
    discovery.result = RunChild(module, mode, {}, profile_dir, {}, true, [&](const QDir& profile) {
        const auto output = QString::fromUtf8(ReadFile(profile.filePath("test-cases.txt")));
        QSet<QString> selectors;
        const QString prefix = class_name + ' ';
        for (auto line : output.split('\n', Qt::SkipEmptyParts)) {
            // freopen opens stdout in text mode on Windows. Preserve spaces
            // in data tags while removing its CRLF line ending.
            if (line.endsWith('\r')) line.chop(1);
            if (!line.startsWith(prefix)) continue;
            const auto entry = line.mid(prefix.size());
            const auto function = entry.section(' ', 0, 0);
            if (!functions.contains(function)) continue;
            const auto tag = entry.contains(' ') ? entry.mid(function.size() + 1) : QString{};
            if (tag.startsWith("__global__ ") || tag.contains(" __global__ ")) {
                return IntegrationTestResult{IntegrationTestStatus::Failed, "Global test data cannot be selected independently on Qt 6.2; use per-function data rows"};
            }
            const QString selector = function + (tag.isEmpty() ? QString{} : ':' + tag);
            if (selectors.contains(selector)) return IntegrationTestResult{IntegrationTestStatus::Failed, "Duplicate test data row: " + selector};
            selectors.insert(selector);
            discovery.tests.push_back(selector);
        }
        for (const auto& function : functions) {
            const auto found = std::any_of(discovery.tests.begin(), discovery.tests.end(), [&](const QString& selector) {
                return selector.section(':', 0, 0) == function;
            });
            if (!found) return IntegrationTestResult{IntegrationTestStatus::Failed, "No discovered rows for test function: " + function};
        }
        return IntegrationTestResult{IntegrationTestStatus::Passed, {}};
    });
    return discovery;
}

IntegrationTestResult RunIntegrationTest(const IntegrationTestModule& module, WalletMode mode,
                                         const QString& selector, const QString& profile_dir,
                                         const QStringList& options)
{
    return RunChild(module, mode, selector, profile_dir, options, false, [&](const QDir& profile) {
        return ReadResult(profile.filePath("qt-results.xml"), selector);
    });
}

void ReportIntegrationMessages(const IntegrationTestResult& result)
{
    for (const auto& message : result.messages) {
        const auto file = message.source_file.toUtf8();
        auto description = message.text;
        // Qt Test drops a Qt message's context from its structured fields.
        // Keep locations from child assertions and warnings in the text too.
        if (!message.source_file.isEmpty()) {
            description += QString{"\n   Loc: [%1(%2)]"}.arg(message.source_file).arg(message.source_line);
        }
        const auto text = description.toUtf8();
        const QMessageLogger logger{file.constData(), message.source_line, nullptr};
        if (message.type == "qdebug") {
            logger.debug("%s", text.constData());
        } else if (message.type == "qwarn" || message.type == "warn") {
            logger.warning("%s", text.constData());
        } else if (message.type == "qcritical" || message.type == "qfatal") {
            logger.critical("%s", text.constData());
        } else {
            logger.info("%s", text.constData());
        }
    }
}
