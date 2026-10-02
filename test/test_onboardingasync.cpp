// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QtTest/QtTest>

#include <chainparams.h>
#include <qml/models/onboardingoptionsmodel.h>
#include <qml/onboarding_settings.h>

#include <QDir>
#include <QFile>
#include <QScopeGuard>
#include <QSemaphore>
#include <QTemporaryDir>
#include <QTimer>

#include <atomic>
#include <thread>

#ifdef Q_OS_UNIX
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace {
bool WriteFile(const QString& path, const QByteArray& contents)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size();
}

std::vector<std::string> Arguments(const QString& path)
{
    return {"bitcoin-qt", "-regtest", "-datadir=" + path.toStdString()};
}
} // namespace

class OnboardingAsyncTests : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void previewReadsOwnNetworkWithoutChangingGlobalParams();
    void startupStatusReadsOwnNetworkWithoutChangingGlobalParams();
    void blockedConfigReadKeepsGuiResponsiveAndRejectsStaleSelection();
};

void OnboardingAsyncTests::previewReadsOwnNetworkWithoutChangingGlobalParams()
{
    SelectParams(ChainType::MAIN);
    const auto* live_params = &Params();
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QVERIFY(QDir(directory.path()).mkpath("regtest"));
    QVERIFY(WriteFile(directory.filePath("settings.json"), R"({"listen":true})"));
    QVERIFY(WriteFile(directory.filePath("regtest/settings.json"), R"({"listen":false})"));
    const auto preview = QmlOnboardingSettings::Preview(Arguments(directory.path()), false, directory.path());
    QVERIFY2(preview.ok, qPrintable(preview.error));
    QVERIFY(!preview.values.listen);
    QVERIFY(preview.profile.has_settings_file);
    QCOMPARE(&Params(), live_params);
    QCOMPARE(Params().GetChainType(), ChainType::MAIN);
}

void OnboardingAsyncTests::startupStatusReadsOwnNetworkWithoutChangingGlobalParams()
{
    SelectParams(ChainType::MAIN);
    const auto* live_params = &Params();
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QVERIFY(QDir(directory.path()).mkpath("regtest"));
    QVERIFY(WriteFile(directory.filePath("settings.json"), R"({"qml_onboarded":false})"));
    QVERIFY(WriteFile(directory.filePath("regtest/settings.json"), R"({"qml_onboarded":true})"));
    const auto status = QmlOnboardingSettings::ResolveOnboardingStartupStatus(Arguments(directory.path()), false);
    QVERIFY2(status.ok, qPrintable(status.error));
    QVERIFY(status.qml_onboarded);
    QCOMPARE(&Params(), live_params);
    QCOMPARE(Params().GetChainType(), ChainType::MAIN);
}

void OnboardingAsyncTests::blockedConfigReadKeepsGuiResponsiveAndRejectsStaleSelection()
{
#ifndef Q_OS_UNIX
    QSKIP("The gated filesystem fixture uses a POSIX FIFO.");
#else
    SelectParams(ChainType::MAIN);
    QTemporaryDir first;
    QTemporaryDir latest;
    QVERIFY(first.isValid());
    QVERIFY(latest.isValid());
    const QByteArray pipe = QFile::encodeName(first.filePath("bitcoin.conf"));
    const QByteArray replacement = QFile::encodeName(first.filePath("bitcoin.conf.next"));
    QVERIFY(WriteFile(QString::fromLocal8Bit(replacement), "regtest=1\n"));
    QVERIFY(::mkfifo(pipe.constData(), 0600) == 0);

    QSemaphore entered;
    QSemaphore release;
    std::atomic<bool> stop{false};
    std::thread writer([&] {
        int fd{-1};
        while (!stop && fd < 0) {
            fd = ::open(pipe.constData(), O_WRONLY | O_NONBLOCK);
            if (fd < 0) QThread::msleep(1);
        }
        if (fd < 0) return;
        entered.release();
        // A finite watchdog also releases a regressed synchronous constructor.
        release.tryAcquire(1, 10000);
        ::rename(replacement.constData(), pipe.constData());
        constexpr char config[]{"regtest=1\n"};
        const auto written = ::write(fd, config, sizeof(config) - 1);
        Q_UNUSED(written);
        ::close(fd);
    });
    const auto finish_writer = qScopeGuard([&] {
        stop = true;
        release.release();
        writer.join();
    });
    OnboardingOptionsModel model(Arguments(first.path()), false);
    QVERIFY(model.validationPending());
    QTRY_VERIFY(entered.available() > 0);
    entered.acquire();
    bool heartbeat{false};
    QTimer::singleShot(0, &model, [&] { heartbeat = true; });
    QTRY_VERIFY(heartbeat);
    QVERIFY(!model.canFinish());
    QVERIFY(model.selectCustomDataDir(latest.path()));
    release.release();
    QTRY_VERIFY_WITH_TIMEOUT(!model.validationPending(), 10000);
    QCOMPARE(model.dataDir(), latest.path());
    QCOMPARE(Params().GetChainType(), ChainType::MAIN);
    QSignalSpy drained(&model, &OnboardingOptionsModel::shutdownFinished);
    model.beginShutdown();
    QTRY_COMPARE(drained.count(), 1);
#endif
}

#ifdef BITCOINQML_NO_TEST_MAIN
#include <test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(OnboardingAsyncTests)
#else
QTEST_MAIN(OnboardingAsyncTests)
#endif
#include "test_onboardingasync.moc"
