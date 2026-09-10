// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <logging.h>
#include <qml/qtinfo.h>
#include <qml/test/integration_test_registry.h>

#include <QGuiApplication>
#include <QQuickStyle>
#include <QScopeGuard>
#include <QScreen>
#include <QSysInfo>
#include <QTest>

#include <mutex>
#include <string>
#include <string_view>

class QtInfoIntegrationTests : public QObject
{
    Q_OBJECT
public:
    explicit QtInfoIntegrationTests(BitcoinQmlApplication&) {}

private Q_SLOTS:
    void logsRuntimeEnvironmentThroughCore()
    {
        std::string captured;
        std::mutex mutex;
        {
            const auto callback = LogInstance().PushBackCallback([&](const std::string& line) {
                const std::lock_guard lock(mutex);
                captured += line;
            });
            const auto remove = qScopeGuard([&] { LogInstance().DeleteCallback(callback); });
            QmlDiagnostics::LogQtInfo();
        }
        const auto contains = [&](std::string_view text) { return captured.find(text) != std::string::npos; };
        QVERIFY(contains(std::string("Qt ") + qVersion()));
#ifdef QT_STATIC
        QVERIFY(contains("(static)"));
#else
        QVERIFY(contains("(dynamic)"));
#endif
        QVERIFY(contains("plugin=" + QGuiApplication::platformName().toStdString()));
        QVERIFY(contains("Static plugins:") || contains("No static plugins."));
        QVERIFY(contains("Qt Quick Controls style: " + QQuickStyle::name().toStdString()));
        QVERIFY(contains("System: " + QSysInfo::prettyProductName().toStdString()));
        for (const QScreen* screen : QGuiApplication::screens()) {
            QVERIFY(contains("Screen: " + screen->name().toStdString()));
        }
    }
};

BITCOINQML_REGISTER_INTEGRATION_TEST(QtInfoIntegrationTests)
#include <test_qtinfo_integration.moc>
