// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/startuppolicy.h>
#include <qml/startupeffects.h>
#include <qml/quithandler.h>
#include <test/mocks/manualexecutionenvironment.h>
#include <test/qt_test_registry.h>

#include <chainparamsbase.h>
#include <common/args.h>
#include <init.h>

#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSettings>
#include <QFile>
#include <QDir>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest/QtTest>

#include <deque>
#include <stdexcept>
#include <utility>

namespace {
struct PolicyFixture {
    ManualRoutedExecution commands;
    std::deque<std::function<void()>> gui;
    execution::SnapshotServices services{commands.scheduler, [this](auto turn) { gui.push_back(std::move(turn)); }};
    ~PolicyFixture() { settle(); }
    void publish() { while (!gui.empty()) { auto turn = std::move(gui.front()); gui.pop_front(); turn(); } }
    void settle() { for (int n = 0; n < 16; ++n) { commands.drain(); publish(); } commands.drain(); }
};
QString RawText(const startupdiagnostics::Collection& source)
{
    std::lock_guard lock{source.mutex};
    QString text;
    for (const auto& batch : source.batches) for (const auto& message : batch.messages) text += message;
    return text;
}
QString Complete(const StartupDiagnosticsModel& model)
{
    QString text;
    for (int row = 0; row < model.rowCount(); ++row) text += model.data(model.index(row, 0), Qt::UserRole + 1).toString();
    return text;
}
struct PreparationFixture {
    QStringList trace;
    bool configured{true}, changed{false}, written{true}, initialized{true}, reset{false}, synced{true};
    QString migration_error, throw_at;
    std::vector<std::pair<bilingual_str, std::vector<std::string>>> errors;
    std::vector<bilingual_str> warnings;
    void call(const QString& name) { trace << name; if (throw_at == name) throw std::runtime_error{name.toStdString()}; }
    startup::PreparationPorts ports() {
        return {
            [this] { call("configure"); return configured; },
            [this] { call("migrate"); return startup::Migration{changed, migration_error}; },
            [this] { call("write"); return startup::SettingsWrite{written, {"disk failure", "original detail"}}; },
            [this](const auto& message, const auto& details) { call("error"); errors.emplace_back(message, details); },
            [this] { call("log"); }, [this] { call("node"); }, [this] { call("chain"); },
            [this] { call("base"); return initialized; }, [this] { call("reset-request"); return reset; },
            [this] { call("reset"); return synced; },
            [this](const auto& message) { call("warning"); warnings.push_back(message); }};
    }
};
} // namespace

class StartupPolicyTests : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void readonlyProfileResolutionPreservesTargets_data() {
        QTest::addColumn<bool>("existing_settings");
        QTest::newRow("absent-network-directory") << false;
        QTest::newRow("existing-malformed-settings-remain-untouched") << true;
    }
    void readonlyProfileResolutionPreservesTargets() {
        QFETCH(bool, existing_settings);
        QTemporaryDir directory; QVERIFY(directory.isValid());
        QFile configuration{directory.filePath("bitcoin.conf")};
        QVERIFY(configuration.open(QIODevice::WriteOnly));
        QCOMPARE(configuration.write("regtest=1\n"), qint64{10}); configuration.close();
        const auto network=directory.filePath("regtest");
        const auto settings_path=network+"/settings.json";
        const QByteArray sentinel{"not-json: original settings must remain unchanged"};
        if(existing_settings){
            QVERIFY(QDir{}.mkpath(network)); QFile settings{settings_path};
            QVERIFY(settings.open(QIODevice::WriteOnly)); QCOMPARE(settings.write(sentinel),qint64{sentinel.size()});settings.close();
        }
        ArgsManager args; SetupServerArgs(args,false);
        const auto option=QStringLiteral("-datadir=%1").arg(directory.path()).toStdString();
        const char* argv[]{"profile-test",option.c_str()}; std::string error;
        QVERIFY2(args.ParseParameters(2,argv,error),error.c_str());
        const auto original_chain=gArgs.GetChainType();
        const auto restore=qScopeGuard([original_chain]{SelectBaseParams(original_chain);});
        ManualRoutedExecution runner;
        std::optional<startup::ProfileIdentity> result;
        QVERIFY(execution::Submit(*runner.scheduler,[&]{return startup::ReadProfileIdentity(args);},
            [&](auto value){result=std::move(value);}));
        runner.drain(); QVERIFY(result); QCOMPARE(result->network,QString{"regtest"});
        // Core resolves the existing base directory physically; on macOS
        // /tmp is an alias of /private/tmp, even before regtest exists.
        QCOMPARE(result->directory,QDir(directory.path()).canonicalPath()+QStringLiteral("/regtest"));
        QVERIFY(!QFile::exists(directory.filePath("wallets")));
        QVERIFY(!QFile::exists(network+"/wallets"));
        QCOMPARE(QFile::exists(network),existing_settings);
        if(existing_settings){QFile settings{settings_path};QVERIFY(settings.open(QIODevice::ReadOnly));QCOMPARE(settings.readAll(),sentinel);}
        else QVERIFY(!QFile::exists(settings_path));
        QVERIFY(runner.quiescent());
    }
    void capturedProfileFencePrecedesMigration_data() {
        QTest::addColumn<bool>("matches");
        QTest::newRow("same-profile") << true;
        QTest::newRow("changed-profile") << false;
    }
    void capturedProfileFencePrecedesMigration() {
        QFETCH(bool,matches); PreparationFixture fixture; auto ports=fixture.ports();
        ports.profile_matches=[&]{fixture.call("identity");return matches;};
        const auto result=startup::PrepareNode(ports);
        QCOMPARE(result,matches?startup::PreparationOutcome::Ready:startup::PreparationOutcome::ConfigurationFailed);
        QCOMPARE(fixture.trace.mid(0,2),QStringList({"configure","identity"}));
        if(!matches){QCOMPARE(fixture.trace,QStringList({"configure","identity","error"}));QCOMPARE(fixture.errors.size(),size_t{1});}
        else {QVERIFY(fixture.trace.contains("migrate"));QVERIFY(fixture.trace.contains("base"));QVERIFY(fixture.errors.empty());}
    }
    void warningIntakePreservesFullNativeOrBoundedGuiText_data() {
        QTest::addColumn<int>("mode");
        QTest::newRow("native-short") << 0; QTest::newRow("gui-short") << 1;
        QTest::newRow("native-long-full-tail") << 2; QTest::newRow("gui-long-loss") << 3;
        QTest::newRow("gui-exact-utf8-budget") << 4; QTest::newRow("missing-collection") << 5;
        QTest::newRow("sealed-collection") << 6; QTest::newRow("loss-observer-absent") << 7;
    }
    void warningIntakePreservesFullNativeOrBoundedGuiText() {
        QFETCH(int, mode); PolicyFixture fixture;
        auto collection = startupdiagnostics::Create(fixture.commands.scheduler); QVERIFY(collection);
        auto loss = mode == 7 ? startup::DiagnosticLoss{} : std::make_shared<std::atomic_bool>(false);
        if (mode == 5 || mode == 7) collection.reset();
        if (mode == 6) collection->seal();
        const std::string text = mode == 2 || mode == 3 ? std::string(16384, 'x') + "-full-tail-\xe2\x82\xac"
            : mode == 4 ? std::string(16382, 'x') + "\xc3\xa9" : "  translated warning  ";
        const auto origin = mode == 1 || mode == 3 || mode == 4 ? startup::DiagnosticOrigin::Gui : startup::DiagnosticOrigin::Native;
        const bool accepted = startup::RecordWarning(collection, {"original", text}, origin, loss);
        QCOMPARE(accepted, mode < 5);
        if (loss) QCOMPARE(loss->load(), mode == 3 || mode == 5 || mode == 6);
        if (accepted) {
            const auto expected = mode == 3 ? QString(16384, 'x') + " [complete warning rejected: Core startup work ran on GUI]"
                : QString::fromStdString(text);
            QCOMPARE(RawText(*collection), expected); QVERIFY(!collection->sealed.load());
        } else if (collection) QCOMPARE(collection->owned_messages.load(), size_t{0});
        collection.reset(); fixture.settle(); QVERIFY(fixture.commands.quiescent());
    }
    void errorIntakeRetainsOriginalAndOwnsLoss_data() {
        QTest::addColumn<int>("mode");
        QTest::newRow("native-short") << 0; QTest::newRow("gui-short") << 1;
        QTest::newRow("native-large-original-and-detail") << 2; QTest::newRow("gui-large-explicit-loss") << 3;
        QTest::newRow("collection-capacity-refused") << 4; QTest::newRow("append-refused") << 5;
        QTest::newRow("gui-large-and-capacity-refused") << 6; QTest::newRow("loss-observer-absent") << 7;
    }
    void errorIntakeRetainsOriginalAndOwnsLoss() {
        QFETCH(int, mode); PolicyFixture fixture;
        auto collection = startupdiagnostics::Create(fixture.commands.scheduler); QVERIFY(collection);
        if (mode == 5) collection->seal();
        auto loss = mode == 7 ? startup::DiagnosticLoss{} : std::make_shared<std::atomic_bool>(false);
        const bool large = mode == 2 || mode == 3 || mode == 6;
        const bilingual_str message{large ? std::string(20000, 'o') + "-original-tail" : "original error",
            large ? std::string(20000, 't') + "-translated-tail" : "translated error"};
        int creations{0}; QStringList critical;
        const startup::DiagnosticPorts ports{
            [&] { ++creations; return mode == 4 || mode == 6 || mode == 7 ? std::shared_ptr<startupdiagnostics::Collection>{} : collection; },
            [&](const QString& value) { critical << value; }};
        const bool gui = mode == 1 || mode == 3 || mode == 6;
        auto result = startup::PrepareError(message, gui ? startup::DiagnosticOrigin::Gui : startup::DiagnosticOrigin::Native, ports, loss);
        QCOMPARE(creations, 1); QCOMPARE(critical.size(), 1);
        QCOMPARE(critical.front(), gui && large ? QString(16384, 'o') : QString::fromStdString(message.original));
        const bool retained = mode < 4;
        QCOMPARE(bool(result.details), retained);
        QString expected = large ? QString(16384, 't') : QString{"translated error"};
        if (gui && large) expected += " [complete error rejected: Core startup work ran on GUI]";
        if (!retained) expected += " [full error not available: too many messages]";
        QCOMPARE(result.summary, expected);
        if (retained) { QVERIFY(result.details->sealed.load()); QCOMPARE(RawText(*result.details), gui && large ? QString(16384, 't') : QString::fromStdString(message.translated)); }
        if (loss) QCOMPARE(loss->load(), (gui && large) || !retained);
        result.details.reset(); collection.reset(); fixture.settle(); QVERIFY(fixture.commands.quiescent());
    }
    void diagnosticPortValidationPrecedesEffects_data() {
        QTest::addColumn<bool>("missing_create");
        QTest::newRow("create") << true; QTest::newRow("critical") << false;
    }
    void diagnosticPortValidationPrecedesEffects() {
        QFETCH(bool, missing_create);
        int effects{0}; startup::DiagnosticPorts ports{
            [&] { ++effects; return std::shared_ptr<startupdiagnostics::Collection>{}; }, [&](const auto&) { ++effects; }};
        if (missing_create) ports.create = {}; else ports.critical = {};
        QString error;
        try { (void)startup::PrepareError(Untranslated("error"), startup::DiagnosticOrigin::Native, ports, {}); }
        catch (const std::invalid_argument& exception) { error = QString::fromUtf8(exception.what()); }
        QCOMPARE(error, QString{"Missing startup diagnostic port"}); QCOMPARE(effects, 0);
    }
    void errorDetailOwnershipAndProjection_data() {
        QTest::addColumn<int>("mode");
        QTest::newRow("existing-details") << 0; QTest::newRow("owned-summary") << 1;
        QTest::newRow("owned-full-collection") << 2; QTest::newRow("projection-refused") << 3;
    }
    void errorDetailOwnershipAndProjection() {
        QFETCH(int, mode); PolicyFixture fixture;
        StartupDiagnosticsModel existing{fixture.services};
        QVERIFY(existing.append("existing details", false)); QVERIFY(existing.project(false)); fixture.settle();
        auto collection = startupdiagnostics::Create(fixture.commands.scheduler); QVERIFY(collection);
        QVERIFY(collection->append("complete imported detail", false)); collection->seal();
        auto loss = std::make_shared<std::atomic_bool>(false);
        if (mode == 3) fixture.commands.rejectLane(execution::Lane::Domain);
        auto details = startup::PrepareErrorDetails(fixture.services, "fallback summary", mode == 2 ? collection : nullptr,
            mode == 0 ? &existing : nullptr, loss);
        QVERIFY(details.model); QCOMPARE(bool(details.owned), mode != 0);
        QCOMPARE(bool(details.replacement_message), mode == 3);
        QCOMPARE(loss->load(), mode == 3);
        if (mode == 0) { QCOMPARE(details.model, &existing); QCOMPARE(Complete(existing), QString{"existing details"}); }
        if (mode == 3) { QVERIFY(!details.model->error().isEmpty()); QCOMPARE(*details.replacement_message, details.model->error()); }
        else {
            fixture.settle();
            QCOMPARE(Complete(*details.model), mode == 0 ? QString{"existing details"}
                : mode == 2 ? QString{"complete imported detail"} : QString{"fallback summary"});
        }
        *loss = false; startup::RecordProjectionOutcome(*details.model, loss); QCOMPARE(loss->load(), mode == 3);
        startup::RecordProjectionOutcome(*details.model, {});
        details.owned.reset(); collection.reset(); existing.close(); fixture.settle();
    }
    void canceledErrorNeverEntersModalEffect_data() {
        QTest::addColumn<bool>("canceled"); QTest::addColumn<bool>("quitting"); QTest::addColumn<bool>("answer");
        QTest::newRow("canceled") << true << false << true; QTest::newRow("quitting") << false << true << true;
        QTest::newRow("both") << true << true << true; QTest::newRow("live-false") << false << false << false;
        QTest::newRow("live-true") << false << false << true;
    }
    void canceledErrorNeverEntersModalEffect() {
        QFETCH(bool, canceled); QFETCH(bool, quitting); QFETCH(bool, answer);
        int views{0}; const auto result = startup::DeliverError(canceled, quitting, [&] { ++views; return answer; });
        QCOMPARE(views, canceled || quitting ? 0 : 1); QCOMPARE(result, !canceled && !quitting && answer);
    }

    void migrationCleanupFollowsOnlyItsSuccessfulReplacement_data() {
        QTest::addColumn<int>("mode");
        QTest::newRow("replacement-rejected")<<0;QTest::newRow("cleanup-rejected")<<1;
        QTest::newRow("cleanup-throws")<<2;QTest::newRow("replacement-and-cleanup-complete")<<3;
    }
    void migrationCleanupFollowsOnlyItsSuccessfulReplacement() {
        QFETCH(int,mode);PreparationFixture fixture;fixture.changed=true;fixture.written=mode!=0;
        auto ports=fixture.ports();const auto migrate=ports.migrate;
        ports.migrate=[&]{auto result=migrate();result.complete=[&]{fixture.call(QStringLiteral("cleanup"));if(mode==2)throw std::runtime_error("cleanup boundary");return startup::SettingsWrite{mode==3,{"original cleanup failure"}};};return result;};
        QString exception;auto outcome=startup::PreparationOutcome::Ready;
        try{outcome=startup::PrepareNode(ports);}catch(const std::runtime_error& error){exception=QString::fromUtf8(error.what());}
        QCOMPARE(fixture.trace.count(QStringLiteral("write")),1);
        QCOMPARE(fixture.trace.count(QStringLiteral("cleanup")),mode==0?0:1);
        QCOMPARE(fixture.trace.count(QStringLiteral("node")),mode==3?1:0);
        if(mode==0)QCOMPARE(outcome,startup::PreparationOutcome::SettingsWriteFailed);
        if(mode==1){QCOMPARE(outcome,startup::PreparationOutcome::MigrationFailed);QCOMPARE(fixture.errors.front().second,std::vector<std::string>{"original cleanup failure"});}
        if(mode==2)QCOMPARE(exception,QStringLiteral("cleanup boundary"));
        if(mode==3){QCOMPARE(outcome,startup::PreparationOutcome::Ready);QVERIFY(fixture.trace.indexOf(QStringLiteral("write"))<fixture.trace.indexOf(QStringLiteral("cleanup")));QVERIFY(fixture.trace.indexOf(QStringLiteral("cleanup"))<fixture.trace.indexOf(QStringLiteral("node")));}
    }
    void nodePreparationHasExactFailFastOutcomes_data() {
        QTest::addColumn<int>("mode"); QTest::addColumn<int>("outcome"); QTest::addColumn<QStringList>("trace");
        using O = startup::PreparationOutcome;
        QTest::newRow("configuration-error") << 0 << int(O::ConfigurationFailed) << QStringList{"configure"};
        QTest::newRow("migration-error-before-write") << 1 << int(O::MigrationFailed) << QStringList{"configure", "migrate", "error"};
        QTest::newRow("settings-write-error") << 2 << int(O::SettingsWriteFailed) << QStringList{"configure", "migrate", "write", "error"};
        QTest::newRow("base-initialization-error") << 3 << int(O::BaseInitializationFailed) << QStringList{"configure", "migrate", "log", "node", "chain", "base"};
        QTest::newRow("no-migration-no-reset") << 4 << int(O::Ready) << QStringList{"configure", "migrate", "log", "node", "chain", "base", "reset-request"};
        QTest::newRow("persist-migration") << 5 << int(O::Ready) << QStringList{"configure", "migrate", "write", "log", "node", "chain", "base", "reset-request"};
        QTest::newRow("reset-success") << 6 << int(O::Ready) << QStringList{"configure", "migrate", "log", "node", "chain", "base", "reset-request", "reset"};
        QTest::newRow("reset-warning-keeps-ready") << 7 << int(O::Ready) << QStringList{"configure", "migrate", "log", "node", "chain", "base", "reset-request", "reset", "warning"};
    }
    void nodePreparationHasExactFailFastOutcomes() {
        QFETCH(int, mode); QFETCH(int, outcome); QFETCH(QStringList, trace); PreparationFixture fixture;
        fixture.configured = mode != 0; fixture.changed = mode == 1 || mode == 2 || mode == 5;
        if (mode == 1) fixture.migration_error = "migration failure";
        fixture.written = mode != 2; fixture.initialized = mode != 3; fixture.reset = mode >= 6; fixture.synced = mode != 7;
        QCOMPARE(startup::PrepareNode(fixture.ports()), static_cast<startup::PreparationOutcome>(outcome));
        QCOMPARE(fixture.trace, trace); QCOMPARE(fixture.errors.size(), mode == 1 || mode == 2 ? size_t{1} : size_t{0});
        if (mode == 1) { QCOMPARE(fixture.errors[0].first.original, std::string{"migration failure"}); QVERIFY(fixture.errors[0].second.empty()); }
        if (mode == 2) { QCOMPARE(fixture.errors[0].first.original, std::string{"Settings file could not be written"}); QCOMPARE(fixture.errors[0].second, std::vector<std::string>({"disk failure", "original detail"})); }
        QCOMPARE(fixture.warnings.size(), mode == 7 ? size_t{1} : size_t{0});
        if (mode == 7) QCOMPARE(fixture.warnings[0].original, std::string{"Unable to save window settings after reset."});
    }
    void nodePreparationExceptionsKeepTheirOriginalEffectBoundary_data() {
        QTest::addColumn<QString>("effect");
        for (const auto* effect : {"configure", "migrate", "write", "log", "node", "chain", "base", "reset-request", "reset", "warning", "error"})
            QTest::newRow(effect) << QString::fromLatin1(effect);
    }
    void nodePreparationExceptionsKeepTheirOriginalEffectBoundary() {
        QFETCH(QString, effect); PreparationFixture fixture;
        fixture.changed = true; fixture.reset = true; fixture.synced = false; fixture.throw_at = effect;
        if (effect == "error") fixture.migration_error = "migration failure";
        QString error;
        try { (void)startup::PrepareNode(fixture.ports()); }
        catch (const std::runtime_error& exception) { error = QString::fromUtf8(exception.what()); }
        QCOMPARE(error, effect); QVERIFY(!fixture.trace.empty()); QCOMPARE(fixture.trace.back(), effect);
        QCOMPARE(fixture.trace.count(effect), 1);
    }
    void missingPreparationPortsStartNoEffects_data() {
        QTest::addColumn<int>("port");
        for (int n = 0; n < 11; ++n) QTest::newRow(qPrintable(QString::number(n))) << n;
    }
    void nodePreparationOutcomeSurvivesQuitAndAdmissionClose_data() {
        QTest::addColumn<int>("mode");
        QTest::newRow("prepared") << 0; QTest::newRow("configuration-error") << 1;
        QTest::newRow("original-native-exception") << 2;
    }
    void nodePreparationOutcomeSurvivesQuitAndAdmissionClose() {
        QFETCH(int, mode); PolicyFixture fixture; PreparationFixture preparation;
        preparation.configured = mode != 1; if (mode == 2) preparation.throw_at = "base";
        QmlQuitHandler quit_handler; int stage{0}; bool returned{false};
        QTimer driver;
        connect(&driver, &QTimer::timeout, this, [&] {
            if (stage == 0) {
                ++stage; QVERIFY(!returned);
                QEvent quit{QEvent::Quit}; QCoreApplication::sendEvent(QCoreApplication::instance(), &quit);
                QVERIFY(quit_handler.isQuitRequested()); fixture.commands.scheduler->stopAcceptingWork();
                QVERIFY(fixture.commands.step(execution::Lane::Core)); QVERIFY(!preparation.trace.empty());
                QVERIFY(fixture.gui.empty()); QVERIFY(!returned);
            } else if (stage == 1) {
                ++stage; QVERIFY(fixture.commands.step(execution::Lane::Domain));
                QCOMPARE(fixture.gui.size(), size_t{1}); QVERIFY(!returned);
            } else if (stage == 2) { ++stage; driver.stop(); fixture.publish(); }
        });
        driver.start(0); QString error;
        auto outcome = startup::PreparationOutcome::Ready;
        try { outcome = startup::Run(fixture.services, [&] { return startup::PrepareNode(preparation.ports()); }); }
        catch (const std::runtime_error& exception) { error = QString::fromUtf8(exception.what()); }
        returned = true; driver.stop(); QCOMPARE(stage, 3);
        QCOMPARE(error, mode == 2 ? QString{"base"} : QString{});
        if (mode != 2) QCOMPARE(outcome, mode == 0 ? startup::PreparationOutcome::Ready : startup::PreparationOutcome::ConfigurationFailed);
        QCOMPARE(preparation.trace.count("configure"), 1); QCOMPARE(preparation.trace.count("base"), mode == 1 ? 0 : 1);
        QVERIFY(quit_handler.isQuitRequested()); fixture.settle(); QVERIFY(fixture.commands.quiescent());
    }
    void missingPreparationPortsStartNoEffects() {
        QFETCH(int, port); PreparationFixture fixture; auto ports = fixture.ports();
        switch (port) {
        case 0: ports.configure = {}; break; case 1: ports.migrate = {}; break; case 2: ports.write_settings = {}; break;
        case 3: ports.error = {}; break; case 4: ports.initialize_logging = {}; break; case 5: ports.make_node = {}; break;
        case 6: ports.make_chain = {}; break; case 7: ports.initialize_base = {}; break; case 8: ports.reset_requested = {}; break;
        case 9: ports.reset_window_settings = {}; break; case 10: ports.warning = {}; break;
        }
        QString error;
        try { (void)startup::PrepareNode(ports); }
        catch (const std::invalid_argument& exception) { error = QString::fromUtf8(exception.what()); }
        QCOMPARE(error, QString{"Missing node preparation port"}); QVERIFY(fixture.trace.empty());
    }
    void resetSettingsOwnsExactKeysAndSyncOutcome_data() {
        QTest::addColumn<int>("mode");
        QTest::newRow("synced") << 0; QTest::newRow("sync-failed") << 1; QTest::newRow("sync-threw") << 2;
        QTest::newRow("missing-remove") << 3; QTest::newRow("missing-sync") << 4;
    }
    void resetSettingsOwnsExactKeysAndSyncOutcome() {
        QFETCH(int, mode); QStringList trace;
        startup::WindowSettingsPorts ports{[&](const QString& key) { trace << key; }, [&] {
            trace << "sync"; if (mode == 2) throw std::runtime_error{"original sync error"}; return mode == 0;
        }};
        if (mode == 3) ports.remove = {};
        if (mode == 4) ports.sync = {};
        bool result{false}; QString error;
        try { result = startup::ResetWindowSettings(ports); }
        catch (const std::exception& exception) { error = QString::fromUtf8(exception.what()); }
        QCOMPARE(result, mode == 0);
        if (mode >= 3) { QVERIFY(trace.empty()); QCOMPARE(error, QString{"Missing window settings port"}); }
        else {
            QCOMPARE(trace, QStringList({"fHideTrayIcon", "fMinimizeToTray", "fMinimizeOnClose", "sync"}));
            QCOMPARE(error, mode == 2 ? QString{"original sync error"} : QString{});
        }
    }
    void preferencesInstallUsesExplicitPhaseAndOriginalNamespace_data() {
        QTest::addColumn<int>("mode");
        QTest::newRow("before-window-ready") << 0; QTest::newRow("error-handoff-does-not-wait") << 1;
        QTest::newRow("before-window-refused-read") << 2;
    }
    void preferencesInstallUsesExplicitPhaseAndOriginalNamespace() {
        QFETCH(int, mode); PolicyFixture fixture; QTemporaryDir directory; QVERIFY(directory.isValid());
        const QString path = directory.filePath("preferences.ini");
        { QSettings settings{path, QSettings::IniFormat}; settings.setValue("windowWidth", 1234); settings.sync(); }
        UiPreferencesNamespace identity; identity.file = path; identity.format = QSettings::IniFormat;
        QQmlApplicationEngine engine;
        QTimer driver; int turns{0};
        connect(&driver, &QTimer::timeout, this, [&] { ++turns; fixture.settle(); });
        if (mode == 2) fixture.commands.rejectLane(execution::Lane::Core);
        if (mode != 1) driver.start(0);
        auto* store = startup::InstallPreferences(engine, fixture.services, identity,
            mode == 1 ? startup::PreferencesPhase::ErrorHandoff : startup::PreferencesPhase::BeforeWindow);
        driver.stop(); QVERIFY(store); QCOMPARE(store->parent(), &engine);
        QCOMPARE(engine.rootContext()->contextProperty("uiPreferencesStore").value<QObject*>(), store);
        if (mode == 1) { QCOMPARE(turns, 0); QVERIFY(!store->ready()); QVERIFY(store->pending()); fixture.settle(); }
        else if (mode == 0) QVERIFY(turns > 0);
        QCOMPARE(store->ready(), mode != 2);
        if (mode != 2) QCOMPARE(store->values().windowWidth, 1234.0);
        else QVERIFY(!store->error().isEmpty());
        store->close();
    }
    void preferencesRetirementWaitsForOriginalWrite_data() {
        QTest::addColumn<bool>("window"); QTest::newRow("no-window") << false; QTest::newRow("disable-native-window") << true;
    }
    void preferencesRetirementWaitsForOriginalWrite() {
        QFETCH(bool, window); PolicyFixture fixture; QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto path = directory.filePath("preferences.ini"); UiPreferencesNamespace identity; identity.file = path;
        QQmlApplicationEngine engine; QQuickWindow native_window;
        auto* store = startup::InstallPreferences(engine, fixture.services, identity, startup::PreferencesPhase::ErrorHandoff);
        fixture.settle(); QVERIFY(store->ready()); AppPreferences view; view.configure(store);
        QVERIFY(view.requestGeometry(25, 35, 1200, 800)); auto receipt = store->lastWriteReceipt(); QVERIFY(receipt);
        QCOMPARE(receipt->load(), UiPreferencesStore::Pending);
        QTimer driver; int turns{0};
        connect(&driver, &QTimer::timeout, this, [&] {
            ++turns; if (window) QVERIFY(!native_window.contentItem()->isEnabled());
            fixture.settle();
        });
        driver.start(0); startup::FinishPreferences(store, window ? &native_window : nullptr); driver.stop();
        QVERIFY(turns > 0); QCOMPARE(receipt->load(), UiPreferencesStore::Applied); QVERIFY(!store->pending());
        QVERIFY(!view.requestGeometry(1, 2, 3, 4));
        QSettings persisted{path, QSettings::IniFormat}; QCOMPARE(persisted.value("windowX").toInt(), 25); QCOMPARE(persisted.value("windowHeight").toInt(), 800);
        startup::FinishPreferences(nullptr, &native_window);
    }
};

BITCOINQML_REGISTER_QT_TEST(StartupPolicyTests)
#include <test_startuppolicy.moc>
