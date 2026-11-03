// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/options_model.h>
#include <qml/models/settings_keys.h>
#include <test/mocks/mocknode.h>
#include <chainparams.h>
#include <init.h>
#include <util/translation.h>

#include <QDir>
#include <QFile>
#include <QScopeGuard>
#include <QSemaphore>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QTranslator>
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
        QSignalSpy mempool_changed(&model, &OptionsQmlModel::maxMempoolSizeMBChanged);
        QSignalSpy language_changed(&model, &OptionsQmlModel::languageChanged);
        model.setMaxMempoolSizeMB(initial + 10);
        QVERIFY(model.settingsPending());
        QTRY_VERIFY(!model.settingsPending());
        QVERIFY(!model.settingsError().isEmpty());
        QCOMPARE(model.maxMempoolSizeMB(), initial);
        QVERIFY(fixture.args.GetPersistentSetting("maxmempool").isNull());
        QVERIFY(!model.mempoolSettingsDirty());
        QCOMPARE(mempool_changed.size(), 2); // Optimistic edit followed by rollback.
        QCOMPARE(mempool_changed.last().first().toInt(), initial);
        QVERIFY(language_changed.empty());
    }

    void savingSettingsOnlyNotifiesChangedValues()
    {
        Fixture fixture;
        OptionsQmlModel model(fixture.node, fixture.args);
        QTRY_VERIFY(!model.settingsPending());
        QVERIFY(model.settingsReady());
        QSignalSpy dbcache(&model, &OptionsQmlModel::dbcacheSizeMiBChanged);
        QSignalSpy mempool(&model, &OptionsQmlModel::maxMempoolSizeMBChanged);
        QSignalSpy threads(&model, &OptionsQmlModel::scriptThreadsChanged);
        QSignalSpy signer(&model, &OptionsQmlModel::externalSignerPathChanged);
        QSignalSpy data_dir(&model, &OptionsQmlModel::dataDirChanged);
        QSignalSpy defaults(&model, &OptionsQmlModel::dataDirDefaultsChanged);
        QSignalSpy custom_data_dir(&model, &OptionsQmlModel::customDataDirStringChanged);
        QSignalSpy language(&model, &OptionsQmlModel::languageChanged);
        QSignalSpy display_unit(&model, &OptionsQmlModel::displayUnitChanged);
        QSignalSpy urls(&model, &OptionsQmlModel::thirdPartyTransactionUrlsChanged);
        QSignalSpy font_choice(&model, &OptionsQmlModel::moneyFontChoiceChanged);
        QSignalSpy font(&model, &OptionsQmlModel::moneyFontChanged);

        const int unit = (model.displayUnit() + 1) % 4;
        model.setDisplayUnit(unit);
        QTRY_VERIFY(!model.settingsPending());
        QVERIFY2(model.settingsError().isEmpty(), qPrintable(model.settingsError()));
        QCOMPARE(model.displayUnit(), unit);
        QCOMPARE(display_unit.size(), 1); // Saving the optimistic value adds no notification.
        for (const auto* spy : {&dbcache, &mempool, &threads, &signer, &data_dir, &defaults,
                               &custom_data_dir, &language, &urls, &font_choice, &font}) {
            QVERIFY(spy->empty());
        }
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

    void legacyLanguageMatchesStartupFallback_data()
    {
        QTest::addColumn<QString>("chain_language");
        QTest::addColumn<QVariant>("gui_language");
        QTest::addColumn<QString>("cli_language");
        QTest::addColumn<QString>("expected");
        QTest::newRow("legacy-chain") << QStringLiteral("es") << QVariant{} << QString{} << QStringLiteral("es");
        QTest::newRow("legacy-default") << QString{} << QVariant{} << QString{} << QStringLiteral("es");
        QTest::newRow("explicit-system-default") << QStringLiteral("es") << QVariant{QString{}} << QString{} << QString{};
        QTest::newRow("explicit-current-language") << QStringLiteral("es") << QVariant{QStringLiteral("fr")} << QString{} << QStringLiteral("fr");
        QTest::newRow("command-line") << QStringLiteral("es") << QVariant{} << QStringLiteral("de") << QStringLiteral("de");
    }

    void legacyLanguageMatchesStartupFallback()
    {
        QFETCH(QString, chain_language);
        QFETCH(QVariant, gui_language);
        QFETCH(QString, cli_language);
        QFETCH(QString, expected);
        Fixture fixture;
        fixture.args.ForceSetArg("-regtest", "1");
        if (!cli_language.isEmpty()) fixture.args.ForceSetArg("-lang", cli_language.toStdString());
        QSettings current;
        current.remove(SettingsKeys::LANGUAGE);
        if (gui_language.isValid()) current.setValue(SettingsKeys::LANGUAGE, gui_language);
        current.sync();
        QSettings legacy_chain(QSettings::defaultFormat(), QSettings::UserScope, QStringLiteral("Bitcoin"), QStringLiteral("Bitcoin-Qt-regtest"));
        QSettings legacy_default(QSettings::defaultFormat(), QSettings::UserScope, QStringLiteral("Bitcoin"), QStringLiteral("Bitcoin-Qt"));
        const QVariant old_chain = legacy_chain.value(SettingsKeys::LANGUAGE);
        const QVariant old_default = legacy_default.value(SettingsKeys::LANGUAGE);
        const auto restore_legacy = qScopeGuard([&] {
            if (old_chain.isValid()) legacy_chain.setValue(SettingsKeys::LANGUAGE, old_chain);
            else legacy_chain.remove(SettingsKeys::LANGUAGE);
            if (old_default.isValid()) legacy_default.setValue(SettingsKeys::LANGUAGE, old_default);
            else legacy_default.remove(SettingsKeys::LANGUAGE);
            legacy_chain.sync();
            legacy_default.sync();
        });
        if (chain_language.isEmpty()) legacy_chain.remove(SettingsKeys::LANGUAGE);
        else legacy_chain.setValue(SettingsKeys::LANGUAGE, chain_language);
        legacy_default.setValue(SettingsKeys::LANGUAGE, QStringLiteral("es"));
        legacy_chain.sync();
        legacy_default.sync();
        OptionsQmlModel model(fixture.node, fixture.args);
        QSignalSpy changed(&model, &OptionsQmlModel::languageChanged);
        QTRY_VERIFY(model.settingsReady());
        QCOMPARE(model.language(), expected);
        QCOMPARE(changed.size(), expected.isEmpty() ? 0 : 1);
        if (gui_language.isValid() || !cli_language.isEmpty()) return;

        model.setLanguage(QString{});
        QTRY_VERIFY(!model.settingsPending());
        QVERIFY2(model.settingsError().isEmpty(), qPrintable(model.settingsError()));
        QCOMPARE(model.language(), QString{});
        QCOMPARE(changed.size(), 2);
        QVERIFY(current.contains(SettingsKeys::LANGUAGE));
        QCOMPARE(current.value(SettingsKeys::LANGUAGE).toString(), QString{});
        QCOMPARE(QString::fromStdString(fixture.args.GetArg("-lang", "")), QString{});

        ArgsManager restarted_args;
        SetupServerArgs(restarted_args, false);
        restarted_args.ForceSetArg("-datadir", fixture.directory.path().toStdString());
        restarted_args.ForceSetArg("-settings", fixture.directory.filePath("settings.json").toStdString());
        restarted_args.ForceSetArg("-regtest", "1");
        restarted_args.SelectConfigNetwork("regtest");
        std::vector<std::string> errors;
        QVERIFY(restarted_args.ReadSettingsFile(&errors));
        MockNode restarted_node;
        restarted_node.get_persistent_setting_fn = [&](const std::string& name) { return restarted_args.GetPersistentSetting(name); };
        OptionsQmlModel restarted(restarted_node, restarted_args);
        QTRY_VERIFY(restarted.settingsReady());
        QCOMPARE(restarted.language(), QString{});
    }

    void signerErrorsKeepSpanishTranslations_data()
    {
        QTest::addColumn<QString>("kind");
        QTest::addColumn<QString>("expected");
        QTest::newRow("missing") << QStringLiteral("missing") << QStringLiteral("La ruta configurada del firmante no existe.");
        QTest::newRow("directory") << QStringLiteral("directory") << QStringLiteral("La ruta configurada del firmante no es un archivo.");
        QTest::newRow("not-executable") << QStringLiteral("not-executable") << QStringLiteral("La ruta configurada del firmante no es ejecutable.");
    }

    void signerErrorsKeepSpanishTranslations()
    {
        QFETCH(QString, kind);
        QFETCH(QString, expected);
        Q_INIT_RESOURCE(qml_gui_translations);
        QTranslator translator;
        QVERIFY(translator.load(QStringLiteral(":/translations/bitcoin_qml_es.qm")));
        QVERIFY(QCoreApplication::installTranslator(&translator));
        struct RemoveTranslator {
            QTranslator& translator;
            ~RemoveTranslator() { QCoreApplication::removeTranslator(&translator); }
        } remove_translator{translator};
        Fixture fixture;
        const QString path = fixture.directory.filePath(kind);
        if (kind == QStringLiteral("directory")) {
            QVERIFY(QDir().mkpath(path));
        } else if (kind == QStringLiteral("not-executable")) {
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.close();
            QVERIFY(file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner));
        }
        OptionsQmlModel model(fixture.node, fixture.args);
        QTRY_VERIFY(model.settingsReady());
        model.requestExternalSignerPathValidation(path);
        QTRY_VERIFY(!model.signerPathValidationPending());
        QCOMPARE(model.signerPathError(), expected);
        model.setExternalSignerPath(path);
        QTRY_VERIFY(!model.settingsPending());
        QCOMPARE(model.settingsError(), expected);
        QVERIFY(model.externalSignerPath().isEmpty());
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
