// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/models/signerdiscoverymodel.h>
#include <qml/backendexecutor.h>
#include <qml/walletcreation.h>
#include <test/mocks/manualexecutionenvironment.h>
#include <test/mocks/mocknode.h>
#include <test/qt_test_registry.h>
#include <common/args.h>
#include <init.h>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScopeGuard>
#include <QFile>
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
    ManualRoutedExecution process, configuration;
    std::deque<std::function<void()>> gui;
    std::function<void()> deadline;
    std::chrono::milliseconds now{0}, due{0};
    signerdiscovery::Result result{Healthy()};
    int reads{0}, writes{0};
    bool throw_save{false};
    std::unique_ptr<SignerDiscoveryModel> model;
    Fixture()
    {
        SignerDiscoveryModel::Services services;
        auto post = [this](auto callback) { gui.push_back(std::move(callback)); };
        services.discovery = {process.scheduler, post}; services.configuration = {configuration.scheduler, post};
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
            if (process.stepNext() || configuration.stepNext() || publish()) continue;
            return process.quiescent() && configuration.quiescent();
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
        QCOMPARE(f.reads, 0); QCOMPARE(f.writes, 0); QVERIFY(f.process.quiescent());
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
        Fixture f; f.model->refresh(); QVERIFY(f.process.step(Lane::Domain));
        auto held = f.process.dispatchNext(Lane::Core); QVERIFY(held);
        f.result = Healthy("config-b", "00000002");
        for (int i{0}; i < 256; ++i) f.model->refresh();
        QVERIFY(f.process.finishWork(*held)); QVERIFY(f.settle());
        QCOMPARE(f.reads, 2); QCOMPARE(f.model->effectiveConfigurationId(), QStringLiteral("config-b"));
        QCOMPARE(f.model->uniqueSignerId(), QStringLiteral("config-b:00000002")); QVERIFY(f.model->fresh());
    }
    void obsoleteFailureCannotReplaceCurrentIdentity() {
        Fixture f; f.model->refresh(); QVERIFY(f.process.step(Lane::Domain));
        f.result.error = Error::Backend; f.result.message = "Locked device";
        QVERIFY(f.process.step(Lane::Core));
        f.result = Healthy("config-b", "00000002"); f.model->refresh();
        QVERIFY(f.settle()); QCOMPARE(f.reads, 2); QVERIFY(f.model->fresh());
        QCOMPARE(f.model->error().code, SignerDiscoveryError::None); QCOMPARE(f.model->uniqueSignerId(), QStringLiteral("config-b:00000002"));
    }
    void configurationMetadataRemainsResponsiveWhileTheProcessIsHeld() {
        Fixture f; f.model->refresh(); QVERIFY(f.process.step(Lane::Domain));
        auto held = f.process.dispatchNext(Lane::Core); QVERIFY(held);
        QVERIFY(!f.model->configurationKnown()); f.configuration.drain(); while (f.publish()) {}
        QVERIFY(f.model->configurationKnown()); QCOMPARE(f.model->effectivePath(), Executable()); QVERIFY(f.model->editable());
        f.model->editPath(Executable() + " --next"); QVERIFY(f.model->canApply());
        QCOMPARE(f.reads, 0); QVERIFY(f.model->pending());
        QVERIFY(f.process.finishWork(*held)); QVERIFY(f.settle());
    }
    void timeoutDoesNotCancelOrStartAnotherProcess() {
        Fixture f; f.model->refresh(); QVERIFY(f.process.step(Lane::Domain));
        auto held = f.process.dispatchNext(Lane::Core); QVERIFY(held);
        while (f.publish()) {}
        f.advance(std::chrono::milliseconds{29999}); QVERIFY(!f.process.queuedCountIf([](auto turn) { return turn.lane == Lane::Core; }));
        QVERIFY(f.model->pending()); f.advance(std::chrono::milliseconds{1});
        while (f.process.step(Lane::Domain)) {} while (f.publish()) {}
        QCOMPARE(f.model->error().code, SignerDiscoveryError::Timeout); QVERIFY(!f.model->pending()); QVERIFY(f.model->invocationActive()); QVERIFY(!f.model->canRefresh());
        QCOMPARE(f.reads, 0); QVERIFY(f.process.finishWork(*held)); QVERIFY(f.settle());
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
        QFETCH(bool, domain); Fixture f; f.process.rejectLane(domain ? Lane::Domain : Lane::Core); f.model->refresh(); QVERIFY(f.settle());
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
        Fixture f; f.model->refresh(); QVERIFY(f.process.step(Lane::Domain)); auto held = f.process.dispatchNext(Lane::Core); QVERIFY(held);
        f.configuration.drain(); while (f.publish()) {}
        f.model->editPath(Executable() + " --configuration"); QVERIFY(f.model->apply()); f.configuration.drain(); while (f.publish()) {}
        QCOMPARE(f.writes, 1); QCOMPARE(f.model->effectivePath(), Executable() + " --configuration"); QCOMPARE(f.reads, 0);
        QVERIFY(f.process.finishWork(*held)); QVERIFY(f.settle()); QCOMPARE(f.reads, 2);
    }
    void rejectedSaveCannotPersistOrRemainBusy() {
        Fixture f; f.model->refresh(); QVERIFY(f.settle()); f.model->editPath(Executable() + " --configuration");
        f.configuration.rejectLane(Lane::Core); QVERIFY(!f.model->apply()); QVERIFY(f.settle());
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
        Fixture f; f.model->refresh(); QVERIFY(f.process.step(Lane::Domain)); auto held = f.process.dispatchNext(Lane::Core); QVERIFY(held);
        f.model->close(); QVERIFY(!f.model->drained()); QSignalSpy finished{f.model.get(), &SignerDiscoveryModel::shutdownFinished};
        QVERIFY(f.process.finishWork(*held)); QVERIFY(f.settle()); QCOMPARE(f.reads, 1); QVERIFY(f.model->drained()); QCOMPARE(finished.count(), 1);
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
        const auto file_permissions = QFile::permissions(path), directory_permissions = QFile::permissions(directory.path());
        const auto restore = qScopeGuard([&] { QFile::setPermissions(directory.path(), directory_permissions); QFile::setPermissions(path, file_permissions); });
        QVERIFY(QFile::setPermissions(path, QFile::ReadOwner)); QVERIFY(QFile::setPermissions(directory.path(), QFile::ReadOwner | QFile::ExeOwner));
        QVERIFY(!QFileInfo(directory.path()).isWritable());
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
        auto held = f.configuration.dispatchNext(Lane::Core); QVERIFY(held);
        f.model->close(); while (f.publish()) {} QVERIFY(!f.model->drained()); QCOMPARE(f.writes, 0);
        QVERIFY(f.configuration.finishWork(*held)); QVERIFY(f.settle());
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
        if (change == 0) f.model->refresh();
        else if (change == 1) { f.model->editPath(Executable() + " --changed"); QVERIFY(f.model->apply()); }
        else f.model->close();
        MockNode node; ManualRoutedExecution native; std::optional<walletcreation::NativeResult> result;
        walletcreation::Decision decision; decision.name = QStringLiteral("never-created");
        const auto admitted = execution::Submit(*native.scheduler, [&] { return walletcreation::Create(node, request, decision); }, [&](auto value) { result = std::move(value); }, [](std::exception_ptr) { QFAIL("Unexpected native guard exception"); });
        QVERIFY(admitted); native.drain(); QVERIFY(result);
        QVERIFY(!result->effect_started); QVERIFY(!result->created); QVERIFY(!result->uncertain);
        QVERIFY(result->error.contains("configuration changed")); QCOMPARE(node.calls.walletLoader.load(), 0); QVERIFY(native.quiescent());
    }
    void actualThreadsNeverRunConfigurationOrDiscoveryOnGui() {
        const auto gui = QThread::currentThread(); std::atomic_bool native_config{false}, native_process{false};
        SignerDiscoveryModel::Services services;
        services.ports.configuration = [&] { native_config = QThread::currentThread() != gui; return Healthy().configuration; };
        services.ports.enumerate = [&](const auto&) { native_process = QThread::currentThread() != gui; return Healthy().devices; };
        SignerDiscoveryModel model{std::move(services)}; model.refresh(); QTRY_VERIFY(model.fresh());
        QVERIFY(native_config.load()); QVERIFY(native_process.load()); model.close(); QTRY_VERIFY(model.drained());
    }
};
BITCOINQML_REGISTER_QT_TEST(SignerDiscoveryTests)
#include <test_signerdiscovery.moc>
