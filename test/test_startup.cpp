// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/bitcoin.h>
#include <qml/models/onboardingoptionsmodel.h>
#include <test/application_test_context.h>
#include <util/fs.h>
#include <util/translation.h>
#include <univalue.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <string_view>

const TranslateFn G_TRANSLATION_FUN{[](const char* text) {
    return QCoreApplication::translate("bitcoin-core", text).toStdString();
}};

namespace {
QString DataDir(const QString& profile) { return profile + QString::fromUtf8("/node with spaces \xc3\xbc"); }
QString SettingsPath(const QString& profile) { return DataDir(profile) + "/regtest/settings.json"; }

QJsonObject ReadSettings(const QString& profile)
{
    QFile file{SettingsPath(profile)};
    return file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll()).object() : QJsonObject{};
}

struct Lifecycle {
    int onboarding_windows{0};
    int interface_sets{0};
    int main_windows{0};
    std::atomic<int> initializations{0};
    std::atomic<int> shutdowns{0};
};
} // namespace

class StartupOnboardingTests : public QObject
{
    Q_OBJECT
public:
    QQmlApplicationEngine& engine;
    Lifecycle& lifecycle;
    QString profile;
    bool cancel;
    StartupOnboardingTests(QQmlApplicationEngine& engine_in, Lifecycle& lifecycle_in, QString profile_in, bool cancel_in)
        : engine{engine_in}, lifecycle{lifecycle_in}, profile{std::move(profile_in)}, cancel{cancel_in} {}

private Q_SLOTS:
    void driveProductionOnboarding()
    {
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().constFirst());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QVERIFY(window->width() >= window->minimumWidth());
        QVERIFY(window->height() >= window->minimumHeight());
        auto* options = qobject_cast<OnboardingOptionsModel*>(engine.rootContext()->contextProperty("optionsModel").value<QObject*>());
        QVERIFY(options);
        QTRY_VERIFY(!options->storageCheckPending());
        QVERIFY2(options->previewError().isEmpty(), qPrintable(options->previewError()));
        // macOS may return the same directory with decomposed Unicode.
        QVERIFY(fs::equivalent(fs::PathFromString(options->dataDir().toStdString()),
                               fs::PathFromString(DataDir(profile).toStdString())));
        const auto initial_settings = ReadSettings(profile);
        QSignalSpy finished{window, SIGNAL(finished())};
        QSignalSpy warnings{&engine, &QQmlEngine::warnings};
        for (const auto* name : {"onboardingCover", "onboardingStrengthen", "onboardingBlockclock", "onboardingStorageLocation", "onboardingStorageAmount", "onboardingConnection"}) {
            QTRY_VERIFY(ApplicationTestContext::find(window, name));
            auto* page = ApplicationTestContext::find(window, name);
            QTRY_VERIFY(page->property("visible").toBool());
            QTRY_VERIFY(ApplicationTestContext::transitionsFinished(window));
            QCOMPARE(lifecycle.interface_sets, 0);
            QCOMPARE(lifecycle.initializations.load(), 0);
            QCOMPARE(ReadSettings(profile), initial_settings);
            if (std::string_view{name} == "onboardingStorageAmount") {
                auto* reduced = ApplicationTestContext::find(window, "storageReduceOption");
                QTRY_VERIFY(reduced && reduced->property("enabled").toBool());
                QVERIFY(ApplicationTestContext::click(reduced));
                QVERIFY(options->prune());
            }
            if (cancel && std::string_view{name} == "onboardingConnection") {
                window->close();
                QCOMPARE(finished.count(), 0);
                QVERIFY(!window->property("completed").toBool());
                return;
            }
            auto* button = ApplicationTestContext::find(window, QString::fromLatin1(name) + "Button");
            auto* item = qobject_cast<QQuickItem*>(button);
            QTRY_VERIFY2(item && item->isVisible() && item->isEnabled() && item->width() > 0 && item->height() > 0, name);
            QVERIFY2(ApplicationTestContext::click(button), name);
        }
        QCOMPARE(finished.count(), 1);
        QVERIFY(window->property("completed").toBool());
        QVERIFY(window->property("starting").toBool());
        QCOMPARE(warnings.count(), 0);
        // applyToArgs and Core startup happen only after this callback returns
        // to the production pre-init event loop.
        QCOMPARE(lifecycle.interface_sets, 0);
    }
};

class StartupRuntimeTests : public QObject
{
    Q_OBJECT
public:
    ApplicationTestContext& app;
    QSignalSpy& initialized;
    Lifecycle& lifecycle;
    bool onboarded_now;
    bool pruned;
    StartupRuntimeTests(ApplicationTestContext& app_in, QSignalSpy& initialized_in, Lifecycle& lifecycle_in,
                        bool onboarded_now_in, bool pruned_in)
        : app{app_in}, initialized{initialized_in}, lifecycle{lifecycle_in}, onboarded_now{onboarded_now_in}, pruned{pruned_in} {}

private Q_SLOTS:
    void realNodeUsesStartupChoices()
    {
        QTRY_VERIFY_WITH_TIMEOUT(!initialized.isEmpty(), 30'000);
        QVERIFY(!app.model<NodeModel>("nodeModel")->errorState());
        QCOMPARE(lifecycle.initializations.load(), 1);
        QCOMPARE(lifecycle.interface_sets, 1);
        QCOMPARE(app.window()->property("preInitOnboardingRanForUi").toBool(), onboarded_now);
        QTRY_VERIFY(app.find("nodeRunner"));
        QVERIFY(!app.find("preInitWindow"));
        QVERIFY(!app.find("desktopWalletsPage"));
        QVERIFY(!app.engine.rootContext()->contextProperty("walletController").isValid());
        QVERIFY(!app.engine.rootContext()->contextProperty("walletListModel").isValid());
        const auto info = app.call(&interfaces::Node::executeRpc, "getblockchaininfo", UniValue{UniValue::VARR}, "");
        QCOMPARE(info.find_value("chain").get_str(), std::string{"regtest"});
        QCOMPARE(info.find_value("pruned").get_bool(), pruned);
        QCOMPARE(app.call(&interfaces::Node::getNodeCount, ConnectionDirection::Both), 0U);
        QVERIFY(!app.call(&interfaces::Node::getNetworkActive));
    }
};

// A child starts through the same native argument handling and application entry
// point as production. Each process creates exactly one QApplication/Core node.
int RunStartupChild(int argc, char* argv[], const QString& scenario, const QString& profile)
{
    QSettings::setDefaultFormat(QSettings::IniFormat);
    for (const auto format : {QSettings::IniFormat, QSettings::NativeFormat}) {
        QSettings::setPath(format, QSettings::UserScope, profile + "/gui");
        QSettings::setPath(format, QSettings::SystemScope, profile + "/system");
    }
    const bool cancel = scenario == "cancel";
    const bool onboarding = cancel || scenario == "onboarding";
    Lifecycle lifecycle;
    int status{0};
    QPointer<QQmlApplicationEngine> preinit_engine;
    QPointer<QQmlApplicationEngine> main_engine;
    std::shared_ptr<qmlintegration::ThreadAudit> audit;
    QmlApplicationHooks hooks;
    hooks.onboarding_created = [&](QQmlApplicationEngine& engine) {
        ++lifecycle.onboarding_windows;
        preinit_engine = &engine;
        QTimer::singleShot(0, &engine, [&engine, &lifecycle, &status, profile, cancel, onboarding, argv] {
            if (onboarding) {
                StartupOnboardingTests tests{engine, lifecycle, profile, cancel};
                status |= QTest::qExec(&tests, 1, argv);
            } else {
                std::fprintf(stderr, "Unexpected onboarding on an existing profile\n");
                status = EXIT_FAILURE;
            }
            if (status) qobject_cast<QQuickWindow*>(engine.rootObjects().constFirst())->close();
        });
    };
    hooks.interfaces_created = [&](std::unique_ptr<interfaces::Node>& node, std::unique_ptr<interfaces::Chain>& chain) {
        ++lifecycle.interface_sets;
        audit = std::make_shared<qmlintegration::ThreadAudit>();
        audit->setObserver([&lifecycle](const char* method) {
            if (std::string_view{method} == "Node::appInitMain") ++lifecycle.initializations;
            if (std::string_view{method} == "Node::appShutdown") ++lifecycle.shutdowns;
        });
        node = qmlintegration::CheckNode(std::move(node), audit);
        chain = qmlintegration::CheckChain(std::move(chain), audit);
    };
    hooks.window_created = [&](interfaces::Node& node, QQmlApplicationEngine& engine) {
        ++lifecycle.main_windows;
        main_engine = &engine;
        if (preinit_engine || cancel) status = EXIT_FAILURE;
        audit->setPhase(qmlintegration::TestPhase::Running);
        ApplicationTestContext context{node, engine, audit};
        auto* model = context.model<NodeModel>("nodeModel");
        auto initialized = std::make_shared<QSignalSpy>(model, &NodeModel::nodeInitialized);
        QObject::connect(model, &NodeModel::requestedShutdown, &engine, [audit] { audit->setPhase(qmlintegration::TestPhase::Shutdown); });
        QTimer::singleShot(0, &engine, [&, context, initialized]() mutable {
            StartupRuntimeTests tests{context, *initialized, lifecycle, onboarding, scenario != "unpruned"};
            status |= QTest::qExec(&tests, 1, argv);
            context.model<NodeModel>("nodeModel")->requestShutdown();
        });
    };
    status |= QmlGuiMain(argc, argv, hooks);
    const int expected_nodes = cancel ? 0 : 1;
    if (preinit_engine || main_engine || lifecycle.onboarding_windows != int{onboarding} ||
        lifecycle.interface_sets != expected_nodes || lifecycle.main_windows != expected_nodes ||
        lifecycle.initializations != expected_nodes || lifecycle.shutdowns != expected_nodes) {
        std::fprintf(stderr, "Startup lifecycle count or teardown mismatch\n");
        status = EXIT_FAILURE;
    }
    return status;
}

class StartupProcessTests : public QObject
{
    Q_OBJECT

    void launch(const QString& profile, const QString& scenario, const QStringList& extra_args)
    {
        QProcess process;
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert("BITCOIN_QML_STARTUP_SCENARIO", scenario);
        environment.insert("BITCOIN_QML_STARTUP_PROFILE", profile);
        process.setProcessEnvironment(environment);
        QStringList arguments{"-regtest", "-datadir=" + DataDir(profile), "-listen=0", "-listenonion=0", "-connect=0",
                              "-dnsseed=0", "-fixedseeds=0", "-discover=0", "-natpmp=0", "-networkactive=0", "-printtoconsole=0"};
        arguments.append(extra_args);
        process.start(QCoreApplication::applicationFilePath(), arguments);
        QVERIFY2(process.waitForStarted(), qPrintable(process.errorString()));
        const bool finished = process.waitForFinished(45'000);
        if (!finished) {
            process.kill();
            process.waitForFinished(5'000);
        }
        const QByteArray output = process.readAllStandardOutput() + process.readAllStandardError();
        if (!finished || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
            // QtTest truncates assertion details; retain the full child diagnostic.
            std::fwrite(output.constData(), 1, output.size(), stderr);
        }
        QVERIFY2(finished, output.constData());
        QVERIFY2(process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0, output.constData());
        for (const auto* diagnostic : {"Core thread policy violation", "AddressSanitizer", "LeakSanitizer", "UndefinedBehaviorSanitizer",
                                      "ThreadSanitizer", "MemorySanitizer", "runtime error:"}) {
            QVERIFY2(!output.contains(diagnostic), output.constData());
        }
    }

    void prepare(const QString& profile, const QByteArray& config, const QJsonObject& settings)
    {
        QVERIFY(QDir{}.mkpath(DataDir(profile) + "/regtest"));
        QFile conf{DataDir(profile) + "/bitcoin.conf"};
        QVERIFY(conf.open(QIODevice::WriteOnly));
        QCOMPARE(conf.write(config), config.size());
        if (!settings.isEmpty()) {
            QFile file{SettingsPath(profile)};
            QVERIFY(file.open(QIODevice::WriteOnly));
            const auto json = QJsonDocument{settings}.toJson();
            QCOMPARE(file.write(json), json.size());
        }
    }

private Q_SLOTS:
    void firstRunPersistsChoicesAndRestartSkipsOnboarding()
    {
        QTemporaryDir profile;
        QVERIFY(profile.isValid());
        prepare(profile.path(), {}, {});
        QVERIFY(!QTest::currentTestFailed());
        launch(profile.path(), "onboarding", {"-disablewallet"});
        QVERIFY(!QTest::currentTestFailed());
        const auto settings = ReadSettings(profile.path());
        QVERIFY(settings.value("qml_onboarded").toBool());
        QVERIFY(settings.value("prune").toVariant().toLongLong() > 0);
        launch(profile.path(), "restart", {"-disablewallet"});
        QVERIFY(!QTest::currentTestFailed());
        QCOMPARE(ReadSettings(profile.path()).value("prune"), settings.value("prune"));
    }

    void cancelBeforeStartupLeavesProfileUntouched()
    {
        QTemporaryDir profile;
        QVERIFY(profile.isValid());
        prepare(profile.path(), {}, {});
        QVERIFY(!QTest::currentTestFailed());
        launch(profile.path(), "cancel", {"-disablewallet"});
        QVERIFY(!QTest::currentTestFailed());
        QVERIFY(!QFile::exists(SettingsPath(profile.path())));
        QVERIFY(!QFile::exists(DataDir(profile.path()) + "/regtest/blocks"));
        // A cancelled run must not mark the profile onboarded.
        launch(profile.path(), "onboarding", {"-disablewallet"});
    }

    void resetSettingsRunsOnboardingAgain()
    {
        QTemporaryDir profile;
        QVERIFY(profile.isValid());
        prepare(profile.path(), {}, {{"qml_onboarded", true}, {"prune", "550"}});
        QVERIFY(!QTest::currentTestFailed());
        launch(profile.path(), "onboarding", {"-disablewallet", "-resetguisettings"});
        QVERIFY(!QTest::currentTestFailed());
        QVERIFY(ReadSettings(profile.path()).value("qml_onboarded").toBool());
        launch(profile.path(), "restart", {"-disablewallet"});
    }

    void disabledSettingsStartsWithoutOnboardingOrPersistence()
    {
        QTemporaryDir profile;
        QVERIFY(profile.isValid());
        prepare(profile.path(), {}, {});
        QVERIFY(!QTest::currentTestFailed());
        launch(profile.path(), "restart", {"-disablewallet", "-nosettings", "-prune=550"});
        QVERIFY(!QTest::currentTestFailed());
        QVERIFY(!QFile::exists(SettingsPath(profile.path())));
    }

    void existingProfilePrecedence_data()
    {
        QTest::addColumn<QByteArray>("config");
        QTest::addColumn<QJsonObject>("settings");
        QTest::addColumn<QStringList>("arguments");
        QTest::addColumn<QString>("scenario");
        QTest::newRow("bitcoin-conf") << QByteArray{"disablewallet=1\nprune=550\n"}
            << QJsonObject{{"qml_onboarded", true}} << QStringList{} << QString{"restart"};
        QTest::newRow("settings-over-config") << QByteArray{"disablewallet=0\nprune=0\n"}
            << QJsonObject{{"qml_onboarded", true}, {"disablewallet", true}, {"prune", "550"}} << QStringList{} << QString{"restart"};
        QTest::newRow("cli-over-settings") << QByteArray{"disablewallet=0\nprune=550\n"}
            << QJsonObject{{"qml_onboarded", true}, {"disablewallet", false}, {"prune", "550"}}
            << QStringList{"-disablewallet=1", "-prune=0"} << QString{"unpruned"};
    }

    void existingProfilePrecedence()
    {
        QFETCH(QByteArray, config);
        QFETCH(QJsonObject, settings);
        QFETCH(QStringList, arguments);
        QFETCH(QString, scenario);
        QTemporaryDir profile;
        QVERIFY(profile.isValid());
        prepare(profile.path(), config, settings);
        QVERIFY(!QTest::currentTestFailed());
        launch(profile.path(), scenario, arguments);
    }
};

int main(int argc, char* argv[])
{
    const auto scenario = qEnvironmentVariable("BITCOIN_QML_STARTUP_SCENARIO");
    if (!scenario.isEmpty()) return RunStartupChild(argc, argv, scenario, qEnvironmentVariable("BITCOIN_QML_STARTUP_PROFILE"));
    QCoreApplication application{argc, argv};
    StartupProcessTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include <test_startup.moc>
