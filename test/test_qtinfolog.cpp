// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QtTest/QtTest>

#include <qml/util.h>

#include <logging.h>

#include <string>
#include <vector>

#include <QGuiApplication>
#include <QString>

namespace {
std::vector<std::string> CaptureQtInfo()
{
    std::vector<std::string> lines;
    LogInstance().m_print_to_file = false;
    LogInstance().m_print_to_console = false;
    const auto callback = LogInstance().PushBackCallback([&lines](const std::string& str) {
        lines.push_back(str);
    });
    LogInstance().StartLogging();

    QmlUtil::LogQtInfo();

    LogInstance().DeleteCallback(callback);
    LogInstance().DisconnectTestLogger();
    return lines;
}

bool ContainsSubstring(const std::vector<std::string>& lines, const std::string& needle)
{
    for (const std::string& line : lines) {
        if (line.find(needle) != std::string::npos) return true;
    }
    return false;
}
} // namespace

class QtInfoLogTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void logsQtVersionAndPlatform();
    void logsPluginsStyleSystemAndScreens();
};

void QtInfoLogTests::logsQtVersionAndPlatform()
{
    const std::vector<std::string> lines{CaptureQtInfo()};

    QVERIFY(!lines.empty());
    QVERIFY(ContainsSubstring(lines, std::string{"Qt "} + qVersion()));
#ifdef QT_STATIC
    QVERIFY(ContainsSubstring(lines, "(static)"));
#else
    QVERIFY(ContainsSubstring(lines, "(dynamic)"));
#endif
    QVERIFY(ContainsSubstring(lines, "plugin=" + QGuiApplication::platformName().toStdString()));
}

void QtInfoLogTests::logsPluginsStyleSystemAndScreens()
{
    const std::vector<std::string> lines{CaptureQtInfo()};

    QVERIFY(ContainsSubstring(lines, "static plugins") || ContainsSubstring(lines, "Static plugins:"));
    QVERIFY(ContainsSubstring(lines, "Qt Quick Controls style:"));
    QVERIFY(ContainsSubstring(lines, "System: " + QSysInfo::prettyProductName().toStdString()));
    for (const QScreen* screen : QGuiApplication::screens()) {
        QVERIFY(ContainsSubstring(lines, "Screen: " + screen->name().toStdString()));
    }
}

#ifdef BITCOINQML_NO_TEST_MAIN
#include <test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(QtInfoLogTests)
#else
QTEST_MAIN(QtInfoLogTests)
#endif
#include "test_qtinfolog.moc"
