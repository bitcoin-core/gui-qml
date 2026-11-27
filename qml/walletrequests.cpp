// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/walletrequests.h>
#include <qml/walletreceipts.h>

#include <key_io.h>
#include <util/string.h>
#include <wallet/types.h>

#include <algorithm>
#include <limits>
#include <QScopeGuard>
#include <utility>

namespace walletrequests {
namespace {
constexpr size_t MAX_COMMAND_BYTES{65'536}, WRITE_BATCH{32};
size_t Bytes(const Command& command)
{
    size_t size{sizeof(Command)};
    for (const auto* text : {&command.address, &command.label, &command.draft.address, &command.draft.label,
                            &command.draft.message, &command.draft.note, &command.draft.address_type}) {
        size += static_cast<size_t>(text->size()) * sizeof(QChar);
    }
    if (command.expected_label) size += static_cast<size_t>(command.expected_label->size()) * sizeof(QChar);
    size += static_cast<size_t>(command.sent_labels.size()) * (sizeof(QString) * 2 + 32);
    if (command.secret) size += command.secret->size();
    size += secretinput::Bytes(command.captured_secret);
    if (command.resume) {
        const auto& draft = command.resume->draft;
        for (const auto* text : {&draft.address, &draft.label, &draft.message, &draft.note, &draft.address_type}) {
            size += static_cast<size_t>(text->size()) * sizeof(QChar);
        }
    }
    return size;
}
}

std::optional<Result> Operation::result() const
{
    std::lock_guard lock{m_mutex};
    return m_result;
}

void Operation::complete(Result result)
{
    {
        std::lock_guard lock{m_mutex};
        if (m_result) return;
        m_result = std::move(result);
    }
    try { if (m_result_notify) m_result_notify(*m_result); }
    catch (...) { std::lock_guard lock{m_mutex}; m_result->publication_failed = true; }
    try { if (m_notify) m_notify(); }
    catch (...) { std::lock_guard lock{m_mutex}; m_result->publication_failed = true; }
}

struct Store::Effect {
    struct Write { Record record; std::string blob; };
    std::shared_ptr<Operation> operation;
    std::shared_ptr<const walletopening::Session::Action> action;
    std::shared_ptr<const AddressIntent> reservation;
    Kind kind{Kind::Edit};
    std::vector<Write> writes;
    size_t offset{0};
    uint64_t receipt_epoch{0};
    bool paid{false}, require_unused{false}, consume_address{false};
    bool label_stream{false}, label_counted{false}, label_exhausted{false};
    QString label_address;
    std::string label_note;
    int64_t label_cursor{0};
    std::optional<std::pair<CTxDestination, QString>> label;
    std::optional<QString> expected_label;
    Result result;
};

struct Store::Loading {
    std::vector<std::string> blobs;
    std::vector<interfaces::WalletAddress> addresses;
    SharedRecords retired_records;
    std::map<QString, std::set<int64_t>> retired_record_index;
    std::map<QString, QString> retired_labels;
    std::set<int64_t> retired_dirty;
    QSet<QString> known_addresses;
    size_t next_blob{0}, next_address{0};
    bool receipts_reusable{false};
};

struct Store::Observation {
    explicit Observation(std::shared_ptr<const walletreceipts::Snapshot> value)
        : input(std::move(value)), next(input->observed.begin()) {}
    std::shared_ptr<const walletreceipts::Snapshot> input;
    std::set<QString>::const_iterator next;
    std::optional<QString> address;
    int64_t last_id{0};
};

struct Store::SentLabels {
    struct Note { CTxDestination destination; QString address; std::string label; };
    explicit SentLabels(Queued value) : queued(std::move(value)), next(queued.command.sent_labels.cbegin()) {
        result.sent_transaction = queued.command.sent_transaction;
        result.notes_total = static_cast<size_t>(queued.command.sent_labels.size());
    }
    Queued queued;
    QMap<QString, QString>::const_iterator next;
    size_t bytes{0};
    bool validated{false};
    Result result;
};

Store::~Store() = default;

Store::Store(std::shared_ptr<walletopening::Session> opening, Notify notify, Watch watch, Now now, Ready ready, Release release, ReservationStorage storage, Encoder encoder, Decoder decoder, Prepare prepare)
    : m_opening(std::move(opening)), m_notify(std::move(notify)), m_watch(std::move(watch)),
      m_now(std::move(now)), m_encode(std::move(encoder)), m_decode(std::move(decoder)), m_prepare_record(std::move(prepare)), m_ready(std::move(ready)), m_release(std::move(release)), m_storage(std::move(storage)),
      m_retirement([opening = m_opening](auto release) { return opening->postDomain(std::move(release)); }),
      m_preparation_retirement([opening = m_opening](auto release) { return opening->retireValues(std::move(release)); }),
      m_loading_retirement([opening = m_opening](auto release) { return opening->retireValues(std::move(release)); }),
      m_observation_retirement([opening = m_opening](auto release) { return opening->retireValues(std::move(release)); }) {}

Admission Store::submit(Command command, Notify complete)
{
    if (command.kind == Kind::SentLabels) return {{}, execution::Rejection::Invalid};
    return submitImpl(std::move(command), std::move(complete), {});
}

Admission Store::submitResult(Command command, std::function<void(const Result&)> complete, std::shared_ptr<const walletopening::Session::Action> action)
{
    if (command.kind == Kind::SentLabels) return {{}, execution::Rejection::Invalid};
    if (action && ((command.kind != Kind::Label && command.kind != Kind::Create && command.kind != Kind::Edit && command.kind != Kind::Erase) ||
                   action->identity() != m_opening->identity())) return {{}, execution::Rejection::Invalid};
    return submitImpl(std::move(command), {}, std::move(action), std::move(complete));
}

Admission Store::submitSentLabels(std::shared_ptr<const walletopening::Session::Action> action,
                                 const Txid& transaction, QMap<QString, QString> labels, Notify complete)
{
    if (!action || action->identity() != m_opening->identity() || transaction.IsNull()) return {{}, execution::Rejection::Invalid};
    Command command; command.kind = Kind::SentLabels; command.sent_transaction = transaction; command.sent_labels = std::move(labels);
    return submitImpl(std::move(command), std::move(complete), std::move(action));
}

Admission Store::submitImpl(Command command, Notify complete, std::shared_ptr<const walletopening::Session::Action> action, std::function<void(const Result&)> result_notify)
{
    const auto wipe_rejected = qScopeGuard([&command] { clearSecret(command.secret); });
    if (command.secret_capture_error != secretinput::Error::None) return {{}, execution::Rejection::Capacity};
    if (command.resume && command.kind != Kind::ResumeAddress) return {{}, execution::Rejection::Invalid};
    const size_t bytes{Bytes(command)};
    // The per-command bound covers request metadata, not a captured password.
    if (bytes - secretinput::Bytes(command.captured_secret) > MAX_COMMAND_BYTES)
        return {{}, execution::Rejection::Capacity};
    std::shared_ptr<Operation> operation;
    {
        std::lock_guard lock{m_mutex};
        if (m_closed.load() && !action) return {{}, execution::Rejection::Closed};
        if (!action) action = m_opening->startAction();
        if (!action) return {{}, execution::Rejection::Closed};
        if (m_next_operation == std::numeric_limits<uint64_t>::max()) return {{}, execution::Rejection::Capacity};
        const auto token = m_next_operation++;
        auto intent = command.resume;
        if (command.kind == Kind::ReserveAddress || command.kind == Kind::SelectAddress || command.kind == Kind::ForgetAddress ||
            (command.kind == Kind::Create && (command.draft.address.isEmpty() || command.secret || command.captured_secret))) {
            intent = std::make_shared<const AddressIntent>(AddressIntent{m_opening->identity(), token, command.view,
                command.view_revision, command.draft, command.next, command.explicit_type,
                command.kind == Kind::Create, command.kind == Kind::SelectAddress, command.remember_type});
            m_latest_address_intent[command.view] = token;
        }
        operation = std::make_shared<Operation>(m_opening->identity(), token, command.kind, std::move(complete), std::move(intent), std::move(result_notify));
        m_queue.push_back({std::move(command), operation, action});
        // A publication built before this command may still be waiting for
        // the GUI. It must not report idle once the command is queued, or a
        // reader that marked itself busy on submit sees busy=false first.
        if (m_latest) m_latest->busy = true;
        m_source_epoch->advance();
    }
    const auto admission = schedule(action);
    return {std::move(operation), admission.rejection};
}

execution::SubmitResult Store::schedule(const std::shared_ptr<const walletopening::Session::Action>& action)
{
    {
        std::lock_guard lock{m_mutex};
        if (m_scheduled) return {};
        if (!action) return {{}, execution::Rejection::Closed};
        m_scheduled = true;
    }
    const auto self = shared_from_this();
    const auto admission = m_opening->planAction(action, [self] { self->plan(); });
    if (!admission) {
        std::deque<Queued> rejected;
        {
            std::lock_guard lock{m_mutex};
            m_scheduled = false;
            rejected.swap(m_queue);
        }
        for (const auto& queued : rejected) { clearSecret(queued.command.secret); Result result; result.error = Error::Rejected; queued.operation->complete(std::move(result)); }
    }
    return admission;
}

void Store::plan()
{
    if (m_preparing || m_loading) {
        afterPreparation([self = shared_from_this()] { self->plan(); });
        return;
    }
    m_active = true;
    m_preparation_retirement.retry(); m_loading_retirement.retry();
    // Always derive writes from the latest authority, including payment facts
    // received while a previous Native effect was waiting for consumption.
    bool command_waiting;
    { std::lock_guard lock{m_mutex}; command_waiting = !m_queue.empty(); }
    if (m_loaded && !m_paid_dirty.empty() && m_paid_action && !m_uncertain && (!command_waiting || !m_previous_paid_batch)) {
        m_previous_paid_batch = true;
        auto effect = std::make_shared<Effect>();
        effect->action = m_paid_action; effect->paid = true;
        m_active_action = effect->action;
        // Bound preparation as well as Native writes. Keep the original
        // admitted Action until every remaining dirty row has had its turn.
        // Derive each next batch from current authority, never an old vector.
        for (size_t selected{0}; selected < WRITE_BATCH && !m_paid_dirty.empty(); ++selected) {
            const auto id = *m_paid_dirty.begin();
            m_paid_dirty.erase(m_paid_dirty.begin());
            if (const auto found = m_records.find(id); found != m_records.end()) {
                effect->writes.push_back({*found->second, m_encode(found->second->entry)});
            }
        }
        if (m_paid_dirty.empty()) m_paid_action.reset();
        execute(std::move(effect));
        return;
    }
    Queued queued;
    {
        std::lock_guard lock{m_mutex};
        if (m_queue.empty()) { m_scheduled = false; m_active = false; }
        else { queued = std::move(m_queue.front()); m_queue.pop_front(); }
    }
    if (!queued.operation) { m_active_action.reset(); publish(m_persistence_error); return; }
    m_previous_paid_batch = false;
    m_active_action = queued.action;
    publishMetadata(m_persistence_error);
    const auto& command = queued.command;
    if (m_preparation_failed_revision == m_revision.load() && command.kind != Kind::Reload && command.kind != Kind::SentLabels) {
        fail(queued.operation, Error::Rejected); return;
    }
    const auto self = shared_from_this();
    if (command.kind == Kind::Reload) {
        if (m_loading_retirement.pending() >= 2) { fail(queued.operation, Error::Rejected); return; }
        m_mutating.store(true);
        const auto admitted = m_opening->perform(queued.action, [](interfaces::Wallet& wallet) {
            return std::pair{wallet.getAddressReceiveRequests(), wallet.getAddresses()};
        }, [self, operation = queued.operation, action = queued.action](auto result) {
            self->loaded(std::move(result.first), std::move(result.second), operation, action);
        }, [self, operation = queued.operation](std::exception_ptr) { self->fail(operation, Error::Persistence); });
        if (!admitted) fail(queued.operation, Error::Rejected);
        return;
    }
    if (command.kind == Kind::SentLabels) {
        auto work = std::make_shared<SentLabels>(std::move(queued));
        scheduleSentLabels(work); return;
    }
    if (queued.operation->address_intent || command.kind == Kind::ResumeAddress || command.kind == Kind::ForgetAddress) {
        reserve(std::move(queued)); return;
    }
    planMutation(std::move(queued));
}

void Store::planMutation(Queued queued)
{
    const auto& command = queued.command;
    auto reject = [&](Error error) {
        clearSecret(queued.command.secret);
        Result result; result.error = error; result.address = queued.prepared_address;
        finish(queued.operation, std::move(result));
    };
    if (!m_loaded) { reject(Error::Loading); return; }
    if (m_uncertain) { reject(Error::Uncertain); return; }
    const auto existing = m_records.find(command.id);
    if (command.kind == Kind::Edit || command.kind == Kind::Erase) {
        if (existing == m_records.end()) { reject(Error::Missing); return; }
        if (existing->second->revision != command.revision) { reject(Error::Changed); return; }
    }
    if (command.kind == Kind::Create && command.consume_address) {
        if (const auto consumed = m_consumed_addresses.find(command.draft.address); consumed != m_consumed_addresses.end()) {
            Result result; result.error = Error::AlreadySaved; result.id = consumed->second;
            finish(queued.operation, std::move(result)); return;
        }
        if (m_storage.read) {
            const auto found = m_reservations.find(command.view);
            if (!command.reservation || command.reservation->revoked->load() || found == m_reservations.end() || found->second.intent != command.reservation ||
                found->second.address.address != command.draft.address || !found->second.address.persisted) {
                reject(Error::Changed); return;
            }
        }
    }
    if (command.kind != Kind::Erase && (!m_receipts_ready || !m_ready() ||
        m_checked_revision.load() != m_opening->transactionRevision())) { reject(Error::Checking); return; }
    if (command.kind == Kind::Create && command.preserve_current_note) {
        auto original = std::make_shared<Queued>(std::move(queued));
        original->command.preserve_current_note = false;
        const auto self = shared_from_this();
        const auto admitted = m_opening->perform(original->action, [address = original->command.draft.address](interfaces::Wallet& wallet) -> std::optional<QString> {
            const auto destination = DecodeDestination(address.toStdString());
            std::string label;
            if (!IsValidDestination(destination) || !wallet.getAddress(destination, &label, nullptr) || label.size() > 65536) return {};
            auto note = QString::fromStdString(label);
            if (note.size() > 32768) return {};
            return note;
        }, [self, original](std::optional<QString> note) {
            if (!note) { self->fail(original->operation, Error::Invalid); return; }
            original->command.draft.note = *note;
            if (Bytes(original->command) > MAX_COMMAND_BYTES) { self->fail(original->operation, Error::Rejected); return; }
            const auto& address = original->command.draft.address;
            const auto previous = self->m_labels.find(address);
            if (previous == self->m_labels.end() || previous->second != *note) { self->m_labels[address] = *note; ++self->m_revision; }
            self->planMutation(std::move(*original));
        }, [self, original](std::exception_ptr) { self->fail(original->operation, Error::Persistence); });
        if (!admitted) fail(original->operation, Error::Rejected);
        return;
    }
    auto effect = std::make_shared<Effect>();
    effect->operation = queued.operation; effect->action = std::move(queued.action); effect->kind = command.kind;
    effect->result.address = std::move(queued.prepared_address);
    effect->receipt_epoch = m_receipt_epoch.load();
    effect->reservation = command.reservation;
    effect->expected_label = command.expected_label;
    if (command.kind == Kind::Label) {
        const auto destination = DecodeDestination(command.address.toStdString());
        if (!IsValidDestination(destination)) { reject(Error::Invalid); return; }
        effect->label = {destination, command.label};
        effect->label_stream = true; effect->label_address = command.address; effect->label_note = command.label.toStdString();
        m_mutating.store(true); publishMetadata(m_persistence_error);
        scheduleLabel(effect); return;
    } else {
        Record record;
        if (command.kind != Kind::Create) {
            record = *existing->second;
        } else {
            if (m_highest_id == std::numeric_limits<int64_t>::max()) { reject(Error::IdExhausted); return; }
            // Never reuse a locally attempted ID, including a failed or
            // uncertain create. The public bool is not a no-effect receipt.
            record.entry.id = ++m_highest_id; record.entry.date = m_now();
        }
        effect->result.id = record.entry.id;
        if (command.kind == Kind::Erase) {
            effect->writes.push_back({record, {}});
        } else {
            const auto& draft = command.draft;
            if (!MoneyRange(draft.amount) || draft.address.isEmpty()) { reject(Error::Invalid); return; }
            if (command.kind == Kind::Edit && record.entry.recipient.address != draft.address.toStdString()) { reject(Error::Changed); return; }
            const bool paid = record.entry.payment_received || m_observed.contains(draft.address);
            if ((command.kind == Kind::Create && command.require_unused && paid) ||
                (command.kind == Kind::Edit && paid && (record.entry.recipient.amount != draft.amount ||
                 record.entry.recipient.label != draft.label.toStdString() || record.entry.recipient.message != draft.message.toStdString()))) {
                reject(Error::Paid); return;
            }
            record.destination = DecodeDestination(draft.address.toStdString());
            if (!IsValidDestination(record.destination)) { reject(Error::Invalid); return; }
            const QString old_note = QString::fromStdString(record.entry.recipient.noteSelf);
            const QString address_label = m_labels[draft.address];
            if ((command.kind == Kind::Create || old_note != draft.note) && draft.note != address_label &&
                (!draft.note.isEmpty() || (command.kind == Kind::Edit && address_label == old_note))) effect->label = {record.destination, draft.note};
            record.entry.recipient.address = draft.address.toStdString(); record.entry.recipient.amount = draft.amount;
            record.entry.recipient.label = draft.label.toStdString(); record.entry.recipient.message = draft.message.toStdString();
            record.entry.recipient.noteSelf = draft.note.toStdString(); record.entry.payment_received = paid;
            effect->require_unused = command.require_unused;
            effect->consume_address = command.consume_address && command.kind == Kind::Create;
            effect->writes.push_back({record, m_encode(record.entry)});
        }
    }
    effect->result.records_total = effect->writes.size();
    execute(std::move(effect));
}

execution::SubmitResult Store::inspect(int64_t id, uint64_t view, std::function<void(Inspection)> done)
{
    const auto self = shared_from_this();
    return m_opening->postDomain([self, id, view] {
        Inspection result;
        result.closed = self->m_closed.load(); result.loaded = self->m_loaded;
        result.error = self->m_uncertain.load() ? Error::Uncertain : self->m_preparation_error != Error::None ? self->m_preparation_error :
            self->m_observation_error != Error::None ? self->m_observation_error : !self->m_loaded ? self->m_persistence_error : Error::None;
        result.current = self->presentationCurrent();
        { std::lock_guard lock{self->m_mutex}; result.current = result.current && !self->m_active && !self->m_scheduled && self->m_queue.empty(); }
        result.revision = self->m_revision.load(); result.receipt_epoch = self->m_receipt_epoch.load();
        result.native_revision = self->m_opening->transactionRevision();
        const auto epoch = self->m_source_epoch;
        const auto expected = epoch->value.load();
        result.permission = [epoch, expected, opening = self->m_opening->observationGuard(), current = result.current] {
            return current && epoch->current(expected) && opening();
        };
        if (const auto record = self->m_records.find(id); record != self->m_records.end()) {
            result.record = record->second;
            result.used = record->second->entry.payment_received;
        }
        if (const auto reserved = self->m_reservations.find(view); reserved != self->m_reservations.end()) {
            result.address = reserved->second.address;
            if (self->currentAddressIntent(reserved->second.intent)) {
                result.reservation = reserved->second.intent;
            }
            result.used = result.used || self->m_observed.contains(reserved->second.address.address);
        }
        const auto address = result.record ? result.record->display.address : result.address ? result.address->address : QString{};
        if (const auto label = self->m_labels.find(address); label != self->m_labels.end() && label->second.size() <= 16'384)
            result.current_label = label->second;
        else if (result.address && result.address->address == address && result.address->label.size() <= 16'384)
            result.current_label = result.address->label;
        return result;
    }, done, [done](std::exception_ptr) { Inspection failure; failure.error = Error::Rejected; done(std::move(failure)); });
}

void Store::loaded(std::vector<std::string> blobs, std::vector<interfaces::WalletAddress> addresses,
                   const std::shared_ptr<Operation>& operation,
                   const std::shared_ptr<const walletopening::Session::Action>& action)
{
    if (m_preparing) {
        afterPreparation([self = shared_from_this(), blobs = std::move(blobs), addresses = std::move(addresses), operation, action]() mutable {
            self->loaded(std::move(blobs), std::move(addresses), operation, action);
        });
        return;
    }
    m_active_action = action;
    m_preparation_failed_revision.reset(); m_preparation_error = Error::None;
    auto loading = std::make_shared<Loading>();
    loading->receipts_reusable = m_receipts_ready.load() && m_ready() && m_checked_revision.load() == m_opening->transactionRevision();
    loading->known_addresses = m_watched;
    loading->blobs = std::move(blobs); loading->addresses = std::move(addresses);
    loading->retired_records.swap(m_records); loading->retired_record_index.swap(m_records_by_address); loading->retired_labels.swap(m_labels); loading->retired_dirty.swap(m_paid_dirty);
    m_paid_action.reset(); m_receipts_ready.store(false); m_loaded = false; m_malformed = 0; ++m_revision;
    m_loading = std::move(loading); m_loading_operation = operation;
    publishMetadata(m_persistence_error);
    const auto self = shared_from_this();
    if (!m_opening->planAction(m_active_action, [self] { self->advanceLoad(); })) stopLoad(Error::Rejected);
}

void Store::advanceLoad()
{
    if (!m_loading) return;
    if (m_closed.load()) { stopLoad(Error::Closed); return; }
    try {
        constexpr size_t ROWS{32}, BYTES{256 * 1024};
        size_t used{0};
        for (size_t row{0}; row < ROWS; ++row) {
            if (m_loading->next_blob < m_loading->blobs.size()) {
                const size_t bytes = m_loading->blobs[m_loading->next_blob].size();
                // An existing oversized record gets its own turn rather than
                // being dropped. Per-record legacy limits remain independent
                // of this aggregate row/byte scheduling budget.
                if (used && bytes > BYTES - std::min(used, BYTES)) break;
                auto blob = std::move(m_loading->blobs[m_loading->next_blob++]);
                used += bytes;
                auto entry = m_decode(blob);
                if (!entry) { ++m_malformed; continue; }
                auto destination = DecodeDestination(entry->recipient.address);
                if (entry->id <= 0 || !IsValidDestination(destination) || m_records.contains(entry->id)) { ++m_malformed; continue; }
                const auto address = QString::fromStdString(entry->recipient.address);
                m_loading->receipts_reusable = m_loading->receipts_reusable && m_loading->known_addresses.contains(address);
                m_highest_id = std::max(m_highest_id, entry->id);
                if (m_observed.contains(address) && !entry->payment_received) { entry->payment_received = true; m_paid_dirty.insert(entry->id); }
                const auto id = entry->id;
                m_consumed_addresses.try_emplace(address, id);
                m_records.emplace(id, m_prepare_record(Record{std::move(*entry), ++m_revision, std::move(destination)}));
                m_records_by_address[address].insert(id);
            } else if (m_loading->next_address < m_loading->addresses.size()) {
                const auto& source = m_loading->addresses[m_loading->next_address];
                const size_t bytes = source.name.size() + 128;
                if (used && bytes > BYTES - std::min(used, BYTES)) break;
                auto address = std::move(m_loading->addresses[m_loading->next_address++]);
                used += bytes;
                m_labels[QString::fromStdString(EncodeDestination(address.dest))] = QString::fromStdString(address.name);
            } else break;
        }
        if (m_loading->next_blob != m_loading->blobs.size() || m_loading->next_address != m_loading->addresses.size()) {
            const auto self = shared_from_this();
            if (!m_opening->planAction(m_active_action, [self] { self->advanceLoad(); })) stopLoad(Error::Rejected);
            return;
        }
        const bool reusable = m_loading->receipts_reusable && !m_observation_pending.load() && m_ready() && m_checked_revision.load() == m_opening->transactionRevision();
        ++m_revision; m_reload_needs_receipts = !reusable; m_receipts_ready.store(reusable);
        m_loaded = true; m_uncertain = false; m_persistence_error = Error::None; m_watch_started = false;
        if (!m_paid_dirty.empty()) m_paid_action = m_active_action;
        const auto operation = std::exchange(m_loading_operation, {});
        // Vectors of consumed inputs and old maps are released on Cleanup,
        // never by one full-history destructor on Domain or GUI.
        m_loading_retirement.retire(std::exchange(m_loading, {}));
        finish(operation, {});
    } catch (const std::exception&) { stopLoad(Error::Rejected); }
}

void Store::stopLoad(Error error)
{
    if (m_loading) m_loading_retirement.retire(std::exchange(m_loading, {}));
    const auto operation = std::exchange(m_loading_operation, {});
    m_loaded = false; m_receipts_ready.store(false);
    if (error != Error::Closed) { m_preparation_failed_revision = m_revision.load(); m_preparation_error = error; }
    Result result; result.error = error; finish(operation, std::move(result));
}

void Store::scheduleSentLabels(const std::shared_ptr<SentLabels>& work)
{
    const auto self = shared_from_this();
    if (m_opening->planAction(work->queued.action, [self, work] { self->advanceSentLabels(work); })) return;
    work->result.error = work->result.notes_saved || work->result.notes_unchanged ? Error::Partial : Error::Rejected;
    finish(work->queued.operation, work->result);
}

void Store::advanceSentLabels(const std::shared_ptr<SentLabels>& work)
{
    // Validate the shared immutable input incrementally before any note write.
    // Root snapshots are captured in O(1); string conversion/Core work is here.
    if (!work->validated) {
        for (size_t n{0}; n < 128 && work->next != work->queued.command.sent_labels.cend(); ++n, ++work->next) {
            work->bytes += static_cast<size_t>(work->next.key().size() + work->next.value().size()) * 3 + sizeof(SentLabels::Note);
            if (work->bytes > MAX_COMMAND_BYTES) { work->result.error = Error::Rejected; finish(work->queued.operation, work->result); return; }
        }
        if (work->next == work->queued.command.sent_labels.cend()) { work->validated = true; work->next = work->queued.command.sent_labels.cbegin(); }
        scheduleSentLabels(work); return;
    }
    std::vector<SentLabels::Note> notes;
    for (size_t n{0}; n < 16 && work->next != work->queued.command.sent_labels.cend(); ++n, ++work->next) {
        const auto address = work->next.key(); const auto label = work->next.value();
        const auto destination = DecodeDestination(address.toStdString());
        if (!IsValidDestination(destination)) { ++work->result.notes_failed; work->result.error = Error::Partial; continue; }
        if (label.trimmed().isEmpty()) { ++work->result.notes_unchanged; continue; }
        notes.push_back({destination, address, label.toStdString()});
    }
    if (notes.empty()) {
        if (work->next != work->queued.command.sent_labels.cend()) scheduleSentLabels(work);
        else finish(work->queued.operation, work->result);
        return;
    }
    struct Native { size_t saved{0}, unchanged{0}, failed{0}; bool uncertain{false}; std::vector<size_t> confirmed; };
    const auto self = shared_from_this();
    const auto batch = std::make_shared<const std::vector<SentLabels::Note>>(std::move(notes));
    const auto admitted = m_opening->perform(work->queued.action, [batch](interfaces::Wallet& wallet) {
        Native result;
        try {
            for (size_t n{0}; n < batch->size(); ++n) {
                const auto& note = batch->at(n); std::string old;
                const bool exists = wallet.getAddress(note.destination, &old, nullptr);
                if (exists && old == note.label) { ++result.unchanged; result.confirmed.push_back(n); continue; }
                if (wallet.setAddressBook(note.destination, note.label, exists ? std::nullopt : std::optional{wallet::AddressPurpose::SEND})) {
                    ++result.saved; result.confirmed.push_back(n);
                } else ++result.failed;
            }
        } catch (...) { result.uncertain = true; }
        return result;
    }, [self, work, batch](Native result) {
        self->afterPreparation([self, work, batch, result = std::move(result)] {
            work->result.notes_saved += result.saved; work->result.notes_unchanged += result.unchanged; work->result.notes_failed += result.failed;
            for (const auto index : result.confirmed) {
                const auto& note = batch->at(index); const auto label = QString::fromStdString(note.label);
                if (const auto prior = self->m_labels.find(note.address); prior == self->m_labels.end() || prior->second != label) {
                    self->m_labels[note.address] = label; ++self->m_revision;
                }
            }
            if (result.uncertain) { work->result.error = Error::Uncertain; self->finish(work->queued.operation, work->result); return; }
            if (result.failed) work->result.error = Error::Partial;
            if (work->next == work->queued.command.sent_labels.cend()) self->finish(work->queued.operation, work->result);
            else self->scheduleSentLabels(work);
        });
    }, [self, work](std::exception_ptr error) {
        try { std::rethrow_exception(error); }
        catch (const execution::NotStarted&) { work->result.error = work->result.notes_saved || work->result.notes_unchanged ? Error::Partial : Error::Rejected; }
        catch (...) { work->result.error = Error::Uncertain; }
        self->finish(work->queued.operation, work->result);
    });
    if (!admitted) { work->result.error = work->result.notes_saved || work->result.notes_unchanged ? Error::Partial : Error::Rejected; finish(work->queued.operation, work->result); }
}

void Store::scheduleLabel(const std::shared_ptr<Effect>& effect)
{
    const auto self = shared_from_this();
    if (m_opening->planAction(effect->action, [self, effect] { self->advanceLabel(effect); })) return;
    auto result = effect->result;
    result.error = result.label_saved || result.records_saved ? Error::Partial : Error::Rejected;
    finish(effect->operation, std::move(result));
}

void Store::advanceLabel(const std::shared_ptr<Effect>& effect)
{
    // One admitted original-opening action owns every turn, including close.
    // Count without copying rows, then prepare only the next Native batch from
    // current authority after the previous batch's result has been consumed.
    try {
        const auto index = m_records_by_address.find(effect->label_address);
        if (effect->label_counted && effect->label_exhausted) { execute(effect); return; }
        size_t bytes{0};
        for (size_t scanned{0}; scanned < 128; ++scanned) {
            if (index == m_records_by_address.end()) { effect->label_exhausted = true; break; }
            const auto next = index->second.upper_bound(effect->label_cursor);
            if (next == index->second.end()) { effect->label_exhausted = true; break; }
            const auto& record = m_records.at(*next);
            const auto& entry = record->entry;
            if (!effect->label_counted) {
                effect->label_cursor = *next;
                if (entry.recipient.noteSelf != effect->label_note) ++effect->result.records_total;
                continue;
            }
            if (entry.recipient.noteSelf == effect->label_note) { effect->label_cursor = *next; continue; }
            const size_t row_bytes = entry.recipient.address.size() + entry.recipient.label.size() + entry.recipient.message.size() + effect->label_note.size() + 128;
            if (!effect->writes.empty() && row_bytes > 256 * 1024 - std::min(bytes, size_t{256 * 1024})) break;
            auto edited = *record; edited.entry.recipient.noteSelf = effect->label_note;
            auto blob = m_encode(edited.entry);
            effect->writes.push_back({std::move(edited), std::move(blob)});
            effect->label_cursor = *next; bytes += row_bytes;
            if (effect->writes.size() == WRITE_BATCH) break;
        }
        if (!effect->label_counted) {
            if (effect->label_exhausted) {
                effect->label_counted = true; effect->label_cursor = 0;
                effect->label_exhausted = effect->result.records_total == 0;
            }
            scheduleLabel(effect); return;
        }
        if (effect->writes.empty() && !effect->label_exhausted) { scheduleLabel(effect); return; }
        if (effect->writes.empty() && effect->result.label_saved) {
            auto result = effect->result;
            if (result.records_saved && result.records_saved != result.records_total && result.error == Error::Persistence) result.error = Error::Partial;
            finish(effect->operation, std::move(result)); return;
        }
        effect->receipt_epoch = m_receipt_epoch.load();
        execute(effect);
    } catch (const std::exception&) {
        auto result = effect->result;
        result.error = result.label_saved || result.records_saved ? Error::Partial : Error::Rejected;
        finish(effect->operation, std::move(result));
    }
}

void Store::execute(std::shared_ptr<Effect> effect)
{
    if (!effect->paid) { m_mutating.store(true); publishMetadata(m_persistence_error); }
    if (!effect->label_stream) effect->result.records_total = effect->writes.size();
    struct Native {
        std::vector<size_t> saved;
        std::optional<QString> current_label;
        bool label_saved{false};
        bool reservation_released{false};
        Error error{Error::None};
    };
    const auto self = shared_from_this();
    const size_t end = std::min(effect->offset + WRITE_BATCH, effect->writes.size());
    const auto admission = m_opening->perform(effect->action, [self, effect, end](interfaces::Wallet& wallet) {
        Native result;
        if (!effect->paid && effect->kind != Kind::Erase && (!self->m_receipts_ready.load() || !self->m_ready() ||
            effect->receipt_epoch != self->m_receipt_epoch.load() ||
            self->m_checked_revision.load() != self->m_opening->transactionRevision())) {
            result.error = Error::Checking; return result;
        }
        auto label = [&] {
            wallet::AddressPurpose purpose{wallet::AddressPurpose::RECEIVE};
            std::string current;
            if (!wallet.getAddress(effect->label->first, &current, &purpose)) {
                result.error = Error::LabelPersistence; return false;
            }
            if (effect->expected_label && (purpose != wallet::AddressPurpose::RECEIVE || current != effect->expected_label->toStdString())) {
                if (current.size() <= 32768) result.current_label = QString::fromStdString(current);
                result.error = Error::Changed; return false;
            }
            if (!wallet.setAddressBook(effect->label->first, effect->label->second.toStdString(), purpose)) {
                result.error = Error::LabelPersistence; return false;
            }
            result.label_saved = true;
            return true;
        };
        try {
            // This is the original bind's Native admission point. Later view
            // revocation cannot erase an effect already admitted here; its
            // immutable result still accounts for every confirmed write.
            if (effect->consume_address && effect->reservation && effect->reservation->revoked->load()) {
                result.error = Error::Changed; return result;
            }
            if (effect->consume_address && effect->offset == 0 && self->m_storage.retire &&
                !self->m_storage.retire(wallet, QString::fromStdString(effect->writes.front().record.entry.recipient.address))) {
                result.error = Error::ReservationPersistence; return result;
            }
            if (effect->label && effect->kind == Kind::Label && effect->offset == 0 && !effect->result.label_saved && !label()) return result;
            if (effect->require_unused && effect->offset == 0) {
                const auto& destination = effect->writes.front().record.destination;
                for (const auto& tx : wallet.getWalletTxs()) {
                    if (!tx.tx) continue;
                    for (size_t i{0}; i < tx.tx->vout.size(); ++i) {
                        if (i >= tx.txout_is_mine.size() || !tx.txout_is_mine[i] || tx.tx->vout[i].nValue <= 0) continue;
                        CTxDestination output;
                        if (ExtractDestination(tx.tx->vout[i].scriptPubKey, output) && output == destination) { result.error = Error::Paid; return result; }
                    }
                }
            }
            for (size_t index{effect->offset}; index < end; ++index) {
                const auto& write = effect->writes[index];
                if (wallet.setAddressReceiveRequest(write.record.destination, util::ToString(write.record.entry.id), write.blob)) result.saved.push_back(index);
                else result.error = Error::Persistence;
            }
            if (effect->label && effect->kind != Kind::Label && end == effect->writes.size() && !result.saved.empty()) label();
            if (effect->consume_address && !result.saved.empty()) {
                result.reservation_released = self->m_release && self->m_release(wallet,
                    QString::fromStdString(effect->writes.front().record.entry.recipient.address));
                if (!result.reservation_released && result.error == Error::None) result.error = Error::Partial;
            }
        } catch (...) {
            // Do not replay a mutation after an exception. Retain confirmed
            // preceding writes, fence new mutations and require an explicit read.
            result.error = Error::Uncertain;
        }
        return result;
    }, [self, effect, end](Native native) {
        self->afterPreparation([self, effect, end, native = std::move(native)]() mutable {
        if (native.error == Error::Paid && !effect->writes.empty()) {
            const auto address = QString::fromStdString(effect->writes.front().record.entry.recipient.address);
            effect->result.paid_address = address;
            self->noteNativePayment(address);
        }
        effect->result.reservation_released = effect->result.reservation_released || native.reservation_released;
        // Native has already confirmed this entire batch. Projection failure
        // cannot erase later saved rows or make the original action replayable.
        effect->result.records_saved += native.saved.size();
        if ((effect->kind == Kind::Create || effect->kind == Kind::Edit) && !native.saved.empty())
            effect->result.request_saved = true;
        if (native.label_saved) effect->result.label_saved = true;
        if (native.current_label) effect->result.current_label = std::move(native.current_label);
        if (native.error != Error::None) effect->result.error = native.error;
        if (native.error == Error::Uncertain) {
            self->m_uncertain = true; self->m_paid_action.reset(); self->m_paid_dirty.clear();
        }
        try {
            if (native.label_saved) {
                ++self->m_revision;
                self->m_labels[QString::fromStdString(EncodeDestination(effect->label->first))] = effect->label->second;
            }
            for (const auto index : native.saved) {
                // Paid facts were committed by observed()/loaded() before the
                // write was planned. Confirmation changes persistence status, not
                // row values/revision. Rebuilding every history between batches
                // would turn bounded serialization into quadratic snapshot work.
                if (effect->paid) continue;
                auto record = effect->writes[index].record;
                const int64_t id = record.entry.id;
                if (effect->kind == Kind::Erase) {
                    ++self->m_revision;
                    if (const auto old = self->m_records.find(id); old != self->m_records.end()) {
                        const auto address = old->second->display.address;
                        if (const auto index = self->m_records_by_address.find(address); index != self->m_records_by_address.end()) {
                            index->second.erase(id);
                            if (index->second.empty()) self->m_records_by_address.erase(index);
                        }
                        self->m_records.erase(old);
                    }
                    self->m_paid_dirty.erase(id); effect->result.erased = true;
                } else {
                    if (self->m_observed.contains(QString::fromStdString(record.entry.recipient.address)) && !record.entry.payment_received) {
                        record.entry.payment_received = true; self->m_paid_dirty.insert(id);
                        if (!self->m_paid_action) self->m_paid_action = effect->action;
                    }
                    record.revision = ++self->m_revision; self->m_highest_id = std::max(self->m_highest_id, id);
                    if (effect->consume_address) self->m_consumed_addresses.try_emplace(
                        QString::fromStdString(record.entry.recipient.address), id);
                    const auto prepared = self->m_prepare_record(std::move(record));
                    self->m_records[id] = prepared;
                    self->m_records_by_address[prepared->display.address].insert(id);
                    if (effect->kind == Kind::Create || effect->kind == Kind::Edit) {
                        effect->result.record = prepared;
                    }
                }
            }
        } catch (...) {
            // The persisted documents remain authoritative; this partial row
            // cache must not derive another mutation until an explicit reload.
            effect->result.error = native.error == Error::Uncertain ? Error::Uncertain : Error::Partial;
            self->m_loaded = false; self->m_snapshot_ready.store(false);
            self->m_preparation_failed_revision = self->m_revision.load();
            self->m_preparation_error = effect->result.error;
            self->m_paid_action.reset(); self->m_paid_dirty.clear();
            self->finish(effect->operation, effect->result); return;
        }
        effect->offset = end;
        if (end < effect->writes.size() && native.error != Error::Uncertain && native.error != Error::Checking && native.error != Error::Changed &&
            !(native.error == Error::LabelPersistence && !effect->result.label_saved)) {
            self->execute(effect); return;
        }
        if (effect->label_stream && native.error != Error::Uncertain && native.error != Error::Checking && native.error != Error::Changed &&
            !(native.error == Error::LabelPersistence && !effect->result.label_saved) && !effect->label_exhausted) {
            effect->offset = 0; effect->writes.clear();
            self->scheduleLabel(effect); return;
        }
        if (effect->result.records_saved && effect->result.records_saved != effect->result.records_total && effect->result.error == Error::Persistence) effect->result.error = Error::Partial;
        if (effect->paid && effect->result.error != Error::None) self->m_persistence_error = effect->result.error;
        self->finish(effect->operation, effect->result);
        });
    }, [self, effect](std::exception_ptr error) {
        Error outcome{Error::Uncertain};
        try { std::rethrow_exception(error); } catch (const execution::NotStarted&) { outcome = Error::Rejected; } catch (...) {}
        if (outcome == Error::Uncertain) {
            self->m_uncertain = true; self->m_paid_action.reset(); self->m_paid_dirty.clear();
        }
        if (effect->paid) self->m_persistence_error = outcome;
        auto result = effect->result;
        result.error = outcome == Error::Rejected && (result.records_saved > 0 || result.label_saved) ? Error::Partial : outcome;
        self->finish(effect->operation, std::move(result));
    });
    if (!admission) {
        if (effect->paid) m_persistence_error = Error::Rejected;
        auto result = effect->result;
        result.error = result.records_saved > 0 || result.label_saved ? Error::Partial : Error::Rejected;
        finish(effect->operation, std::move(result));
    }
}

void Store::finish(const std::shared_ptr<Operation>& operation, Result result)
{
    m_active = false;
    m_mutating.store(false);
    m_retirement.retry();
    m_preparation_retirement.retry(); m_loading_retirement.retry();
    publish(m_persistence_error);
    afterPreparation([self = shared_from_this(), operation, result = std::move(result)]() mutable {
        self->finishPrepared(operation, std::move(result));
    });
}

void Store::finishPrepared(const std::shared_ptr<Operation>& operation, Result result)
{
    updateWatch();
    const bool sent_notes = operation && operation->kind == Kind::SentLabels;
    const bool confirmed = result.request_saved || result.label_saved || result.erased || result.notes_saved || result.notes_unchanged ||
        (sent_notes && result.notes_total == 0) || (result.address && result.address->persisted);
    if (m_closed.load() && !confirmed && result.error == Error::None) result.error = Error::Closed;
    if (!sent_notes && m_preparation_failed_revision == m_revision.load() && result.error == Error::None)
        result.error = result.request_saved || result.label_saved ? Error::Partial : Error::Rejected;
    // During preparation only monotonic payment facts may replace rows. Publish
    // their latest prepared values with the original command's truthful result.
    if (result.record) {
        if (const auto row = m_records.find(result.id); row != m_records.end()) result.record = row->second;
    }
    if (operation) operation->complete(std::move(result));
    m_active_action.reset();
    // Yield between high-level commands, including validation-only failures.
    // Keep the mailbox reserved until the next admitted Domain turn exists.
    std::shared_ptr<const walletopening::Session::Action> action;
    {
        std::lock_guard lock{m_mutex};
        if (!m_queue.empty()) action = m_queue.front().action;
        else action = m_paid_action;
        if (!action) m_scheduled = false;
    }
    if (!action) return;
    const auto self = shared_from_this();
    if (m_opening->planAction(action, [self] { self->plan(); })) return;
    std::deque<Queued> rejected;
    {
        std::lock_guard lock{m_mutex};
        m_scheduled = false; rejected.swap(m_queue);
    }
    m_paid_action.reset(); m_paid_dirty.clear();
    m_persistence_error = Error::Rejected;
    publish(m_persistence_error);
    for (const auto& queued : rejected) {
        clearSecret(queued.command.secret);
        Result rejected_result; rejected_result.error = Error::Rejected;
        queued.operation->complete(std::move(rejected_result));
    }
}

void Store::fail(const std::shared_ptr<Operation>& operation, Error error)
{
    if (operation && operation->kind == Kind::Reload) m_persistence_error = error;
    if (operation && operation->kind == Kind::ForgetAddress && error != Error::None) m_persistence_error = Error::ReservationPersistence;
    Result result; result.error = error; finish(operation, std::move(result));
}

void Store::noteNativePayment(const QString& address)
{
    m_source_epoch->advance();
    if (m_observed.insert(address).second) {
        ++m_receipt_epoch;
        if (m_records_by_address.contains(address)) {
            m_native_paid_repairs.insert(address);
            m_receipts_ready.store(false);
        }
    }
    // A specific Native historical-use read is not a reconciled receipt view.
    // Refresh through Reader even when the currently watched addresses match.
    m_watch_started = false;
}

void Store::observed(const std::shared_ptr<const walletreceipts::Snapshot>& snapshot)
{
    if (!snapshot) return;
    if (m_closed.load() || snapshot->opening != m_opening->identity()) return;
    m_source_epoch->advance();
    m_observation_retirement.retry();
    // Reader snapshots are cumulative for one opening. Equal/smaller sets add
    // no new facts; retain the newest native revision without re-walking rows.
    m_observation_native_revision = std::max(m_observation_native_revision, snapshot->native_revision);
    if (snapshot->observed.size() <= m_accepted_observed_size && !m_incomplete_paid_address && m_native_paid_repairs.empty()) {
        if (!m_observing) {
            m_checked_revision.store(m_observation_native_revision); m_receipts_ready.store(true);
            publishMetadata(m_persistence_error);
        }
        return;
    }
    m_receipts_ready.store(false);
    if (m_observation_retirement.pending() >= 2) {
        // Leave the Reader's current immutable observation with its producer.
        // Explicit reload/new observation can retry after cleanup recovers.
        stopObservation(Error::Rejected); return;
    }
    m_observation_error = Error::None;
    m_accepted_observed_size = std::max(m_accepted_observed_size, snapshot->observed.size());
    ++m_receipt_epoch; // Fence already-planned writes before new facts are reduced.
    if (m_observing) {
        m_observation_retirement.retire(std::exchange(m_pending_observation, snapshot));
        publishMetadata(m_persistence_error); return;
    }
    m_observation_action = m_opening->startAction();
    if (!m_observation_action) { m_accepted_observed_size = m_reconciled_observed_size; m_observation_error = Error::Closed; publishMetadata(m_observation_error); return; }
    m_observation_pending.store(true);
    m_observing = std::make_unique<Observation>(snapshot);
    m_observing->address = m_incomplete_paid_address;
    const auto self = shared_from_this();
    if (!m_opening->planAction(m_observation_action, [self] { self->advanceObservation(); })) stopObservation(Error::Rejected);
    else publishMetadata(m_persistence_error);
}

void Store::advanceObservation()
{
    if (!m_observing) return;
    if (m_closed.load()) { stopObservation(Error::Closed); return; }
    try {
        for (size_t work{0}; work < 128; ++work) {
            auto& observation = *m_observing;
            if (observation.address) {
                // IDs are looked up afresh: erase/reload may run between turns.
                // New rows already inherit monotonic facts at their own commit.
                const auto index = m_records_by_address.find(*observation.address);
                if (index == m_records_by_address.end()) { m_native_paid_repairs.erase(*observation.address); observation.address.reset(); m_incomplete_paid_address.reset(); continue; }
                const auto next = index->second.upper_bound(observation.last_id);
                if (next == index->second.end()) { m_native_paid_repairs.erase(*observation.address); observation.address.reset(); m_incomplete_paid_address.reset(); continue; }
                observation.last_id = *next;
                auto& record = m_records.at(*next);
                if (record->entry.payment_received) continue;
                auto paid = *record; paid.entry.payment_received = true; paid.revision = ++m_revision;
                record = m_prepare_record(std::move(paid));
                if (m_preparing) m_preparing->replace(record);
                m_paid_dirty.insert(*next);
            } else if (observation.next != observation.input->observed.end()) {
                const auto& address = *observation.next++;
                if (m_observed.insert(address).second || m_native_paid_repairs.contains(address)) {
                    observation.address = m_incomplete_paid_address = address; observation.last_id = 0;
                }
            } else {
                m_reconciled_observed_size = std::max(m_reconciled_observed_size, observation.input->observed.size());
                m_observation_retirement.retire(std::move(observation.input));
                m_observing.reset();
                if (m_pending_observation) {
                    m_observing = std::make_unique<Observation>(std::exchange(m_pending_observation, {}));
                    continue; // Finish the admitted input before taking the latest; never restart it.
                }
                m_checked_revision.store(m_observation_native_revision); m_observation_pending.store(false); m_receipts_ready.store(m_native_paid_repairs.empty());
                m_observation_error = Error::None;
                flushPaid(); m_observation_action.reset(); return;
            }
        }
        flushPaid();
        const auto self = shared_from_this();
        if (!m_opening->planAction(m_observation_action, [self] { self->advanceObservation(); })) stopObservation(Error::Rejected);
    } catch (const std::exception&) { stopObservation(Error::Rejected); }
}

void Store::stopObservation(Error error)
{
    if (m_observing) { m_observation_retirement.retire(std::move(m_observing->input)); m_observing.reset(); }
    m_observation_retirement.retire(std::exchange(m_pending_observation, {}));
    m_accepted_observed_size = m_reconciled_observed_size;
    m_observation_pending.store(false); m_receipts_ready.store(false); m_observation_error = error;
    flushPaid(); m_observation_action.reset();
}

void Store::flushPaid()
{
    if (!m_loaded && !m_loading) {
        m_paid_dirty.clear(); m_paid_action.reset(); publish(m_persistence_error); return;
    }
    if (m_uncertain) {
        m_paid_dirty.clear(); m_paid_action.reset(); publish(Error::Uncertain); return;
    }
    if (!m_paid_dirty.empty() && !m_paid_action) m_paid_action = m_observation_action ? m_observation_action : m_opening->startAction();
    // Never call plan while another high-level command owns the writer.
    publish(m_persistence_error);
    if (!m_paid_dirty.empty() && m_paid_action) {
        const auto admitted = schedule(m_paid_action);
        if (!admitted) {
            m_paid_action.reset(); m_paid_dirty.clear();
            m_persistence_error = Error::Rejected; publish(m_persistence_error);
        }
    }
}

void Store::updateWatch()
{
    if (!m_current) return;
    if (m_watch_started && !m_interests_dirty && m_watch_snapshot.lock() == m_current) return;
    auto addresses = m_current->addresses;
    for (const auto& [view, address] : m_interests) addresses.insert(address);
    m_watch_snapshot = m_current;
    m_interests_dirty = false;
    if (m_watch_started && m_watched == addresses) return;
    m_watch_started = true; m_watched = std::move(addresses);
    if (m_watch) m_watch(m_watched);
}

execution::SubmitResult Store::interest(uint64_t view, QString address)
{
    if (m_closed.load()) return {{}, execution::Rejection::Closed};
    if (address.size() > 128) return {{}, execution::Rejection::Capacity};
    const auto self = shared_from_this();
    return m_opening->postDomain([self, view, address = std::move(address)] {
        if (self->m_closed.load()) return;
        if (address.isEmpty()) self->m_interests_dirty |= self->m_interests.erase(view) != 0;
        else if (const auto found = self->m_interests.find(view); found == self->m_interests.end() || found->second != address) {
            self->m_interests[view] = address;
            self->m_interests_dirty = true;
        }
        self->updateWatch();
    });
}

void Store::afterPreparation(std::function<void()> work)
{
    if (m_preparing || m_loading) m_after_preparation.push_back(std::move(work));
    else work();
}

void Store::publish(Error error)
{
    if (!m_closed.load() && m_loaded && (!m_current || m_current->revision != m_revision.load()) && !m_preparing &&
        m_preparation_failed_revision != m_revision.load()) {
        m_prepare_action = m_active_action ? m_active_action : m_paid_action ? m_paid_action : m_opening->startAction();
        if (!m_prepare_action || m_preparation_retirement.pending() >= 2) {
            m_prepare_action.reset(); m_preparation_failed_revision = m_revision.load();
            m_preparation_error = Error::Rejected;
        } else {
            try {
                m_preparing = std::make_shared<SnapshotPreparation>(m_opening->identity(), m_records, m_labels, m_malformed);
                const auto self = shared_from_this();
                if (!m_opening->planAction(m_prepare_action, [self] { self->advancePreparation(); })) stopPreparation(Error::Rejected);
            } catch (const std::exception&) { stopPreparation(Error::Rejected); }
        }
    }
    publishMetadata(error == Error::None ? m_persistence_error : error);
}

void Store::advancePreparation()
{
    if (!m_preparing) return;
    if (m_closed.load()) { stopPreparation(Error::Closed); return; }
    try {
        if (!m_preparing->step(128)) {
            const auto self = shared_from_this();
            if (!m_opening->planAction(m_prepare_action, [self] { self->advancePreparation(); })) stopPreparation(Error::Rejected);
            return;
        }
        m_current = m_preparing->take(m_revision.load());
        if (std::exchange(m_reload_needs_receipts, false)) m_receipts_ready.store(false);
        m_published_revision.store(m_current->revision);
        m_snapshot_ready.store(true);
        m_preparation_failed_revision.reset(); m_preparation_error = Error::None;
        // Sorted candidates/counts were consumed incrementally. This completed
        // preparation has no full-history temporary container left to destroy.
        m_preparing.reset(); m_prepare_action.reset();
        updateWatch();
        publishMetadata(m_persistence_error);
        auto ready = std::exchange(m_after_preparation, {});
        for (auto& work : ready) afterPreparation(std::move(work));
        if (!m_preparing && !m_scheduled && !m_active && m_current->revision != m_revision.load()) publish(m_persistence_error);
    } catch (const std::exception&) { stopPreparation(Error::Rejected); }
}

void Store::stopPreparation(Error error)
{
    if (m_preparing) m_preparation_retirement.retire(std::exchange(m_preparing, {}));
    m_prepare_action.reset();
    if (error != Error::Closed) {
        m_preparation_failed_revision = m_revision.load(); m_preparation_error = error;
    }
    publishMetadata(error);
    auto ready = std::exchange(m_after_preparation, {});
    for (auto& work : ready) afterPreparation(std::move(work));
}

void Store::publishMetadata(Error error)
{
    m_source_epoch->advance();
    if (error == Error::None) error = m_preparation_error;
    if (error == Error::None) error = m_observation_error;
    if (m_uncertain) error = Error::Uncertain;
    bool notify;
    std::optional<Publication> superseded;
    {
        std::lock_guard lock{m_mutex};
        superseded = std::exchange(m_latest, Publication{m_current, error, m_loaded && bool(m_current),
            m_active || bool(m_preparing) || bool(m_loading) || bool(m_observing) || !m_queue.empty() || !m_paid_dirty.empty()});
        notify = !std::exchange(m_notice_pending, true);
    }
    superseded.reset();
    if (notify && m_notify) m_notify();
}

std::optional<Publication> Store::take()
{
    std::lock_guard lock{m_mutex};
    m_notice_pending = false;
    return std::exchange(m_latest, {});
}

void Store::retire(std::shared_ptr<const Snapshot> old_snapshot)
{
    m_retirement.retire(std::move(old_snapshot));
}

void Store::close()
{
    // Accepted commands retain their original Action; no GUI acknowledgement
    // is needed to finish, release it or let the opening physically drain.
    m_closed.store(true);
    m_source_epoch->closed.store(true);
}
} // namespace walletrequests
