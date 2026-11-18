// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QtTest/QtTest>

#include <qml/util.h>
#include <qml/startupeffects.h>

#include <logging.h>

#include <string>
#include <vector>
#include <stdexcept>

#include <QGuiApplication>
#include <QScreen>
#include <QSysInfo>
#include <QThread>
#include <QString>
#include <QScopeGuard>

namespace {
std::vector<std::string> CaptureQtInfo()
{
    const auto info = QmlUtil::CaptureQtInfo();
    auto* gui_thread = QThread::currentThread();
    execution::Executor executor{execution::Executor::Config{}};
    auto core = executor.makeQueue(execution::Lane::Core);
    auto outcomes = executor.makeQueue(execution::Lane::Utility);
    auto lines = startup::Run(&core, &outcomes, [info, gui_thread] {
        std::vector<std::string> lines;
        bool wrote_on_gui{false};
        LogInstance().m_print_to_file = false;
        LogInstance().m_print_to_console = false;
        const auto callback = LogInstance().PushBackCallback([&](const std::string& str) {
            wrote_on_gui |= QThread::currentThread() == gui_thread;
            lines.push_back(str);
        });
        const auto disconnect = qScopeGuard([callback] {
            LogInstance().DeleteCallback(callback);
            LogInstance().DisconnectTestLogger();
        });
        LogInstance().StartLogging();
        QmlUtil::LogQtInfo(info);
        if (wrote_on_gui) throw std::runtime_error{"Qt diagnostics wrote on GUI"};
        return lines;
    });
    executor.stopAndJoin();
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
#include <test_qtinfolog.moc>
