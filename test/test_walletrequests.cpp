// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/backendexecutor.h>
#include <qml/models/receiverequesthistorymodel.h>
#include <qml/walletrequests.h>
#include <qml/walletreceipts.h>
#include <test/mocks/manualexecutionenvironment.h>
#include <test/mocks/mockwallet.h>
#include <test/qt_test_registry.h>

#include <key_io.h>
#include <util/string.h>

#include <QScopeGuard>
#include <QSemaphore>
#include <QtTest/QtTest>

#include <array>
#include <limits>
#include <stdexcept>

namespace {
using namespace walletrequests;
using execution::Lane;
QString Address() { return QString::fromStdString(EncodeDestination(PKHash{uint160{}})); }
QmlRecentRequestEntry Entry(int64_t id = 1)
{
    QmlRecentRequestEntry entry;
    entry.id = id; entry.date = QDateTime::fromSecsSinceEpoch(1'700'000'000);
    entry.recipient.address = Address().toStdString(); entry.recipient.amount = 100;
    entry.recipient.label = "public"; entry.recipient.message = "message"; entry.recipient.noteSelf = "private";
    return entry;
}
Command Intent(Kind kind, int64_t id = 0, uint64_t revision = 0)
{
    Command command; command.kind = kind; command.id = id; command.revision = revision; return command;
}
Command Create()
{
    Command command; command.kind = Kind::Create;
    command.draft = {Address(), "public", "message", "private", {}, 100};
    return command;
}
struct Wallet : StubWallet {
    std::map<std::string, std::string> stored;
    std::set<interfaces::WalletTx> transactions;
    std::vector<std::string> effects;
    std::vector<std::pair<std::string, std::string>> written_requests;
    std::string label{"private"};
    int loads{0}, writes{0}, history_reads{0};
    bool throw_history{false};
    int fail_write{0}, throw_write{0};
    bool fail_label{false}, fail_all_writes{false}, address_exists{true};
    int label_writes{0}, fail_label_number{0}, throw_label_number{0};
    std::map<CTxDestination, std::string> book;
    std::vector<std::optional<wallet::AddressPurpose>> purposes;
    std::function<void()> before_label;
    TransactionChangedFn transaction;
    std::string getWalletName() override { return "requests"; }
    std::unique_ptr<interfaces::Handler> handleTransactionChanged(TransactionChangedFn fn) override { transaction = std::move(fn); return {}; }
    std::set<interfaces::WalletTx> getWalletTxs() override {
        ++history_reads;
        if (throw_history) throw std::runtime_error("request history unavailable");
        return transactions;
    }
    std::vector<std::string> getAddressReceiveRequests() override {
        ++loads; std::vector<std::string> result;
        result.reserve(stored.size());
        for (const auto& [id, blob] : stored) result.push_back(blob);
        return result;
    }
    std::vector<interfaces::WalletAddress> getAddresses() override {
        return {{DecodeDestination(Address().toStdString()), true, wallet::AddressPurpose::RECEIVE, label}};
    }
    bool getAddress(const CTxDestination& destination, std::string* name, wallet::AddressPurpose* purpose) override {
        if (purpose) *purpose = wallet::AddressPurpose::RECEIVE;
        if (name) {
            const auto found = book.find(destination);
            *name = found != book.end() ? found->second : destination == DecodeDestination(Address().toStdString()) ? label : "private";
        }
        return address_exists || book.contains(destination);
    }
    bool setAddressBook(const CTxDestination& destination, const std::string& name, const std::optional<wallet::AddressPurpose>& purpose) override {
        if (before_label) before_label();
        effects.push_back("label:" + name); ++label_writes; purposes.push_back(purpose);
        if (label_writes == throw_label_number) { book[destination] = name; label = name; throw std::runtime_error("uncertain note write"); }
        if (fail_label || label_writes == fail_label_number) return false;
        book[destination] = name; label = name; return true;
    }
    bool setAddressReceiveRequest(const CTxDestination&, const std::string& id, const std::string& blob) override {
        written_requests.emplace_back(id, blob);
        effects.push_back(blob.empty() ? "erase:" + id : "write:" + id); ++writes;
        if (writes == throw_write) { stored[id] = blob; throw std::runtime_error("uncertain native write"); }
        if (fail_all_writes || writes == fail_write) return false;
        if (blob.empty()) stored.erase(id); else stored[id] = blob;
        return true;
    }
};
struct Fixture {
    ManualRoutedExecution commands, retirement;
    std::shared_ptr<Wallet> wallet{std::make_shared<Wallet>()};
    std::shared_ptr<walletopening::Session> opening;
    std::shared_ptr<interfaces::Wallet> client;
    std::shared_ptr<Store> store;
    int wakes{0}, watches{0};
    QSet<QString> watched;
    std::atomic_bool reader_ready{true};
    explicit Fixture(Store::Encoder encoder = Encode, Store::Decoder decoder = DecodeOne, Store::Prepare prepare = FreezeRecord) {
        opening = std::make_shared<walletopening::Session>(commands.scheduler, retirement.scheduler,
            std::make_shared<std::atomic_bool>(false), [] {});
        client = opening->start(wallet).borrower; commands.drain();
        store = std::make_shared<Store>(opening, [this] { ++wakes; }, [this](QSet<QString> addresses) { ++watches; watched = std::move(addresses); }, [] { return Entry().date; },
            [this] { return reader_ready.load(); }, Store::Release{}, ReservationStorage{}, std::move(encoder), std::move(decoder), std::move(prepare));
    }
    ~Fixture() {
        store->close(); opening->retire(); client.reset(); commands.drain(); retirement.drain();
        QVERIFY(commands.quiescent() && retirement.quiescent());
    }
    std::shared_ptr<Operation> settle(Command command) {
        auto admitted = store->submit(std::move(command)); commands.drain(); return admitted.operation;
    }
    std::shared_ptr<const walletreceipts::Snapshot> facts(std::set<QString> addresses = {}) const {
        return std::make_shared<const walletreceipts::Snapshot>(walletreceipts::Snapshot{opening->identity(), {}, std::move(addresses), opening->transactionRevision()});
    }
    void observe(std::set<QString> addresses = {}) {
        const auto store_copy = store; auto snapshot = facts(std::move(addresses));
        QVERIFY(opening->postDomain([store_copy, snapshot = std::move(snapshot)] { store_copy->observed(snapshot); }));
        commands.drain();
    }
    void load(int count = 1) {
        for (int id{1}; id <= count; ++id) wallet->stored[util::ToString(id)] = Encode(Entry(id));
        QVERIFY(settle(Intent(Kind::Reload))->result()->error == Error::None); observe();
    }
    Record record(int64_t id = 1) {
        const auto update = store->take();
        if (!update || !update->snapshot || !update->snapshot->find(id)) { QTest::qFail("Missing authoritative request", __FILE__, __LINE__); return {}; }
        return *update->snapshot->find(id);
    }
    Command edit(const Record& record, QString note = "edited") {
        auto command = Create(); command.kind = Kind::Edit; command.id = record.entry.id; command.revision = record.revision;
        command.draft.note = std::move(note); return command;
    }
};
}

class WalletRequestsTests : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void legacyHistoryPreviewsRemainBoundedWithoutTruncatingTheRecord()
    {
        Fixture f; auto legacy = Entry(); legacy.recipient.message = std::string(20'000, 'm');
        legacy.recipient.label = std::string(1024, 'l'); legacy.recipient.noteSelf = std::string(2048, 'n');
        f.wallet->stored["1"] = Encode(legacy);
        const auto loaded = f.settle(Intent(Kind::Reload)); QCOMPARE(loaded->result()->error, Error::None); f.observe();
        const auto snapshot = f.store->take()->snapshot; QVERIFY(snapshot); QCOMPARE(snapshot->records.size(), size_t{1});
        ReceiveRequestHistoryModel model; model.setSnapshot(snapshot);
        QVERIFY(model.data(model.index(0), ReceiveRequestHistoryModel::UriRole).toString().isEmpty());
        for (const auto role : {ReceiveRequestHistoryModel::LabelRole, ReceiveRequestHistoryModel::MessageRole, ReceiveRequestHistoryModel::NoteSelfRole}) {
            const auto preview = model.data(model.index(0), role).toString();
            QCOMPARE(preview.size(), 513); QVERIFY(preview.endsWith(QChar{0x2026}));
        }
        QCOMPARE(snapshot->find(1)->entry.recipient.message, legacy.recipient.message);
        QCOMPARE(snapshot->find(1)->display.message.size(), 20'000);
        QCOMPARE(Decode({f.wallet->stored.at("1")}).entries.front().recipient.message, legacy.recipient.message);
        QCOMPARE(f.wallet->writes, 0); QCOMPARE(f.wallet->label_writes, 0);
    }
    void requestNoteConflictRetainsTheSavedRequestWithoutOverwritingTheAddress_data()
    {
        QTest::addColumn<bool>("external");
        QTest::newRow("unchanged-label") << false; QTest::newRow("external-relabel") << true;
    }
    void requestNoteConflictRetainsTheSavedRequestWithoutOverwritingTheAddress()
    {
        QFETCH(bool, external);
        Fixture f; f.load(); auto command = f.edit(f.record(), "new request note"); command.expected_label = "private";
        const auto operation = f.store->submit(command).operation; QVERIFY(operation);
        while (f.commands.step(Lane::Domain)) {}
        if (external) f.wallet->label = "external address note";
        f.commands.drain(); const auto result = operation->result(); QVERIFY(result); QVERIFY(result->request_saved);
        QCOMPARE(result->error, external ? Error::Changed : Error::None); QCOMPARE(result->label_saved, !external);
        QCOMPARE(f.wallet->label_writes, external ? 0 : 1);
        QCOMPARE(f.wallet->label, external ? std::string{"external address note"} : std::string{"new request note"});
        const auto saved = Decode({f.wallet->stored.at("1")}).entries.front();
        QCOMPARE(saved.recipient.noteSelf, std::string{"new request note"}); QCOMPARE(saved.recipient.label, std::string{"public"});
        QCOMPARE(saved.recipient.message, std::string{"message"}); QCOMPARE(saved.recipient.amount, CAmount{100});
    }
    void confirmedPersistenceOwnsItsOutcomeWhenRowPreparationFails_data()
    {
        QTest::addColumn<int>("mode");
        QTest::newRow("create") << 0;
        QTest::newRow("edit") << 1;
        QTest::newRow("label-batch") << 2;
        QTest::newRow("label-multiple-batches") << 3;
        QTest::newRow("label-known-write-failure") << 4;
        QTest::newRow("label-uncertain-after-write") << 5;
        QTest::newRow("create-after-opening-close") << 6;
        QTest::newRow("create-non-standard-exception") << 7;
    }
    void confirmedPersistenceOwnsItsOutcomeWhenRowPreparationFails()
    {
        QFETCH(int, mode);
        bool reject_preparation{false}; int completed{0}, rejected_projections{0};
        const bool create = mode == 0 || mode == 6 || mode == 7;
        const int64_t failed_id = mode == 1 ? 1 : 2;
        Fixture f{Encode, DecodeOne, [&](Record row) {
            if (reject_preparation && row.entry.id == failed_id) {
                ++rejected_projections;
                if (mode == 7) throw 42;
                throw std::runtime_error("confirmed request projection unavailable");
            }
            return FreezeRecord(std::move(row));
        }};
        const int count = create || mode == 1 ? 1 : mode == 3 ? 70 : 3;
        f.load(count);
        auto command = Create();
        if (mode == 1) command = f.edit(f.record(), "persisted-private");
        if (!create && mode != 1) { command = Intent(Kind::Label); command.address = Address(); command.label = "persisted-private"; }
        if (mode == 4) f.wallet->fail_write = 3;
        if (mode == 5) f.wallet->throw_write = 3;
        reject_preparation = true;
        const auto admitted = f.store->submit(command, [&] { ++completed; }); QVERIFY(admitted);
        if (mode == 6) {
            while (f.commands.step(Lane::Domain)) {}
            QVERIFY(f.commands.step(Lane::Core)); QCOMPARE(f.wallet->writes, 1);
            f.opening->close();
        }
        bool escaped{false};
        try { f.commands.drain(); }
        catch (...) { escaped = true; }
        // Public persistence confirms every write in the completed Native batch,
        // including rows after the one whose projection failed on Domain.
        const int writes = create || mode == 1 ? 1 : mode == 3 ? 32 : 3;
        const int saved = mode == 4 || mode == 5 ? 2 : writes;
        QCOMPARE(f.wallet->writes, writes); QCOMPARE(rejected_projections, 1);
        const auto persisted = Decode({f.wallet->stored.at(util::ToString(failed_id))});
        QCOMPARE(persisted.entries.size(), size_t{1});
        QCOMPARE(persisted.entries.front().recipient.noteSelf, create ? std::string{"private"} : std::string{"persisted-private"});
        if (!create && mode != 1) for (int id{1}; id <= count; ++id) {
            const auto row = Decode({f.wallet->stored.at(util::ToString(id))}).entries.front();
            QCOMPARE(row.recipient.noteSelf, id <= (mode == 5 ? writes : saved) ? std::string{"persisted-private"} : std::string{"private"});
        }
        QVERIFY2(!escaped, "A Domain projection failure escaped after confirmed Native persistence");
        QVERIFY(admitted.operation->result()); const auto result = *admitted.operation->result();
        QCOMPARE(result.error, mode == 5 ? Error::Uncertain : Error::Partial);
        QCOMPARE(result.records_saved, static_cast<size_t>(saved));
        QCOMPARE(result.records_total, create || mode == 1 ? size_t{1} : static_cast<size_t>(count));
        QCOMPARE(result.request_saved, create || mode == 1); QCOMPARE(result.label_saved, !create);
        QVERIFY(!result.record); QCOMPARE(completed, 1); QVERIFY(!f.store->presentationCurrent());
        const auto publication = f.store->take(); QVERIFY(publication && !publication->loaded);
        if (mode == 6) {
            QCOMPARE(f.store->submit(Create()).rejection, execution::Rejection::Closed);
            QCOMPARE(admitted.operation->result()->id, int64_t{2});
            return;
        }
        const auto refused = f.settle(Create()); QVERIFY(refused && refused->result());
        QVERIFY(refused->result()->error != Error::None); QVERIFY(!refused->result()->request_saved);
        QCOMPARE(f.wallet->writes, writes);
        // Explicit reload is a read of the already saved values, never a replay
        // of the failed operation or its remaining label batches.
        reject_preparation = false;
        QCOMPARE(f.settle(Intent(Kind::Reload))->result()->error, Error::None); f.observe();
        QVERIFY(f.store->presentationCurrent()); QCOMPARE(f.wallet->writes, writes);
        const auto recovered = f.record(failed_id);
        QCOMPARE(recovered.entry.recipient.noteSelf, persisted.entries.front().recipient.noteSelf);
        QCOMPARE(admitted.operation->result()->records_saved, static_cast<size_t>(saved));
        QCOMPARE(completed, 1);
    }
    void receiptsCannotReplayAConfirmedRequestWhileItsProjectionNeedsReload()
    {
        bool reject_preparation{false};
        Fixture f{Encode, DecodeOne, [&](Record row) {
            if (reject_preparation && row.entry.id == 2)
                throw std::runtime_error("confirmed request projection unavailable");
            return FreezeRecord(std::move(row));
        }};
        f.load(); reject_preparation = true;
        const auto original = f.store->submit(Create()); QVERIFY(original);
        bool escaped{false};
        try { f.commands.drain(); }
        catch (const std::runtime_error&) { escaped = true; }
        QCOMPARE(f.wallet->writes, 1);
        QVERIFY2(!escaped, "A confirmed request lost its terminal outcome during projection");
        QVERIFY(original.operation->result()); QCOMPARE(original.operation->result()->error, Error::Partial);
        QVERIFY(original.operation->result()->request_saved);
        QCOMPARE(original.operation->result()->records_saved, size_t{1});
        reject_preparation = false;
        // A new cumulative receipt changes the cached old row and its revision.
        // It cannot restore completeness or derive Native writes from that cache.
        f.observe({Address()}); QVERIFY(!f.store->presentationCurrent());
        QCOMPARE(f.wallet->writes, 1); QCOMPARE(f.wallet->written_requests.size(), size_t{1});
        const auto refused = f.settle(Create()); QVERIFY(refused && refused->result());
        QVERIFY(refused->result()->error != Error::None); QVERIFY(!refused->result()->request_saved);
        QCOMPARE(f.wallet->writes, 1);
        const auto first_payload = Decode({f.wallet->written_requests.front().second}).entries.front();
        QCOMPARE(first_payload.id, int64_t{2}); QVERIFY(!first_payload.payment_received);
        QCOMPARE(f.settle(Intent(Kind::Reload))->result()->error, Error::None); f.observe({Address()});
        QVERIFY(f.store->presentationCurrent());
        // Reload discovers both real rows. Only their new paid flags need writes;
        // the originally saved unpaid payload is never replayed.
        QCOMPARE(f.wallet->writes, 3); QCOMPARE(f.wallet->written_requests.size(), size_t{3});
        QCOMPARE(f.wallet->written_requests[1].first, std::string{"1"});
        QCOMPARE(f.wallet->written_requests[2].first, std::string{"2"});
        auto paid_payload = first_payload; paid_payload.payment_received = true;
        QCOMPARE(f.wallet->written_requests[2].second, Encode(paid_payload));
        for (size_t n{1}; n < f.wallet->written_requests.size(); ++n)
            QVERIFY(Decode({f.wallet->written_requests[n].second}).entries.front().payment_received);
        const auto recovered = f.store->take(); QVERIFY(recovered && recovered->loaded && recovered->snapshot);
        QCOMPARE(recovered->snapshot->records.size(), size_t{2});
        QVERIFY(recovered->snapshot->find(1) && recovered->snapshot->find(2));
        QVERIFY(recovered->snapshot->find(1)->entry.payment_received);
        QVERIFY(recovered->snapshot->find(2)->entry.payment_received);
        QCOMPARE(original.operation->result()->error, Error::Partial);
        QCOMPARE(original.operation->result()->records_saved, size_t{1});
    }
    void unusedAddressCheckRequiresPositiveOwnedMatchingOutputs_data()
    {
        QTest::addColumn<int>("mode");
        const std::array names{"paid", "unowned", "missing-ownership", "zero-output", "negative-output", "non-address-output", "other-address", "matching-later-output", "empty-transaction"};
        for (size_t i{0}; i < names.size(); ++i) QTest::newRow(names[i]) << int(i);
    }
    void unusedAddressCheckRequiresPositiveOwnedMatchingOutputs()
    {
        QFETCH(int, mode);
        Fixture f; f.load();
        const auto original = f.wallet->stored.at("1");
        CMutableTransaction transaction;
        const auto destination = DecodeDestination(Address().toStdString());
        uint160 other_key; other_key.begin()[0] = 1;
        const auto script = mode == 5 ? CScript{} << OP_TRUE
            : GetScriptForDestination(mode == 6 ? CTxDestination{PKHash{other_key}} : destination);
        transaction.vout.emplace_back(mode == 3 ? 0 : mode == 4 ? -1 : 100, script);
        interfaces::WalletTx history{};
        if (mode != 8) history.tx = MakeTransactionRef(transaction);
        if (mode != 2) history.txout_is_mine = {mode != 1};
        if (mode == 7) {
            transaction.vout[0].scriptPubKey = GetScriptForDestination(PKHash{other_key});
            transaction.vout.emplace_back(50, GetScriptForDestination(destination));
            history.tx = MakeTransactionRef(transaction); history.txout_is_mine = {true, true};
        }
        f.wallet->transactions.insert(history);
        auto create = Create(); create.require_unused = true;
        const auto operation = f.settle(create); QVERIFY(operation && operation->result());
        const auto result = *operation->result();
        const bool paid = mode == 0 || mode == 7;
        QCOMPARE(result.error, paid ? Error::Paid : Error::None);
        QCOMPARE(result.request_saved, !paid);
        QCOMPARE(f.wallet->history_reads, 1);
        QCOMPARE(f.wallet->writes, paid ? 0 : 1);
        QCOMPARE(f.wallet->stored.at("1"), original);
        QCOMPARE(result.paid_address, paid ? Address() : QString{});
        if (!paid) {
            QVERIFY(result.record); QCOMPARE(result.id, int64_t{2});
            const auto decoded = Decode({f.wallet->stored.at("2")});
            QCOMPARE(decoded.entries.size(), size_t{1});
            QCOMPARE(decoded.entries.front().recipient.amount, CAmount{100});
            QCOMPARE(decoded.entries.front().recipient.noteSelf, std::string{"private"});
        }
    }
    void waitingPublicationReportsBusyOnceACommandIsQueued()
    {
        // The GUI marks storage busy when it submits and then adopts the
        // busy flag of the next publication it takes. A publication built
        // before the submit, but taken after it, must not report idle while
        // the command has not run yet.
        Fixture idle; idle.load();
        const auto settled = idle.store->take(); QVERIFY(settled); QVERIFY(!settled->busy);

        Fixture f; f.load();
        const auto admitted = f.store->submit(Intent(Kind::Reload)); QVERIFY(admitted);
        const auto waiting = f.store->take(); QVERIFY(waiting); QVERIFY(waiting->busy);
        QVERIFY(!admitted.operation->result());
        f.commands.drain(); QVERIFY(admitted.operation->result());
        QCOMPARE(admitted.operation->result()->error, Error::None);
        f.observe();
        const auto done = f.store->take(); QVERIFY(done); QVERIFY(!done->busy);
    }
    void nativeHistoryFailureFencesMutationUntilAnExplicitReload()
    {
        Fixture f; f.load(); const auto original = f.wallet->stored;
        f.wallet->throw_history = true;
        auto create = Create(); create.require_unused = true;
        const auto failed = f.settle(create); QVERIFY(failed && failed->result());
        QCOMPARE(failed->result()->error, Error::Uncertain);
        QVERIFY(!failed->result()->request_saved); QCOMPARE(f.wallet->writes, 0);
        QCOMPARE(f.wallet->history_reads, 1); QCOMPARE(f.wallet->stored, original);
        f.wallet->throw_history = false;
        QCOMPARE(f.settle(Create())->result()->error, Error::Uncertain);
        QCOMPARE(f.wallet->writes, 0); QCOMPARE(f.wallet->history_reads, 1);
        QCOMPARE(f.settle(Intent(Kind::Reload))->result()->error, Error::None); f.observe();
        const auto retried = f.settle(create); QVERIFY(retried && retried->result());
        QCOMPARE(retried->result()->error, Error::None); QVERIFY(retried->result()->request_saved);
        QCOMPARE(f.wallet->history_reads, 2); QCOMPARE(f.wallet->writes, 1);
        // The failed create attempted id 2; recovery must not reuse it.
        QCOMPARE(retried->result()->id, int64_t{3});
    }
    void disappearedAddressBookEntryKeepsConfirmedRequestWrites_data()
    {
        QTest::addColumn<bool>("label_first");
        QTest::newRow("label-before-history") << true;
        QTest::newRow("history-before-label") << false;
    }
    void disappearedAddressBookEntryKeepsConfirmedRequestWrites()
    {
        QFETCH(bool, label_first);
        Fixture f; f.load(); const auto original = f.wallet->stored.at("1");
        f.wallet->address_exists = false;
        auto command = Create(); command.draft.note = "new-private";
        if (label_first) { command = Intent(Kind::Label); command.address = Address(); command.label = "new-private"; }
        const auto operation = f.settle(command); QVERIFY(operation && operation->result());
        const auto result = *operation->result();
        QCOMPARE(result.error, Error::LabelPersistence); QVERIFY(!result.label_saved);
        QCOMPARE(result.request_saved, !label_first);
        QCOMPARE(result.records_saved, label_first ? size_t{0} : size_t{1});
        QCOMPARE(f.wallet->label_writes, 0); QCOMPARE(f.wallet->writes, label_first ? 0 : 1);
        QCOMPARE(f.wallet->stored.at("1"), original);
        QCOMPARE(f.wallet->label, std::string{"private"});
        if (!label_first) {
            QVERIFY(result.record);
            QCOMPARE(result.record->entry.recipient.noteSelf, std::string{"new-private"});
            QCOMPARE(Decode({f.wallet->stored.at("2")}).entries.front().recipient.noteSelf, std::string{"new-private"});
        }
    }
    void queuedRequestOwnsItsOutcomeWhenContinuationAdmissionChanges_data()
    {
        QTest::addColumn<int>("boundary");
        QTest::newRow("continue") << 0;
        QTest::newRow("reject-next-domain") << 1;
        QTest::newRow("close-ordinary-admission") << 2;
    }
    void queuedRequestOwnsItsOutcomeWhenContinuationAdmissionChanges()
    {
        QFETCH(int, boundary);
        int completed{0};
        Fixture f; f.load(); const auto original = f.wallet->stored.at("1");
        const auto first = f.store->submit(Create(), [&] {
            ++completed;
            if (boundary == 1) f.commands.rejectLane(Lane::Domain);
            if (boundary == 2) f.commands.scheduler->stopAcceptingWork();
        });
        const auto second = f.store->submit(Create(), [&] { ++completed; });
        QVERIFY(first && second); QVERIFY(!first.operation->result() && !second.operation->result());
        f.commands.drain();
        QVERIFY(first.operation->result() && second.operation->result());
        QCOMPARE(first.operation->result()->error, Error::None);
        QVERIFY(first.operation->result()->request_saved);
        QCOMPARE(second.operation->result()->error, boundary == 1 ? Error::Rejected : Error::None);
        QCOMPARE(second.operation->result()->request_saved, boundary != 1);
        QCOMPARE(f.wallet->writes, boundary == 1 ? 1 : 2);
        QCOMPARE(f.wallet->stored.at("1"), original);
        QCOMPARE(first.operation->result()->record->entry.id, int64_t{2});
        QCOMPARE(Decode({f.wallet->stored.at("2")}).entries.front().id, int64_t{2});
        if (boundary != 1) QCOMPARE(second.operation->result()->record->entry.id, int64_t{3});
        QCOMPARE(completed, 2);
    }
    void oversizedInterestIsRejectedBeforeScheduling()
    {
        Fixture f; f.load();
        const auto watches = f.watches; const auto trace = f.commands.traceSnapshot();
        const auto admission = f.store->interest(17, QString(129, 'a'));
        QCOMPARE(admission.rejection, execution::Rejection::Capacity);
        QCOMPARE(f.commands.traceSnapshot(), trace); QCOMPARE(f.watches, watches);
        QVERIFY(!f.watched.contains(QString(129, 'a')));
    }
    void interestQueuedBeforeCloseCannotPublishAfterClose()
    {
        Fixture f; f.load(); const auto watches = f.watches;
        QVERIFY(f.store->interest(17, QStringLiteral("late-visible-address")));
        f.store->close(); f.commands.drain();
        QCOMPARE(f.watches, watches); QVERIFY(!f.watched.contains(QStringLiteral("late-visible-address")));
        QCOMPARE(f.store->interest(18, Address()).rejection, execution::Rejection::Closed);
        QCOMPARE(f.wallet->writes, 0);
    }
    void sentNotesChangeOnlyTheBookAndPreserveRequestRows()
    {
        Fixture f; f.load(3); const auto original = f.store->take()->snapshot; const auto blobs = f.wallet->stored;
        const auto txid = Txid::FromUint256(uint256{uint8_t{9}});
        auto accepted = f.store->submitSentLabels(f.opening->startAction(), txid, {{Address(), "outgoing note"}}); QVERIFY(accepted);
        QVERIFY(!accepted.operation->result()); QCOMPARE(f.wallet->label_writes, 0);
        f.commands.drain(); const auto result = *accepted.operation->result();
        QCOMPARE(result.error, Error::None); QCOMPARE(result.sent_transaction, txid); QCOMPARE(result.notes_total, size_t{1}); QCOMPARE(result.notes_saved, size_t{1});
        QCOMPARE(f.wallet->stored, blobs); QCOMPARE(f.wallet->writes, 0); QCOMPARE(f.wallet->label, std::string{"outgoing note"});
        QCOMPARE(f.wallet->purposes.back(), std::optional<wallet::AddressPurpose>{});
        const auto current = f.store->take()->snapshot;
        for (int id{1}; id <= 3; ++id) QCOMPARE(current->find(id), original->find(id));
        QCOMPARE(current->labels.at(Address()), QStringLiteral("outgoing note"));
        const auto same = f.store->submitSentLabels(f.opening->startAction(), txid, {{Address(), "outgoing note"}}); QVERIFY(same); f.commands.drain();
        QCOMPARE(same.operation->result()->notes_unchanged, size_t{1}); QCOMPARE(same.operation->result()->notes_saved, size_t{0}); QCOMPARE(f.wallet->label_writes, 1);
    }
    void sentNotesUseSendPurposeOnlyForAnUnknownAddress_data()
    {
        QTest::addColumn<bool>("exists"); QTest::newRow("owned-receive") << true; QTest::newRow("new-send") << false;
    }
    void sentNotesUseSendPurposeOnlyForAnUnknownAddress()
    {
        QFETCH(bool, exists); Fixture f; f.wallet->address_exists = exists;
        const auto admitted = f.store->submitSentLabels(f.opening->startAction(), Txid::FromUint256(uint256{uint8_t{1}}), {{Address(), "user note"}}); QVERIFY(admitted);
        f.commands.drain(); QCOMPARE(admitted.operation->result()->error, Error::None); QCOMPARE(f.wallet->label_writes, 1);
        QCOMPARE(f.wallet->purposes.back(), exists ? std::optional<wallet::AddressPurpose>{} : std::optional{wallet::AddressPurpose::SEND});
        QCOMPARE(f.wallet->writes, 0); // History loading is not a precondition for outgoing metadata.
    }
    void acceptedSentNotesYieldAndFinishAfterOriginalOpeningCloses()
    {
        Fixture f; f.load(3); const auto blobs = f.wallet->stored; const auto core_before = f.commands.coreSubmissions();
        QMap<QString, QString> labels;
        for (unsigned char n{0}; n < 65; ++n) { uint160 key; key.begin()[0] = n; labels.insert(QString::fromStdString(EncodeDestination(PKHash{key})), QStringLiteral("note-%1").arg(n)); }
        const auto operation = f.store->submitSentLabels(f.opening->startAction(), Txid::FromUint256(uint256{uint8_t{1}}), labels).operation; QVERIFY(operation);
        int largest{0}; bool closed{false};
        while (!f.commands.quiescent()) {
            const auto before = f.wallet->label_writes;
            QVERIFY(f.commands.stepNext()); largest = std::max(largest, f.wallet->label_writes - before); QVERIFY(f.wallet->label_writes - before <= 16);
            if (f.wallet->label_writes && !closed) {
                closed = true; QVERIFY(!operation->result()); f.store->close(); f.opening->retire(); f.client.reset();
            }
        }
        f.retirement.drain(); QVERIFY(closed); QCOMPARE(largest, 16); QCOMPARE(f.wallet->label_writes, 65);
        QVERIFY(operation->result()); QCOMPARE(operation->result()->error, Error::None); QCOMPARE(operation->result()->notes_saved, size_t{65});
        QCOMPARE(f.commands.coreSubmissions() - core_before, size_t{5}); QCOMPARE(f.wallet->stored, blobs);
    }
    void sentNoteFailureRetainsConfirmedCounts_data()
    {
        QTest::addColumn<bool>("uncertain"); QTest::newRow("known-failure") << false; QTest::newRow("exception-after-effect") << true;
    }
    void sentNoteFailureRetainsConfirmedCounts()
    {
        QFETCH(bool, uncertain); Fixture f; f.load();
        if (uncertain) f.wallet->throw_label_number = 17; else f.wallet->fail_label_number = 17;
        QMap<QString, QString> labels;
        for (unsigned char n{0}; n < 33; ++n) { uint160 key; key.begin()[0] = n; labels.insert(QString::fromStdString(EncodeDestination(PKHash{key})), QStringLiteral("note-%1").arg(n)); }
        const auto op = f.store->submitSentLabels(f.opening->startAction(), Txid::FromUint256(uint256{uint8_t{1}}), labels).operation; QVERIFY(op); f.commands.drain();
        const auto result = *op->result(); QCOMPARE(result.error, uncertain ? Error::Uncertain : Error::Partial);
        QCOMPARE(result.notes_total, size_t{33}); QCOMPARE(result.notes_saved, uncertain ? size_t{16} : size_t{32});
        QCOMPARE(result.notes_failed, uncertain ? size_t{0} : size_t{1}); QCOMPARE(f.wallet->writes, 0);
        QCOMPARE(f.wallet->label_writes, uncertain ? 17 : 33);
    }
    void sentNotesRejectInvalidIdentityAndOverBudgetInputWithoutWrites()
    {
        Fixture original, reopened;
        const auto txid = Txid::FromUint256(uint256{uint8_t{1}});
        auto action = original.opening->startAction(); QVERIFY(action);
        QCOMPARE(reopened.store->submitSentLabels(action, txid, {{Address(), "wrong"}}).rejection, execution::Rejection::Invalid);
        QCOMPARE(original.store->submitSentLabels(action, {}, {{Address(), "wrong"}}).rejection, execution::Rejection::Invalid);
        const auto too_big = original.store->submitSentLabels(action, txid, {{Address(), QString(30'000, 'x')}}); QVERIFY(too_big);
        original.commands.drain(); QCOMPARE(too_big.operation->result()->error, Error::Rejected); QCOMPARE(original.wallet->label_writes, 0);
    }
    void retainedSentCapabilityCanBeSubmittedAfterTheOriginalClose()
    {
        Fixture f; f.load(); auto capability = f.opening->startAction(); QVERIFY(capability);
        f.store->close(); f.opening->retire(); f.client.reset();
        const auto admitted = f.store->submitSentLabels(std::move(capability), Txid::FromUint256(uint256{uint8_t{1}}), {{Address(), "committed-note"}}); QVERIFY(admitted);
        f.commands.drain(); f.retirement.drain(); QCOMPARE(admitted.operation->result()->error, Error::None); QCOMPARE(f.wallet->label_writes, 1);
        QVERIFY(f.commands.quiescent() && f.retirement.quiescent());
    }
    void explicitSentNotesAreNotStarvedByPaidPersistence()
    {
        Fixture f; f.load(97); bool progressed{false}; f.wallet->before_label = [&] { progressed = f.wallet->writes < 97; };
        std::shared_ptr<Operation> outgoing;
        QVERIFY(f.opening->postDomain([&] { f.store->observed(f.facts({Address()})); }));
        QVERIFY(f.opening->postDomain([&] { outgoing = f.store->submitSentLabels(f.opening->startAction(), Txid::FromUint256(uint256{uint8_t{1}}), {{Address(), "sent-note"}}).operation; }));
        f.commands.drain(); QVERIFY(outgoing && outgoing->result()); QCOMPARE(outgoing->result()->error, Error::None); QVERIFY(progressed);
        QCOMPARE(f.wallet->writes, 97);
        for (int id{1}; id <= 97; ++id) { const auto entry = Decode({f.wallet->stored.at(util::ToString(id))}).entries.front(); QVERIFY(entry.payment_received); QCOMPARE(entry.recipient.noteSelf, std::string{"private"}); }
    }
    void reservedValueLifetimeTeardownRunsAfterReleaseWithoutGui()
    {
        Fixture f; bool released{false};
        auto held = f.opening->holdValues([&] { released = true; }); QVERIFY(held);
        QVERIFY(!f.commands.step(Lane::Cleanup)); QVERIFY(!released);
        f.commands.scheduler->stopAcceptingWork(); held.lease.reset();
        QVERIFY(f.commands.step(Lane::Cleanup)); QVERIFY(released); f.commands.drain();
        QVERIFY(f.commands.quiescent());
    }
    void paymentObservationYieldsBeforePreparingTheWholeAddressHistory()
    {
        int prepared{0}, largest_turn{0};
        Fixture f{Encode, DecodeOne, [&](Record row) { if (row.entry.payment_received) ++prepared; return FreezeRecord(std::move(row)); }};
        f.load(513); const auto old = f.store->take()->snapshot;
        QVERIFY(f.opening->postDomain([&] { f.store->observed(f.facts({Address()})); }));
        bool marker{false}, queued_marker{false};
        while (!f.commands.quiescent()) {
            const int before = prepared; QVERIFY(f.commands.stepNext());
            largest_turn = std::max(largest_turn, prepared - before); QVERIFY(prepared - before <= 128);
            if (prepared && !queued_marker) {
                queued_marker = true; QVERIFY(!f.store->presentationCurrent());
                QVERIFY(f.opening->postDomain([&] { marker = true; QVERIFY(prepared < 513); QVERIFY(!f.store->presentationCurrent()); }));
            }
        }
        QVERIFY(marker); QCOMPARE(prepared, 513); QCOMPARE(largest_turn, 128); QCOMPARE(f.wallet->writes, 513);
        QVERIFY(f.store->presentationCurrent());
        const auto current = f.store->take()->snapshot;
        for (int64_t id{1}; id <= 513; ++id) {
            QVERIFY(!old->find(id)->entry.payment_received); QVERIFY(current->find(id)->entry.payment_received);
            QVERIFY(Decode({f.wallet->stored.at(util::ToString(id))}).entries.front().payment_received);
        }
    }
    void largeReceiptInputYieldsEvenWhenNoHistoryRowsNeedUpdating()
    {
        Fixture f; f.load(0); const auto original = f.store->take()->snapshot;
        std::set<QString> addresses;
        for (int n{0}; n < 3'000; ++n) addresses.insert(QStringLiteral("unused-%1").arg(n));
        auto input = f.facts(std::move(addresses));
        QVERIFY(f.opening->postDomain([&] { f.store->observed(input); }));
        QVERIFY(f.commands.step(Lane::Domain)); QVERIFY(!f.store->presentationCurrent());
        bool marker{false};
        QVERIFY(f.opening->postDomain([&] { marker = true; QVERIFY(!f.store->presentationCurrent()); }));
        f.commands.drain(); QVERIFY(marker); QVERIFY(f.store->presentationCurrent());
        QCOMPARE(f.wallet->writes, 0); QCOMPARE(f.store->take()->snapshot, original);
    }
    void unchangedFullReceiptSnapshotsDoNotPrepareOrPersistHistoryAgain()
    {
        int prepared{0};
        Fixture f{Encode, DecodeOne, [&](Record row) { ++prepared; return FreezeRecord(std::move(row)); }};
        f.load(257); f.observe({Address()}); const auto current = f.store->take()->snapshot;
        prepared = 0; const auto writes = f.wallet->writes; const auto watches = f.watches;
        for (int repeat{0}; repeat < 12; ++repeat) f.observe({Address()});
        QCOMPARE(prepared, 0); QCOMPARE(f.wallet->writes, writes); QCOMPARE(f.watches, watches);
        QCOMPARE(f.store->take()->snapshot, current); QVERIFY(f.store->presentationCurrent());
    }
    void unchangedReceiptIngressBorrowsWithoutRetainingCleanupBacklog()
    {
        Fixture f; f.load(3); f.observe({Address()});
        auto unchanged = f.facts({Address()});
        QVERIFY(f.opening->postDomain([&] {
            const auto references = unchanged.use_count();
            for (int repeat{0}; repeat < 64; ++repeat) {
                f.store->observed(unchanged);
                QCOMPARE(unchanged.use_count(), references);
            }
        }));
        QVERIFY(f.commands.step(Lane::Domain));
        QVERIFY((f.commands.queuedCountIf([](const auto& turn) { return turn.lane == Lane::Cleanup; }) == 0));
        f.commands.drain(); QVERIFY(f.store->presentationCurrent()); QCOMPARE(f.wallet->writes, 3);
    }
    void cleanupBackpressureRejectsUnconsumedReceiptInputAndFullRetryRecovers()
    {
        Fixture f; f.load(3); std::set<QString> addresses{Address()};
        QVERIFY(f.opening->postDomain([&] {
            // Hold Cleanup and all reduction turns while the producer supplies
            // newer complete views. Superseded payloads must not grow forever.
            for (int n{0}; n < 20; ++n) {
                addresses.insert(QStringLiteral("unused-%1").arg(n));
                auto current = f.facts(addresses); f.store->observed(current);
                if (n > 5) QCOMPARE(current.use_count(), 1);
            }
        }));
        QVERIFY(f.commands.step(Lane::Domain)); QVERIFY(!f.store->presentationCurrent());
        f.commands.drain(); QCOMPARE(f.wallet->writes, 0);
        const auto failed = f.store->take(); QVERIFY(failed); QCOMPARE(failed->error, Error::Rejected);
        QVERIFY(!f.store->presentationCurrent());
        f.observe(addresses); QVERIFY(f.store->presentationCurrent()); QCOMPARE(f.wallet->writes, 3);
        const auto current = f.store->take(); QVERIFY(current); QCOMPARE(current->error, Error::None);
        for (int id{1}; id <= 3; ++id) QVERIFY(current->snapshot->find(id)->entry.payment_received);
    }
    void pendingFullSnapshotsCoalesceWithoutRestartingAdmittedPaymentWork()
    {
        int prepared{0};
        Fixture f{Encode, DecodeOne, [&](Record row) { if (row.entry.payment_received) ++prepared; return FreezeRecord(std::move(row)); }};
        f.load(513);
        std::set<QString> addresses{Address()};
        for (int n{0}; n < 1'000; ++n) addresses.insert(QStringLiteral("z-unused-%1").arg(n));
        auto first = f.facts(addresses); std::weak_ptr<const walletreceipts::Snapshot> original{first};
        QVERIFY(f.opening->postDomain([&store = f.store, first] { store->observed(first); })); first.reset();
        int injected{0}, turns{0};
        while (!f.commands.quiescent()) {
            QVERIFY(++turns < 2'000);
            QVERIFY(f.commands.stepNext());
            while (f.commands.step(Lane::Cleanup)) {}
            if (injected < 20) {
                addresses.insert(QStringLiteral("z-latest-%1").arg(injected++));
                // A complete cumulative source, as supplied by Reader; replaced
                // pending values can be dropped without losing older facts.
                auto latest = f.facts(addresses);
                QVERIFY(f.opening->postDomain([&store = f.store, latest] { store->observed(latest); }));
            }
        }
        QCOMPARE(injected, 20); QCOMPARE(prepared, 513); QCOMPARE(f.wallet->writes, 513);
        QVERIFY(original.expired()); QVERIFY(f.store->presentationCurrent());
        const auto current = f.store->take()->snapshot;
        for (int64_t id{1}; id <= 513; ++id) QVERIFY(current->find(id)->entry.payment_received);
    }
    void rejectedObservationAdmissionRetainsCheckingUntilARealRetry()
    {
        Fixture f; f.load(3);
        QVERIFY(f.opening->postDomain([&] { f.store->observed(f.facts({Address()})); }));
        f.commands.rejectLane(Lane::Domain);
        QVERIFY(f.commands.step(Lane::Domain)); f.commands.drain();
        QVERIFY(!f.store->presentationCurrent()); QCOMPARE(f.wallet->writes, 0);
        const auto failed = f.store->take(); QVERIFY(failed); QCOMPARE(failed->error, Error::Rejected);
        f.observe({Address()}); QVERIFY(f.store->presentationCurrent()); QCOMPARE(f.wallet->writes, 3);
    }
    void failedPartialPaymentPreparationRepairsAlreadyObservedAddressOnRetry()
    {
        bool fail{true}; int attempts{0};
        Fixture f{Encode, DecodeOne, [&](Record row) {
            if (row.entry.payment_received && ++attempts == 10 && fail) throw std::bad_alloc{};
            return FreezeRecord(std::move(row));
        }};
        f.load(257); f.observe({Address()});
        QVERIFY(!f.store->presentationCurrent());
        const auto failed = f.store->take(); QVERIFY(failed); QCOMPARE(failed->error, Error::Rejected);
        fail = false; f.observe({Address()});
        QVERIFY(f.store->presentationCurrent()); QCOMPARE(f.wallet->writes, 257);
        const auto current = f.store->take()->snapshot;
        for (int64_t id{1}; id <= 257; ++id) {
            QVERIFY(current->find(id)->entry.payment_received);
            QVERIFY(Decode({f.wallet->stored.at(util::ToString(id))}).entries.front().payment_received);
        }
    }
    void closedOpeningRetiresPartialReceiptInputWithoutGuiDelivery()
    {
        int prepared{0};
        Fixture f{Encode, DecodeOne, [&](Record row) { if (row.entry.payment_received) ++prepared; return FreezeRecord(std::move(row)); }};
        f.load(513);
        QVERIFY(f.opening->postDomain([&] { f.store->observed(f.facts({Address()})); }));
        while (!prepared) { QVERIFY(!f.commands.quiescent()); QVERIFY(f.commands.stepNext()); }
        const int admitted_preparation = prepared; QVERIFY(admitted_preparation <= 128);
        f.store->close(); f.opening->retire(); f.client.reset(); f.commands.drain(); f.retirement.drain();
        QCOMPARE(prepared, admitted_preparation); QVERIFY(!f.store->presentationCurrent());
        QVERIFY(f.commands.quiescent() && f.retirement.quiescent());
        // Work already admitted before close may persist its confirmed facts.
        QVERIFY(f.wallet->writes <= admitted_preparation);
    }
    void nativeUseFactIsReconciledIntoExistingRowsEvenWhenAlreadyKnown_data()
    {
        QTest::addColumn<bool>("intermediate"); QTest::newRow("complete-view") << false; QTest::newRow("older-empty-view-first") << true;
    }
    void nativeUseFactIsReconciledIntoExistingRowsEvenWhenAlreadyKnown()
    {
        QFETCH(bool, intermediate); Fixture f; f.load(3);
        CMutableTransaction transaction;
        transaction.vout.emplace_back(100, GetScriptForDestination(DecodeDestination(Address().toStdString())));
        interfaces::WalletTx native{}; native.tx = MakeTransactionRef(transaction); native.txout_is_mine = {true};
        f.wallet->transactions.insert(native);
        auto create = Create(); create.require_unused = true;
        const auto refused = f.settle(create); QCOMPARE(refused->result()->error, Error::Paid);
        QCOMPARE(f.wallet->writes, 0); QVERIFY(!f.store->presentationCurrent());
        if (intermediate) {
            f.observe(); QVERIFY(!f.store->presentationCurrent()); QCOMPARE(f.wallet->writes, 0);
        }
        f.observe({Address()}); QVERIFY(f.store->presentationCurrent()); QCOMPARE(f.wallet->writes, 3);
        const auto current = f.store->take(); QVERIFY(current && current->snapshot);
        for (int id{1}; id <= 3; ++id) {
            QVERIFY(current->snapshot->find(id)->entry.payment_received);
            QVERIFY(Decode({f.wallet->stored.at(util::ToString(id))}).entries.front().payment_received);
        }
    }
    void cachedReaderPendingFencesPresentationBeforeAnyGuiNotification()
    {
        Fixture f; f.load(3); const auto prior = f.record(); QVERIFY(f.store->presentationCurrent());
        f.reader_ready.store(false); QVERIFY(!f.store->presentationCurrent());
        QCOMPARE(f.settle(f.edit(prior))->result()->error, Error::Checking); QCOMPARE(f.wallet->writes, 0);
        f.reader_ready.store(true); f.observe(); QVERIFY(f.store->presentationCurrent());
        QVERIFY(f.settle(f.edit(prior))->result()->request_saved); QCOMPARE(f.wallet->writes, 1);
    }
    void foreignOpeningReceiptCannotChangeTheCurrentHistory()
    {
        Fixture f; f.load(3); const auto original = f.store->take()->snapshot;
        auto foreign = std::make_shared<const walletreceipts::Snapshot>(walletreceipts::Snapshot{std::make_shared<const walletopening::Identity>(), {}, {Address()}, f.opening->transactionRevision()});
        QVERIFY(f.opening->postDomain([&] { f.store->observed(foreign); })); f.commands.drain();
        QVERIFY(f.store->presentationCurrent()); QCOMPARE(f.wallet->writes, 0);
        f.observe(); QCOMPARE(f.store->take()->snapshot, original);
    }
    void unchangedSnapshotsCoalesceWatchWorkAndInterestChangesStillApply()
    {
        Fixture f; f.load(); const int watches = f.watches;
        QVERIFY(f.watched.contains(Address()));
        QVERIFY(f.store->interest(7, Address())); f.commands.drain(); QCOMPARE(f.watches, watches);
        QVERIFY(f.store->interest(7, Address())); f.commands.drain(); QCOMPARE(f.watches, watches);
        const QString other = QStringLiteral("other-visible-address");
        QVERIFY(f.store->interest(7, other)); f.commands.drain(); QCOMPARE(f.watches, watches + 1); QVERIFY(f.watched.contains(other));
        QVERIFY(f.store->interest(7, {})); f.commands.drain(); QCOMPARE(f.watches, watches + 2); QVERIFY(!f.watched.contains(other));
        QVERIFY(f.store->interest(7, {})); f.commands.drain(); QCOMPARE(f.watches, watches + 2);
    }
    void paidPreparationYieldsBeforeEncodingTheWholeHistory_data()
    {
        QTest::addColumn<bool>("fail"); QTest::newRow("persist") << false; QTest::newRow("persistence-fails") << true;
    }
    void paidPreparationYieldsBeforeEncodingTheWholeHistory()
    {
        QFETCH(bool, fail);
        int encodes{0}, max_in_turn{0};
        Fixture f{[&](const QmlRecentRequestEntry& entry) { ++encodes; return Encode(entry); }};
        f.load(97); encodes = 0; f.wallet->fail_all_writes = fail;
        QVERIFY(f.opening->postDomain([&] { f.store->observed(f.facts({Address()})); }));
        bool marker{false}, marker_queued{false};
        std::shared_ptr<const Snapshot> paid_snapshot;
        std::optional<Publication> latest;
        while (!f.commands.quiescent()) {
            QVERIFY(!f.commands.queuedIdle());
            const int before = encodes;
            QVERIFY(f.commands.stepNext());
            max_in_turn = std::max(max_in_turn, encodes - before);
            QVERIFY(encodes - before <= 32);
            if (auto publication = f.store->take()) {
                latest = std::move(publication);
                if (latest->snapshot && latest->snapshot->find(1)->entry.payment_received) {
                    if (paid_snapshot) QCOMPARE(latest->snapshot.get(), paid_snapshot.get());
                    else paid_snapshot = latest->snapshot;
                }
            }
            if (encodes == 32 && !marker_queued) {
                marker_queued = true;
                QVERIFY(f.opening->postDomain([&] { marker = true; QCOMPARE(encodes, 32); }));
            }
        }
        QVERIFY(marker); QCOMPARE(encodes, 97); QCOMPARE(max_in_turn, 32); QCOMPARE(f.wallet->writes, 97);
        const auto& update = latest; QVERIFY(update && update->snapshot); QCOMPARE(update->snapshot->records.size(), size_t{97});
        for (int64_t id{1}; id <= 97; ++id) {
            const auto record = update->snapshot->find(id); QVERIFY(record); QVERIFY(record->entry.payment_received);
            const auto stored = Decode({f.wallet->stored.at(util::ToString(id))});
            QCOMPARE(stored.entries.size(), size_t{1}); QCOMPARE(stored.entries.front().payment_received, !fail);
            QCOMPARE(stored.entries.front().recipient.message, std::string{"message"});
            QCOMPARE(stored.entries.front().recipient.noteSelf, std::string{"private"});
        }
        QCOMPARE(update->error, fail ? Error::Persistence : Error::None);
    }
    void historyLoadingYieldsWithItsOriginalOutcomeAndAllData_data()
    {
        QTest::addColumn<int>("count"); QTest::addColumn<int>("payload"); QTest::addColumn<int>("maximum");
        QTest::newRow("row-budget") << 97 << 0 << 32;
        QTest::newRow("byte-budget") << 7 << 140'000 << 1;
        QTest::newRow("legacy-oversized-record") << 7 << 300'000 << 1;
    }
    void historyLoadingYieldsWithItsOriginalOutcomeAndAllData()
    {
        QFETCH(int, count); QFETCH(int, payload); QFETCH(int, maximum);
        int decoded{0}, largest_turn{0};
        Fixture f{Encode, [&](std::string_view blob) { ++decoded; return DecodeOne(blob); }};
        for (int id{1}; id <= count; ++id) {
            auto entry = Entry(id); entry.recipient.message.assign(payload, 'm');
            f.wallet->stored[util::ToString(id)] = Encode(entry);
        }
        const auto admitted = f.store->submit(Intent(Kind::Reload)); QVERIFY(admitted);
        QVERIFY(f.commands.step(Lane::Domain)); QVERIFY(f.commands.step(Lane::Core));
        QVERIFY(f.commands.step(Lane::Domain)); QCOMPARE(decoded, 0);
        bool marker{false}, marker_queued{false};
        while (!f.commands.quiescent()) {
            const int before = decoded;
            QVERIFY(f.commands.stepNext());
            largest_turn = std::max(largest_turn, decoded - before);
            QVERIFY(decoded - before <= maximum);
            if (decoded && decoded < count && !marker_queued) {
                marker_queued = true;
                QVERIFY(!admitted.operation->result()); QVERIFY(!f.store->presentationCurrent());
                QVERIFY(f.opening->postDomain([&] {
                    marker = true; QVERIFY(decoded < count); QVERIFY(!admitted.operation->result());
                }));
            }
        }
        QVERIFY(marker); QCOMPARE(decoded, count); QCOMPARE(largest_turn, maximum);
        QVERIFY(admitted.operation->result()); QCOMPARE(admitted.operation->result()->error, Error::None);
        const auto publication = f.store->take(); QVERIFY(publication && publication->loaded);
        QCOMPARE(publication->snapshot->records.size(), static_cast<size_t>(count));
        QCOMPARE(publication->snapshot->records.front()->entry.id, static_cast<int64_t>(count));
        QCOMPARE(publication->snapshot->labels.at(Address()), QStringLiteral("private"));
        for (int id{1}; id <= count; ++id) {
            const auto* record = publication->snapshot->find(id); QVERIFY(record);
            QCOMPARE(record->entry.recipient.message, std::string(payload, 'm'));
            QCOMPARE(record->display.note, QStringLiteral("private"));
        }
        f.observe(); QVERIFY(f.store->presentationCurrent()); QCOMPARE(f.wallet->writes, 0);
    }
    void decodingResourceFailureIsExplicitAndReloadCanRecover()
    {
        int decoded{0}; bool fail{true};
        Fixture f{Encode, [&](std::string_view blob) {
            if (++decoded == 2 && fail) throw std::bad_alloc{};
            return DecodeOne(blob);
        }};
        for (int id{1}; id <= 4; ++id) f.wallet->stored[util::ToString(id)] = Encode(Entry(id));
        const auto first = f.settle(Intent(Kind::Reload));
        QVERIFY(first->result()); QCOMPARE(first->result()->error, Error::Rejected);
        QCOMPARE(decoded, 2); QVERIFY(!f.store->presentationCurrent());
        const auto failed = f.store->take(); QVERIFY(failed); QVERIFY(!failed->loaded); QCOMPARE(failed->error, Error::Rejected);
        fail = false; decoded = 0;
        QCOMPARE(f.settle(Intent(Kind::Reload))->result()->error, Error::None);
        f.observe(); QVERIFY(f.store->presentationCurrent()); QCOMPARE(decoded, 4);
        const auto recovered = f.store->take(); QVERIFY(recovered && recovered->snapshot);
        QCOMPARE(recovered->snapshot->records.size(), size_t{4}); QCOMPARE(recovered->snapshot->malformed, size_t{0});
        QCOMPARE(f.wallet->writes, 0);
    }
    void paymentFactsDuringLoadingApplyToBothDecodedAndFutureRows()
    {
        int decoded{0};
        Fixture f{Encode, [&](std::string_view blob) { ++decoded; return DecodeOne(blob); }};
        for (int id{1}; id <= 97; ++id) f.wallet->stored[util::ToString(id)] = Encode(Entry(id));
        const auto admitted = f.store->submit(Intent(Kind::Reload)); QVERIFY(admitted);
        while (!decoded) { QVERIFY(!f.commands.quiescent()); QVERIFY(f.commands.stepNext()); }
        QCOMPARE(decoded, 32); QVERIFY(!admitted.operation->result());
        QVERIFY(f.opening->postDomain([&] { f.store->observed(f.facts({Address()})); }));
        f.commands.drain(); QVERIFY(admitted.operation->result()); QCOMPARE(admitted.operation->result()->error, Error::None);
        QCOMPARE(decoded, 97); QCOMPARE(f.wallet->writes, 97);
        const auto publication = f.store->take(); QVERIFY(publication && publication->snapshot);
        for (int id{1}; id <= 97; ++id) {
            const auto* row = publication->snapshot->find(id); QVERIFY(row && row->entry.payment_received);
            QVERIFY(row->display.uri.isEmpty());
            const auto saved = Decode({f.wallet->stored.at(util::ToString(id))});
            QCOMPARE(saved.entries.size(), size_t{1}); QVERIFY(saved.entries.front().payment_received);
        }
        f.observe({Address()}); QVERIFY(f.store->presentationCurrent());
    }
    void rejectedLoadContinuationDoesNotSpinAndExplicitReloadRecovers()
    {
        int decoded{0};
        Fixture f{Encode, [&](std::string_view blob) { ++decoded; return DecodeOne(blob); }};
        for (int id{1}; id <= 97; ++id) f.wallet->stored[util::ToString(id)] = Encode(Entry(id));
        const auto admitted = f.store->submit(Intent(Kind::Reload)); QVERIFY(admitted);
        QVERIFY(f.commands.step(Lane::Domain)); QVERIFY(f.commands.step(Lane::Core)); QVERIFY(f.commands.step(Lane::Domain));
        while (!decoded) { QVERIFY(!f.commands.quiescent()); QVERIFY(f.commands.stepNext()); }
        QCOMPARE(decoded, 32);
        f.commands.rejectLane(Lane::Domain);
        QVERIFY(f.commands.step(Lane::Domain)); f.commands.drain();
        QCOMPARE(decoded, 64); QVERIFY(admitted.operation->result()); QCOMPARE(admitted.operation->result()->error, Error::Rejected);
        QCOMPARE(f.wallet->loads, 1); QVERIFY(!f.store->presentationCurrent());
        QCOMPARE(f.settle(Intent(Kind::Reload))->result()->error, Error::None);
        f.observe(); QVERIFY(f.store->presentationCurrent()); QCOMPARE(decoded, 161); QCOMPARE(f.wallet->loads, 2);
        QCOMPARE(f.store->take()->snapshot->records.size(), size_t{97}); QCOMPARE(f.wallet->writes, 0);
    }
    void closeDuringHistoryDecodingStopsFurtherDecodeAndRetainsOutcome()
    {
        int decoded{0};
        Fixture f{Encode, [&](std::string_view blob) { ++decoded; return DecodeOne(blob); }};
        for (int id{1}; id <= 97; ++id) f.wallet->stored[util::ToString(id)] = Encode(Entry(id));
        const auto admitted = f.store->submit(Intent(Kind::Reload)); QVERIFY(admitted);
        while (!decoded) { QVERIFY(!f.commands.quiescent()); QVERIFY(f.commands.stepNext()); }
        QCOMPARE(decoded, 32); QVERIFY(!admitted.operation->result());
        f.store->close(); f.opening->retire(); f.client.reset(); f.commands.drain(); f.retirement.drain();
        QCOMPARE(decoded, 32); QVERIFY(admitted.operation->result()); QCOMPARE(admitted.operation->result()->error, Error::Closed);
        QCOMPARE(f.wallet->writes, 0); QVERIFY(f.commands.quiescent() && f.retirement.quiescent());
    }
    void incrementalPreparationPatchesPendingEmittedAndUncapturedRows()
    {
        SharedRecords rows;
        for (int64_t id{1}; id <= 3; ++id) rows.emplace(id, FreezeRecord(Record{Entry(id), static_cast<uint64_t>(id), {}}));
        const auto original = rows.at(1);
        const std::map<QString, QString> labels;
        SnapshotPreparation build{std::make_shared<const walletopening::Identity>(), rows, labels, 7};
        QVERIFY(!build.take(4)); QVERIFY(!build.step(0)); QVERIFY(!build.step(1));
        const auto paid = [&](int64_t id) {
            auto record = *rows.at(id); record.entry.payment_received = true; record.revision += 10;
            rows[id] = FreezeRecord(std::move(record)); build.replace(rows.at(id));
        };
        paid(1); // Captured, not emitted.
        paid(2); // Not yet captured.
        QVERIFY(!build.step(2)); QVERIFY(!build.step(1));
        paid(3); // Already emitted as the newest row.
        QVERIFY(build.step(2));
        const auto snapshot = build.take(42); QVERIFY(snapshot);
        QCOMPARE(snapshot->revision, uint64_t{42}); QCOMPARE(snapshot->malformed, size_t{7});
        QCOMPARE(snapshot->records.front()->entry.id, int64_t{3});
        for (int64_t id{1}; id <= 3; ++id) {
            QCOMPARE(snapshot->find(id), rows.at(id).get());
            QVERIFY(snapshot->find(id)->entry.payment_received); QVERIFY(snapshot->find(id)->display.uri.isEmpty());
        }
        QVERIFY(!original->entry.payment_received); QVERIFY(original->display.uri.isEmpty());
        QVERIFY(!BuildBitcoinUri(original->display.address, original->entry.recipient.amount, original->display.label, original->display.message).isEmpty());
        QCOMPARE(snapshot->by_address.at(Address()), std::vector<int64_t>({3, 2, 1}));
    }
    void ownerYieldsBeforeCompletingHistoryPreparation()
    {
        int decoded{0};
        Fixture f{Encode, [&](std::string_view blob) { ++decoded; return DecodeOne(blob); }};
        for (int64_t id{1}; id <= 257; ++id) f.wallet->stored[util::ToString(id)] = Encode(Entry(id));
        const auto admitted = f.store->submit(Intent(Kind::Reload)); QVERIFY(admitted);
        QVERIFY(f.commands.step(Lane::Domain)); QVERIFY(f.commands.step(Lane::Core));
        QVERIFY(f.commands.step(Lane::Domain));
        while (decoded < 257) { QVERIFY(f.commands.step(Lane::Domain)); }
        QVERIFY(!admitted.operation->result()); QVERIFY(!f.store->presentationCurrent());
        bool other_work{false};
        QVERIFY(f.opening->postDomain([&] { other_work = true; QVERIFY(!admitted.operation->result()); }));
        f.commands.drain(); QVERIFY(other_work); QVERIFY(admitted.operation->result());
        QCOMPARE(admitted.operation->result()->error, Error::None);
        const auto publication = f.store->take(); QVERIFY(publication && publication->loaded);
        QCOMPARE(publication->snapshot->records.size(), size_t{257});
        QCOMPARE(publication->snapshot->records.front()->entry.id, int64_t{257});
        QCOMPARE(publication->snapshot->by_address.at(Address()).size(), size_t{257});
        f.observe(); QVERIFY(f.store->presentationCurrent());
    }
    void newPaymentFactsPatchTheInFlightHistoryWithoutRestarting()
    {
        int decoded{0};
        Fixture f{Encode, [&](std::string_view blob) { ++decoded; return DecodeOne(blob); }};
        for (int64_t id{1}; id <= 257; ++id) f.wallet->stored[util::ToString(id)] = Encode(Entry(id));
        const auto admitted = f.store->submit(Intent(Kind::Reload)); QVERIFY(admitted);
        QVERIFY(f.commands.step(Lane::Domain)); QVERIFY(f.commands.step(Lane::Core));
        QVERIFY(f.commands.step(Lane::Domain)); QVERIFY(f.commands.step(Lane::Domain));
        while (decoded < 257) { QVERIFY(f.commands.step(Lane::Domain)); }
        QVERIFY(!admitted.operation->result());
        QVERIFY(f.opening->postDomain([&] { f.store->observed(f.facts({Address()})); }));
        f.commands.drain(); QVERIFY(admitted.operation->result()); QCOMPARE(admitted.operation->result()->error, Error::None);
        const auto publication = f.store->take(); QVERIFY(publication && publication->snapshot);
        QCOMPARE(publication->snapshot->revision, f.store->revision());
        QCOMPARE(publication->snapshot->records.size(), size_t{257});
        for (int64_t id{1}; id <= 257; ++id) {
            const auto* record = publication->snapshot->find(id); QVERIFY(record);
            QVERIFY(record->entry.payment_received); QVERIFY(record->display.payment_received); QVERIFY(record->display.uri.isEmpty());
            QVERIFY(Decode({f.wallet->stored.at(util::ToString(id))}).entries.front().payment_received);
        }
        // The watch set is only installed at commit. Supply its fresh receipt
        // observation; facts from the earlier watch remain monotonic above.
        f.observe({Address()});
        QVERIFY(f.store->presentationCurrent()); QCOMPARE(f.wallet->writes, 257);
    }
    void preparationRejectionRetainsAnExplicitFailureAndAllowsReload()
    {
        int decoded{0};
        Fixture f{Encode, [&](std::string_view blob) { ++decoded; return DecodeOne(blob); }}; f.wallet->stored["1"] = Encode(Entry());
        const auto admitted = f.store->submit(Intent(Kind::Reload)); QVERIFY(admitted);
        QVERIFY(f.commands.step(Lane::Domain)); QVERIFY(f.commands.step(Lane::Core));
        QVERIFY(f.commands.step(Lane::Domain));
        f.commands.rejectLane(Lane::Domain);
        QVERIFY(f.commands.step(Lane::Domain)); f.commands.drain();
        QVERIFY(admitted.operation->result()); QCOMPARE(admitted.operation->result()->error, Error::Rejected);
        QVERIFY(!f.store->presentationCurrent()); QCOMPARE(f.wallet->loads, 1); QCOMPARE(f.wallet->writes, 0);
        const auto failed = f.store->take(); QVERIFY(failed); QVERIFY(!failed->loaded); QCOMPARE(failed->error, Error::Rejected);
        QCOMPARE(f.settle(Intent(Kind::Reload))->result()->error, Error::None);
        f.observe(); QVERIFY(f.store->presentationCurrent()); QCOMPARE(f.wallet->loads, 2);
    }
    void closeRetiresPartialPreparationAndItsOriginalOutcome()
    {
        int decoded{0};
        Fixture f{Encode, [&](std::string_view blob) { ++decoded; return DecodeOne(blob); }};
        for (int64_t id{1}; id <= 257; ++id) f.wallet->stored[util::ToString(id)] = Encode(Entry(id));
        const auto admitted = f.store->submit(Intent(Kind::Reload)); QVERIFY(admitted);
        QVERIFY(f.commands.step(Lane::Domain)); QVERIFY(f.commands.step(Lane::Core));
        QVERIFY(f.commands.step(Lane::Domain)); QVERIFY(f.commands.step(Lane::Domain));
        while (decoded < 257) { QVERIFY(f.commands.step(Lane::Domain)); }
        QVERIFY(!admitted.operation->result()); f.store->close();
        f.opening->retire(); f.client.reset(); f.commands.drain(); f.retirement.drain();
        QVERIFY(admitted.operation->result()); QCOMPARE(admitted.operation->result()->error, Error::Closed);
        QCOMPARE(f.wallet->writes, 0); QVERIFY(f.commands.quiescent() && f.retirement.quiescent());
    }
    void unchangedRowsKeepTheirStorageAcrossAnUnrelatedEdit()
    {
        Fixture f; f.load(3);
        const auto before = f.store->take()->snapshot;
        const auto* unchanged = before->find(2);
        const auto* edited = before->find(1);
        QVERIFY(unchanged && edited);
        QVERIFY(f.settle(f.edit(*edited, "new private note"))->result()->request_saved);
        const auto after = f.store->take()->snapshot;
        QCOMPARE(after->find(2), unchanged);
        QCOMPARE(after->find(3), before->find(3));
        QVERIFY(after->find(1) != edited);
        QCOMPARE(before->find(1)->display.note, QStringLiteral("private"));
        QCOMPARE(after->find(1)->display.note, QStringLiteral("new private note"));
        QCOMPARE(static_cast<const void*>(after->find(2)->entry.recipient.message.data()),
                 static_cast<const void*>(unchanged->entry.recipient.message.data()));
        QCOMPARE(after->find(2)->display.uri, unchanged->display.uri);
        QCOMPARE(after->records.front()->entry.id, int64_t{3});
        ReceiveRequestHistoryModel view;
        view.setSnapshot(before);
        QCOMPARE(view.data(view.index(2, 0), ReceiveRequestHistoryModel::NoteSelfRole).toString(), QStringLiteral("private"));
        const auto retired = view.setSnapshot(after);
        QCOMPARE(retired, before);
        QCOMPARE(view.data(view.index(2, 0), ReceiveRequestHistoryModel::NoteSelfRole).toString(), QStringLiteral("new private note"));
        QCOMPARE(view.data(view.index(1, 0), ReceiveRequestHistoryModel::UriRole).toString(), unchanged->display.uri);
    }
    void committedOutcomeAndHistoryRetainTheSameImmutableRow()
    {
        Fixture f; f.load(); const auto before = f.record();
        const auto operation = f.settle(f.edit(before, "first note"));
        QVERIFY(operation->result()->request_saved);
        const auto first = operation->result()->record;
        const auto publication = f.store->take(); QVERIFY(publication && first);
        QCOMPARE(publication->snapshot->find(1), first.get());
        QVERIFY(f.settle(f.edit(*first, "second note"))->result()->request_saved);
        const auto second = f.store->take()->snapshot;
        QVERIFY(second->find(1) != first.get());
        QCOMPARE(first->display.note, QStringLiteral("first note"));
        QCOMPARE(first->entry.recipient.noteSelf, std::string{"first note"});
        QCOMPARE(second->find(1)->display.note, QStringLiteral("second note"));
    }
    void paymentFactsReplaceOnlyTheAffectedImmutableRows()
    {
        Fixture f;
        auto other = Entry(2); uint160 key; key.begin()[0] = 2;
        other.recipient.address = EncodeDestination(PKHash{key});
        f.wallet->stored["2"] = Encode(other); f.load();
        const auto before = f.store->take()->snapshot;
        const auto* unpaid = before->find(1); const auto* untouched = before->find(2);
        QVERIFY(unpaid && untouched); QVERIFY(unpaid->display.uri.isEmpty());
        const auto original_payload = BuildBitcoinUri(unpaid->display.address, unpaid->entry.recipient.amount, unpaid->display.label, unpaid->display.message);
        QVERIFY(!original_payload.isEmpty());
        f.observe({Address()}); const auto after = f.store->take()->snapshot;
        QVERIFY(after->find(1) != unpaid); QCOMPARE(after->find(2), untouched);
        QVERIFY(after->find(1)->entry.payment_received); QVERIFY(after->find(1)->display.payment_received);
        QVERIFY(after->find(1)->display.uri.isEmpty());
        QVERIFY(!unpaid->entry.payment_received); QVERIFY(unpaid->display.uri.isEmpty());
        QCOMPARE(BuildBitcoinUri(unpaid->display.address, unpaid->entry.recipient.amount, unpaid->display.label, unpaid->display.message), original_payload);
        QVERIFY(!untouched->entry.payment_received); QCOMPARE(f.wallet->writes, 1);
    }
    void retainedOutcomeOwnsPreparedValuesAcrossLaterHistoryChanges()
    {
        Fixture f; f.load();
        auto command = Create(); command.draft.label = "tea & cake"; command.draft.message = "two?";
        auto operation = f.settle(command); QVERIFY(operation->result()->request_saved);
        const auto result = *operation->result(); const auto record = result.record; QVERIFY(record);
        QCOMPARE(record->display.numeric_id, int64_t{2});
        QCOMPARE(record->display.created, Entry().date);
        QCOMPARE(record->display.amount_sats, CAmount{100});
        QCOMPARE(record->display.label, QStringLiteral("tea & cake"));
        QVERIFY(record->display.uri.isEmpty());
        const auto selected_payload = BuildBitcoinUri(record->display.address, record->entry.recipient.amount, record->display.label, record->display.message);
        QCOMPARE(selected_payload, QStringLiteral("bitcoin:") + Address() + QStringLiteral("?amount=0.00000100&label=tea%20%26%20cake&message=two%3F"));
        QVERIFY(!selected_payload.contains("private"));
        QCOMPARE(operation->result()->record.get(), record.get());
        auto edit = f.edit(*record, "new private note"); edit.draft.label = "changed label";
        QVERIFY(f.settle(edit)->result()->request_saved);
        operation.reset();
        QCOMPARE(record->display.label, QStringLiteral("tea & cake"));
        QCOMPARE(record->display.note, QStringLiteral("private"));
        QCOMPARE(f.record(2).display.label, QStringLiteral("changed label"));
    }
    void idlePresentationRetirementDoesNotNeedNativeWork()
    {
        Fixture f;
        const auto snapshot = std::make_shared<const Snapshot>();
        const std::weak_ptr<const Snapshot> old = snapshot;
        auto last = snapshot;
        // Keep Native unavailable while the independent Domain release runs.
        int native{0};
        QVERIFY(execution::Submit(*f.commands.scheduler, [&] { ++native; }, [] {}));
        const auto held = f.commands.dispatchNext(Lane::Core); QVERIFY(held);
        f.store->retire(std::move(last));
        QVERIFY(!old.expired());
        while (f.commands.step(Lane::Domain)) {}
        QCOMPARE(native, 0);
        QCOMPARE(f.wallet->loads, 0); QCOMPARE(f.wallet->writes, 0);
        // Drop the independent test observer, then require the owner's copy
        // to have gone without a load, command or GUI acknowledgement.
        QVERIFY(old.use_count() == 1);
        QVERIFY(f.commands.finishWork(*held)); f.commands.drain();
    }
    void presentationRetirementYieldsToOtherDomainWork()
    {
        Fixture f;
        int released{0};
        std::vector<std::weak_ptr<const Snapshot>> old;
        for (int n{0}; n < 40; ++n) {
            std::shared_ptr<const Snapshot> snapshot{new Snapshot, [&released](const Snapshot* value) { ++released; delete value; }};
            old.push_back(snapshot); f.store->retire(std::move(snapshot));
        }
        QCOMPARE(released, 0);
        int at_marker{-1};
        QVERIFY(f.opening->postDomain([&] { at_marker = released; }));
        while (at_marker < 0 && f.commands.step(Lane::Domain)) {}
        QVERIFY(at_marker > 0); QVERIFY(at_marker < 40);
        f.commands.drain(); QCOMPARE(released, 40);
        for (const auto& snapshot : old) QVERIFY(snapshot.expired());
        QCOMPARE(f.wallet->loads, 0); QCOMPARE(f.wallet->writes, 0);
    }
    void rejectedRetirementRetainsTheOriginalDataForRetry()
    {
        Fixture f;
        auto snapshot = std::make_shared<const Snapshot>();
        const std::weak_ptr<const Snapshot> old = snapshot;
        f.commands.rejectLane(Lane::Domain);
        f.store->retire(std::move(snapshot));
        f.commands.drain(); QVERIFY(!old.expired());
        f.store->retire(std::make_shared<const Snapshot>());
        f.commands.drain(); QVERIFY(old.expired());
    }
    void realWorkersConsumeStorageOutcomeAndRetireWithoutGuiDelivery()
    {
        struct State {
            QSemaphore entered, release;
            std::atomic<int> metadata{0}, outcomes{0};
            std::atomic<QThread*> effect{nullptr}, outcome{nullptr}, destroyed{nullptr}, reservation{nullptr}, snapshot_destroyed{nullptr};
        };
        struct Native final : Wallet {
            explicit Native(std::shared_ptr<State> state) : state(std::move(state)) {}
            ~Native() override { state->destroyed = QThread::currentThread(); }
            bool setAddressReceiveRequest(const CTxDestination& destination, const std::string& id, const std::string& blob) override {
                state->effect = QThread::currentThread(); state->entered.release(); state->release.acquire();
                return Wallet::setAddressReceiveRequest(destination, id, blob);
            }
            const std::shared_ptr<State> state;
        };
        auto state = std::make_shared<State>();
        const auto unblock = qScopeGuard([state] { state->release.release(); });
        BackendExecutor commands{execution::Options{}}, retirement;
        auto native = std::make_shared<Native>(state);
        auto opening = std::make_shared<walletopening::Session>(commands.scheduler(), retirement.scheduler(),
            std::make_shared<std::atomic_bool>(false), [state] { ++state->metadata; });
        auto client = opening->start(native).borrower;
        QTRY_VERIFY(state->metadata.load() > 0);
        auto store = std::make_shared<Store>(opening, [] {}, Store::Watch{}, QDateTime::currentDateTimeUtc,
            [] { return true; }, [state](interfaces::Wallet& wallet, const QString& address) {
                state->reservation = QThread::currentThread();
                return wallet.getWalletName() == "requests" && address == Address();
            });
        auto load = store->submit(Intent(Kind::Reload)); QVERIFY(load);
        QTRY_VERIFY(load.operation->result().has_value());
        std::atomic_bool ready{false};
        QVERIFY(opening->postDomain([&] { store->observed(std::make_shared<const walletreceipts::Snapshot>(walletreceipts::Snapshot{opening->identity(), {}, {}, opening->transactionRevision()})); ready = true; }));
        QTRY_VERIFY(ready.load());
        auto create = Create(); create.consume_address = true;
        const auto admitted = store->submit(create, [state] { state->outcome = QThread::currentThread(); ++state->outcomes; });
        QVERIFY(admitted); QTRY_VERIFY(state->entered.available() > 0);
        QVERIFY(state->effect.load() != qApp->thread());
        std::shared_ptr<const Snapshot> old_snapshot{new Snapshot, [state](const Snapshot* value) {
            state->snapshot_destroyed = QThread::currentThread(); delete value;
        }};
        store->retire(std::move(old_snapshot));
        // Physical Domain release progresses while Native is still held.
        QTRY_VERIFY(state->snapshot_destroyed.load());
        QVERIFY(state->snapshot_destroyed.load() != qApp->thread());
        QCOMPARE(state->outcomes.load(), 0);
        store->close(); opening->retire(); client.reset(); native.reset();
        bool heartbeat{false}; QMetaObject::invokeMethod(qApp, [&] { heartbeat = true; }, Qt::QueuedConnection);
        QTRY_VERIFY(heartbeat); QVERIFY(!retirement.isDrained()); QVERIFY(!admitted.operation->result());
        state->release.release();
        QTRY_VERIFY(admitted.operation->result().has_value());
        commands.shutdown(); retirement.shutdown(); QTRY_VERIFY(commands.isDrained() && retirement.isDrained());
        QCOMPARE(state->outcomes.load(), 1); QVERIFY(admitted.operation->result()->request_saved);
        QVERIFY(admitted.operation->result()->reservation_released);
        // The QThread objects have been deleted by physical drain. Compare
        // their recorded addresses without QtTest formatting a dead QObject.
        QCOMPARE(static_cast<const void*>(state->reservation.load()),
                 static_cast<const void*>(state->effect.load()));
        QVERIFY(state->outcome.load() && state->outcome.load() != qApp->thread());
        QVERIFY(state->destroyed.load() && state->destroyed.load() != qApp->thread());
    }
    void codecPreservesWidgetsPrefixAndQmlExtensions()
    {
        for (const bool paid : {false, true}) for (const bool note : {false, true}) {
            auto entry = Entry(); entry.payment_received = paid; if (!note) entry.recipient.noteSelf.clear();
            const auto blob = Encode(entry);
            QCOMPARE(blob, ReceiveRequestHistoryModel::SerializeEntry(entry));
            const auto decoded = Decode({blob, "broken"});
            QCOMPARE(decoded.malformed, size_t{1}); QCOMPARE(decoded.entries.size(), size_t{1});
            QCOMPARE(decoded.entries.front().recipient.noteSelf, entry.recipient.noteSelf);
            QCOMPARE(decoded.entries.front().payment_received, paid);
            QCOMPARE(Encode(decoded.entries.front()), blob);
        }
    }
    void preparationSortsAndIndexesWithoutMutatingRetainedSnapshot()
    {
        const auto opening = std::make_shared<const walletopening::Identity>();
        std::map<int64_t, Record> records{{1, {Entry(1), 7, {}}}, {2, {Entry(2), 8, {}}}};
        const auto first = Prepare(opening, 1, records);
        QCOMPARE(first->records.front()->entry.id, int64_t{2}); QCOMPARE(first->find(1)->revision, uint64_t{7});
        QCOMPARE(first->by_address.at(Address()).size(), size_t{2}); QVERIFY(!first->find(3));
        records.erase(1); const auto next = Prepare(opening, 2, records);
        QVERIFY(first->find(1)); QVERIFY(!next->find(1)); QCOMPARE(next->opening, opening);
    }
    void historyReadDecodeAndPublicationHaveIndependentTurns()
    {
        Fixture f; f.wallet->stored["1"] = Encode(Entry());
        auto admitted = f.store->submit(Intent(Kind::Reload)); QVERIFY(admitted);
        QCOMPARE(f.wallet->loads, 0); QVERIFY(!admitted.operation->result());
        QVERIFY(f.commands.step(Lane::Domain)); QCOMPARE(f.wallet->loads, 0);
        QVERIFY(f.commands.step(Lane::Core)); QCOMPARE(f.wallet->loads, 1);
        QVERIFY(!admitted.operation->result()); QVERIFY(!f.store->take()->loaded);
        f.commands.drain(); QVERIFY(admitted.operation->result());
        const auto update = f.store->take(); QVERIFY(update && update->loaded && update->snapshot->find(1));
    }
    void createAdmissionDoesNotClaimPersistence()
    {
        Fixture f; f.load(0); int completions{0};
        const auto admitted = f.store->submit(Create(), [&] { ++completions; }); QVERIFY(admitted);
        QVERIFY(!admitted.operation->result()); QCOMPARE(f.wallet->writes, 0);
        while (f.commands.step(Lane::Domain)) {}
        QVERIFY(f.commands.step(Lane::Core)); QCOMPARE(f.wallet->writes, 1);
        QVERIFY(!admitted.operation->result()); QCOMPARE(completions, 0);
        f.commands.drain(); QCOMPARE(completions, 1);
        QVERIFY(admitted.operation->result()->request_saved);
        QCOMPARE(admitted.operation->result()->record->entry.date, Entry().date);
    }
    void consumedReceivingAddressCannotBeCreatedTwiceBeforeGuiPublication()
    {
        Fixture f; f.load(0);
        auto command = Create(); command.consume_address = true;
        const auto first = f.store->submit(command);
        const auto second = f.store->submit(command);
        QVERIFY(first && second);
        QVERIFY(!first.operation->result() && !second.operation->result());
        // Both commands are admitted while the GUI still shows the same
        // receiving address. Domain must consume it independently of GUI.
        f.commands.drain();
        QVERIFY(first.operation->result()->request_saved);
        QVERIFY(!second.operation->result()->request_saved);
        QCOMPARE(second.operation->result()->error, Error::AlreadySaved);
        QCOMPARE(second.operation->result()->id, first.operation->result()->id);
        QCOMPARE(f.wallet->writes, 1);
        QCOMPARE(f.wallet->stored.size(), size_t{1});
    }
    void consumedAddressStaysRetiredAfterEraseAndReload()
    {
        Fixture f; f.load(0);
        auto command = Create(); command.consume_address = true;
        const auto first = f.settle(command); QVERIFY(first->result()->request_saved);
        const auto record = f.record(first->result()->id);
        const auto erased = f.settle(Intent(Kind::Erase, record.entry.id, record.revision));
        QVERIFY(erased->result()->erased);
        const auto reloaded = f.settle(Intent(Kind::Reload)); QCOMPARE(reloaded->result()->error, Error::None);
        QVERIFY(f.wallet->stored.empty());
        const auto repeated = f.settle(command);
        QVERIFY(!repeated->result()->request_saved);
        QCOMPARE(repeated->result()->error, Error::AlreadySaved);
        QCOMPARE(f.wallet->writes, 2); // One create and one erase.
    }
    void loadedRequestsRetireTheirReceivingAddresses()
    {
        Fixture f; f.load();
        auto command = Create(); command.consume_address = true;
        const auto repeated = f.settle(command);
        QCOMPARE(repeated->result()->error, Error::AlreadySaved);
        QCOMPARE(repeated->result()->id, int64_t{1});
        QVERIFY(!repeated->result()->request_saved);
        QCOMPARE(f.wallet->writes, 0);
    }
    void failedCreateDoesNotConsumeAnUnusedReceivingAddress()
    {
        Fixture f; f.load(0); f.wallet->fail_write = 1;
        auto command = Create(); command.consume_address = true;
        const auto failed = f.settle(command);
        QCOMPARE(failed->result()->error, Error::Persistence);
        QVERIFY(!failed->result()->request_saved);
        const auto retry = f.settle(command);
        QVERIFY(retry->result()->request_saved);
        QCOMPARE(retry->result()->id, int64_t{2});
        QCOMPARE(f.wallet->stored.size(), size_t{1});
    }
    void explicitSiblingRequestsCanStillShareAnAddress()
    {
        Fixture f; f.load(0);
        const auto first = f.settle(Create()); const auto second = f.settle(Create());
        QVERIFY(first->result()->request_saved && second->result()->request_saved);
        QVERIFY(first->result()->id != second->result()->id);
        QCOMPARE(f.wallet->writes, 2); QCOMPARE(f.wallet->stored.size(), size_t{2});
    }
    void paymentBeforeEditFreezesPublicFieldsButAllowsPrivateNote()
    {
        Fixture f; f.load(); const auto before = f.record(); f.observe({Address()}); const auto paid = f.record();
        QVERIFY(paid.entry.payment_received);
        QCOMPARE(f.settle(f.edit(before))->result()->error, Error::Changed);
        auto public_edit = f.edit(paid); public_edit.draft.amount = 200;
        QCOMPARE(f.settle(public_edit)->result()->error, Error::Paid);
        const auto private_edit = f.settle(f.edit(paid)); QVERIFY(private_edit->result()->request_saved);
        const auto stored = Decode({f.wallet->stored.at("1")}).entries.front();
        QVERIFY(stored.payment_received); QCOMPARE(stored.recipient.amount, CAmount{100});
        QCOMPARE(stored.recipient.noteSelf, std::string{"edited"});
    }
    void delayedPaymentWriteCannotRecreateDeletedRequest()
    {
        Fixture f; f.load(); f.observe({Address()}); const auto paid = f.record();
        const auto erased = f.settle(Intent(Kind::Erase, paid.entry.id, paid.revision));
        QVERIFY(erased->result()->erased); QVERIFY(f.wallet->stored.empty());
        f.observe({Address()}); QVERIFY(f.wallet->stored.empty());
        QVERIFY(!f.store->take()->snapshot->find(1));
    }
    void paymentDuringAcceptedEditIsMergedBeforeAnyLaterWriter()
    {
        Fixture f; f.load(); auto edit = f.store->submit(f.edit(f.record())); QVERIFY(edit);
        while (f.commands.step(Lane::Domain)) {}
        // The Native effect has completed, but its authoritative consumption
        // is held while a receipt arrives on Domain.
        QVERIFY(f.commands.step(Lane::Core));
        f.store->observed(f.facts({Address()}));
        f.commands.drain(); QVERIFY(edit.operation->result()->request_saved);
        const auto stored = Decode({f.wallet->stored.at("1")}).entries.front();
        QVERIFY(stored.payment_received); QCOMPARE(stored.recipient.noteSelf, std::string{"edited"});
    }
    void reconciledPaymentInvalidatesAnEditStillWaitingForNativeDispatch()
    {
        Fixture f; f.load(); auto command = f.edit(f.record()); command.draft.amount = 200;
        const auto admitted = f.store->submit(command); QVERIFY(admitted);
        while (f.commands.step(Lane::Domain)) {}
        // Reconciliation has already consumed the payment. Matching current
        // native revision alone must not authorize the previously derived edit.
        f.store->observed(f.facts({Address()}));
        f.commands.drain(); QCOMPARE(admitted.operation->result()->error, Error::Checking);
        const auto stored = Decode({f.wallet->stored.at("1")}).entries.front();
        QVERIFY(stored.payment_received); QCOMPARE(stored.recipient.amount, CAmount{100});
    }
    void aFailedCreateDoesNotReuseItsAttemptedRecordId()
    {
        Fixture f; f.load(0); f.wallet->fail_write = 1;
        const auto failed = f.settle(Create()); QCOMPARE(failed->result()->error, Error::Persistence);
        QCOMPARE(failed->result()->id, int64_t{1});
        const auto next = f.settle(Create()); QVERIFY(next->result()->request_saved);
        QCOMPARE(next->result()->id, int64_t{2}); QVERIFY(!f.wallet->stored.contains("1"));
    }
    void failedPaidPersistenceKeepsAddressLockedAndDoesNotSpin()
    {
        Fixture f; f.load(); f.wallet->fail_write = 1; f.observe({Address()});
        const auto update = f.store->take(); QVERIFY(update && update->snapshot->find(1)->entry.payment_received);
        QCOMPARE(update->error, Error::Persistence); QCOMPARE(f.wallet->writes, 1);
        f.commands.drain(); QCOMPARE(f.wallet->writes, 1);
        auto changed = f.edit(*update->snapshot->find(1)); changed.draft.message = "unsafe";
        QCOMPARE(f.settle(changed)->result()->error, Error::Paid);
    }
    void nativeNoticeBeforeDispatchRejectsUnreconciledEdit()
    {
        Fixture f; f.load(); auto edit = f.store->submit(f.edit(f.record()));
        while (f.commands.step(Lane::Domain)) {}
        f.wallet->transaction(Txid::FromUint256(uint256{uint8_t{2}}), CT_NEW);
        f.commands.drain(); QCOMPARE(edit.operation->result()->error, Error::Checking);
        QCOMPARE(f.wallet->writes, 0);
    }
    void labelPreparationIsBoundedAndDoesNotVisitOtherAddressRows_data()
    {
        QTest::addColumn<int>("count"); QTest::addColumn<bool>("same");
        QTest::newRow("multiple-batches") << 97 << false;
        QTest::newRow("exact-batch-multiple") << 512 << false;
        QTest::newRow("unchanged-notes") << 97 << true;
    }
    void labelPreparationIsBoundedAndDoesNotVisitOtherAddressRows()
    {
        QFETCH(int, count); QFETCH(bool, same); int encodes{0}, largest_turn{0};
        Fixture f{[&](const QmlRecentRequestEntry& entry) { ++encodes; return Encode(entry); }};
        const auto other = EncodeDestination(WitnessV0KeyHash{uint160{}});
        for (int id{1}; id <= count * 2; ++id) {
            auto entry = Entry(id); if (id > count) entry.recipient.address = other;
            f.wallet->stored[util::ToString(id)] = Encode(entry);
        }
        QCOMPARE(f.settle(Intent(Kind::Reload))->result()->error, Error::None); f.observe();
        const auto old = f.store->take()->snapshot;
        const auto native_before = f.commands.coreSubmissions();
        Command command; command.kind = Kind::Label; command.address = Address(); command.label = same ? "private" : "new note";
        const auto admitted = f.store->submit(command); QVERIFY(admitted);
        bool marker{false}, marker_queued{false};
        while (!f.commands.quiescent()) {
            const int before = encodes; QVERIFY(f.commands.stepNext());
            largest_turn = std::max(largest_turn, encodes - before); QVERIFY(encodes - before <= 32);
            if (!marker_queued) {
                marker_queued = true;
                QVERIFY(f.opening->postDomain([&] { marker = true; QCOMPARE(encodes, 0); QCOMPARE(f.wallet->writes, 0); QVERIFY(!admitted.operation->result()); }));
            }
        }
        QVERIFY(marker); QVERIFY(admitted.operation->result()); const auto result = *admitted.operation->result();
        QCOMPARE(result.error, Error::None); QVERIFY(result.label_saved);
        QCOMPARE(result.records_total, same ? size_t{0} : static_cast<size_t>(count)); QCOMPARE(result.records_saved, result.records_total);
        QCOMPARE(encodes, same ? 0 : count); QCOMPARE(largest_turn, same ? 0 : 32);
        QCOMPARE(f.wallet->writes, encodes);
        QCOMPARE(f.commands.coreSubmissions() - native_before, same ? size_t{1} : static_cast<size_t>((count + 31) / 32));
        QCOMPARE(std::count(f.wallet->effects.begin(), f.wallet->effects.end(), "label:" + command.label.toStdString()), 1);
        const auto current = f.store->take()->snapshot;
        for (int id{1}; id <= count * 2; ++id) {
            const auto saved = Decode({f.wallet->stored.at(util::ToString(id))}).entries.front();
            QCOMPARE(saved.recipient.noteSelf, id <= count && !same ? std::string{"new note"} : std::string{"private"});
            QCOMPARE(saved.recipient.label, std::string{"public"}); QCOMPARE(saved.recipient.message, std::string{"message"});
            QVERIFY(!saved.payment_received);
            if (id > count || same) QCOMPARE(current->find(id), old->find(id));
        }
    }
    void labelEncoderFailureRetainsOnlyConfirmedEffectsAndRetryCanFinish_data()
    {
        QTest::addColumn<int>("failure"); QTest::newRow("before-native-effect") << 20; QTest::newRow("after-first-batch") << 40;
    }
    void labelEncoderFailureRetainsOnlyConfirmedEffectsAndRetryCanFinish()
    {
        QFETCH(int, failure); bool fail{true};
        Fixture f{[&](const QmlRecentRequestEntry& entry) {
            if (fail && entry.id == failure) throw std::bad_alloc{};
            return Encode(entry);
        }};
        f.load(97); Command command; command.kind = Kind::Label; command.address = Address(); command.label = "renamed";
        const auto result = *f.settle(command)->result();
        const size_t saved = failure > 32 ? 32 : 0;
        QCOMPARE(result.error, saved ? Error::Partial : Error::Rejected); QCOMPARE(result.label_saved, saved != 0);
        QCOMPARE(result.records_total, size_t{97}); QCOMPARE(result.records_saved, saved); QCOMPARE(f.wallet->writes, static_cast<int>(saved));
        for (int id{1}; id <= 97; ++id) {
            const auto stored = Decode({f.wallet->stored.at(util::ToString(id))}).entries.front();
            QCOMPARE(stored.recipient.noteSelf, id <= static_cast<int>(saved) ? std::string{"renamed"} : std::string{"private"});
        }
        fail = false; const auto recovered = *f.settle(command)->result(); QCOMPARE(recovered.error, Error::None);
        QCOMPARE(recovered.records_total, 97 - saved); QCOMPARE(recovered.records_saved, recovered.records_total);
        for (int id{1}; id <= 97; ++id) QCOMPARE(Decode({f.wallet->stored.at(util::ToString(id))}).entries.front().recipient.noteSelf, std::string{"renamed"});
    }
    void paymentDuringLabelPropagationCannotBeOverwrittenByItsLaterBatches()
    {
        Fixture f; f.load(70);
        Command command; command.kind = Kind::Label; command.address = Address(); command.label = "renamed";
        const auto operation = f.store->submit(command).operation;
        while (f.commands.step(Lane::Domain)) {}
        QVERIFY(f.commands.step(Lane::Core)); QCOMPARE(f.wallet->writes, 32);
        // The first real effect exists; hold its Domain consumption while the
        // immutable receipt input arrives, then release all legal lanes.
        f.store->observed(f.facts({Address()})); f.commands.drain();
        QVERIFY(operation->result()); QVERIFY(operation->result()->label_saved);
        QVERIFY(operation->result()->records_saved >= 32);
        QCOMPARE(f.settle(command)->result()->error, Error::None);
        for (int id{1}; id <= 70; ++id) {
            const auto stored = Decode({f.wallet->stored.at(util::ToString(id))}).entries.front();
            QVERIFY(stored.payment_received); QCOMPARE(stored.recipient.noteSelf, std::string{"renamed"});
            QCOMPARE(stored.recipient.label, std::string{"public"}); QCOMPARE(stored.recipient.message, std::string{"message"});
        }
    }
    void labelPropagationYieldsAndReportsPartialPersistence()
    {
        Fixture f; f.load(70); f.wallet->fail_write = 33;
        Command label; label.kind = Kind::Label; label.address = Address(); label.label = "renamed";
        auto admitted = f.store->submit(label); while (f.commands.step(Lane::Domain)) {}
        QVERIFY(f.commands.step(Lane::Core)); QCOMPARE(f.wallet->writes, 32);
        QVERIFY(!admitted.operation->result()); f.commands.drain();
        const auto result = *admitted.operation->result(); QCOMPARE(result.error, Error::Partial);
        QVERIFY(result.label_saved); QCOMPARE(result.records_saved, size_t{69}); QCOMPARE(result.records_total, size_t{70});
        const auto failed = Decode({f.wallet->stored.at("33")}).entries.front(); QCOMPARE(failed.recipient.noteSelf, std::string{"private"});
        const auto saved = Decode({f.wallet->stored.at("34")}).entries.front(); QCOMPARE(saved.recipient.noteSelf, std::string{"renamed"});
        QCOMPARE(saved.recipient.label, std::string{"public"}); QCOMPARE(saved.recipient.message, std::string{"message"});
    }
    void resultNotificationFailureDoesNotHoldTheOriginalActionOrErasePersistence()
    {
        Fixture f; f.load(0);
        const auto admitted = f.store->submitResult(Create(), [](const Result&) { throw std::runtime_error{"Rejected result transport"}; });
        QVERIFY(admitted); f.commands.drain();
        const auto result = admitted.operation->result(); QVERIFY(result); QVERIFY(result->request_saved); QVERIFY(result->publication_failed);
        QCOMPARE(f.wallet->writes, 1);
        f.store->close(); f.opening->retire(); f.client.reset(); f.commands.drain(); f.retirement.drain();
        QVERIFY(f.commands.quiescent()); QVERIFY(f.retirement.quiescent());
    }
    void editorPreconditionChecksTheActualPublicLabelBeforeAnyWrite()
    {
        Fixture f; f.load(70); Command command; command.kind = Kind::Label;
        command.address = Address(); command.label = "my edit"; command.expected_label = "private";
        int completed{0}; std::optional<Result> receipt;
        const auto admitted = f.store->submitResult(command, [&](const Result& value) { ++completed; receipt = value; });
        QVERIFY(admitted); while (f.commands.step(Lane::Domain)) {}
        f.wallet->label = "external";
        f.commands.drain(); QCOMPARE(completed, 1); QVERIFY(receipt);
        QCOMPARE(receipt->error, Error::Changed); QCOMPARE(receipt->current_label, std::optional<QString>{"external"});
        QVERIFY(!receipt->label_saved); QCOMPARE(f.wallet->label_writes, 0); QCOMPARE(f.wallet->writes, 0);
        QCOMPARE(f.wallet->label, std::string{"external"});
        const auto stored = Decode({f.wallet->stored.at("1")}).entries.front();
        QCOMPARE(stored.recipient.noteSelf, std::string{"private"}); QCOMPARE(stored.recipient.label, std::string{"public"});
        command.expected_label = "external";
        const auto retry = f.settle(command); QVERIFY(retry->result()->label_saved);
        QCOMPARE(f.wallet->label_writes, 1); QCOMPARE(f.wallet->writes, 70); QCOMPARE(f.wallet->label, std::string{"my edit"});
    }
    void untouchedCreatePrefillUsesCurrentMetadataWithoutWritingItBack_data()
    {
        QTest::addColumn<QString>("current"); QTest::addColumn<bool>("touched");
        QTest::newRow("external-rename") << QString{"Landlord"} << false;
        QTest::newRow("external-clear") << QString{} << false;
        QTest::newRow("deliberate-edit") << QString{"Landlord"} << true;
        QTest::newRow("deliberate-clear") << QString{"Landlord"} << true;
    }
    void untouchedCreatePrefillUsesCurrentMetadataWithoutWritingItBack()
    {
        QFETCH(QString, current); QFETCH(bool, touched); Fixture f; f.load(0);
        auto command = Create(); command.draft.note = QByteArray{QTest::currentDataTag()} == QByteArray{"deliberate-clear"} ? QString{} : QString{"Rent"};
        command.preserve_current_note = !touched;
        const auto operation = f.store->submit(command); QVERIFY(operation);
        f.wallet->label = current.toStdString();
        f.commands.drain(); QVERIFY(operation.operation->result()->request_saved);
        const auto saved = Decode({f.wallet->stored.at("1")}).entries.front();
        const auto expected = touched ? command.draft.note : current;
        QCOMPARE(QString::fromStdString(saved.recipient.noteSelf), expected);
        const bool writes_label = touched && !command.draft.note.isEmpty();
        QCOMPARE(QString::fromStdString(f.wallet->label), writes_label ? expected : current);
        QCOMPARE(f.wallet->label_writes, writes_label ? 1 : 0); QCOMPARE(f.wallet->writes, 1);
        QCOMPARE(saved.recipient.label, std::string{"public"}); QCOMPARE(saved.recipient.message, std::string{"message"});
    }
    void uncertainMutationFencesFurtherWritesUntilExplicitReload()
    {
        Fixture f; f.load(); const auto prior = f.record(); f.wallet->throw_write = 1;
        const auto uncertain = f.settle(f.edit(prior)); QCOMPARE(uncertain->result()->error, Error::Uncertain);
        QCOMPARE(f.settle(f.edit(prior))->result()->error, Error::Uncertain); QCOMPARE(f.wallet->writes, 1);
        QVERIFY(f.settle(Intent(Kind::Reload))->result()->error == Error::None);
        const auto reloaded = f.record(); QCOMPARE(reloaded.entry.recipient.noteSelf, std::string{"edited"});
        QVERIFY(f.settle(f.edit(reloaded, "recovered"))->result()->request_saved);
    }
    void rejectedPlanningAndNativeDispatchHaveTerminalOutcomes()
    {
        Fixture f; f.load(); f.commands.rejectLane(Lane::Domain);
        const auto planning = f.store->submit(Create()); QVERIFY(!planning);
        QVERIFY(planning.operation && planning.operation->result()->error == Error::Rejected);
        f.commands.rejectLane(Lane::Core);
        const auto native = f.settle(Create()); QCOMPARE(native->result()->error, Error::Rejected);
        QCOMPARE(f.wallet->writes, 0);
    }
    void closeFencesNewCommandsButDrainsOriginalActionWithoutGui()
    {
        Fixture f; f.load(); const auto original = f.store->submit(f.edit(f.record())); QVERIFY(original);
        f.store->close(); f.opening->retire(); f.client.reset();
        QVERIFY(!f.retirement.step(Lane::Cleanup));
        QCOMPARE(f.store->submit(Create()).rejection, execution::Rejection::Closed);
        f.commands.drain(); QVERIFY(original.operation->result()->request_saved);
        f.retirement.drain(); QVERIFY(f.retirement.quiescent());
        QCOMPARE(original.operation->opening, f.opening->identity());
    }
    void closeBetweenLabelBatchesRetainsOriginalActionThroughAllEffects()
    {
        Fixture f; f.load(70);
        auto command = Intent(Kind::Label); command.address = Address(); command.label = "after-close";
        const auto operation = f.store->submit(command).operation;
        while (f.commands.step(Lane::Domain)) {}
        QVERIFY(f.commands.step(Lane::Core)); QCOMPARE(f.wallet->writes, 32);
        f.store->close(); f.opening->retire(); f.client.reset();
        QVERIFY(!f.retirement.step(Lane::Cleanup));
        f.commands.drain(); f.retirement.drain();
        QVERIFY(operation->result()); QCOMPARE(operation->result()->records_saved, size_t{70});
        QCOMPARE(operation->result()->error, Error::None); QCOMPARE(f.wallet->writes, 70);
        QVERIFY(f.retirement.quiescent());
    }
    void rejectedContinuationRetainsConfirmedPartialEffects_data()
    {
        QTest::addColumn<bool>("transport_failure");
        QTest::addColumn<bool>("label_only");
        QTest::newRow("closed-admission") << false << false;
        QTest::newRow("rejected-transport") << true << false;
        QTest::newRow("label-only-closed-admission") << false << true;
        QTest::newRow("label-only-rejected-transport") << true << true;
    }
    void rejectedContinuationRetainsConfirmedPartialEffects()
    {
        QFETCH(bool, transport_failure);
        QFETCH(bool, label_only);
        Fixture f; f.load(70);
        f.wallet->fail_all_writes = label_only;
        auto command = Intent(Kind::Label); command.address = Address(); command.label = "partial";
        if (transport_failure) f.commands.rejectCoreNumber(f.commands.coreSubmissions() + 2);
        const auto operation = f.store->submit(command).operation;
        while (f.commands.step(Lane::Domain)) {}
        QVERIFY(f.commands.step(Lane::Core)); QCOMPARE(f.wallet->writes, 32);
        if (!transport_failure) f.commands.scheduler->stopAcceptingWork();
        f.commands.drain();
        QVERIFY(operation->result());
        const auto result = *operation->result();
        // Ordinary close cannot revoke an accepted original action. A real
        // continuation transport rejection still reports exactly its partial effects.
        QCOMPARE(result.error, transport_failure ? Error::Partial : label_only ? Error::Persistence : Error::None);
        QVERIFY(result.label_saved);
        const size_t attempted = transport_failure ? 32 : 70;
        QCOMPARE(result.records_saved, label_only ? size_t{0} : attempted); QCOMPARE(result.records_total, size_t{70});
        QCOMPARE(f.wallet->label, std::string{"partial"});
        QCOMPARE(Decode({f.wallet->stored.at("32")}).entries.front().recipient.noteSelf, label_only ? std::string{"private"} : std::string{"partial"});
        QCOMPARE(Decode({f.wallet->stored.at("33")}).entries.front().recipient.noteSelf,
                 transport_failure || label_only ? std::string{"private"} : std::string{"partial"});
        QCOMPARE(f.wallet->writes, static_cast<int>(attempted));
    }
    void originalActionCannotBeUsedByReopenedWallet()
    {
        Fixture old, reopened;
        auto action = old.opening->startAction(); QVERIFY(action);
        QCOMPARE(reopened.opening->planAction(action, [] {}).rejection, execution::Rejection::Invalid);
        QCOMPARE(reopened.opening->perform(action, [](interfaces::Wallet&) { return true; }, [](bool) {}).rejection, execution::Rejection::Invalid);
        old.opening->retire();
        int effects{0}; QVERIFY(old.opening->perform(action, [&](interfaces::Wallet&) { ++effects; return true; }, [](bool) {}));
        old.commands.drain(); QCOMPARE(effects, 1); action.reset(); old.retirement.drain();
    }
    void idExhaustionDoesNotWrapToAnExistingRecord()
    {
        Fixture f; const auto maximum = Entry(std::numeric_limits<int64_t>::max());
        f.wallet->stored[util::ToString(maximum.id)] = Encode(maximum); f.load(0);
        QCOMPARE(f.settle(Create())->result()->error, Error::IdExhausted); QCOMPARE(f.wallet->writes, 0);
    }
    void ingressRejectsOversizeCommands()
    {
        Fixture f; f.load(0); auto oversized = Create(); oversized.draft.note = QString(40'000, 'x');
        QCOMPARE(f.store->submit(oversized).rejection, execution::Rejection::Capacity);
        f.commands.drain();
        QCOMPARE(f.wallet->writes, 0); QVERIFY(f.wallet->stored.empty());
    }
};

#include <test_walletrequests.moc>
BITCOINQML_REGISTER_QT_TEST(WalletRequestsTests)
