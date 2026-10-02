// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/options_model.h>
#include <test/mocks/mocknode.h>
#include <chainparams.h>
#include <init.h>
#include <util/translation.h>

#include <QDir>
#include <QSemaphore>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QtTest/QtTest>

#include <atomic>

#ifndef BITCOINQML_NO_TEST_MAIN
const TranslateFn G_TRANSLATION_FUN{nullptr};
#endif

namespace {
struct ReleaseSemaphore {
    QSemaphore& semaphore;
    ~ReleaseSemaphore() { semaphore.release(); }
};

struct Fixture {
    QTemporaryDir directory;
    ArgsManager args;
    MockNode node;
    QVariantMap saved_settings;

    Fixture()
    {
        QSettings settings;
        for (const auto& key : settings.allKeys()) saved_settings.insert(key, settings.value(key));
        SetupServerArgs(args, false);
        args.ForceSetArg("-datadir", directory.path().toStdString());
        args.ForceSetArg("-settings", directory.filePath("settings.json").toStdString());
        SelectParams(ChainType::REGTEST);
        args.SelectConfigNetwork("regtest");
        node.get_persistent_setting_fn = [this](const std::string& name) { return args.GetPersistentSetting(name); };
    }
    ~Fixture()
    {
        QSettings settings;
        settings.clear();
        for (auto it = saved_settings.cbegin(); it != saved_settings.cend(); ++it) settings.setValue(it.key(), it.value());
        settings.sync();
    }
};
} // namespace

class AsyncOptionsTests : public QObject
{
    Q_OBJECT
    QString m_organization;
    QString m_application;
private Q_SLOTS:
    void initTestCase()
    {
        m_organization = QCoreApplication::organizationName();
        m_application = QCoreApplication::applicationName();
        QCoreApplication::setOrganizationName(QStringLiteral("BitcoinQmlTests"));
        QCoreApplication::setApplicationName(QStringLiteral("AsyncOptions"));
    }
    void cleanupTestCase()
    {
        QCoreApplication::setOrganizationName(m_organization);
        QCoreApplication::setApplicationName(m_application);
    }

    void blockedInitialReadLeavesGuiAndGettersResponsive()
    {
        Fixture fixture;
        QSemaphore gate;
        std::atomic<bool> entered{false};
        std::atomic<bool> gui_call{false};
        std::atomic<int> calls{0};
        QThread* gui = QThread::currentThread();
        fixture.node.get_persistent_setting_fn = [&](const std::string& name) {
            if (QThread::currentThread() == gui) gui_call = true;
            if (++calls == 1) { entered = true; gate.acquire(); }
            return fixture.args.GetPersistentSetting(name);
        };
        OptionsQmlModel model(fixture.node, fixture.args);
        ReleaseSemaphore release{gate};
        QTRY_VERIFY(entered.load());
        QVERIFY(!model.settingsReady());
        int heartbeats{0};
        QTimer timer;
        connect(&timer, &QTimer::timeout, [&] { ++heartbeats; });
        timer.start(0);
        QTRY_VERIFY(heartbeats > 2);
        for (int i = 0; i < 10; ++i) {
            model.coreSettingStatus(QStringLiteral("unknown"));
            model.coreSettingStatuses();
            model.dataDir();
            model.externalSignerPathValidationError(QStringLiteral("/missing"));
            model.validateCustomDataDir(QStringLiteral("/missing"));
        }
        QCOMPARE(calls.load(), 1);
        gate.release();
        QTRY_VERIFY(model.settingsReady());
        QVERIFY(!gui_call.load());
        const int loaded_calls = calls.load();
        model.coreSettingStatus(QStringLiteral("unknown"));
        model.coreSettingStatuses();
        QCOMPARE(calls.load(), loaded_calls);
    }

    void failedWriteRestoresCacheAndPersistentState()
    {
        Fixture fixture;
        OptionsQmlModel model(fixture.node, fixture.args);
        QTRY_VERIFY(model.settingsReady());
        const int initial = model.maxMempoolSizeMB();
        const QString blocked = fixture.directory.filePath("blocked-settings");
        QVERIFY(QDir().mkpath(blocked));
        fixture.args.ForceSetArg("-settings", blocked.toStdString());
        model.setMaxMempoolSizeMB(initial + 10);
        QVERIFY(model.settingsPending());
        QTRY_VERIFY(!model.settingsPending());
        QVERIFY(!model.settingsError().isEmpty());
        QCOMPARE(model.maxMempoolSizeMB(), initial);
        QVERIFY(fixture.args.GetPersistentSetting("maxmempool").isNull());
        QVERIFY(!model.mempoolSettingsDirty());
    }

    void mappingDrainDoesNotBlockGuiOrAcceptMoreCommands()
    {
        Fixture fixture;
        QSemaphore gate;
        std::atomic<bool> entered{false};
        std::atomic<bool> gui_call{false};
        std::atomic<int> calls{0};
        QThread* gui = QThread::currentThread();
        fixture.node.map_port_fn = [&](bool) {
            gui_call = QThread::currentThread() == gui;
            ++calls;
            entered = true;
            gate.acquire();
        };
        OptionsQmlModel model(fixture.node, fixture.args);
        ReleaseSemaphore release{gate};
        QTRY_VERIFY(model.settingsReady());
        const bool initial = model.natpmp();
        model.setNatpmp(!initial);
        QTRY_VERIFY(entered.load());
        QVERIFY(model.settingsPending());
        QSignalSpy drained(&model, &OptionsQmlModel::shutdownFinished);
        model.beginShutdown();
        model.setNatpmp(initial);
        int heartbeats{0};
        QTimer timer;
        connect(&timer, &QTimer::timeout, [&] { ++heartbeats; });
        timer.start(0);
        QTRY_VERIFY(heartbeats > 2);
        QCOMPARE(drained.size(), 0);
        gate.release();
        QTRY_COMPARE(drained.size(), 1);
        QCOMPARE(calls.load(), 1);
        QVERIFY(!gui_call.load());
    }

    void queuedTogglesKeepLastRequestedValue()
    {
        Fixture fixture;
        QSemaphore gate;
        std::atomic<int> calls{0};
        fixture.node.map_port_fn = [&](bool) {
            if (++calls == 1) gate.acquire();
        };
        OptionsQmlModel model(fixture.node, fixture.args);
        ReleaseSemaphore release{gate};
        QTRY_VERIFY(model.settingsReady());
        const bool initial = model.natpmp();
        model.setNatpmp(!initial);
        QTRY_COMPARE(calls.load(), 1);
        model.setNatpmp(initial);
        QCOMPARE(model.natpmp(), initial);
        gate.release();
        QTRY_VERIFY(!model.settingsPending());
        QCOMPARE(calls.load(), 2);
        QCOMPARE(model.natpmp(), initial);
        QVERIFY(model.settingsError().isEmpty());
        QVERIFY(!model.restartRequired());
    }

    void signerValidationAndDataDirectoryReturnOwnedResults()
    {
        Fixture fixture;
        OptionsQmlModel model(fixture.node, fixture.args);
        QTRY_VERIFY(model.settingsReady());
        model.setExternalSignerPath(QStringLiteral("/definitely/missing/signer"));
        QTRY_VERIFY(!model.settingsPending());
        QVERIFY(!model.settingsError().isEmpty());
        QVERIFY(model.externalSignerPath().isEmpty());
        model.requestExternalSignerPathValidation(QStringLiteral("/definitely/missing/signer"));
        model.requestExternalSignerPathValidation(QStringLiteral("hwi"));
        QTRY_VERIFY(!model.signerPathValidationPending());
        QVERIFY(model.signerPathError().isEmpty());
        QSignalSpy selected(&model, &OptionsQmlModel::dataDirSelectionFinished);
        const QString path = fixture.directory.filePath("new-directory");
        QVERIFY(model.selectCustomDataDir(path));
        QVERIFY(model.validationPending());
        QTRY_COMPARE(selected.size(), 1);
        QVERIFY(selected.front().front().toBool());
        QCOMPARE(model.dataDir(), path);
        QVERIFY(QDir(path).exists());
    }
};

#ifdef BITCOINQML_NO_TEST_MAIN
#include <test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(AsyncOptionsTests)
#else
QTEST_MAIN(AsyncOptionsTests)
#endif
#include "test_async_options.moc"
