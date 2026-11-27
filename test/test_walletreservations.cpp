// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/walletrequests.h>
#include <qml/walletreceipts.h>
#include <test/mocks/walletsession.h>
#include <test/mocks/manualstartupexecution.h>
#include <test/mocks/mockwallet.h>
#include <test/qt_test_registry.h>

#include <chainparams.h>
#include <chainparamsbase.h>
#include <key.h>
#include <key_io.h>
#include <script/solver.h>

#include <QtTest/QtTest>
#include <QScopeGuard>
#include <QSemaphore>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QTemporaryDir>
#include <array>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace {
using namespace walletrequests;
using execution::Lane;
CTxDestination Destination(unsigned char value)
{
    uint160 key; key.begin()[0] = value; return PKHash{key};
}
QString Address(unsigned char value) { return QString::fromStdString(EncodeDestination(Destination(value))); }
struct Wallet : StubWallet {
    int generated{0}, unlocked{0}, relocked{0}, reads{0}, writes{0};
    std::string expected_password{"correct"};
    bool locked{false}, encrypted{false}, fail_lock{false}, fail_unlock{false}, can_generate{true}, taproot{true};
    bool throw_lock{false}, throw_generate{false}, fail_generate{false}, throw_history{false};
    std::set<interfaces::WalletTx> transactions;
    std::map<CTxDestination, std::string> book;
    std::optional<CTxDestination> next_destination;
    std::vector<OutputType> generated_types;
    bool canGetAddresses() override { return can_generate; }
    bool taprootEnabled() override { return taproot; }
    bool isCrypted() override { return encrypted; }
    bool isLocked() override { return locked; }
    util::Expected<void, wallet::WalletError> unlock(const SecureString& password) override {
        ++unlocked;
        if (fail_unlock) return util::Unexpected{wallet::WalletError{wallet::WalletErrorCode::GenericError, Untranslated("device unavailable")}};
        if (std::string_view{password.data(),password.size()} != std::string_view{expected_password}) return util::Unexpected{wallet::WalletError{wallet::WalletErrorCode::PassphraseIncorrect, Untranslated("incorrect")}};
        locked = false; return {};
    }
    bool lock() override { ++relocked; if (throw_lock) throw std::runtime_error{"lock outcome unavailable"}; if (fail_lock) return false; locked = true; return true; }
    OutputType getDefaultAddressType() override { return OutputType::LEGACY; }
    util::Result<CTxDestination> getNewDestination(OutputType type, const std::string& label) override {
        ++generated;
        generated_types.push_back(type);
        if (throw_generate) throw std::runtime_error{"generation outcome unavailable"};
        if (fail_generate) return util::Error{Untranslated("keypool unavailable")};
        if (locked) return util::Error{Untranslated("locked keypool")};
        const auto destination = std::exchange(next_destination, {}).value_or(Destination(static_cast<unsigned char>(generated)));
        book[destination] = label; return destination;
    }
    bool getAddress(const CTxDestination& destination, std::string* label, wallet::AddressPurpose* purpose) override {
        const auto found = book.find(destination); if (found == book.end()) return false;
        if (label) *label = found->second;
        if (purpose) *purpose = wallet::AddressPurpose::RECEIVE;
        return true;
    }
    std::set<interfaces::WalletTx> getWalletTxs() override { ++reads; if (throw_history) throw std::runtime_error{"history unavailable"}; return transactions; }
    bool setAddressReceiveRequest(const CTxDestination&, const std::string&, const std::string&) override { ++writes; return true; }
};
struct Storage {
    QString current;
    std::set<QString> retired;
    int reads{0}, writes{0}, marks{0};
    bool fail_read{false}, fail_write{false}, fail_retire{false}, fail_clear{false};
    bool throw_read{false}, throw_write{false};
    ReservationStorage port() {
        return {
            [this](interfaces::Wallet&, const QString& candidate) {
                ++reads; if (throw_read) throw std::runtime_error{"reservation read unavailable"}; const auto address = candidate.isEmpty() ? current : candidate;
                return StoredAddress{address, retired.contains(address), !fail_read};
            },
            [this](interfaces::Wallet&, const QString& address) {
                ++writes; if (throw_write) throw std::runtime_error{"reservation write outcome unavailable"}; if (fail_write) return false; current = address; return true;
            },
            [this](interfaces::Wallet&, const QString& address) {
                ++marks; if (fail_retire) return false; retired.insert(address); return true;
            },
            [this](interfaces::Wallet&, const QString& address) {
                if (fail_clear) return false;
                if (address.isEmpty() || current == address) current.clear();
                return true;
            },
        };
    }
};
Command Reserve(bool next = false, uint64_t view = 1)
{
    Command command; command.kind = Kind::ReserveAddress; command.view = view;
    command.draft.address_type = "legacy"; command.next = next; return command;
}
struct Fixture {
    ManualStartupExecution commands, cleanup;
    std::shared_ptr<Wallet> wallet{std::make_shared<Wallet>()};
    Storage storage;
    std::shared_ptr<walletopening::Session> opening{std::make_shared<walletopening::Session>(
        std::make_shared<std::atomic_bool>(false), [] {}, walletopening::Session::Preferences{},
        std::chrono::steady_clock::now, nullptr, walletopening::NativeEvents{}, WalletSessionQueues(commands.executor(), cleanup.executor()))};
    std::shared_ptr<interfaces::Wallet> client{opening->start(wallet).borrower};
    std::shared_ptr<Store> store;
    explicit Fixture(std::function<void(ReservationStorage&)> configure = {}) {
        commands.drain();
        auto port = storage.port();
        if (configure) configure(port);
        store = std::make_shared<Store>(opening, [] {}, Store::Watch{}, QDateTime::currentDateTimeUtc,
            [] { return true; }, port.clear, port);
        Command load; load.kind = Kind::Reload; settle(load);
        QVERIFY(opening->postDomain([this] { store->observed(std::make_shared<const walletreceipts::Snapshot>(walletreceipts::Snapshot{opening->identity(), {}, {}, opening->transactionRevision()})); })); commands.drain();
    }
    ~Fixture() {
        store->close(); opening->stop(false, {}); client.reset(); commands.drain(); cleanup.drain();
        QVERIFY(commands.quiescent() && cleanup.quiescent());
    }
    std::shared_ptr<Operation> settle(Command command) {
        const auto admitted = store->submit(std::move(command));
        if (!admitted) { QTest::qFail("Reservation command was not admitted", __FILE__, __LINE__); return {}; }
        commands.drain(); return admitted.operation;
    }
    Command Resume(const std::shared_ptr<Operation>& operation, const char* password) {
        Command command; command.kind = Kind::ResumeAddress; command.resume = operation->address_intent;
        if (password) command.secret = std::make_shared<SecureString>(password);
        return command;
    }
};
}

class WalletReservationTests : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void cachedTypeAndExplicitDefaultAffinityAreSeparateFacts_data()
    {
        QTest::addColumn<bool>("explicit_default");
        QTest::newRow("ordinary-observation-preserves-actual-cached-type") << false;
        QTest::newRow("new-draft-honors-the-current-default-type") << true;
    }
    void cachedTypeAndExplicitDefaultAffinityAreSeparateFacts()
    {
        QFETCH(bool, explicit_default); Fixture f;
        const auto first = f.settle(Reserve()); QVERIFY(first->result()->address); const auto original = first->result()->address->address;
        uint160 key; key.begin()[0] = 77; const CTxDestination witness{WitnessV0KeyHash{key}};
        if (explicit_default) f.wallet->next_destination = witness;
        auto next = Reserve(); next.draft.address_type = "bech32"; next.explicit_type = explicit_default;
        const auto result = f.settle(next)->result(); QVERIFY(result); QCOMPARE(result->error, Error::None); QVERIFY(result->address);
        QCOMPARE(f.wallet->generated, explicit_default ? 2 : 1);
        QCOMPARE(f.wallet->generated_types.back(), explicit_default ? OutputType::BECH32 : OutputType::LEGACY);
        QCOMPARE(result->address->address_type, explicit_default ? QStringLiteral("bech32") : QStringLiteral("legacy"));
        QCOMPARE(result->address->address, explicit_default ? QString::fromStdString(EncodeDestination(witness)) : original);
        QVERIFY(result->address->persisted); QCOMPARE(f.storage.writes, 2); QCOMPARE(f.wallet->writes, 0);
    }
    void inspectionRetainsTheNewAddressLabelWithoutAnEagerHistoryReload()
    {
        Fixture f; const auto reserved = f.settle(Reserve()); QVERIFY(reserved->result()->address);
        std::optional<Inspection> source;
        QVERIFY(f.store->inspect(0, reserved->address_intent->view, [&](Inspection value) { source = std::move(value); }));
        f.commands.drain(); QVERIFY(source); QVERIFY(source->current_label);
        QCOMPARE(*source->current_label, reserved->result()->address->label);
        QCOMPARE(source->address->address, reserved->result()->address->address);
        QCOMPARE(f.wallet->generated, 1); QCOMPARE(f.wallet->writes, 0);
    }
    void initTestCase() { SelectBaseParams(ChainType::MAIN); SelectParams(ChainType::MAIN); }
    void aggregateKeypoolUnavailabilityIsNotATypeSupportFailure()
    {
        Fixture f; f.wallet->encrypted = true; f.wallet->locked = true; f.wallet->can_generate = false;
        const auto original = f.settle(Reserve());
        QCOMPARE(original->result()->error, Error::LockedAttemptFailed); QCOMPARE(f.wallet->generated, 1);
        const auto resumed = f.settle(f.Resume(original, "correct"));
        QCOMPARE(resumed->result()->error, Error::None); QVERIFY(resumed->result()->address->persisted);
        QCOMPARE(f.wallet->unlocked, 1); QCOMPARE(f.wallet->relocked, 1); QVERIFY(f.wallet->locked);
    }
    void successfulAuthorizationKeepsALaterGenerationFailureSeparate()
    {
        Fixture f; f.wallet->encrypted = true; f.wallet->locked = true;
        const auto original = f.settle(Reserve()); QCOMPARE(original->result()->error, Error::LockedAttemptFailed);
        f.wallet->fail_generate = true;
        const auto resumed = f.settle(f.Resume(original, "correct"));
        QCOMPARE(resumed->result()->error, Error::AddressCreation);
        QVERIFY(!resumed->result()->address); QCOMPARE(f.wallet->unlocked, 1); QCOMPARE(f.wallet->relocked, 1); QVERIFY(f.wallet->locked);
    }
    void preferenceFailureRetainsTheGeneratedAddressAndRetryDoesNotAllocateAgain()
    {
        Fixture f; auto command = Reserve(); command.remember_type = true;
        const auto original = f.settle(command);
        QCOMPARE(original->result()->error, Error::PreferencePersistence); QVERIFY(original->result()->address->persisted);
        QVERIFY(original->result()->preference_requested); QVERIFY(!original->result()->preference_saved);
        const auto address = original->result()->address->address;
        const auto retry = f.settle(f.Resume(original, ""));
        QCOMPARE(retry->result()->error, Error::PreferencePersistence); QCOMPARE(retry->result()->address->address, address);
        QCOMPARE(f.wallet->generated, 1); QCOMPARE(f.wallet->unlocked, 0);
    }
    void revokedReservationCannotBeConsumedEvenBeforeItsGuiReplacementArrives()
    {
        Fixture f; const auto reserved = f.settle(Reserve()); const auto address = reserved->result()->address->address;
        RevokeAddressIntent(reserved->address_intent);
        Command create; create.kind = Kind::Create; create.consume_address = true; create.require_unused = true;
        create.reservation = reserved->address_intent; create.draft.address = address;
        const auto refused = f.settle(create); QCOMPARE(refused->result()->error, Error::Changed);
        QVERIFY(!refused->result()->request_saved); QCOMPARE(f.wallet->writes, 0); QCOMPARE(f.wallet->generated, 1);
    }
    void persistedReservationsKeepExactWalletIdentity_data()
    {
        QTest::addColumn<QString>("parent_name");
        QTest::addColumn<QString>("child_name");
        QTest::newRow("original-address-component") << QStringLiteral("Ledger") << QStringLiteral("Ledger/address");
        QTest::newRow("noncolliding-matched-control") << QStringLiteral("Ledger") << QStringLiteral("Ledger/invoices");
        QTest::newRow("unnamed-wallet") << QString{} << QStringLiteral("address");
        QTest::newRow("unicode-wallet") << QString::fromUtf8("Caf\xc3\xa9") << QString::fromUtf8("Caf\xc3\xa9/address");
    }
    void persistedReservationsKeepExactWalletIdentity()
    {
        QFETCH(QString, parent_name);
        QFETCH(QString, child_name);
        QTemporaryDir identity;
        QVERIFY(identity.isValid());
        const auto previous_format = QSettings::defaultFormat();
        const auto previous_organization = QCoreApplication::organizationName();
        const auto previous_application = QCoreApplication::applicationName();
        QString owned_file;
        // Each body owns a unique INI namespace. Do not clear shared settings,
        // or alter the global search paths used by another test class.
        const auto restore = qScopeGuard([&] {
            if (!owned_file.isEmpty()) QFile::remove(owned_file);
            QSettings::setDefaultFormat(previous_format);
            QCoreApplication::setOrganizationName(previous_organization);
            QCoreApplication::setApplicationName(previous_application);
        });
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QCoreApplication::setOrganizationName(QStringLiteral("BitcoinCoreGuiQmlR14"));
        QCoreApplication::setApplicationName(QStringLiteral("reservation-") + QFileInfo(identity.path()).fileName());
        {
            QSettings settings;
            owned_file = settings.fileName();
        }
        struct NamedWallet final : StubWallet {
            explicit NamedWallet(const QString& value) : name(value.toStdString()) {}
            std::string getWalletName() override { return name; }
            const std::string name;
        };
        NamedWallet parent{parent_name}, child{child_name};
        const auto storage = SettingsReservationStorage();
        const auto parent_address = Address(71);
        const auto child_address = Address(72);
        const auto retired_address = Address(73);
        QVERIFY(storage.write(parent, parent_address));
        QVERIFY(storage.write(child, child_address));
        QVERIFY(storage.retire(child, retired_address));
        QCOMPARE(storage.read(parent, {}).address, parent_address);
        QCOMPARE(storage.read(child, {}).address, child_address);

        // Exercise the production adapter, rather than duplicating its key
        // construction or inferring isolation from an injected map fixture.
        QVERIFY(storage.clear(parent, parent_address));
        const auto child_after_parent_clear = storage.read(child, {});
        QVERIFY(child_after_parent_clear.ok);
        QCOMPARE(child_after_parent_clear.address, child_address);
        QVERIFY(storage.read(child, retired_address).retired);
        QVERIFY(storage.read(parent, {}).address.isEmpty());
    }
    void capturedPasswordRetainsOriginalReceiveIntent_data()
    {
        QTest::addColumn<int>("scenario");QTest::newRow("held-result")<<0;QTest::newRow("quiet-close-after-generation")<<1;QTest::newRow("native-submitted-refused")<<2;
    }
    void capturedPasswordRetainsOriginalReceiveIntent()
    {
        QFETCH(int,scenario);Fixture f;f.wallet->encrypted=true;f.wallet->locked=true;f.wallet->expected_password=std::string(40'000,'x');
        QString source=QString::fromStdString(f.wallet->expected_password);
        auto captured=secretinput::Capture(f.opening,source);
        auto command=Reserve();command.captured_secret=captured.input;command.secret_capture_error=captured.error;
        QVERIFY(captured);
        if(scenario==2)f.commands.reject_core = true;
        const auto admitted=f.store->submit(std::move(command));source.fill(QChar{'y'});QCOMPARE(f.wallet->unlocked,0);QCOMPARE(f.wallet->generated,0);captured.input.reset();
        if(scenario==2){f.commands.drain();QVERIFY(admitted.operation->result());QCOMPARE(admitted.operation->result()->error,Error::Rejected);QCOMPARE(f.wallet->unlocked,0);QCOMPARE(f.wallet->generated,0);}
        else{QVERIFY(admitted);int turns{0};while(f.wallet->generated==0&&!f.commands.quiescent()&&turns++<128)QVERIFY(f.commands.step(Lane::Core) || f.commands.step(Lane::Utility));
            QCOMPARE(f.wallet->unlocked,1);QCOMPARE(f.wallet->generated,1);QCOMPARE(f.wallet->relocked,1);QVERIFY(f.wallet->locked);QVERIFY(!admitted.operation->result());
            if(scenario==1){f.store->close();f.opening->stop(false, {});}
            f.commands.drain();QVERIFY(admitted.operation->result());QVERIFY(admitted.operation->result()->address);QCOMPARE(admitted.operation->result()->address->address,Address(1));}
        f.commands.drain();f.cleanup.drain();
    }
    void allocationAndStorageHaveSeparateNativeAndDomainTurns()
    {
        Fixture f;
        const auto admitted = f.store->submit(Reserve()); QVERIFY(admitted);
        QCOMPARE(f.wallet->generated, 0); QCOMPARE(f.storage.reads, 0);
        while (f.commands.step(Lane::Utility)) {}
        QVERIFY(f.commands.step(Lane::Core)); QCOMPARE(f.storage.reads, 1); QCOMPARE(f.wallet->generated, 0);
        while (f.commands.step(Lane::Utility)) {}
        QVERIFY(f.commands.step(Lane::Core)); QCOMPARE(f.wallet->generated, 1); QCOMPARE(f.storage.writes, 0);
        QVERIFY(!admitted.operation->result());
        while (f.commands.step(Lane::Utility)) {}
        while (f.storage.writes == 0) {
            QVERIFY(f.commands.step(Lane::Core));
            // An independent metadata read may run between Native effects.
            if (f.storage.writes == 0) while (f.commands.step(Lane::Utility)) {}
        }
        QCOMPARE(f.storage.writes, 1);
        QVERIFY(!admitted.operation->result());
        f.commands.drain(); QVERIFY(admitted.operation->result()->address->persisted);
    }
    void reservationFailuresOwnTheirExactNativeEffects_data()
    {
        QTest::addColumn<int>("mode");
        const std::array names{"invalid-type", "aggregate-unavailable-but-generation-succeeds", "taproot-unsupported", "read-failed",
            "read-threw", "missing-read-port", "missing-write-port", "write-failed", "write-threw",
            "generation-failed", "generation-threw", "paid-retirement-failed", "relock-threw", "history-threw"};
        for(int mode=0;mode<int(names.size());++mode)QTest::newRow(names[mode])<<mode;
    }
    void reservationFailuresOwnTheirExactNativeEffects()
    {
        QFETCH(int,mode);
        Fixture f{[mode](ReservationStorage& port){if(mode==5)port.read={};if(mode==6)port.write={};}};
        auto command=Reserve();
        if(mode==0)command.draft.address_type=QStringLiteral("unsupported-output-type");
        if(mode==1)f.wallet->can_generate=false;
        if(mode==2){command.draft.address_type=QStringLiteral("bech32m");f.wallet->taproot=false;}
        f.storage.fail_read=mode==3;f.storage.throw_read=mode==4;
        f.storage.fail_write=mode==7;f.storage.throw_write=mode==8;
        f.wallet->fail_generate=mode==9;f.wallet->throw_generate=mode==10;
        if(mode==11||mode==13){
            f.storage.current=Address(50);f.wallet->book[Destination(50)]="old owner";
            CMutableTransaction tx;tx.vout.emplace_back(100,GetScriptForDestination(Destination(50)));
            interfaces::WalletTx payment{};payment.tx=MakeTransactionRef(tx);payment.txout_is_mine={true};
            f.wallet->transactions.insert(payment);
        }
        f.storage.fail_retire=mode==11;f.wallet->throw_history=mode==13;
        if(mode==12){f.wallet->encrypted=f.wallet->locked=true;f.wallet->throw_lock=true;command.secret=std::make_shared<SecureString>("correct");}
        const auto secret=command.secret;
        const auto operation=f.settle(command);QVERIFY(operation&&operation->result());
        const Error expected[]={Error::Invalid,Error::None,Error::Unsupported,Error::ReservationPersistence,
            Error::Uncertain,Error::Unsupported,Error::ReservationPersistence,Error::ReservationPersistence,
            Error::Uncertain,Error::AddressCreation,Error::Uncertain,Error::ReservationPersistence,Error::Uncertain,Error::Uncertain};
        QCOMPARE(operation->result()->error,expected[mode]);
        const bool generated=mode==1||mode==6||mode==7||mode==8||mode==9||mode==10||mode==12;
        QCOMPARE(f.wallet->generated,generated?1:0);QCOMPARE(f.wallet->writes,0);
        QCOMPARE(f.storage.reads,mode==0||mode==2||mode==5?0:1);
        QCOMPARE(f.storage.writes,mode==1||mode==7||mode==8?1:0);QCOMPARE(f.storage.marks,mode==11?1:0);
        const bool retained_address=mode==1||mode==6||mode==7||mode==8||mode==12;
        QCOMPARE(bool(operation->result()->address),retained_address);
        if(retained_address){QCOMPARE(operation->result()->address->address,Address(1));QCOMPARE(operation->result()->address->persisted,mode==1);}
        if(mode==1)QCOMPARE(f.storage.current,Address(1));
        if(mode==12){QCOMPARE(f.wallet->unlocked,1);QCOMPARE(f.wallet->relocked,1);QVERIFY(secret&&secret->empty());}
        else{QCOMPARE(f.wallet->unlocked,0);QCOMPARE(f.wallet->relocked,0);}
        if(expected[mode]==Error::Uncertain){
            const auto generated_before=f.wallet->generated;
            QCOMPARE(f.settle(Reserve(true))->result()->error,Error::Uncertain);
            QCOMPARE(f.wallet->generated,generated_before);
        }
    }
    void explicitSelectionKeepsTheExistingDestinationType_data()
    {
        QTest::addColumn<int>("mode");
        QTest::newRow("legacy")<<0;QTest::newRow("nested-witness")<<1;QTest::newRow("witness-key")<<2;
        QTest::newRow("witness-script")<<3;QTest::newRow("taproot")<<4;
    }
    void explicitSelectionKeepsTheExistingDestinationType()
    {
        QFETCH(int,mode);
        ECC_Context ecc;CKey key;key.MakeNewKey(true);const auto pubkey=key.GetPubKey();
        CTxDestination destination=PKHash{pubkey};
        if(mode==1)destination=ScriptHash{GetScriptForDestination(WitnessV0KeyHash{pubkey})};
        if(mode==2)destination=WitnessV0KeyHash{pubkey};
        if(mode==3)destination=WitnessV0ScriptHash{GetScriptForMultisig(1,{pubkey})};
        if(mode==4)destination=WitnessV1Taproot{XOnlyPubKey{pubkey}};
        const QString address=QString::fromStdString(EncodeDestination(destination));
        Fixture f;f.wallet->book[destination]="original selection";f.wallet->can_generate=false;f.wallet->taproot=false;
        Command select;select.kind=Kind::SelectAddress;select.draft.address=address;select.draft.address_type=QStringLiteral("legacy");
        const auto operation=f.settle(select);QVERIFY(operation->result());QCOMPARE(operation->result()->error,Error::None);
        QVERIFY(operation->result()->address);QCOMPARE(operation->result()->address->destination,destination);
        QCOMPARE(operation->result()->address->address,address);QCOMPARE(operation->result()->address->label,QStringLiteral("original selection"));
        const QString types[]={QStringLiteral("legacy"),QStringLiteral("p2sh-segwit"),QStringLiteral("bech32"),QStringLiteral("bech32"),QStringLiteral("bech32m")};
        QCOMPARE(operation->result()->address->address_type,types[mode]);QVERIFY(operation->result()->address->persisted);
        QCOMPARE(f.wallet->generated,0);QCOMPARE(f.storage.writes,1);QCOMPARE(f.storage.current,address);
    }
    void historicalUseRequiresPositiveOwnedAddressOutputs_data()
    {
        QTest::addColumn<int>("mode");
        const std::array names{"paid", "unowned", "missing-ownership", "zero-value", "negative-value",
            "non-address-script", "other-address", "matching-later-output"};
        for(int mode=0;mode<int(names.size());++mode)QTest::newRow(names[mode])<<mode;
    }
    void historicalUseRequiresPositiveOwnedAddressOutputs()
    {
        QFETCH(int,mode);Fixture f;
        f.storage.current=Address(50);f.wallet->book[Destination(50)]="original reservation";
        CMutableTransaction tx;tx.nLockTime=100+mode;
        const auto script=mode==5?CScript{}<<OP_TRUE:GetScriptForDestination(Destination(mode==6?51:50));
        tx.vout.emplace_back(mode==3?0:mode==4?-1:100,script);
        interfaces::WalletTx payment{};payment.txout_is_mine=mode==2?std::vector<bool>{}:std::vector<bool>{mode!=1};
        if(mode==7){tx.vout[0].scriptPubKey=GetScriptForDestination(Destination(51));tx.vout.emplace_back(25,GetScriptForDestination(Destination(50)));payment.txout_is_mine={true,true};}
        payment.tx=MakeTransactionRef(tx);const auto original_hash=payment.tx->GetHash();f.wallet->transactions.insert(payment);
        const auto operation=f.settle(Reserve());QVERIFY(operation->result());QCOMPARE(operation->result()->error,Error::None);
        const bool paid=mode==0||mode==7;
        QCOMPARE(operation->result()->address->address,paid?Address(1):Address(50));
        QCOMPARE(f.wallet->generated,paid?1:0);QCOMPARE(f.wallet->reads,1);QCOMPARE(f.storage.marks,paid?1:0);
        QCOMPARE(f.storage.retired.contains(Address(50)),paid);QVERIFY(operation->result()->address->persisted);
        QCOMPARE(f.wallet->transactions.begin()->tx->GetHash(),original_hash);
    }
    void largeHistoricalScanYieldsBeforeAllocation_data()
    {
        QTest::addColumn<int>("boundary");
        QTest::newRow("continue")<<0;QTest::newRow("opening-closes")<<1;
        QTest::newRow("continuation-rejected")<<2;QTest::newRow("newer-intent")<<3;
    }
    void largeHistoricalScanYieldsBeforeAllocation()
    {
        QFETCH(int,boundary);bool independent_domain{false};Fixture f;
        f.storage.current=Address(50);f.wallet->book[Destination(50)]="original reservation";
        CMutableTransaction tx;
        for(int i=0;i<599;++i)tx.vout.emplace_back(1,GetScriptForDestination(Destination(51)));
        tx.vout.emplace_back(100,GetScriptForDestination(Destination(50)));
        interfaces::WalletTx payment{};payment.tx=MakeTransactionRef(tx);payment.txout_is_mine.assign(600,true);f.wallet->transactions.insert(payment);
        const auto original=f.store->submit(Reserve());QVERIFY(original);
        while(f.commands.step(Lane::Utility)){}
        QVERIFY(f.commands.step(Lane::Core));QCOMPARE(f.wallet->reads,1);QCOMPARE(f.wallet->generated,0);
        if(boundary==2)f.opening->utilityQueue()->close();
        QVERIFY(f.commands.step(Lane::Utility));
        std::shared_ptr<Operation> newer;
        if(boundary==0){
            QVERIFY(!f.commands.step(Lane::Core));
            QVERIFY(f.opening->postDomain([&]{independent_domain=true;}));
            QVERIFY(f.commands.step(Lane::Utility));QVERIFY(!independent_domain);QCOMPARE(f.wallet->generated,0);
            QVERIFY(f.commands.step(Lane::Utility));QVERIFY(independent_domain);QCOMPARE(f.wallet->generated,0);
        }else if(boundary==1)f.opening->close();
        else if(boundary==3){const auto admitted=f.store->submit(Reserve(true));QVERIFY(admitted);newer=admitted.operation;}
        f.commands.drain();QVERIFY(original.operation->result());
        const Error expected[]={Error::None,Error::Closed,Error::Rejected,Error::Changed};
        QCOMPARE(original.operation->result()->error,expected[boundary]);
        if(boundary==0){QCOMPARE(original.operation->result()->address->address,Address(1));QCOMPARE(f.storage.marks,1);}
        if(boundary==1||boundary==2){QCOMPARE(f.wallet->generated,0);QCOMPARE(f.storage.writes,0);}
        if(boundary==3){QVERIFY(newer&&newer->result());QCOMPARE(newer->result()->error,Error::None);QCOMPARE(newer->result()->address->address,Address(1));QVERIFY(!original.operation->result()->address);}
        QCOMPARE(f.wallet->generated,boundary==0||boundary==3?1:0);
    }
    void failedHistoricalRetirementCannotAllocateAndExplicitRetryRecovers()
    {
        Fixture f;f.storage.current=Address(50);f.wallet->book[Destination(50)]="paid original";
        CMutableTransaction tx;tx.vout.emplace_back(100,GetScriptForDestination(Destination(50)));
        interfaces::WalletTx payment{};payment.tx=MakeTransactionRef(tx);payment.txout_is_mine={true};f.wallet->transactions.insert(payment);
        f.storage.fail_retire=true;const auto failed=f.settle(Reserve());
        QCOMPARE(failed->result()->error,Error::ReservationPersistence);QVERIFY(!failed->result()->address);
        QCOMPARE(f.wallet->generated,0);QCOMPARE(f.storage.current,Address(50));QCOMPARE(f.storage.marks,1);
        f.storage.fail_retire=false;const auto retry=f.settle(f.Resume(failed,nullptr));
        QCOMPARE(retry->result()->error,Error::None);QCOMPARE(retry->result()->address->address,Address(1));
        QVERIFY(retry->result()->address->persisted);QCOMPARE(f.wallet->generated,1);QCOMPARE(f.storage.marks,2);
        QVERIFY(f.storage.retired.contains(Address(50)));
    }
    void unusedAddressIsRestoredWhileNextAllocates()
    {
        Fixture f; f.storage.current = Address(50); f.wallet->book[Destination(50)] = "private";
        const auto restored = f.settle(Reserve());
        QCOMPARE(restored->result()->address->address, Address(50)); QCOMPARE(f.wallet->generated, 0);
        QCOMPARE(restored->result()->address->label, QString{"private"});
        const auto next = f.settle(Reserve(true));
        QCOMPARE(next->result()->address->address, Address(1)); QCOMPARE(f.wallet->generated, 1);
    }
    void persistedRetirementPreventsRestoreWithoutARequestRecord()
    {
        Fixture f; f.storage.current = Address(50); f.storage.retired.insert(Address(50)); f.wallet->book[Destination(50)] = "old";
        const auto next = f.settle(Reserve());
        QCOMPARE(next->result()->address->address, Address(1)); QCOMPARE(f.wallet->generated, 1);
    }
    void twoViewsKeepIndependentUnusedReservations()
    {
        Fixture f;
        const auto first = f.settle(Reserve(false, 1)); const auto second = f.settle(Reserve(false, 2));
        QVERIFY(first->result()->address->address != second->result()->address->address);
        const auto again = f.settle(Reserve(false, 1));
        QCOMPARE(again->result()->address->address, first->result()->address->address);
        QCOMPARE(f.wallet->generated, 2);
    }
    void resetRemainsOrderedWhenANewerReserveIsAlreadyAdmitted()
    {
        Fixture f; f.storage.current = Address(50); f.wallet->book[Destination(50)] = "old";
        const auto old = f.settle(Reserve()); QCOMPARE(old->result()->address->address, Address(50));
        Command reset; reset.kind = Kind::ForgetAddress; reset.clear_reservation = true; reset.draft.address = Address(50);
        const auto forgotten = f.store->submit(reset);
        const auto next = f.store->submit(Reserve()); QVERIFY(forgotten && next);
        QCOMPARE(f.storage.current, Address(50));
        f.commands.drain(); QCOMPARE(forgotten.operation->result()->error, Error::None);
        QCOMPARE(next.operation->result()->address->address, Address(1));
        QCOMPARE(f.wallet->generated, 1); QCOMPARE(f.storage.current, Address(1));
    }
    void failedResetStillRequiresAFreshAddress()
    {
        Fixture f; f.storage.current = Address(50); f.wallet->book[Destination(50)] = "old"; f.storage.fail_clear = true;
        Command reset; reset.kind = Kind::ForgetAddress; reset.clear_reservation = true;
        QCOMPARE(f.settle(reset)->result()->error, Error::ReservationPersistence);
        const auto next = f.settle(Reserve());
        QCOMPARE(next->result()->address->address, Address(1)); QCOMPARE(f.wallet->generated, 1);
    }
    void resettingInvalidatesTheOriginalPasswordQuestion()
    {
        Fixture f; f.wallet->encrypted = true; f.wallet->locked = true;
        const auto question = f.settle(Reserve()); QCOMPARE(question->result()->error, Error::LockedAttemptFailed);
        Command reset; reset.kind = Kind::ForgetAddress; reset.clear_reservation = true;
        QCOMPARE(f.settle(reset)->result()->error, Error::None);
        const auto resumed = f.settle(f.Resume(question, "correct"));
        QCOMPARE(resumed->result()->error, Error::Changed); QCOMPARE(f.wallet->unlocked, 0); QCOMPARE(f.wallet->generated, 1);
    }
    void invalidSelectionNeverAllocatesAReplacement()
    {
        Fixture f; Command select; select.kind = Kind::SelectAddress; select.draft.address = Address(42);
        QCOMPARE(f.settle(select)->result()->error, Error::Invalid);
        QCOMPARE(f.wallet->generated, 0); QCOMPARE(f.storage.writes, 0);
    }
    void explicitSelectionUsesItsActualTypeWithoutGenerationCapabilities()
    {
        Fixture f; f.wallet->book[Destination(42)] = "selected"; f.wallet->can_generate = false; f.wallet->taproot = false;
        Command reset; reset.kind = Kind::ForgetAddress; reset.clear_reservation = true; f.settle(reset);
        Command select; select.kind = Kind::SelectAddress; select.draft.address = Address(42);
        select.draft.address_type = "bech32m";
        const auto selected = f.settle(select); QCOMPARE(selected->result()->error, Error::None);
        QCOMPARE(selected->result()->address->address, Address(42));
        QCOMPARE(selected->result()->address->address_type, QStringLiteral("legacy"));
        QCOMPARE(f.wallet->generated, 0); QCOMPARE(f.storage.writes, 1);
    }
    void unlockInfrastructureFailureIsNotReportedAsAWrongPassword()
    {
        Fixture f; f.wallet->encrypted = true; f.wallet->locked = true;
        const auto question = f.settle(Reserve()); f.wallet->fail_unlock = true;
        const auto resumed = f.settle(f.Resume(question, "correct"));
        QCOMPARE(resumed->result()->error, Error::AddressCreation);
        QCOMPARE(f.wallet->unlocked, 1); QCOMPARE(f.wallet->relocked, 0); QCOMPARE(f.wallet->generated, 1);
    }
    void passwordQuestionRetainsItsOriginalAttemptAndRelocks()
    {
        Fixture f; f.wallet->encrypted = true; f.wallet->locked = true;
        const auto question = f.settle(Reserve()); QCOMPARE(question->result()->error, Error::LockedAttemptFailed);
        const auto retry = f.Resume(question, "correct"); const auto secret = retry.secret;
        const auto ready = f.settle(retry);
        QCOMPARE(ready->address_intent, question->address_intent);
        QVERIFY(ready->result()->address->persisted); QVERIFY(secret->empty());
        QCOMPARE(f.wallet->unlocked, 1); QCOMPARE(f.wallet->relocked, 1); QVERIFY(f.wallet->locked);
    }
    void wrongPasswordDoesNotAllocateOrRelock()
    {
        Fixture f; f.wallet->encrypted = true; f.wallet->locked = true;
        const auto question = f.settle(Reserve()); const int generated = f.wallet->generated;
        auto retry = f.Resume(question, "wrong"); const auto secret = retry.secret;
        const auto refused = f.settle(retry);
        QCOMPARE(refused->result()->error, Error::IncorrectPassphrase); QVERIFY(secret->empty());
        QCOMPARE(f.wallet->generated, generated); QCOMPARE(f.wallet->relocked, 0); QVERIFY(f.wallet->locked);
    }
    void newerIntentRejectsAnOldPasswordBeforeCoreDispatch()
    {
        Fixture f; f.wallet->encrypted = true; f.wallet->locked = true;
        const auto question = f.settle(Reserve());
        const auto newer = f.store->submit(Reserve(true)); QVERIFY(newer);
        const auto old = f.store->submit(f.Resume(question, "correct")); QVERIFY(old);
        f.commands.drain();
        QCOMPARE(old.operation->result()->error, Error::Changed); QCOMPARE(f.wallet->unlocked, 0);
    }
    void changedIntentAfterCaptureStillCannotUnlock()
    {
        Fixture f; f.wallet->encrypted = true; f.wallet->locked = true;
        const auto question = f.settle(Reserve());
        auto command = f.Resume(question, "correct"); const auto secret = command.secret;
        const auto approval = f.store->submit(command); QVERIFY(approval);
        while (f.commands.step(Lane::Utility)) {}
        QVERIFY(f.commands.step(Lane::Core)); // Capture completed, credentials not used.
        while (f.commands.step(Lane::Utility)) {}
        const auto newer = f.store->submit(Reserve(true)); QVERIFY(newer);
        QVERIFY(f.commands.step(Lane::Core)); // Final Native admission guard sees the newer intent.
        f.commands.drain();
        QCOMPARE(approval.operation->result()->error, Error::Changed);
        QCOMPARE(f.wallet->unlocked, 0); QVERIFY(secret->empty());
    }
    void alreadyUnlockedWalletStaysUnlocked()
    {
        Fixture f; f.wallet->encrypted = true;
        auto command = Reserve(); command.secret = std::make_shared<SecureString>("ignored while unlocked");
        const auto ready = f.settle(command); QVERIFY(ready->result()->address->persisted);
        QCOMPARE(f.wallet->unlocked, 0); QCOMPARE(f.wallet->relocked, 0); QVERIFY(!f.wallet->locked);
    }
    void failedReservationWriteRetriesTheSameGeneratedAddress()
    {
        Fixture f; f.storage.fail_write = true;
        const auto failed = f.settle(Reserve(true));
        QCOMPARE(failed->result()->error, Error::ReservationPersistence);
        QVERIFY(failed->result()->address); QVERIFY(!failed->result()->address->persisted);
        f.storage.fail_write = false;
        const auto retry = f.settle(f.Resume(failed, nullptr));
        QCOMPARE(retry->result()->error, Error::None); QVERIFY(retry->result()->address->persisted);
        QCOMPARE(retry->result()->address->address, failed->result()->address->address);
        QCOMPARE(f.wallet->generated, 1);
    }
    void retirementIsPersistedBeforeRequestWriteEvenIfCleanupFails()
    {
        Fixture f; const auto reserved = f.settle(Reserve());
        const auto address = reserved->result()->address->address;
        f.storage.fail_clear = true;
        Command create; create.kind = Kind::Create; create.consume_address = true; create.reservation = reserved->address_intent;
        create.draft.address = address;
        const auto saved = f.settle(create);
        QVERIFY(saved->result()->request_saved); QCOMPARE(f.wallet->writes, 1);
        QVERIFY(f.storage.retired.contains(address)); QCOMPARE(f.storage.current, address);
        QCOMPARE(saved->result()->error, Error::Partial);
    }
    void failedRetirementDoesNotStartTheRequestWrite()
    {
        Fixture f; const auto reserved = f.settle(Reserve()); f.storage.fail_retire = true;
        Command create; create.kind = Kind::Create; create.consume_address = true; create.reservation = reserved->address_intent;
        create.draft.address = reserved->result()->address->address;
        const auto refused = f.settle(create);
        QCOMPARE(refused->result()->error, Error::ReservationPersistence); QCOMPARE(f.wallet->writes, 0);
    }
    void aQuestionDoesNotPinPhysicalOpeningDrain()
    {
        Fixture f; f.wallet->encrypted = true; f.wallet->locked = true;
        const auto question = f.settle(Reserve()); QCOMPARE(question->result()->error, Error::LockedAttemptFailed);
        f.store->close(); f.opening->stop(false, {}); f.client.reset(); f.commands.drain(); f.cleanup.drain();
        const auto closed = f.opening->take();
        QVERIFY(closed && closed->phase == walletopening::Phase::Closed); QVERIFY(question->address_intent);
    }
    void realWorkersRelockAndRetainTheOutcomeAfterCloseWithoutGuiDelivery_data()
    {
        QTest::addColumn<bool>("captured_long_secret");QTest::newRow("native-secure-string")<<false;QTest::newRow("captured-40000-character-literal")<<true;
    }
    void realWorkersRelockAndRetainTheOutcomeAfterCloseWithoutGuiDelivery()
    {
        QFETCH(bool,captured_long_secret);
        struct State {
            QSemaphore entered, release;
            std::atomic<QThread*> allocated{nullptr}, unlocked{nullptr}, relocked{nullptr}, storage{nullptr}, outcome{nullptr}, destroyed{nullptr};
            std::atomic_bool locked{false};
        };
        struct Native : Wallet {
            explicit Native(std::shared_ptr<State> state) : state(std::move(state)) { encrypted = true; locked = true; }
            ~Native() override { state->destroyed = QThread::currentThread(); }
            util::Expected<void, wallet::WalletError> unlock(const SecureString& password) override {
                state->unlocked = QThread::currentThread(); return Wallet::unlock(password);
            }
            bool lock() override {
                state->relocked = QThread::currentThread(); const bool result = Wallet::lock(); state->locked = locked; return result;
            }
            util::Result<CTxDestination> getNewDestination(OutputType type, const std::string& label) override {
                state->allocated = QThread::currentThread(); state->entered.release(); state->release.acquire();
                return Wallet::getNewDestination(type, label);
            }
            const std::shared_ptr<State> state;
        };
        auto state = std::make_shared<State>();
        execution::Executor executor{execution::Executor::Config{}};
        auto opening = MakeWalletSession(executor, [] {});
        const auto stopped = std::make_shared<std::atomic_bool>(false);
        const auto shutdown = qScopeGuard([&] {
            state->release.release();
            opening->stop(false, [stopped] { *stopped = true; });
            const bool drained = QTest::qWaitFor([&] { return stopped->load(); }, 5'000);
            executor.stopAndJoin();
            QVERIFY(drained);
        });
        auto wallet = std::make_shared<Native>(state);
        auto client = opening->start(wallet).borrower;
        ReservationStorage storage;
        storage.read = [](interfaces::Wallet&, const QString&) { return StoredAddress{}; };
        storage.write = [state](interfaces::Wallet&, const QString&) { state->storage = QThread::currentThread(); return true; };
        auto store = std::make_shared<Store>(opening, [] {}, Store::Watch{}, QDateTime::currentDateTimeUtc, [] { return true; }, Store::Release{}, storage);
        Command load; load.kind = Kind::Reload; const auto loaded = store->submit(load); QVERIFY(loaded);
        QTRY_VERIFY(loaded.operation->result().has_value());
        auto command = Reserve();
        if(captured_long_secret){wallet->expected_password=std::string(40'000,'r');auto captured=secretinput::Capture(opening,QString::fromStdString(wallet->expected_password));QVERIFY(captured);command.captured_secret=std::move(captured.input);}
        else command.secret=std::make_shared<SecureString>("correct");
        const auto secret=command.secret;
        const auto admitted = store->submit(std::move(command), [state] { state->outcome = QThread::currentThread(); }); QVERIFY(admitted);
        QTRY_VERIFY(state->entered.available() > 0);
        QVERIFY(state->allocated.load() != qApp->thread());
        store->close(); opening->stop(false, [stopped] { *stopped = true; }); client.reset(); wallet.reset();
        bool heartbeat{false}; QMetaObject::invokeMethod(qApp, [&] { heartbeat = true; }, Qt::QueuedConnection);
        QTRY_VERIFY(heartbeat); QVERIFY(!stopped->load()); QVERIFY(!admitted.operation->result());
        state->release.release();
        QTRY_VERIFY(admitted.operation->result().has_value());
        QVERIFY(admitted.operation->result()->address); if(secret)QVERIFY(secret->empty());
        QTRY_VERIFY(stopped->load());
        QVERIFY(state->locked.load());
        QCOMPARE(static_cast<const void*>(state->unlocked.load()),
                 static_cast<const void*>(state->allocated.load()));
        QCOMPARE(static_cast<const void*>(state->relocked.load()),
                 static_cast<const void*>(state->allocated.load()));
        QVERIFY(state->storage.load() && state->storage.load() != qApp->thread());
        QVERIFY(state->outcome.load() && state->outcome.load() != qApp->thread());
        QVERIFY(state->destroyed.load() && state->destroyed.load() != qApp->thread());
    }
};

#include <test_walletreservations.moc>
BITCOINQML_REGISTER_QT_TEST(WalletReservationTests)
