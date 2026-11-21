// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/mocks/sendtest.h>
#include <test/mocks/preparationtest.h>
#include <test/mocks/provisioningtest.h>
#include <test/mocks/psbtactiontest.h>
#include <test/integration/runner/test_cases.h>
#include <test/integration/fixture/test_wallet.h>
#include <qml/models/psbtqmlmodel.h>
#include <qml/models/signertransaction.h>
#include <qml/models/options_model.h>
#include <key_io.h>
#include <pubkey.h>
#include <rpc/util.h>

#include <QRegularExpression>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QFileInfo>
#include <QDir>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QStandardPaths>
#include <QTcpSocket>
#include <QScopeGuard>

#include <limits>

namespace {
class SignerProcessFixture {
public:
    QTemporaryDir directory;
    QString state, entered, log, script;
    QJsonArray receive, internal;
    SignerProcessFixture()
    {
        if (!directory.isValid()) throw std::runtime_error("Signer fixture directory unavailable");
        state = directory.filePath("state.json"); entered = directory.filePath("entered.json"); log = directory.filePath("calls.jsonl"); script = directory.filePath("signer.py");
        QFile output{script}; if (!output.open(QIODevice::WriteOnly)) throw std::runtime_error("Signer fixture script unavailable");
        const QByteArray source{R"py(import json
from pathlib import Path
import socket
import sys

state = json.loads(Path(sys.argv[1]).read_text())
command = "getdescriptors" if "getdescriptors" in sys.argv[2:] else "enumerate" if "enumerate" in sys.argv[2:] else "unknown"
requested = sys.argv[sys.argv.index("--fingerprint") + 1] if "--fingerprint" in sys.argv else ""
with open(state["log"], "a", encoding="utf8") as output:
    output.write(json.dumps({"command": command, "requested": requested, "fingerprint": state["fingerprint"]}) + "\n")
if (command == "enumerate" and state["hold"]) or (command == "getdescriptors" and state["hold_descriptors"]):
    with socket.socket() as server:
        server.bind(("127.0.0.1", 0))
        server.listen(1)
        server.settimeout(20)
        Path(state["entered"]).write_text(json.dumps({"port": server.getsockname()[1], "command": command, "requested": requested}))
        connection, _ = server.accept()
        with connection:
            connection.settimeout(20)
            if connection.recv(1) != b"x":
                raise RuntimeError("Expected explicit fixture release")
    if command == "getdescriptors":
        state = json.loads(Path(sys.argv[1]).read_text())
if command == "getdescriptors":
    if requested != state["fingerprint"]:
        print(json.dumps({"error": "The captured key origin is not available on this signer."}))
    else:
        print(json.dumps({"receive": state["receive"], "internal": state["internal"]}))
elif command == "enumerate":
    if state["error"]:
        print(json.dumps([{"fingerprint": state["fingerprint"], "error": state["error"]}]))
    else:
        print(json.dumps([{"fingerprint": state["fingerprint"], "model": "R15 device"}]))
else:
    raise RuntimeError("Unexpected fixture command")
)py"};
        if (output.write(source) != source.size()) throw std::runtime_error("Signer fixture script write failed");
        output.close(); set(false, "00000001", {});
    }
    QString command() const {
        const auto python = QStandardPaths::findExecutable(QStringLiteral("python3"));
        if (python.isEmpty() || python.contains(QLatin1Char(' ')) || script.contains(QLatin1Char(' '))) throw std::runtime_error("Core's signer command format requires a Python executable/script without spaces");
        return python + QLatin1Char(' ') + script + QLatin1Char(' ') + state;
    }
    void set(bool hold, const QString& fingerprint, const QString& error, bool hold_descriptors = false) {
        const QJsonObject value{{"hold", hold}, {"fingerprint", fingerprint}, {"error", error}, {"log", log}, {"entered", entered}, {"hold_descriptors", hold_descriptors}, {"receive", receive}, {"internal", internal}};
        QFile output{state}; if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate)) throw std::runtime_error("Signer state unavailable");
        const auto json = QJsonDocument{value}.toJson(); if (output.write(json) != json.size()) throw std::runtime_error("Signer state write failed");
    }
    int calls() const { QFile input{log}; if (!input.open(QIODevice::ReadOnly)) return 0; return input.readAll().count('\n'); }
    bool release() const {
        QFile input{entered}; if (!input.open(QIODevice::ReadOnly)) return false;
        const auto port = QJsonDocument::fromJson(input.readAll()).object().value("port").toInt(); if (!port) return false;
        QTcpSocket socket; socket.connectToHost(QHostAddress::LocalHost, quint16(port));
        if (!QTest::qWaitFor([&] { return socket.state() == QAbstractSocket::ConnectedState; }, 5000)) return false;
        if (socket.write("x", 1) != 1) return false;
        return QTest::qWaitFor([&] { return socket.bytesToWrite() == 0; }, 5000);
    }
};
struct DescriptorFamily {
    QString fingerprint;
    QJsonArray receive, internal;
    QStringList strings;
};
DescriptorFamily ReadDescriptorFamily(BitcoinQmlApplication& app, const QString& wallet)
{
    DescriptorFamily family;
    const auto response = qmltest::Rpc(app, "listdescriptors", {}, wallet);
    for (const auto& entry : response.find_value("descriptors").getValues()) {
        const auto descriptor = QString::fromStdString(entry.find_value("desc").get_str());
        const auto origin = QRegularExpression{QStringLiteral("\\[([0-9a-fA-F]{8})/")}.match(descriptor).captured(1).toLower();
        if (origin.isEmpty()) throw std::runtime_error("Missing public descriptor origin in fixture");
        if (!family.fingerprint.isEmpty() && family.fingerprint != origin) throw std::runtime_error("Fixture has multiple key-origin hints");
        family.fingerprint = origin; family.strings.push_back(descriptor);
        (entry.find_value("internal").get_bool() ? family.internal : family.receive).push_back(descriptor);
    }
    family.strings.sort();
    return family;
}
QString ReceiveXpub(BitcoinQmlApplication& app, const QString& wallet)
{
    const auto descriptors = qmltest::Rpc(app, "listdescriptors", {}, wallet);
    for (const auto& entry : descriptors.find_value("descriptors").getValues()) {
        const auto descriptor = QString::fromStdString(entry.find_value("desc").get_str());
        if (!descriptor.startsWith("wpkh(") || entry.find_value("internal").get_bool()) continue;
        return QRegularExpression{QStringLiteral("([tx]pub[A-Za-z0-9]+)")}.match(descriptor).captured(1);
    }
    return {};
}
bool Contains(const UniValue& names, const std::string& name)
{
    for (const auto& value : names.getValues()) {
        if ((value.isStr() ? value.get_str() : value.find_value("name").get_str()) == name) return true;
    }
    return false;
}
} // namespace

class WatchOnlyIntegrationTests : public QObject
{
    Q_OBJECT
    BitcoinQmlApplication& m_app;

public:
    explicit WatchOnlyIntegrationTests(BitcoinQmlApplication& app) : m_app{app} {}

private Q_SLOTS:
    void commandLineSignerPreservesEffectiveConfiguration_data()
    {
        QTest::addColumn<bool>("conflict"); QTest::addColumn<bool>("disabled");
        QTest::newRow("cli-only") << false << false;
        QTest::newRow("cli-with-persisted-conflict") << true << false;
        QTest::newRow("explicit-disabled") << false << true;
    }
    void commandLineSignerPreservesEffectiveConfiguration()
    {
        QFETCH(bool, conflict); QFETCH(bool, disabled);
        if (conflict) qmltest::Call(m_app, &interfaces::Node::updateRwSetting, std::string{"signer"}, common::SettingsValue{std::string{"/missing/r15-persistent-override"}});
        auto* controller = qmltest::Model<WalletQmlController>(m_app, "walletController"); QVERIFY(controller);
        controller->refreshExternalSignerStatus(); QTRY_VERIFY(!controller->externalSignerPending());
        QCOMPARE(controller->canCreateExternalSignerWallet(), !disabled);
        if (!disabled) {
            QCOMPARE(controller->externalSignerName(), QStringLiteral("trezor_t"));
            const auto result = qmltest::Rpc(m_app, "enumeratesigners");
            QCOMPARE(result.find_value("signers").size(), size_t{1});
            QCOMPARE(result.find_value("signers")[0].find_value("fingerprint").get_str(), std::string{"00000001"});
        }
        if (conflict) QCOMPARE(qmltest::Call(m_app, &interfaces::Node::getPersistentSetting, std::string{"signer"}).get_str(), std::string{"/missing/r15-persistent-override"});
    }
    void actualSignerProcessPreservesErrorsAndRefreshesWithoutReentry()
    {
        SignerProcessFixture fixture;
        auto* controller = qmltest::Model<WalletQmlController>(m_app, "walletController"); QVERIFY(controller);
        auto* discovery = controller->signers(); QVERIFY(discovery); QTRY_VERIFY(discovery->configurationKnown());
        discovery->editPath(fixture.command()); QVERIFY(discovery->apply());
        QTRY_VERIFY(!discovery->pending()); QVERIFY2(discovery->fresh(), qPrintable(discovery->error().message));
        const auto original = discovery->uniqueSignerId(); QVERIFY(original.endsWith(":00000001")); QCOMPARE(fixture.calls(), 1);
        for (int i{0}; i < 100; ++i) { QCOMPARE(discovery->uniqueSignerId(), original); QCOMPARE(discovery->rowCount(), 1); }
        QCOMPARE(fixture.calls(), 1);
        fixture.set(false, "00000001", "Unlock using promptpin then sendpin."); discovery->refresh();
        QTRY_VERIFY(!discovery->pending()); QVERIFY(!discovery->fresh()); QCOMPARE(discovery->rowCount(), 1);
        QVERIFY(discovery->error().message.contains("Unlock using promptpin then sendpin."));
        QVERIFY(!discovery->error().message.contains("Check that the path is correct"));
        fixture.set(false, "00000001", {}); discovery->refresh(); QTRY_VERIFY(!discovery->pending());
        QVERIFY(discovery->fresh()); QCOMPARE(discovery->uniqueSignerId(), original); QCOMPARE(fixture.calls(), 3);
        discovery->editPath("this-signer-executable-does-not-exist-r15"); QVERIFY(discovery->apply());
        QTRY_VERIFY(!discovery->saving()); QCOMPARE(discovery->editError().code, SignerDiscoveryError::InvalidPath);
        QCOMPARE(discovery->effectivePath(), fixture.command()); QCOMPARE(fixture.calls(), 3);
        auto* legacy = qmltest::Model<OptionsQmlModel>(m_app, "optionsModel"); QVERIFY(legacy);
        legacy->setExternalSignerPath("another-missing-signer-executable-r15");
        QTRY_VERIFY(!legacy->settingsPending()); QVERIFY(!legacy->settingsError().isEmpty());
        QCOMPARE(legacy->externalSignerPath(), discovery->effectivePath()); QCOMPARE(fixture.calls(), 3);
    }
    void heldSignerProcessAllowsSoftwareSelectionAndConfigurationChanges()
    {
        auto* first = qmltest::CreateWallet(m_app, "first-software"); QVERIFY(first);
        auto* second = qmltest::CreateWallet(m_app, "second-software"); QVERIFY(second);
        auto* controller = qmltest::Model<WalletQmlController>(m_app, "walletController"); QVERIFY(controller); QVERIFY(controller->selectWallet(first));
        auto* discovery = controller->signers(); QVERIFY(discovery); QTRY_VERIFY(discovery->configurationKnown());
        SignerProcessFixture fixture; fixture.set(true, "00000001", {});
        bool released{false}; const auto cleanup = qScopeGuard([&] { if (!released && QFileInfo::exists(fixture.entered)) fixture.release(); });
        discovery->editPath(fixture.command()); QVERIFY(discovery->apply());
        QTRY_VERIFY(QFileInfo::exists(fixture.entered)); QVERIFY(discovery->pending());
        auto* catalog = qmltest::Model<WalletListModel>(m_app, "walletListModel"); QVERIFY(catalog);
        catalog->refresh(); QTRY_VERIFY(!catalog->projectionPending()); QVERIFY(catalog->fresh());
        QVERIFY(qmltest::Click(m_app, "walletBadge")); QVERIFY(qmltest::WaitForVisible(m_app, "walletSelectPopup"));
        QVERIFY(qmltest::Click(m_app, "walletSelectItem_second_software"));
        QVERIFY(qmltest::WaitForWallet(m_app, second->name()));
        QCOMPARE(controller->selectedWallet(), second); QVERIFY(second->available()); QVERIFY(discovery->pending());
        // The independent public node call completes while the signer is still
        // physically held; a busy flag cannot stand in for responsiveness.
        QCOMPARE(qmltest::Rpc(m_app, "getblockcount").getInt<int>(), 0); QVERIFY(discovery->pending());
        fixture.set(false, "00000002", {});
        discovery->editPath(fixture.command() + " --configuration-b"); QVERIFY(discovery->apply());
        QTRY_VERIFY(!discovery->saving()); QVERIFY(discovery->pending()); QCOMPARE(controller->selectedWallet(), second);
        QVERIFY(fixture.release()); released = true;
        QTRY_VERIFY(!discovery->pending()); QVERIFY2(discovery->fresh(), qPrintable(discovery->error().message));
        QVERIFY(discovery->uniqueSignerId().endsWith(":00000002")); QCOMPARE(fixture.calls(), 2);
        QCOMPARE(controller->selectedWallet(), second); QVERIFY(first->available()); QVERIFY(second->available());
    }
    void heldSignerProcessDoesNotBlockTheWalletPicker()
    {
        auto* first = qmltest::CreateWallet(m_app, "picker-first"); QVERIFY(first);
        auto* second = qmltest::CreateWallet(m_app, "picker-second"); QVERIFY(second);
        auto* controller = qmltest::Model<WalletQmlController>(m_app, "walletController"); QVERIFY(controller); QVERIFY(controller->selectWallet(first));
        auto* options = qmltest::Model<OptionsQmlModel>(m_app, "optionsModel"); QVERIFY(options);
        SignerProcessFixture fixture; fixture.set(true, "00000001", {});
        bool released{false}; const auto cleanup = qScopeGuard([&] { if (!released && QFileInfo::exists(fixture.entered)) fixture.release(); });
        options->setExternalSignerPath(fixture.command()); QTRY_VERIFY(!options->settingsPending()); QVERIFY(options->settingsError().isEmpty());
        controller->refreshExternalSignerStatus(); QTRY_VERIFY(QFileInfo::exists(fixture.entered)); QVERIFY(controller->externalSignerPending());
        auto* catalog = qmltest::Model<WalletListModel>(m_app, "walletListModel"); QVERIFY(catalog);
        catalog->refresh(); QTRY_VERIFY(!catalog->projectionPending()); QVERIFY(catalog->fresh());
        QVERIFY(qmltest::Click(m_app, "walletBadge")); QVERIFY(qmltest::WaitForVisible(m_app, "walletSelectPopup"));
        QVERIFY(qmltest::Click(m_app, "walletSelectItem_picker_second")); QVERIFY(qmltest::WaitForWallet(m_app, second->name()));
        QCOMPARE(controller->selectedWallet(), second); QVERIFY(controller->externalSignerPending());
        fixture.set(false, "00000001", {}); QVERIFY(fixture.release()); released = true;
        QTRY_VERIFY(!controller->externalSignerPending()); QVERIFY(controller->canCreateExternalSignerWallet());
        QCOMPARE(controller->selectedWallet(), second);
    }
    void signerDescriptorOperationDoesNotRetargetAcrossChanges_data()
    {
        QTest::addColumn<int>("scenario");
        QTest::newRow("fresh-creation-chooses-current-sole-origin") << 0;
        QTest::newRow("compatible-replacement-during-descriptors") << 1;
        QTest::newRow("incompatible-origin-during-descriptors") << 2;
        QTest::newRow("discovery-after-descriptor-binding") << 3;
    }
    void signerDescriptorOperationDoesNotRetargetAcrossChanges()
    {
        QFETCH(int, scenario);
        auto* keys_a = qmltest::CreateWallet(m_app, "keys-a"); QVERIFY(keys_a);
        auto* keys_b = qmltest::CreateWallet(m_app, "keys-b"); QVERIFY(keys_b);
        const auto family_a = ReadDescriptorFamily(m_app, keys_a->name()), family_b = ReadDescriptorFamily(m_app, keys_b->name());
        QVERIFY(family_a.strings != family_b.strings);
        QVERIFY2(family_a.fingerprint != family_b.fingerprint, "This fixture needs two distinct protocol hints; fingerprints are not assumed unique wallet/device identifiers.");
        auto* controller = qmltest::Model<WalletQmlController>(m_app, "walletController"); QVERIFY(controller);
        auto* discovery = controller->signers(); QVERIFY(discovery); QTRY_VERIFY(discovery->configurationKnown());
        SignerProcessFixture fixture; fixture.receive = family_a.receive; fixture.internal = family_a.internal;
        fixture.set(false, family_a.fingerprint, {}, scenario == 1 || scenario == 2);
        bool released{false}; const auto cleanup = qScopeGuard([&] { if (!released && QFileInfo::exists(fixture.entered)) fixture.release(); });
        discovery->editPath(fixture.command()); QVERIFY(discovery->apply()); QTRY_VERIFY(!discovery->pending());
        QVERIFY(discovery->fresh()); QVERIFY(discovery->uniqueSignerId().endsWith(QLatin1Char(':') + family_a.fingerprint));
        if (scenario == 0) { fixture.receive = family_b.receive; fixture.internal = family_b.internal; fixture.set(false, family_b.fingerprint, {}); }
        // This native caller owns the factory result. No JavaScript wrapper
        // exists here to collect a JavaScript-owned editor after the flow.
        const std::unique_ptr<WalletCreationModel> editor{controller->newCreation()};
        QVERIFY(editor); editor->chooseKind(WalletCreationModel::ExternalSigner); editor->name()->edit("bound-operation");
        QTRY_VERIFY(editor->canCreate()); auto* operation = editor->create({}, {}); QVERIFY(operation); QVERIFY(operation->acceptedForDelivery());
        if (scenario == 1 || scenario == 2) {
            QTRY_VERIFY(QFileInfo::exists(fixture.entered));
            QFile entered{fixture.entered}; QVERIFY(entered.open(QIODevice::ReadOnly)); const auto captured = QJsonDocument::fromJson(entered.readAll()).object();
            QCOMPARE(captured.value("command").toString(), QStringLiteral("getdescriptors")); QCOMPARE(captured.value("requested").toString(), family_a.fingerprint);
            if (scenario == 2) { fixture.receive = family_b.receive; fixture.internal = family_b.internal; }
            fixture.set(false, scenario == 1 ? family_a.fingerprint : family_b.fingerprint, {});
            QVERIFY(fixture.release()); released = true;
        }
        QTRY_VERIFY(operation->terminal());
        if (scenario == 2) {
            QVERIFY(!operation->resultWallet()); QVERIFY(!operation->canOpenResult()); QVERIFY(!controller->walletForName("bound-operation"));
            QVERIFY(!operation->error().message.isEmpty());
            const auto wallet_dir = qmltest::Call(m_app, [](interfaces::Node* node) { return node->walletLoader().getWalletDir(); });
            const bool artifact = QFileInfo::exists(QDir(QString::fromStdString(wallet_dir)).filePath("bound-operation"));
            // Core throws after beginning creation. A test's independent
            // directory observation cannot turn that receipt into a known
            // successful/failed effect or authorize an automatic replay.
            QVERIFY(artifact); QCOMPARE(operation->phase(), WalletOperationModel::Uncertain);
            QCOMPARE(operation->result().effect, WalletOperationResult::PossibleEffect); QVERIFY(!operation->outcomeKnown());
            QVERIFY(!operation->canRetry()); QVERIFY(operation->error().message.contains("uncertain"));
            QVERIFY(operation->error().message.contains("wallet directory"));
            const auto loaded = qmltest::Rpc(m_app, "listwallets");
            for (const auto& name : loaded.getValues()) QVERIFY(name.get_str() != "bound-operation");
        } else {
            QCOMPARE(operation->phase(), WalletOperationModel::Succeeded); auto* created = operation->resultWallet(); QVERIFY(created); QVERIFY(operation->canOpenResult());
            const auto expected = scenario == 0 ? family_b.strings : family_a.strings;
            QCOMPARE(ReadDescriptorFamily(m_app, created->name()).strings, expected);
            if (scenario == 3) {
                auto* selected = controller->selectedWallet(); const auto result_id = operation->result().result_id;
                fixture.receive = family_b.receive; fixture.internal = family_b.internal; fixture.set(false, family_b.fingerprint, {});
                discovery->refresh(); QTRY_VERIFY(!discovery->pending()); QVERIFY(discovery->fresh());
                QVERIFY(discovery->uniqueSignerId().endsWith(QLatin1Char(':') + family_b.fingerprint));
                QCOMPARE(ReadDescriptorFamily(m_app, created->name()).strings, family_a.strings);
                QCOMPARE(operation->resultWallet(), created); QCOMPARE(operation->result().result_id, result_id); QCOMPARE(controller->selectedWallet(), selected);
            }
        }
        QCOMPARE(qmltest::Rpc(m_app, "getrawmempool").size(), size_t{0});
    }

    void createFromXpubAndPrepareUnsignedTransaction()
    {
        auto* source = qmltest::CreateWallet(m_app, "source");
        QVERIFY(source);
        const auto descriptors = qmltest::Rpc(m_app, "listdescriptors", {}, source->name());
        QString xpub;
        for (const auto& entry : descriptors.find_value("descriptors").getValues()) {
            const auto descriptor = QString::fromStdString(entry.find_value("desc").get_str());
            if (!descriptor.startsWith("wpkh(") || entry.find_value("internal").get_bool()) continue;
            xpub = QRegularExpression{QStringLiteral("([tx]pub[A-Za-z0-9]+)")}.match(descriptor).captured(1);
            break;
        }
        QVERIFY(!xpub.isEmpty());
        QVERIFY(qmltest::Click(m_app, "walletBadge"));
        QVERIFY(qmltest::Click(m_app, "walletSelectAddWalletButton"));
        QVERIFY(qmltest::Click(m_app, "walletTypeViewOnly"));
        QVERIFY(qmltest::SetText(m_app, "createWalletNameInput", "watchonly"));
        QVERIFY(qmltest::SetText(m_app, "watchOnlyXpubInput", xpub));
        QVERIFY(qmltest::Click(m_app, "createWalletFormCreateButton"));
        QVERIFY(qmltest::WaitForVisible(m_app, "walletCreationReadyPage"));
        QVERIFY(qmltest::Click(m_app, "createWalletReadyDoneButton"));
        auto* wallet = qmltest::WaitForWallet(m_app, "watchonly");
        QVERIFY(wallet);
        QCOMPARE(wallet->keySchemeKind(), WalletQmlModel::KeyScheme::WatchOnly);
        QVERIFY(!wallet->canManagePassphrase());
        QVERIFY(!qmltest::Rpc(m_app, "getwalletinfo", {}, wallet->name()).find_value("private_keys_enabled").get_bool());
        // Keep one mature input and one immature reward without 99 unused rewards.
        const auto mining_address = qmltest::NewAddress(m_app, wallet->name());
        qmltest::Rpc(m_app, "generatetoaddress", qmltest::Json("[2,\"" + mining_address.toStdString() + "\"]"));
        qmltest::Rpc(m_app, "generatetodescriptor", qmltest::Json("[99,\"raw(6a)\"]"));
        QTRY_COMPARE(wallet->balanceSatoshi(), 50 * COIN);
        QCOMPARE(AmountFromValue(qmltest::Rpc(m_app, "getbalances", {}, wallet->name()).find_value("mine").find_value("immature")), 50 * COIN);
        QCOMPARE(qmltest::Rpc(m_app, "listunspent", {}, wallet->name()).size(), 1U);
        QVERIFY(qmltest::Click(m_app, "sendTabButton"));
        QVERIFY(qmltest::SetText(m_app, "sendAddressInput", qmltest::NewAddress(m_app, source->name())));
        QVERIFY(qmltest::SetText(m_app, "sendAmountInput", "1"));
        wallet->setCustomFeeEnabled(true);
        wallet->setCustomFeeRate("2");
        auto* review = qmltest::Find(m_app, "sendReviewButton");
        QVERIFY(review);
        // macOS buttons normally keep text focus; exercise the Linux focus
        // handoff too, without pre-blurring the amount or bypassing the click.
        QVERIFY(review->setProperty("focusPolicy", Qt::StrongFocus));
        QSignalSpy reviewed{review, SIGNAL(clicked())};
        QVERIFY(qmltest::Click(m_app, "sendReviewButton"));
        QCOMPARE(reviewed.count(), 1);
        QVERIFY(qmltest::WaitForVisible(m_app, "sendTransactionReviewPage"));
        QTRY_VERIFY(qmltest::Find(m_app, "transactionReviewPopup")->property("opened").toBool());
        QVERIFY(!qmltest::Find(m_app, "sendTransactionReviewSendButton")->property("visible").toBool());
        QVERIFY(qmltest::Find(m_app, "sendTransactionReviewSaveButton")->property("visible").toBool());
        QVERIFY(wallet->currentTransaction());
        QVERIFY(!wallet->currentTransactionCanSend());
        QVERIFY(!wallet->currentTransactionCanBroadcast());
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = directory.filePath("watchonly.psbt");
        auto* save_path = qmltest::Find(m_app, "sendTransactionReviewSavePsbtPathField");
        QVERIFY(save_path && save_path->setProperty("text", path));
        QVERIFY(qmltest::Click(m_app, "sendTransactionReviewSaveButton"));
        QTRY_VERIFY(QFileInfo{path}.size() > 0);
        // The file can land before the save outcome is published; wait for it.
        QTRY_COMPARE(wallet->psbtActionState(), WalletQmlModel::PsbtActionState::PsbtSaved);
        QCOMPARE(wallet->psbtSavedPath(), path);
        QVERIFY(!AwaitCommittedSend(*wallet));
        const auto broadcast = wallet->requestBroadcastCurrentTransaction();
        QVERIFY(AwaitPsbtAction(*wallet, broadcast));
        QCOMPARE(wallet->psbtActionState(), WalletQmlModel::PsbtActionState::PsbtFailed);
        QCOMPARE(qmltest::Rpc(m_app, "getrawmempool").size(), 0U);
        PartiallySignedTransaction psbt{CMutableTransaction{}, /*version=*/2};
        QCOMPARE(PsbtQmlModel::LoadPsbtFromFile(path, psbt), QString{});
        QVERIFY(!psbt.inputs.empty());
        for (const auto& input : psbt.inputs) QVERIFY(input.partial_sigs.empty());
        const auto exported = psbt.GetUnsignedTx();
        QVERIFY(exported);
        const CMutableTransaction reviewed_tx{*wallet->currentTransaction()->getWtx()};
        // The real Core producer and export preserve the commitment consumed by
        // signer approval. Actual hostile-return wiring is exercised by the
        // two external-signer subprocess scenarios in stage 8.
        QVERIFY(MatchesUnsignedTransaction(reviewed_tx, *exported));
        QCOMPARE(exported->GetHash(), reviewed_tx.GetHash());
    }

    // #737: Core writes the blank wallet before the descriptors are imported.
    // A failed import must report the wallet it left on disk, not "no wallet",
    // and must not let the user repeat creation over it.
    void failedXpubImportReportsTheBlankWalletLeftOnDisk()
    {
        auto* source = qmltest::CreateWallet(m_app, "xpub-source"); QVERIFY(source);
        const auto xpub = ReceiveXpub(m_app, source->name()); QVERIFY(!xpub.isEmpty());
        // A key at the maximum BIP32 depth still decodes as a valid public key,
        // so the form accepts it, but Core cannot derive /0/* from it. The real
        // importDescriptors call then fails after createWallet has succeeded.
        auto key = DecodeExtPubKey(xpub.toStdString()); QVERIFY(key.pubkey.IsValid());
        key.nDepth = std::numeric_limits<unsigned char>::max();
        CExtPubKey child; QVERIFY(!key.Derive(child, 0));
        const auto underivable = QString::fromStdString(EncodeExtPubKey(key));
        QVERIFY(DecodeExtPubKey(underivable.toStdString()).pubkey.IsValid());

        auto* controller = qmltest::Model<WalletQmlController>(m_app, "walletController"); QVERIFY(controller);
        const QDir wallet_dir{QString::fromStdString(qmltest::Call(m_app, [](interfaces::Node* node) { return node->walletLoader().getWalletDir(); }))};
        const QString name{QStringLiteral("xpub-import-fails")};
        const auto path = wallet_dir.filePath(name);
        QVERIFY(!QFileInfo::exists(path));

        const std::unique_ptr<WalletCreationModel> editor{controller->newCreation()}; QVERIFY(editor);
        editor->chooseKind(WalletCreationModel::WatchOnly);
        editor->name()->edit(name); editor->extendedPublicKey()->edit(underivable);
        QTRY_VERIFY(editor->canCreate());
        auto* operation = editor->create({}, {}); QVERIFY(operation); QVERIFY(operation->acceptedForDelivery());
        QTRY_VERIFY_WITH_TIMEOUT(operation->terminal(), 30'000);

        // The report: storage was created, setup did not finish, do not retry.
        QCOMPARE(operation->phase(), WalletOperationModel::Partial);
        QVERIFY(operation->outcomeKnown());
        QCOMPARE(operation->result().effect, WalletOperationResult::SetupIncomplete);
        QCOMPARE(operation->error().code, QStringLiteral("SetupIncomplete"));
        QVERIFY2(operation->error().message.contains("Failed to import descriptors"), qPrintable(operation->error().message));
        QVERIFY(!operation->error().recovery.isEmpty());
        QCOMPARE(operation->result().recovery_paths.size(), 1);
        QCOMPARE(QFileInfo{operation->result().recovery_paths.front()}.canonicalFilePath(), QFileInfo{path}.canonicalFilePath());
        QVERIFY(!operation->resultWallet()); QVERIFY(!operation->canOpenResult()); QVERIFY(!operation->canRetry());
        QVERIFY(!controller->walletForName(name));

        // The disk agrees with the report: the wallet exists, is listed, is not
        // loaded, and holds no descriptors.
        QVERIFY(QFileInfo::exists(path));
        QVERIFY(Contains(qmltest::Rpc(m_app, "listwalletdir").find_value("wallets"), name.toStdString()));
        QVERIFY(!Contains(qmltest::Rpc(m_app, "listwallets"), name.toStdString()));
        qmltest::Rpc(m_app, "loadwallet", qmltest::Json("[\"" + name.toStdString() + "\"]"));
        QCOMPARE(qmltest::Rpc(m_app, "listdescriptors", {}, name).find_value("descriptors").size(), size_t{0});
        QVERIFY(!qmltest::Rpc(m_app, "getwalletinfo", {}, name).find_value("private_keys_enabled").get_bool());
        qmltest::Rpc(m_app, "unloadwallet", qmltest::Json("[\"" + name.toStdString() + "\"]"));

        // Creating again under that name is refused before Core is called.
        const std::unique_ptr<WalletCreationModel> again{controller->newCreation()}; QVERIFY(again);
        again->chooseKind(WalletCreationModel::WatchOnly);
        again->name()->edit(name); again->extendedPublicKey()->edit(xpub);
        QTRY_VERIFY(!again->name()->error().isEmpty());
        QVERIFY(!again->canCreate());
    }

    void invalidXpubDoesNotCreateWallet()
    {
        auto* controller = qmltest::Model<WalletQmlController>(m_app, "walletController");
        QVERIFY(controller);
        QSignalSpy created{controller, &WalletQmlController::walletCreateSucceeded};
        controller->createWatchOnlyWallet("invalid", "not an extended public key");
        QTRY_VERIFY(!controller->walletLoadError().isEmpty());
        QTRY_VERIFY(!controller->walletLoadInProgress());
        QCOMPARE(created.count(), 0);
        QCOMPARE(qmltest::Rpc(m_app, "listwallets").size(), 0U);
        QVERIFY(!AwaitWalletPathCheck(*controller, "invalid"));
    }
};

std::unique_ptr<QObject> CreateWatchOnlyTests(BitcoinQmlApplication& app, WalletMode)
{
    return std::make_unique<WatchOnlyIntegrationTests>(app);
}

QStringList WatchOnlyTestArguments(const QString& selector)
{
    if (!selector.startsWith(QStringLiteral("commandLineSignerPreservesEffectiveConfiguration"))) return {};
    if (selector.contains(QStringLiteral("explicit-disabled"))) return {QStringLiteral("-signer=")};
    const auto mock = QStringLiteral(BITCOINQML_SIGNER_MOCK_SOURCE);
    const auto python = QStandardPaths::findExecutable(QStringLiteral("python3"));
    if (python.isEmpty() || !QFileInfo::exists(mock)) throw std::runtime_error("Pinned Core signer fixture/Python unavailable");
    return {QStringLiteral("-signer=") + python + QLatin1Char(' ') + QFileInfo(mock).absoluteFilePath()};
}

#include <test_watchonly.moc>
