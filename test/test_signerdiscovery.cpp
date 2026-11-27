// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/models/signerdiscoverymodel.h>
#include <qml/executor.h>
#include <qml/walletcreation.h>
#include <test/mocks/manualstartupexecution.h>
#include <test/mocks/mocknode.h>
#include <test/qt_test_registry.h>
#include <common/args.h>
#include <init.h>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScopeGuard>
#include <QSemaphore>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>
#include <QtTest/QtTest>
#include <atomic>
#include <deque>
#include <stdexcept>
namespace {
using execution::Lane;
using signerdiscovery::Error;
QString Executable() { return QCoreApplication::applicationFilePath(); }
signerdiscovery::Result Healthy(QString config = QStringLiteral("config-a"), QString fingerprint = QStringLiteral("00000001"), QString name = QStringLiteral("Same model"))
{
    return {{Executable(), config, true, QStringLiteral("Persistent")}, {{config + QLatin1Char(':') + fingerprint, name, fingerprint, true}}, Error::None, {}};
}
struct Fixture {
    ManualStartupExecution worker;
    std::deque<std::function<void()>> gui;
    std::function<void()> deadline;
    std::chrono::milliseconds now{0}, due{0};
    signerdiscovery::Result result{Healthy()};
    int reads{0}, writes{0};
    bool throw_save{false};
    std::unique_ptr<SignerDiscoveryModel> model;
    explicit Fixture(execution::Executor* executor = nullptr)
    {
        SignerDiscoveryModel::Services services;
        auto post = [this](auto callback) { gui.push_back(std::move(callback)); };
        services.executor = executor ? executor : &worker.executor();
        services.discovery_post = post; services.configuration_post = post;
        services.retire = post;
        services.ports.configuration = [this] { return result.configuration; };
        services.ports.enumerate = [this](const auto&) { ++reads; if (result.error != Error::None) throw std::runtime_error(result.message.toStdString()); return result.devices; };
        services.ports.save = [this](const QString& command) {
            ++writes; if (throw_save) throw std::runtime_error("settings result lost");
            auto saved = result; saved.configuration.command = command; saved.configuration.id = QStringLiteral("saved"); for (auto& device : saved.devices) device.id = saved.configuration.id + QLatin1Char(':') + device.fingerprint; saved.persisted = true; result = saved; return saved;
        };
        services.arm = [this](auto duration, auto callback) { due = now + duration; deadline = std::move(callback); };
        services.stop = [this] { deadline = {}; };
        model = std::make_unique<SignerDiscoveryModel>(std::move(services));
    }
    ~Fixture() { model.reset(); settle(); }
    bool publish() { if (gui.empty()) return false; auto callback = std::move(gui.front()); gui.pop_front(); callback(); return true; }
    bool settle()
    {
        for (int i{0}; i < 2048; ++i) {
            if (worker.step(Lane::Utility) || worker.step(Lane::Core) || worker.step(Lane::Utility, execution::UtilityWorker::Signer) || publish()) continue;
            return worker.quiescent();
        }
        return false;
    }
    void advance(std::chrono::milliseconds duration) { now += duration; if (deadline && now >= due) { auto callback = std::move(deadline); deadline = {}; callback(); } }
};
}
class SignerDiscoveryTests : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void gettersNeverEnumerate() {
        Fixture f;
        for (int i{0}; i < 100; ++i) { QCOMPARE(f.model->rowCount(), 0); QVERIFY(f.model->uniqueSignerId().isEmpty()); QVERIFY(!f.model->fresh()); }
        QCOMPARE(f.reads, 0); QCOMPARE(f.writes, 0); QVERIFY(f.worker.quiescent());
        f.model->refresh(); QVERIFY(f.model->pending()); QCOMPARE(f.reads, 0);
        QVERIFY(f.settle()); QCOMPARE(f.reads, 1); QVERIFY(f.model->fresh()); QCOMPARE(f.model->uniqueSignerId(), QStringLiteral("config-a:00000001"));
    }
    void editedTextDoesNotChangeEffectiveDeviceStatus() {
        Fixture f; f.model->refresh(); QVERIFY(f.settle());
        f.model->editPath("/missing/uncommitted");
        QCOMPARE(f.model->editedPath(), QStringLiteral("/missing/uncommitted")); QCOMPARE(f.model->effectivePath(), Executable());
        QVERIFY(f.model->fresh()); QCOMPARE(f.model->uniqueSignerId(), QStringLiteral("config-a:00000001")); QCOMPARE(f.reads, 1); QCOMPARE(f.writes, 0);
    }
    void editAndDiscardNotifyTheDerivedApplyCapability() {
        Fixture f; f.model->refresh(); QVERIFY(f.settle()); QVERIFY(!f.model->canApply());
        QSignalSpy edited{f.model.get(), &SignerDiscoveryModel::editedChanged}, changed{f.model.get(), &SignerDiscoveryModel::changed};
        f.model->editPath(Executable() + " --draft"); QVERIFY(f.model->canApply());
        QCOMPARE(edited.count(), 1); QCOMPARE(changed.count(), 1);
        f.model->discardChanges(); QVERIFY(!f.model->canApply()); QCOMPARE(f.model->editedPath(), f.model->effectivePath());
        QCOMPARE(edited.count(), 2); QCOMPARE(changed.count(), 2); QCOMPARE(f.reads, 1); QCOMPARE(f.writes, 0);
    }
    void configurationChangeWhileDiscoveryOwnsOnlyOneSuccessor() {
        Fixture f; f.model->refresh(); while (f.worker.step(Lane::Utility)) {}
        auto held = f.worker.take(Lane::Utility, execution::UtilityWorker::Signer); QVERIFY(held);
        f.result = Healthy("config-b", "00000002");
        for (int i{0}; i < 256; ++i) f.model->refresh();
        held(); QVERIFY(f.settle());
        QCOMPARE(f.reads, 2); QCOMPARE(f.model->effectiveConfigurationId(), QStringLiteral("config-b"));
        QCOMPARE(f.model->uniqueSignerId(), QStringLiteral("config-b:00000002")); QVERIFY(f.model->fresh());
    }
    void obsoleteFailureCannotReplaceCurrentIdentity() {
        Fixture f; f.model->refresh(); while (f.worker.step(Lane::Utility)) {}
        f.result.error = Error::Backend; f.result.message = "Locked device";
        QVERIFY(f.worker.step(Lane::Utility, execution::UtilityWorker::Signer));
        f.result = Healthy("config-b", "00000002"); f.model->refresh();
        QVERIFY(f.settle()); QCOMPARE(f.reads, 2); QVERIFY(f.model->fresh());
        QCOMPARE(f.model->error().code, SignerDiscoveryError::None); QCOMPARE(f.model->uniqueSignerId(), QStringLiteral("config-b:00000002"));
    }
    void configurationMetadataRemainsResponsiveWhileTheProcessIsHeld() {
        Fixture f; f.model->refresh(); while (f.worker.step(Lane::Utility)) {}
        auto held = f.worker.take(Lane::Utility, execution::UtilityWorker::Signer); QVERIFY(held);
        QVERIFY(!f.model->configurationKnown()); f.worker.drain(); while (f.publish()) {}
        QVERIFY(f.model->configurationKnown()); QCOMPARE(f.model->effectivePath(), Executable()); QVERIFY(f.model->editable());
        f.model->editPath(Executable() + " --next"); QVERIFY(f.model->canApply());
        QCOMPARE(f.reads, 0); QVERIFY(f.model->pending());
        held(); QVERIFY(f.settle());
    }
    void timeoutDoesNotCancelOrStartAnotherProcess() {
        Fixture f; f.model->refresh(); while (f.worker.step(Lane::Utility)) {}
        auto held = f.worker.take(Lane::Utility, execution::UtilityWorker::Signer); QVERIFY(held);
        while (f.publish()) {}
        f.advance(std::chrono::milliseconds{29999}); QVERIFY(!f.worker.take(Lane::Utility, execution::UtilityWorker::Signer));
        QVERIFY(f.model->pending()); f.advance(std::chrono::milliseconds{1});
        while (f.worker.step(Lane::Utility)) {} while (f.publish()) {}
        QCOMPARE(f.model->error().code, SignerDiscoveryError::Timeout); QVERIFY(!f.model->pending()); QVERIFY(f.model->invocationActive()); QVERIFY(!f.model->canRefresh());
        QCOMPARE(f.reads, 0); held(); QVERIFY(f.settle());
        QCOMPARE(f.reads, 1); QVERIFY(!f.model->invocationActive()); QVERIFY(f.model->canRefresh()); QVERIFY(!f.model->fresh());
        QCOMPARE(f.model->error().code, SignerDiscoveryError::Timeout);
        f.model->refresh(); QVERIFY(f.settle()); QCOMPARE(f.reads, 2); QVERIFY(f.model->fresh());
    }
    void expiredBeforeDispatchDoesNotStartProcess() {
        Fixture f; f.model->refresh(); f.advance(std::chrono::seconds{30}); QVERIFY(f.settle());
        QCOMPARE(f.reads, 0); QCOMPARE(f.model->error().code, SignerDiscoveryError::Timeout); QVERIFY(!f.model->pending());
    }
    void admissionRejectionIsTerminalAndRetryable_data() { QTest::addColumn<bool>("domain"); QTest::newRow("domain") << true; QTest::newRow("process") << false; }
    void admissionRejectionIsTerminalAndRetryable() {
        QFETCH(bool, domain);
        std::function<void()> configuration;
        int decisions{0};
        bool rejected{false};
        execution::Executor executor{[&](Lane lane, execution::UtilityWorker worker, auto turn) {
            // Hold the configuration read so its result cannot occupy the
            // decision queue when discovery requests admission.
            if (lane == Lane::Core && !rejected) {
                configuration = std::move(turn);
                return true;
            }
            const bool discovery_decision = lane == Lane::Utility && worker == execution::UtilityWorker::IO && ++decisions == 2;
            if (!rejected && (domain ? discovery_decision : worker == execution::UtilityWorker::Signer)) {
                rejected = true;
                return false;
            }
            turn();
            return true;
        }};
        Fixture f{&executor}; f.model->refresh(); QVERIFY(configuration); configuration(); QVERIFY(f.settle()); QVERIFY(rejected);
        QCOMPARE(f.reads, 0); QCOMPARE(f.model->error().code, SignerDiscoveryError::Rejected); QVERIFY(!f.model->pending());
        f.model->refresh(); QVERIFY(f.settle()); QCOMPARE(f.reads, 1); QVERIFY(f.model->fresh());
    }
    void failureRetainsRowsVisiblyStaleAndActualCause() {
        Fixture f; f.model->refresh(); QVERIFY(f.settle());
        f.result.error = Error::Backend; f.result.message = "Unlock the Trezor using promptpin and sendpin."; f.model->refresh(); QVERIFY(f.settle());
        QCOMPARE(f.model->rowCount(), 1); QVERIFY(!f.model->fresh()); QVERIFY(f.model->uniqueSignerId().isEmpty());
        QVERIFY(!f.model->data(f.model->index(0), SignerDiscoveryModel::AvailableRole).toBool());
        QCOMPARE(f.model->error().message, f.result.message); QVERIFY(!f.model->error().message.contains("path is correct"));
    }
    void sameNameDoesNotRetargetDifferentFingerprint() {
        Fixture f; f.model->refresh(); QVERIFY(f.settle()); const auto first = f.model->uniqueSignerId();
        f.result = Healthy("config-a", "00000002"); f.model->refresh(); QVERIFY(f.settle());
        QCOMPARE(f.model->uniqueSignerName(), QStringLiteral("Same model")); QVERIFY(f.model->uniqueSignerId() != first);
    }
    void sameSeedReplacementSharesTheKeyOriginHint() {
        Fixture f; f.model->refresh(); QVERIFY(f.settle()); const auto original = f.model->uniqueSignerId();
        f.result = Healthy("config-a", "00000001", "Replacement model"); f.model->refresh(); QVERIFY(f.settle());
        QCOMPARE(f.model->uniqueSignerId(), original); QCOMPARE(f.model->uniqueSignerName(), QStringLiteral("Replacement model"));
        QVERIFY(f.model->fresh()); // This proves protocol-hint continuity, not physical-device identity.
    }
    void removalAndReturnRefreshTheCachedStatus() {
        Fixture f; f.model->refresh(); QVERIFY(f.settle()); const auto original = f.model->uniqueSignerId();
        f.result.devices.clear(); f.model->refresh(); QVERIFY(f.settle()); QCOMPARE(f.model->rowCount(), 0); QVERIFY(f.model->uniqueSignerId().isEmpty());
        QVERIFY(f.model->error().message.contains("No external signer"));
        f.result = Healthy(); f.model->refresh(); QVERIFY(f.settle()); QCOMPARE(f.model->uniqueSignerId(), original);
    }
    void saveReadbackDoesNotOverwriteNewerEditedText() {
        Fixture f; f.model->refresh(); QVERIFY(f.settle()); f.model->editPath(Executable() + " --new"); QVERIFY(f.model->apply());
        QCOMPARE(f.writes, 0); f.model->editPath("/later/draft"); QVERIFY(f.settle());
        QCOMPARE(f.writes, 1); QCOMPARE(f.model->effectivePath(), Executable() + " --new"); QCOMPARE(f.model->editedPath(), QStringLiteral("/later/draft")); QVERIFY(!f.model->saving()); QVERIFY(f.model->fresh());
    }
    void configurationSaveAndDiscoveryAreIndependentLanes() {
        Fixture f; f.model->refresh(); while (f.worker.step(Lane::Utility)) {} auto held = f.worker.take(Lane::Utility, execution::UtilityWorker::Signer); QVERIFY(held);
        f.worker.drain(); while (f.publish()) {}
        f.model->editPath(Executable() + " --configuration"); QVERIFY(f.model->apply()); f.worker.drain(); while (f.publish()) {}
        QCOMPARE(f.writes, 1); QCOMPARE(f.model->effectivePath(), Executable() + " --configuration"); QCOMPARE(f.reads, 0);
        held(); QVERIFY(f.settle()); QCOMPARE(f.reads, 2);
    }
    void rejectedSaveCannotPersistOrRemainBusy() {
        Fixture f; f.model->refresh(); QVERIFY(f.settle()); f.model->editPath(Executable() + " --configuration");
        f.worker.reject_core = true; QVERIFY(!f.model->apply()); QVERIFY(f.settle());
        QCOMPARE(f.writes, 0); QVERIFY(!f.model->saving()); QCOMPARE(f.model->effectivePath(), Executable());
        QCOMPARE(f.model->editError().code, SignerDiscoveryError::Rejected);
    }
    void unknownSaveRequiresReadbackBeforeReplay() {
        Fixture f; f.model->refresh(); QVERIFY(f.settle()); f.model->editPath(Executable() + " --configuration"); f.throw_save = true;
        QVERIFY(f.model->apply()); QVERIFY(f.settle()); QCOMPARE(f.writes, 1); QVERIFY(!f.model->canApply());
        QVERIFY(!f.model->apply()); QCOMPARE(f.writes, 1); f.model->refresh(); QVERIFY(f.settle()); QVERIFY(f.model->canApply());
    }
    void oversizedEditIsNeverAppliedAsTruncatedPrefix() {
        Fixture f; f.model->editPath(QString(40000, 'x')); QCOMPARE(f.model->editedPath().size(), 32769); QVERIFY(!f.model->apply());
        QCOMPARE(f.writes, 0); QCOMPARE(f.model->editError().code, SignerDiscoveryError::InvalidPath);
    }
    void closeRetainsPhysicalCompletionAndRejectsLatePublication() {
        Fixture f; f.model->refresh(); while (f.worker.step(Lane::Utility)) {} auto held = f.worker.take(Lane::Utility, execution::UtilityWorker::Signer); QVERIFY(held);
        f.model->close(); QVERIFY(!f.model->drained()); QSignalSpy finished{f.model.get(), &SignerDiscoveryModel::shutdownFinished};
        held(); QVERIFY(f.settle()); QCOMPARE(f.reads, 1); QVERIFY(f.model->drained()); QCOMPARE(finished.count(), 1);
        QCOMPARE(f.model->rowCount(), 0); QVERIFY(!f.model->fresh());
    }
    void pathValidationUsesRealFilesystemFacts_data() {
        QTest::addColumn<int>("kind"); QTest::addColumn<bool>("valid");
        QTest::newRow("executable") << 0 << true; QTest::newRow("quoted-executable-rejected-by-Core-contract") << 1 << false;
        QTest::newRow("missing") << 2 << false; QTest::newRow("directory") << 3 << false; QTest::newRow("not-executable") << 4 << false;
        QTest::newRow("bare-command") << 5 << false; QTest::newRow("empty-clears-config") << 6 << true;
    }
    void pathValidationUsesRealFilesystemFacts() {
        QFETCH(int, kind); QFETCH(bool, valid); QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto path = directory.filePath("test-signer"); QFile file{path}; QVERIFY(file.open(QIODevice::WriteOnly)); file.write("test fixture"); file.close();
        QVERIFY(file.setPermissions(QFile::ReadOwner | QFile::WriteOwner | (kind == 4 ? QFile::Permissions{} : QFile::ExeOwner)));
        const QString command = kind == 0 || kind == 4 ? path : kind == 1 ? '"' + path + "\" --test" : kind == 2 ? directory.filePath("missing") : kind == 3 ? directory.path() : kind == 5 ? QStringLiteral("this-signer-executable-does-not-exist-r15") : QString{};
        QCOMPARE(signerdiscovery::PathError(command).isEmpty(), valid);
    }
    void publicRpcIdentityAndDeviceError_data() { QTest::addColumn<int>("kind"); for (int i{0}; i < 5; ++i) QTest::newRow(qPrintable(QString::number(i))) << i; }
    void publicRpcIdentityAndDeviceError() {
        QFETCH(int, kind); MockNode node;
        node.SetPersistentSetting("signer", Executable().toStdString());
        node.execute_rpc_fn = [kind](const std::string& method, const UniValue& params, const std::string& uri) {
            if (method != "enumeratesigners" || !params.empty() || !uri.empty()) throw std::runtime_error("Unexpected RPC request");
            UniValue response;
            if (kind == 1) { if (!response.read(R"({"code":-1,"message":"Unlock using promptpin then sendpin."})")) throw std::runtime_error("Invalid test JSON"); throw response; }
            if (!response.read(kind == 2 ? R"({"signers":[{"fingerprint":"bad","name":"Model"}]})" : kind == 3 ? R"({"signers":[{"fingerprint":"00000001","name":"A"},{"fingerprint":"00000001","name":"B"}]})" : kind == 4 ? R"({"other":[]})" : R"({"signers":[{"fingerprint":"AbCd0123","name":"Model"}]})")) throw std::runtime_error("Invalid test JSON");
            return response;
        };
        const auto result = signerdiscovery::Discover(signerdiscovery::PersistentPorts(node));
        QCOMPARE(node.calls.executeRpc.load(), 1); QCOMPARE(node.calls.forceSetting.load(), 0);
        if (kind == 0) { QCOMPARE(result.error, Error::None); QCOMPARE(result.devices.size(), size_t{1}); QCOMPARE(result.devices.front().fingerprint, QStringLiteral("abcd0123")); QVERIFY(result.devices.front().id.endsWith(":abcd0123")); }
        else if (kind == 1) { QCOMPARE(result.error, Error::Backend); QCOMPARE(result.message, QStringLiteral("Unlock using promptpin then sendpin.")); }
        else QCOMPARE(result.error, Error::InvalidResponse);
    }
    void commandLineConfigurationNeverForcesThePersistentValue_data() {
        QTest::addColumn<bool>("disabled"); QTest::newRow("usable-command-line-signer") << false; QTest::newRow("explicitly-disabled-command-line-signer") << true;
    }
    void commandLineConfigurationNeverForcesThePersistentValue() {
        QFETCH(bool, disabled); ArgsManager args; args.AddArg("-signer=<cmd>", "signer fixture", ArgsManager::ALLOW_ANY, OptionsCategory::WALLET);
        const std::string literal = "-signer=" + (disabled ? std::string{} : Executable().toStdString());
        const char* argv[]{"signer-test", literal.c_str()}; std::string error; QVERIFY(args.ParseParameters(2, argv, error));
        MockNode node; node.SetPersistentSetting("signer", std::string{"/missing/persistent-command"});
        node.execute_rpc_fn = [](const auto&, const auto&, const auto&) { UniValue result; if (!result.read(R"({"signers":[{"fingerprint":"00000001","name":"CLI signer"}]})")) throw std::runtime_error("Bad fixture"); return result; };
        auto ports = signerdiscovery::PublicPorts(node, args);
        QCOMPARE(ports.configuration().command, disabled ? QString{} : Executable()); QVERIFY(!ports.configuration().editable);
        const auto result = signerdiscovery::Discover(ports);
        QCOMPARE(result.error, disabled ? Error::Unconfigured : Error::None); QCOMPARE(node.calls.executeRpc.load(), disabled ? 0 : 1);
        QCOMPARE(node.calls.forceSetting.load(), 0); QCOMPARE(node.calls.updateRwSetting.load(), 0);
        const auto save = ports.save(Executable() + " --different"); QCOMPARE(save.error, Error::Unavailable);
        QCOMPARE(node.calls.forceSetting.load(), 0); QCOMPARE(node.calls.updateRwSetting.load(), 0);
        QCOMPARE(node.getPersistentSetting("signer").get_str(), std::string{"/missing/persistent-command"});
    }
    void invalidEffectiveExecutableNeverReachesTheProcess() {
        MockNode node; node.SetPersistentSetting("signer", std::string{"this-signer-executable-does-not-exist-r15"});
        const auto result = signerdiscovery::Discover(signerdiscovery::PersistentPorts(node));
        QCOMPARE(result.error, Error::InvalidPath); QCOMPARE(node.calls.executeRpc.load(), 0); QCOMPARE(node.calls.forceSetting.load(), 0);
    }
    void successfulPersistenceSurvivesRuntimeOverrideFailure_data() {
        QTest::addColumn<bool>("nonstandard"); QTest::newRow("standard-exception") << false; QTest::newRow("nonstandard-exception") << true;
    }
    void successfulPersistenceSurvivesRuntimeOverrideFailure() {
        QFETCH(bool, nonstandard); QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto path = directory.filePath("settings.json"); ArgsManager args; SetupServerArgs(args, false);
        args.ForceSetArg("-datadir", directory.path().toStdString()); args.ForceSetArg("-settings", path.toStdString()); args.SelectConfigNetwork("regtest");
        MockNode node; node.get_persistent_setting_fn = [&](const auto& name) { return args.GetPersistentSetting(name); };
        node.force_setting_fn = [nonstandard](const auto&, const auto&) { if (nonstandard) throw 7; throw std::runtime_error("Runtime override failed after persistence"); };
        auto ports = signerdiscovery::PublicPorts(node, args);
        const auto result = ports.save(Executable());
        QVERIFY(result.persisted); QCOMPARE(result.error, Error::Persistence); QVERIFY(result.message.contains("was saved"));
        QCOMPARE(args.GetPersistentSetting("signer").get_str(), Executable().toStdString());
        QFile input{path}; QVERIFY(input.open(QIODevice::ReadOnly));
        QCOMPARE(QJsonDocument::fromJson(input.readAll()).object().value("signer").toString(), Executable()); input.close();
        // A runtime-only retry succeeds where any repeated persistent write
        // would fail. This independently checks the no-rewrite behavior.
        QVERIFY(QDir{directory.path()}.mkdir("settings.json.tmp"));
        std::vector<std::string> write_errors;
        QVERIFY(!args.WriteSettingsFile(&write_errors));
        QVERIFY(!write_errors.empty());
        node.force_setting_fn = [](const auto&, const auto&) {};
        const auto retried = ports.save(Executable()); QCOMPARE(retried.error, Error::None); QVERIFY(retried.persisted);
        QCOMPARE(node.calls.forceSetting.load(), 2);
    }
    void configurationChangingInsideTheProcessDiscardsItsIdentity() {
        signerdiscovery::Ports ports; auto result = Healthy();
        ports.configuration = [&] { return result.configuration; };
        ports.enumerate = [&](const auto&) { auto old = result.devices; result = Healthy("config-b", "00000002"); return old; };
        const auto actual = signerdiscovery::Discover(ports); QCOMPARE(actual.error, Error::ConfigurationChanged);
        QVERIFY(actual.devices.empty()); QCOMPARE(actual.configuration.id, QStringLiteral("config-b"));
    }
    void closeWhileConfigurationIsHeldRetainsItsEffectAndDrain() {
        Fixture f; f.model->refresh(); QVERIFY(f.settle()); f.model->editPath(Executable() + " --new"); QVERIFY(f.model->apply());
        auto held = f.worker.take(Lane::Core); QVERIFY(held);
        f.model->close(); while (f.publish()) {} QVERIFY(!f.model->drained()); QCOMPARE(f.writes, 0);
        held(); QVERIFY(f.settle());
        QCOMPARE(f.writes, 1); QVERIFY(f.model->drained()); QVERIFY(!f.model->pending());
    }
    void changedSignerVersionRejectsQueuedCreationBeforeCoreEntry_data() {
        QTest::addColumn<int>("change"); QTest::newRow("refresh") << 0; QTest::newRow("apply-configuration") << 1; QTest::newRow("close") << 2;
    }
    void changedSignerVersionRejectsQueuedCreationBeforeCoreEntry() {
        QFETCH(int, change); Fixture f; f.model->refresh(); QVERIFY(f.settle());
        walletcreation::Request request; request.kind = walletcreation::Kind::ExternalSigner;
        request.signer.version = f.model->versionGate(); request.signer.admitted_version = f.model->admissionVersion();
        QVERIFY(request.signer.admitted_version);
        MockNode node;
        ManualStartupExecution native;
        std::optional<walletcreation::NativeResult> result;
        walletcreation::Decision decision; decision.name = QStringLiteral("never-created");
        std::unique_ptr<interfaces::Wallet> wallet;
        QVERIFY(native.core.post([&] { result = walletcreation::Create(node, request, decision, wallet); }));
        QVERIFY(!result);
        if (change == 0) f.model->refresh();
        else if (change == 1) { f.model->editPath(Executable() + " --changed"); QVERIFY(f.model->apply()); }
        else f.model->close();
        QVERIFY(native.step(Lane::Core));
        QVERIFY(result);
        QVERIFY(!result->effect_started); QVERIFY(!result->created); QVERIFY(!result->uncertain); QVERIFY(!wallet);
        QVERIFY(result->error.contains("configuration changed")); QCOMPARE(node.calls.walletLoader.load(), 0); QVERIFY(native.quiescent());
    }
    void sharedExecutorSeparatesSettingsFromDiscoveryAndWaitsForTheProcess()
    {
        execution::Executor executor{execution::Executor::Config{}};
        auto core = executor.makeQueue(Lane::Core);
        auto signer = executor.makeQueue(Lane::Utility, execution::UtilityWorker::Signer);
        std::atomic<QThread*> core_thread{nullptr}, signer_thread{nullptr}, enumeration_thread{nullptr}, save_thread{nullptr};
        QVERIFY(core.post([&] { core_thread = QThread::currentThread(); }));
        QVERIFY(signer.post([&] { signer_thread = QThread::currentThread(); }));
        QTRY_VERIFY(core_thread.load() && signer_thread.load());
        QVERIFY(core_thread.load() != signer_thread.load());
        QSemaphore release;
        const QString command{Executable() + QStringLiteral(" --new")};
        std::atomic_bool settings_saved{false};
        std::atomic_int enumerations{0};
        SignerDiscoveryModel::Services services;
        services.executor = &executor;
        services.ports.configuration = [&] {
            auto configuration = Healthy().configuration;
            if (settings_saved.load()) configuration.command = command;
            return configuration;
        };
        services.ports.enumerate = [&](const auto&) {
            enumeration_thread = QThread::currentThread();
            if (++enumerations == 1) release.acquire();
            return Healthy().devices;
        };
        services.ports.save = [&](const QString& value) {
            save_thread = QThread::currentThread();
            auto result = Healthy();
            result.configuration.command = value;
            result.persisted = true;
            settings_saved = true;
            return result;
        };
        SignerDiscoveryModel model{std::move(services)};
        const auto cleanup = qScopeGuard([&] {
            model.close();
            release.release();
            QTRY_VERIFY(model.drained());
            executor.stopAndJoin();
        });
        model.refresh();
        QTRY_VERIFY(enumeration_thread.load());
        QVERIFY(enumeration_thread.load() == signer_thread.load());
        QTRY_VERIFY(model.configurationKnown());
        model.editPath(command);
        QVERIFY(model.apply());
        QTRY_COMPARE(model.effectivePath(), command);
        QVERIFY(save_thread.load() == core_thread.load());
        QSignalSpy finished{&model, &SignerDiscoveryModel::shutdownFinished};
        model.close();
        QVERIFY(!model.drained());
        release.release();
        QTRY_COMPARE(finished.count(), 1);
        QVERIFY(model.drained());
        QCOMPARE(enumerations.load(), 1);
        QCOMPARE(model.rowCount(), 0);
    }
    void actualThreadsNeverRunConfigurationOrDiscoveryOnGui() {
        const auto gui = QThread::currentThread(); std::atomic_bool native_config{false}, native_process{false};
        execution::Executor executor{execution::Executor::Config{}};
        SignerDiscoveryModel::Services services;
        services.executor = &executor;
        services.ports.configuration = [&] { native_config = QThread::currentThread() != gui; return Healthy().configuration; };
        services.ports.enumerate = [&](const auto&) { native_process = QThread::currentThread() != gui; return Healthy().devices; };
        SignerDiscoveryModel model{std::move(services)}; model.refresh(); QTRY_VERIFY(model.fresh());
        QVERIFY(native_config.load()); QVERIFY(native_process.load()); model.close(); QTRY_VERIFY(model.drained());
    }
};
BITCOINQML_REGISTER_QT_TEST(SignerDiscoveryTests)
#include <test_signerdiscovery.moc>
