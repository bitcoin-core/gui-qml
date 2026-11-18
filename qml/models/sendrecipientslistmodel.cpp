// Copyright (c) 2025-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/models/sendrecipientslistmodel.h>
#include <qml/guipresentation.h>
#include <qml/models/walletqmlmodel.h>
#include <qml/retainedvalue.h>
#include <qml/paymentimportports.h>
#include <QPointer>
#include <QScopeGuard>
#include <array>
#include <cassert>
#include <limits>
#include <mutex>
#include <set>
#include <stdexcept>
#include <utility>

namespace { std::atomic<quint64> NEXT_DRAFT{0}; }
struct SendRecipientsListModel::Backend {
    explicit Backend(senddraft::Limits limits) : draft(limits) {}
    struct Delta { quint64 epoch, sequence; std::optional<SendRecipient::Snapshot> recipient; };
    struct Mode { quint64 epoch, sequence, token, amount_intent_revision; senddraft::AmountMode mode; SendRecipient::Snapshot basis; senddraft::Draft::InputFence inputs; };
    std::deque<Mode> modes;
    std::mutex mutex;
    std::map<quint64, Delta> pending;
    quint64 requested_epoch{0}, requested_revision{0}, requested_intent_revision{0};
    quint64 epoch{0};
    senddraft::Draft draft;
    paymentimport::DraftTarget imports;
    std::shared_ptr<const senddraft::AppliedImport> applied_import;
    struct Version { quint64 epoch, revision, intent_revision; };
    Version synchronize()
    {
        std::map<quint64, Delta> deltas; std::deque<Mode> mode_changes; quint64 input_epoch, revision, intent_revision;
        { std::lock_guard lock{mutex}; deltas.swap(pending); mode_changes.swap(modes); input_epoch = requested_epoch; revision = requested_revision; intent_revision = requested_intent_revision; }
        if (epoch != input_epoch) { draft.clear(); epoch = input_epoch; }
        struct Change {
            quint64 token;
            std::optional<SendRecipient::Snapshot> recipient;
            std::optional<senddraft::AmountMode> mode;
            quint64 amount_intent_revision{0};
            senddraft::Draft::InputFence inputs{};
        };
        std::map<quint64, Change> ordered;
        for (auto& [token, delta] : deltas)
            if (delta.epoch == input_epoch) ordered.emplace(delta.sequence, Change{token, std::move(delta.recipient), {}});
        for (auto& mode : mode_changes)
            if (mode.epoch == input_epoch) ordered.emplace(mode.sequence, Change{mode.token, std::move(mode.basis), mode.mode, mode.amount_intent_revision, std::move(mode.inputs)});
        for (auto& [sequence, change] : ordered) {
            if (change.mode) {
                if (!draft.recipients().contains(change.token) && change.recipient) draft.update(std::move(*change.recipient));
                draft.choose(change.token, *change.mode, change.amount_intent_revision, change.inputs);
            }
            else if (change.recipient) draft.update(std::move(*change.recipient));
            else draft.remove(change.token);
        }
        return {input_epoch, revision, intent_revision};
    }
};
struct SendRecipientsListModel::Retired {
    std::shared_ptr<QObject> owner;
    std::function<void(std::function<void()>)> post;
    std::deque<std::shared_ptr<Rows>> rows;
    bool pumping{false};
    std::shared_ptr<Backend> backend;
    std::shared_ptr<const DraftSnapshot> snapshot;
    std::shared_ptr<execution::SnapshotQuery<std::shared_ptr<const DraftSnapshot>>> query;
};
SendRecipientsListModel::SendRecipientsListModel(QObject* parent, senddraft::Limits limits)
    : QAbstractListModel(parent), m_wallet(qobject_cast<WalletQmlModel*>(parent)),
      m_limits{std::clamp(limits.recipients, 1, 256)}, m_identity(++NEXT_DRAFT)
{
    m_recipient_owner = presentation::MakeGuiObject();
    m_retirement_post = presentation::QueuedPost();
    m_recipients->append(createRecipient());
}
SendRecipientsListModel::~SendRecipientsListModel() { close(); retireValues(); }
SendRecipient* SendRecipientsListModel::createRecipient()
{
    auto* recipient = new SendRecipient(m_wallet, m_recipient_owner.get());
    recipient->setImportLock(m_import_pending);
    connectRecipientSignals(recipient);
    if (m_opening) recipient->setOpening(m_opening, m_post);
    return recipient;
}
void SendRecipientsListModel::setOpening(std::shared_ptr<walletopening::Session> opening, presentation::Post post)
{
    if (m_closed || m_query || (m_opening && m_opening != opening)) return;
    m_opening = std::move(opening); m_post = std::move(post);
    const auto setup = ++m_setup_revision;
    const QPointer<SendRecipientsListModel> observer{this};
    if (!m_post) m_post = presentation::QueuedPost();
    m_retirement_post = presentation::ReliablePost(m_post);
    const auto original_opening = m_opening;
    auto submit = original_opening ? original_opening->observationPost(execution::Lane::Domain) : nullptr;
    const auto source = m_recipients;
    const auto epoch = m_epoch;
    const auto current = [observer, setup, original_opening, source, epoch] {
        return observer && !observer->m_closed && observer->m_setup_revision == setup && observer->m_epoch == epoch &&
            observer->m_opening == original_opening && observer->m_recipients == source;
    };
    if (!submit) return;
    const auto make_retired = [] { return std::make_unique<Retired>(); };
    auto retired = walletopening::RetainValues<Retired>(original_opening, make_retired);
    if (!current()) return;
    if (!retired) { m_error = tr("Recipient cleanup is unavailable."); Q_EMIT validationChanged(); return; }
    const auto make_backend = [limits = m_limits] { return std::make_unique<Backend>(limits); };
    auto backend = walletopening::RetainValues<Backend>(original_opening, make_backend);
    if (!current()) return;
    if (!backend) { m_error = tr("Recipient services are unavailable."); Q_EMIT validationChanged(); return; }
    const auto make_rows = [source, current] {
        return current() ? std::make_unique<Rows>(std::move(*source)) : nullptr;
    };
    auto rows = walletopening::RetainValues<Rows>(original_opening, make_rows);
    if (!current()) return;
    if (!rows) { m_error = tr("Recipient services are unavailable."); Q_EMIT validationChanged(); return; }
    m_retired = std::move(retired); m_backend = backend; m_recipients = std::move(rows);
    backend->requested_intent_revision = m_intent_revision;
    m_query = std::make_shared<execution::SnapshotQuery<std::shared_ptr<const DraftSnapshot>>>(
        [backend, original_opening] {
            const auto [epoch, revision, intent_revision] = backend->synchronize();
            const auto make_snapshot = [&] {
                auto value = std::make_unique<DraftSnapshot>(); value->revision = revision; value->intent_revision = intent_revision; value->maximum = backend->draft.maximum(); value->all_valid = !backend->draft.recipients().empty();
                value->applied_import = backend->applied_import;
                std::set<CTxDestination> destinations;
                for (const auto& [token, recipient] : backend->draft.recipients()) {
                    if (value->amount_total_valid) {
                        if ((recipient.amount > 0 && value->total > std::numeric_limits<CAmount>::max() - recipient.amount) ||
                            (recipient.amount < 0 && value->total < std::numeric_limits<CAmount>::min() - recipient.amount)) value->amount_total_valid = false;
                        else value->total += recipient.amount;
                    }
                    value->positions.emplace(token, value->recipients.size()); value->recipients.push_back(recipient);
                    if (recipient.validation_pending) ++value->pending_count;
                    if (recipient.address_valid && !destinations.insert(recipient.destination).second) value->duplicate_destinations = true;
                    if (!recipient.valid) {
                        ++value->invalid_count; value->all_valid = false;
                        if (value->error.isEmpty()) value->error = !recipient.input_acceptable ? recipient.input_error : backend->draft.recipients().size() > 1 ? tr("Complete recipient %1 before continuing.").arg(value->recipients.size()) : QString{};
                    }
                }
                value->amount_total_valid = value->amount_total_valid && MoneyRange(value->total);
                if (value->duplicate_destinations) {
                    value->all_valid = false;
                    if (value->invalid_count == 0) value->error = tr("Recipient addresses must be unique.");
                }
                if (!value->amount_total_valid) { value->all_valid = false; value->error = tr("Total amount is outside the valid monetary range."); }
                if (value->recipients.empty()) value->error = tr("Enter at least one valid recipient to continue.");
                const bool empty = std::all_of(value->recipients.begin(), value->recipients.end(), [](const auto& recipient) {
                    return recipient.address.isEmpty() && !recipient.amount_set && recipient.input_acceptable && recipient.label.isEmpty() &&
                        recipient.message.isEmpty() && !recipient.payment_request && recipient.mode == senddraft::AmountMode::Fixed;
                });
                value->state = empty ? senddraft::State::Empty : value->all_valid ? senddraft::State::Ready : senddraft::State::Editing;
                value->total_text = value->amount_total_valid ? BitcoinAmount::satsToBtcString(value->total) : QStringLiteral("—"); return value;
            };
            auto snapshot = walletopening::RetainValues<DraftSnapshot>(original_opening, make_snapshot);
            if (!snapshot) throw std::runtime_error{"Recipient snapshot cleanup unavailable"};
            return std::shared_ptr<const DraftSnapshot>{std::move(snapshot)};
        }, [post = m_post, observer, setup] { post([observer, setup] { if (observer && observer->m_setup_revision == setup) observer->publishDraft(); }); },
        std::move(submit));
    m_initializing = true; initializeRecipients(0);
}
void SendRecipientsListModel::initializeRecipients(int row)
{
    if (m_closed || !m_query) return;
    const QPointer<SendRecipientsListModel> guard{this};
    const auto rows = m_recipients;
    [[maybe_unused]] const auto owner = m_recipient_owner;
    const auto opening = m_opening;
    const auto post = m_post;
    const auto setup = m_setup_revision, epoch = m_epoch;
    const auto current = [guard, rows, setup, epoch] {
        return guard && !guard->m_closed && guard->m_setup_revision == setup &&
            guard->m_epoch == epoch && guard->m_recipients == rows;
    };
    const int end = std::min(row + 32, rows->size());
    for (; row < end; ++row) {
        auto* recipient = (*rows)[row]; recipient->setOpening(opening, post);
        if (!current()) return;
        submitRecipient(recipient); if (!current()) return;
    }
    m_initialize_row = row;
    if (row < count()) {
        try { post([current, guard, row] { if (current()) guard->initializeRecipients(row); }); }
        catch (...) { if (current()) m_error = tr("Recipient initialization is waiting for GUI delivery."); }
        return;
    }
    m_initializing = false; Q_EMIT validationChanged();
}
void SendRecipientsListModel::close()
{
    if (m_closed) return;
    m_owner_closed->store(true); // Stops this list's own background import work.
    // Close every recipient before any GUI handoff; deletion is batched below.
    for (int row{0}; row < count(); ++row) (*m_recipients)[row]->close();
    m_closed = true; m_maximum_recipient.clear(); ++m_unit_generation; ++m_clear_generation;
    if (m_query) m_query->close();
    if (!m_changing_rows) retireValues();
}
void SendRecipientsListModel::retireValues()
{
    if (!m_recipient_owner) return;
    auto retired = std::move(m_retired);
    if (!retired) retired = std::make_shared<Retired>(); // No Native services were installed.
    retired->owner = std::move(m_recipient_owner); retired->post = m_retirement_post;
    retired->rows.push_back(std::move(m_recipients));
    m_recipients = std::move(m_empty_rows);
    retired->backend = std::move(m_backend); retired->snapshot = std::move(m_snapshot); retired->query = std::move(m_query);
    if (!std::exchange(retired->pumping, true)) retired->post([retired] { retireClosedRows(retired); });
}
void SendRecipientsListModel::retireClosedRows(std::shared_ptr<Retired> retired)
{
    int budget{32};
    while (budget > 0 && !retired->rows.empty()) {
        const auto rows = retired->rows.front();
        if (rows->empty()) { retired->rows.pop_front(); --budget; continue; }
        auto* recipient = rows->takeAt(rows->size() - 1);
        recipient->close(); delete recipient; --budget;
    }
    if (!retired->rows.empty()) retired->post([retired] { retireClosedRows(retired); });
    else retired->pumping = false;
}
void SendRecipientsListModel::retryPublications()
{
    if (m_closed) return;
    const QPointer<SendRecipientsListModel> guard{this};
    if (m_initializing) { initializeRecipients(m_initialize_row); return; }
    if (m_clearing) { trimToFront(m_clear_generation); return; }
    if (m_units_pending) { synchronizeUnits(m_pending_unit, m_unit_row, m_unit_generation); return; }
    if (!m_retrying) { m_retrying = true; retryRecipients(0); if (!guard || m_closed) return; }
    publishDraft(); if (!guard || m_closed) return;
    if (m_query && !m_pending && (m_failed || !m_snapshot || m_snapshot->revision != m_revision)) {
        m_pending = true; m_query->request(); Q_EMIT validationChanged();
    }
}
void SendRecipientsListModel::retryRecipients(int row)
{
    if (m_closed) return;
    const QPointer<SendRecipientsListModel> guard{this};
    const int end = std::min(row + 32, count());
    for (; row < end && row < count(); ++row) { (*m_recipients)[row]->retryPublications(); if (!guard || m_closed) return; }
    if (row < count()) {
        try { m_post([guard, row] { if (guard) guard->retryRecipients(row); }); }
        catch (...) { m_retrying = false; }
        return;
    }
    m_retrying = false;
}
std::shared_ptr<const SendRecipientsListModel::DraftSnapshot> SendRecipientsListModel::draftSnapshot() const
{
    return !m_closed && !importPending() && !m_failed && !validationPending() && m_snapshot && m_snapshot->revision == m_revision && m_snapshot->intent_revision == m_intent_revision ? m_snapshot : nullptr;
}
std::shared_ptr<const paymentimport::TargetPort> SendRecipientsListModel::importTarget(SendRecipient* recipient)
{
    if (m_closed || !m_query || !m_opening || m_initializing || m_clearing || recipientRow(recipient) < 0) return {};
    struct Call { std::shared_ptr<const int> tag; std::shared_ptr<std::atomic_bool> settled;
                  paymentimport::Child child; std::shared_ptr<std::optional<paymentimport::ChildReply>> outcome;
                  bool acknowledging{false}; };
    struct Routes { std::mutex mutex; std::map<paymentimport::CommandKey, Call> calls; };
    const QPointer<SendRecipientsListModel> observer{this};
    const auto backend = m_backend;
    const auto opening = m_opening;
    const auto query = m_query;
    const auto closed = m_owner_closed, edit_gate = m_import_pending;
    const auto setup = m_setup_revision, epoch = m_epoch, draft = m_identity, token = recipient->identity();
    const auto post = presentation::ReliablePost(m_post);
    const auto post_observation = opening->observationPost(execution::Lane::Domain);
    if (!post_observation) return {};
    const auto make_routes = [] { return std::make_unique<Routes>(); };
    const auto routes = walletopening::RetainValues<Routes>(opening, make_routes);
    if (!observer || observer->m_closed || observer->m_setup_revision != setup || observer->m_epoch != epoch ||
        observer->m_backend != backend || !routes) return {};
    const auto notify = [post, observer] {
        try { post([observer] { if (observer && !observer->m_closed) Q_EMIT observer->validationChanged(); }); }
        catch (...) {} // Presentation failure cannot drop the child receipt.
    };
    const auto facts = [backend, opening, closed, edit_gate, epoch, draft, token](const paymentimport::Inspection* source) {
        const auto version = backend->synchronize();
        paymentimport::Target current{opening->identity(), draft, version.intent_revision, token,
            !closed->load() && opening->available() && version.epoch == epoch, false, false};
        const auto found = backend->draft.recipients().find(token);
        current.compatible = current.available && !edit_gate->load() && found != backend->draft.recipients().end();
        if (source && source->kind == paymentimport::Kind::Payment && found != backend->draft.recipients().end()) {
            paymentimport::PaymentBatch batch{token, found->second.revision, found->second.amount_intent_revision,
                source->payment, source->field, false};
            current.conflicts = paymentimport::PreparePaymentBatch(found->second, batch).status == paymentimport::BatchStatus::ReplacementRequired;
            current.matches_request = paymentimport::MatchesPaymentRequest(found->second, source->payment);
        }
        return current;
    };
    const auto retire = [opening](std::function<void()> work) {
        return opening->retireValues(std::move(work));
    };
    const auto submit = [retire](std::shared_ptr<execution::Task> task) {
        return retire([task = std::move(task)] {
            task->execute();
            task->deliver();
        });
    };
    const auto erase = [routes](paymentimport::CommandKey key, const std::shared_ptr<const int>& tag) {
        Call retired;
        {
            std::lock_guard lock{routes->mutex};
            const auto found = routes->calls.find(key);
            if (found == routes->calls.end() || found->second.tag != tag) return false;
            retired = std::move(found->second);
            routes->calls.erase(found);
        }
        return true;
    };
    const auto forget = [backend, erase](paymentimport::CommandKey key, const paymentimport::Child& child, const std::shared_ptr<const int>& tag) {
        if (erase(key, tag)) backend->imports.acknowledge(key, child);
    };
    const auto query_receipt = [backend, routes, retire](paymentimport::CommandKey key, paymentimport::Child child,
                                               paymentimport::TargetPort::Completed done,
                                               paymentimport::TargetPort::Failed failed) {
        std::shared_ptr<std::optional<paymentimport::ChildReply>> outcome;
        { std::lock_guard lock{routes->mutex}; const auto found = routes->calls.find(key);
          if (found != routes->calls.end() && found->second.child == child) outcome = found->second.outcome; }
        if (!outcome) return execution::SubmitResult{{}, execution::Rejection::Invalid};
        auto task = execution::MakeTask([backend, child, key, outcome] {
            paymentimport::ChildReply reply{paymentimport::Receipt{child, paymentimport::Outcome::Unknown, 0, 0}, {}, {}, {}};
            if (const auto receipt = backend->imports.query(key)) {
                reply.receipt = receipt->receipt;
                reply.applied = *outcome && (*outcome)->applied ? (*outcome)->applied
                    : std::make_shared<const paymentimport::ApplyReceipt>(*receipt);
            } else if (outcome && *outcome) return **outcome;
            return reply;
        }, std::move(done), std::move(failed));
        return retire([task = std::move(task)] {
            task->execute();
            task->deliver();
        });
    };
    auto port = std::make_shared<paymentimport::TargetPort>();
    port->opening = opening->identity(); port->draft = draft; port->recipient = token;
    port->observe = [facts, post_observation](auto source, auto done, auto failed) {
        auto task = execution::MakeTask([facts, source = std::move(source)] { return facts(source.get()); },
            std::move(done), std::move(failed));
        return post_observation(std::move(task));
    };
    port->query = query_receipt;
    port->acknowledge = [routes, retire, forget](paymentimport::CommandKey key, const paymentimport::Child& child) {
        Call call;
        {
            std::lock_guard lock{routes->mutex};
            const auto found = routes->calls.find(key);
            if (found == routes->calls.end() || found->second.child != child || !found->second.settled->load())
                return execution::SubmitResult{{}, execution::Rejection::Invalid};
            if (std::exchange(found->second.acknowledging, true)) return execution::SubmitResult{{}, execution::Rejection::Closed};
            call = found->second;
        }
        const auto submitted = retire([forget, key, child, tag = call.tag] { forget(key, child, tag); });
        if (!submitted) {
            std::lock_guard lock{routes->mutex};
            const auto found = routes->calls.find(key);
            if (found != routes->calls.end() && found->second.tag == call.tag) found->second.acknowledging = false;
        }
        return submitted;
    };
    port->dispatch = [backend, opening, routes, facts, edit_gate, closed, query, notify, query_receipt, submit, erase]
        (paymentimport::CommandKey key, paymentimport::Child child, std::shared_ptr<const paymentimport::Inspection> source,
         paymentimport::TargetPort::Completed done, paymentimport::TargetPort::Failed failed) {
        if (!source || source->kind != paymentimport::Kind::Payment || child.kind != paymentimport::ChildKind::Apply ||
            !key.owner || key.child != child.id || closed->load() || !opening->available())
            return execution::SubmitResult{{}, execution::Rejection::Invalid};
        bool duplicate;
        { std::lock_guard lock{routes->mutex}; duplicate = routes->calls.contains(key); }
        if (duplicate) return query_receipt(key, child, std::move(done), std::move(failed));
        const auto tag = std::make_shared<const int>(0);
        const auto settled = std::make_shared<std::atomic_bool>(false);
        const auto outcome = std::make_shared<std::optional<paymentimport::ChildReply>>();
        const auto discard = [&] { erase(key, tag); };
        bool inserted{false};
        try {
            std::lock_guard lock{routes->mutex};
            inserted = routes->calls.emplace(key, Call{tag, settled, child, outcome}).second;
        } catch (...) { discard(); throw; }
        if (!inserted || closed->load() || edit_gate->exchange(true)) {
            discard();
            return execution::SubmitResult{{}, execution::Rejection::Capacity};
        }
        notify();
        const auto submitted = submit(execution::MakeTask(
            [backend, opening, facts, edit_gate, closed, source, child, key, query, notify, settled, outcome] {
                const auto completed = qScopeGuard([settled] { settled->store(true); });
                // Reserve all publication storage before the atomic local edit.
                const auto make_projection = [] { return std::make_unique<senddraft::AppliedImport>(); };
                const auto make_receipt = [] { return std::make_unique<paymentimport::ApplyReceipt>(); };
                auto projection = walletopening::RetainValues<senddraft::AppliedImport>(opening, make_projection);
                auto retained = walletopening::RetainValues<paymentimport::ApplyReceipt>(opening, make_receipt);
                auto current = facts(source.get());
                current.compatible = current.available && !closed->load() &&
                    backend->draft.recipients().contains(child.target.recipient);
                paymentimport::ChildReply reply{paymentimport::Receipt{child, paymentimport::Outcome::Rejected, 0, current.revision}, current, {}, {}};
                const auto retain_outcome = qScopeGuard([&] { *outcome = reply; });
                bool exhausted;
                { std::lock_guard lock{backend->mutex}; exhausted = backend->requested_revision == std::numeric_limits<quint64>::max(); }
                if (!projection || !retained || exhausted) { edit_gate->store(false); notify(); return reply; }
                projection->formatted_address = BitcoinAddress::formattedAddress(source->payment.address);
                paymentimport::PaymentBatch batch{child.target.recipient, 0, 0, source->payment, source->field, child.replace_existing};
                const auto receipt = backend->imports.apply(key, child, std::move(batch), current, backend->draft);
                if (!receipt) { edit_gate->store(false); notify(); return reply; }
                *retained = *receipt; reply.receipt = receipt->receipt; reply.applied = retained;
                if (receipt->receipt.outcome == paymentimport::Outcome::Applied && receipt->projection) {
                    projection->owner = key.owner; projection->child = child.id; projection->source = child.source;
                    projection->expected_intent = child.target.revision; projection->applied_intent = receipt->receipt.revision;
                    projection->recipient = *receipt->projection; projection->amount_changed = receipt->amount_changed;
                    projection->private_note_changed = receipt->private_note_changed;
                    backend->applied_import = std::move(projection);
                    { std::lock_guard lock{backend->mutex};
                      backend->requested_intent_revision = receipt->receipt.revision; ++backend->requested_revision; }
                    query->request();
                } else { edit_gate->store(false); notify(); }
                return reply;
            }, std::move(done), [backend, outcome, child, key, settled, edit_gate, notify, failed](std::exception_ptr error) {
                // A retained committed receipt wins over a failed projection.
                // Otherwise this Domain turn establishes no local edit occurred.
                if (!backend->imports.query(key)) *outcome = paymentimport::ChildReply{
                    paymentimport::Receipt{child, paymentimport::Outcome::Rejected, 0, child.target.revision}, {}, {}, {}};
                settled->store(true); edit_gate->store(false); notify();
                if (failed) failed(error);
            }));
        if (!submitted) { edit_gate->store(false); discard(); notify(); }
        return submitted;
    };
    return port;
}
void SendRecipientsListModel::submitRecipient(SendRecipient* recipient)
{
    if (m_closed || (importPending() && !m_projecting_import) || !m_query || recipientRow(recipient) < 0) return;
    auto value = recipient->draft(); const auto revision = ++m_revision;
    const auto token = value.token;
    const auto recipient_revision = value.revision;
    { std::lock_guard lock{m_backend->mutex}; m_backend->pending.insert_or_assign(token, Backend::Delta{m_epoch, revision, std::move(value)});
      m_backend->requested_epoch = m_epoch; m_backend->requested_revision = revision; }
    const QPointer<SendRecipientsListModel> guard{this};
    const auto query = m_query;
    const auto setup = m_setup_revision, epoch = m_epoch;
    m_pending = true; query->request();
    if (!guard || m_closed || m_revision != revision || m_setup_revision != setup || m_epoch != epoch) return;
    Q_EMIT recipientDraftChanged(token, recipient_revision);
    if (guard && !m_closed) Q_EMIT validationChanged();
}
void SendRecipientsListModel::removeDraft(quint64 token)
{
    if (!m_query || m_closed) return;
    const auto revision = ++m_revision;
    { std::lock_guard lock{m_backend->mutex}; m_backend->pending.insert_or_assign(token, Backend::Delta{m_epoch, revision, {}});
      m_backend->requested_epoch = m_epoch; m_backend->requested_revision = revision; }
    m_pending = true; m_query->request();
}
SendRecipientsListModel::DraftState SendRecipientsListModel::state() const
{
    if (m_closed) return DraftState::Retired;
    if (m_failed || !m_snapshot || validationPending() || m_snapshot->revision != m_revision) return DraftState::Editing;
    switch (m_snapshot->state) {
    case senddraft::State::Empty: return DraftState::Empty;
    case senddraft::State::Editing: return DraftState::Editing;
    case senddraft::State::Ready: return DraftState::Ready;
    case senddraft::State::Retired: return DraftState::Retired;
    }
    assert(false);
}
void SendRecipientsListModel::submitIntent()
{
    if (m_closed || importPending() || m_projecting_import) return;
    ++m_intent_revision; m_pending = true;
    if (m_backend) { std::lock_guard lock{m_backend->mutex}; m_backend->requested_intent_revision = m_intent_revision; }
    const QPointer<SendRecipientsListModel> guard{this}; Q_EMIT intentSubmitted();
    if (guard && !m_closed) Q_EMIT revisionChanged();
}
bool SendRecipientsListModel::clearCommandError()
{
    if (m_command_error.isEmpty()) return !m_closed;
    const QPointer<SendRecipientsListModel> guard{this}; m_command_error.clear(); Q_EMIT budgetErrorChanged();
    return guard && !m_closed;
}
void SendRecipientsListModel::rejectCommand(const QString& error)
{
    const QPointer<SendRecipientsListModel> guard{this};
    if (m_command_error != error) { m_command_error = error; Q_EMIT budgetErrorChanged(); }
    if (guard && !m_closed) Q_EMIT commandRejected(error);
}
void SendRecipientsListModel::chooseAmountMode(SendRecipient* recipient, SendRecipient::AmountMode mode)
{
    if (importPending()) return;
    if (m_closed || !m_query) return;
    if (recipientRow(recipient) < 0) { rejectCommand(tr("This recipient no longer belongs to the draft.")); return; }
    if (mode == SendRecipient::AmountMode::Fixed) { recipient->chooseFixedAmount(); return; }
    if (mode != SendRecipient::AmountMode::Maximum) { rejectCommand(tr("Unknown recipient amount mode.")); return; }
    const auto token = recipient->identity(); const QPointer<SendRecipientsListModel> guard{this};
    senddraft::Draft::InputFence inputs;
    for (int row{0}; row < count(); ++row) {
        const auto* editor = (*m_recipients)[row];
        inputs.emplace(editor->identity(), senddraft::Draft::InputRevision{editor->validationRevision(), editor->amountIntentRevision()});
    }
    bool accepted{false};
    {
        std::lock_guard lock{m_backend->mutex};
        if (m_backend->modes.size() < senddraft::Limits::pending_modes) {
            const auto revision = ++m_revision;
            m_backend->modes.push_back({m_epoch, revision, token, recipient->amountIntentRevision(), mode == SendRecipient::AmountMode::Maximum ? senddraft::AmountMode::Maximum : senddraft::AmountMode::Fixed, recipient->draft(), std::move(inputs)});
            m_backend->requested_epoch = m_epoch; m_backend->requested_revision = revision; accepted = true;
        }
    }
    if (!accepted) { rejectCommand(tr("Send is still processing amount mode changes. Try again after they finish.")); return; }
    if (!clearCommandError()) return;
    submitIntent(); if (!guard || m_closed) return;
    { const auto query = m_query; query->request(); }
}
void SendRecipientsListModel::removeRecipient(const QString& recipient_id)
{
    if (m_closed) return;
    for (int row{0}; row < count(); ++row)
        if ((*m_recipients)[row]->recipientId() == recipient_id) { removeAt(row); return; }
    rejectCommand(tr("This recipient no longer belongs to the draft."));
}
void SendRecipientsListModel::publishDraft()
{
    if (m_closed || !m_query) return;
    const auto result = m_query->take(); if (!result) return;
    const auto snapshot = result->snapshot ? *result->snapshot : std::shared_ptr<const DraftSnapshot>{};
    if (snapshot && snapshot->applied_import &&
        (snapshot->applied_import->owner != m_import_owner || snapshot->applied_import->child != m_import_child)) {
        if (!projectImport(*snapshot->applied_import, snapshot->revision)) return;
    }
    if (snapshot && (snapshot->revision != m_revision || snapshot->intent_revision != m_intent_revision)) return;
    if (!m_pending && snapshot && m_snapshot && snapshot->revision == m_snapshot->revision) return;
    if (!snapshot) { m_pending = false; m_failed = true; m_error = tr("Recipient validation could not be completed."); Q_EMIT validationChanged(); return; }
    m_pending = true; // Coupled mode/value/facade publication remains guarded.
    m_failed = false; m_snapshot = snapshot; m_error = snapshot->error; m_totalAmount = snapshot->total; m_total_text = snapshot->total_text;
    const QPointer<SendRecipientsListModel> guard{this};
    const auto revision = m_revision;
    SendRecipient* maximum{nullptr};
    for (int row{0}; row < count(); ++row) {
        auto* recipient = (*m_recipients)[row];
        const auto found = snapshot->positions.find(recipient->identity());
        if (found == snapshot->positions.end()) continue;
        const auto& value = snapshot->recipients[found->second];
        recipient->projectAmountMode(value.mode);
        if (!guard || m_closed || m_revision != revision) return;
        if (value.mode == senddraft::AmountMode::Maximum) maximum = recipient;
    }
    if (m_maximum_recipient != maximum) {
        m_maximum_recipient = maximum; Q_EMIT maximumRecipientChanged();
        if (!guard || m_closed || m_revision != revision) return;
    }
    for (int row{0}; row < count(); ++row) {
        auto* recipient = (*m_recipients)[row];
        const auto found = snapshot->positions.find(recipient->identity());
        if (found == snapshot->positions.end()) continue;
        const auto& value = snapshot->recipients[found->second];
        if (value.clear_amount && recipient->validationRevision() <= value.revision) recipient->amount()->clear(BitcoinAmount::Origin::Model);
        if (!guard || m_closed || m_revision != revision) return;
    }
    m_pending = false; m_pending_row_creations = 0;
    Q_EMIT totalAmountChanged();
    if (guard && !m_closed && m_revision == revision) Q_EMIT validationChanged();
}
bool SendRecipientsListModel::projectImport(const senddraft::AppliedImport& value, quint64 revision)
{
    if (m_closed || !importPending() || value.expected_intent != m_intent_revision ||
        value.applied_intent != value.expected_intent + 1) return false;
    const auto rows = m_recipients;
    QPointer<SendRecipient> target;
    for (int row{0}; row < rows->size(); ++row)
        if ((*rows)[row]->identity() == value.recipient.token) { target = (*rows)[row]; break; }
    if (!target) return false;
    const QPointer<SendRecipientsListModel> guard{this};
    const auto backend = m_backend;
    m_import_owner = value.owner; m_import_child = value.child;
    m_intent_revision = value.applied_intent; m_revision = revision;
    m_projecting_import = true;
    SendRecipient* maximum = value.recipient.mode == senddraft::AmountMode::Maximum ? target.data()
        : m_maximum_recipient == target ? nullptr : m_maximum_recipient.data();
    if (m_maximum_recipient != maximum) {
        m_maximum_recipient = maximum;
        Q_EMIT maximumRecipientChanged();
        if (!guard || m_closed) return false;
    }
    target->projectImportedPayment(value.recipient, value.formatted_address,
                                   value.amount_changed, value.private_note_changed);
    if (!guard || m_closed) return false;
    m_projecting_import = false;
    // Canonical data and the child receipt were committed on Domain already.
    // This notification invalidates old preparation aliases without creating
    // another draft edit or performing the imported effect again.
    Q_EMIT intentSubmitted(); if (!guard || m_closed) return false;
    Q_EMIT revisionChanged(); if (!guard || m_closed) return false;
    m_import_pending->store(false);
    Q_EMIT validationChanged(); if (!guard || m_closed) return false;
    if (m_opening) m_opening->postDomain([backend, owner = value.owner, child = value.child] {
        if (backend->applied_import && backend->applied_import->owner == owner && backend->applied_import->child == child)
            backend->applied_import.reset();
    });
    return true;
}
int SendRecipientsListModel::rowCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : count(); }
QVariant SendRecipientsListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.model() != this || index.column() != 0 || index.row() < 0 || index.row() >= count()) return {};
    const auto* recipient = (*m_recipients)[index.row()];
    switch (role) {
    case RecipientIdRole: return recipient->recipientId();
    case RecipientRole: return QVariant::fromValue(const_cast<SendRecipient*>(recipient));
    case AddressRole: return recipient->address()->ellipsesAddress();
    case LabelRole: return recipient->label();
    case AmountRole: return recipient->amount()->toDisplay();
    case MessageRole: return recipient->message();
    case FormattedAddressRole: return recipient->address()->formattedAddress();
    case AmountUnitLabelRole: return recipient->amount()->unitLabel();
    default: return {};
    }
}
QHash<int, QByteArray> SendRecipientsListModel::roleNames() const
{
    return {{RecipientIdRole,"recipientId"},{RecipientRole,"recipientObject"},{AddressRole,"address"},{LabelRole,"label"},{AmountRole,"amount"},
        {MessageRole,"message"},{FormattedAddressRole,"formattedAddress"},{AmountUnitLabelRole,"amountUnitLabel"}};
}
QList<SendRecipient*> SendRecipientsListModel::recipients() const
{
    QList<SendRecipient*> list; list.reserve(count()); for (int row = 0; row < count(); ++row) list.push_back((*m_recipients)[row]); return list;
}
int SendRecipientsListModel::recipientRow(const SendRecipient* recipient) const { return m_recipients->indexOf(const_cast<SendRecipient*>(recipient)); }
SendRecipient* SendRecipientsListModel::currentRecipient() const { return m_current < 0 || m_current >= count() ? nullptr : (*m_recipients)[m_current]; }
void SendRecipientsListModel::connectRecipientSignals(SendRecipient* recipient)
{
    const auto changed = [this, recipient](const QList<int>& roles) {
        const int row = recipientRow(recipient); if (m_closed || row < 0) return;
        const auto model_index = index(row); Q_EMIT dataChanged(model_index, model_index, roles);
    };
    connect(recipient, &SendRecipient::labelChanged, this, [changed] { changed({LabelRole}); });
    connect(recipient, &SendRecipient::messageChanged, this, [changed] { changed({MessageRole}); });
    connect(recipient->address(), &BitcoinAddress::formattedAddressChanged, this, [changed] { changed({AddressRole, FormattedAddressRole}); });
    connect(recipient->amount(), &BitcoinAmount::amountChanged, this, [changed] { changed({AmountRole}); });
    connect(recipient->amount(), &BitcoinAmount::unitChanged, this, [changed] { changed({AmountRole, AmountUnitLabelRole}); });
    connect(recipient->amount(), &BitcoinAmount::unitChanged, this, [this, recipient] {
        if (!m_syncing_units && !m_closed) synchronizeUnits(recipient->amount()->unit(), 0, ++m_unit_generation);
    });
    connect(recipient, &SendRecipient::inputIntentSubmitted, this, &SendRecipientsListModel::submitIntent);
    connect(recipient, &SendRecipient::amountModeRequested, this, [this, recipient](SendRecipient::AmountMode mode) { chooseAmountMode(recipient, mode); });
    connect(recipient, &SendRecipient::draftChanged, this, [this, recipient] { submitRecipient(recipient); });
}
void SendRecipientsListModel::synchronizeUnits(BitcoinAmount::Unit unit, int row, quint64 generation)
{
    if (m_closed || generation != m_unit_generation) return;
    const QPointer<SendRecipientsListModel> guard{this}; m_syncing_units = true; m_units_pending = true; m_pending_unit = unit;
    const int end = std::min(row + 32, count());
    for (; row < end; ++row) { (*m_recipients)[row]->amount()->setUnit(unit); if (!guard) return; }
    m_syncing_units = false; m_unit_row = row;
    if (row < count() && m_post) {
        try { m_post([guard, unit, row, generation] { if (guard) guard->synchronizeUnits(unit, row, generation); }); }
        catch (...) { m_error = tr("Recipient unit presentation is waiting for GUI delivery."); }
    } else {
        m_units_pending = false;
    }
    Q_EMIT validationChanged();
}
void SendRecipientsListModel::add()
{
    if (importPending()) return;
    if (m_closed || m_clearing || m_changing_rows) return;
    if (count() >= maximumRecipients()) { rejectCommand(tr("Send supports at most %1 recipients.").arg(maximumRecipients())); return; }
    if (m_pending_row_creations >= senddraft::Limits::pending_recipients) {
        rejectCommand(tr("Send is still processing recipient changes. Try again after they finish.")); return;
    }
    if (!clearCommandError()) return;
    const QPointer<SendRecipientsListModel> guard{this};
    [[maybe_unused]] const auto owner = m_recipient_owner;
    const auto source = m_recipients;
    const auto setup = m_setup_revision, epoch = m_epoch, revision = m_revision;
    const auto* previous = currentRecipient();
    const auto unit = previous ? previous->amount()->unit() : BitcoinAmount::Unit::BTC;
    const auto current = [guard, source, setup, epoch, revision] {
        return guard && !guard->m_closed && !guard->m_clearing && !guard->m_changing_rows &&
            guard->m_recipients == source && guard->m_setup_revision == setup &&
            guard->m_epoch == epoch && guard->m_revision == revision;
    };
    const QPointer<SendRecipient> prepared{createRecipient()};
    bool adopted{false};
    const auto discard_unused = qScopeGuard([&] { if (!adopted && prepared) { prepared->close(); prepared->deleteLater(); } });
    if (!current() || !prepared) return;
    prepared->amount()->setUnit(unit);
    if (!current() || !prepared) return;
    auto* recipient = prepared.data();
    const int row = count(); const auto rows = m_recipients;
    m_pending = true; ++m_revision; m_changing_rows = true;
    beginInsertRows({}, row, row); if (!guard) return;
    rows->append(recipient); adopted = true; ++m_pending_row_creations; endInsertRows(); if (!guard) return;
    m_changing_rows = false; if (m_closed) { retireValues(); return; }
    submitIntent(); if (!guard || m_closed) return;
    submitRecipient(recipient); if (!guard || m_closed) return;
    Q_EMIT countChanged(); if (!guard || m_closed) return;
    if (!m_query) { Q_EMIT validationChanged(); if (!guard || m_closed) return; }
    setCurrentIndex(row);
}
void SendRecipientsListModel::setCurrentIndex(int row)
{
    if (m_closed || row < 0 || row >= count() || row == m_current) return;
    m_current = row; const QPointer<SendRecipientsListModel> guard{this};
    Q_EMIT currentIndexChanged(); if (guard) Q_EMIT currentRecipientChanged();
}
void SendRecipientsListModel::next() { setCurrentIndex(m_current + 1); }
void SendRecipientsListModel::prev() { setCurrentIndex(m_current - 1); }
void SendRecipientsListModel::remove() { removeAt(m_current); }
void SendRecipientsListModel::removeAt(int row)
{
    if (importPending()) return;
    if (m_closed || m_clearing || m_changing_rows || count() <= 1 || row < 0 || row >= count()) return;
    if (!clearCommandError()) return;
    auto* previous = currentRecipient(); const QPointer<SendRecipientsListModel> guard{this};
    const auto rows = m_recipients; m_pending = true; ++m_revision; m_changing_rows = true;
    beginRemoveRows({}, row, row); if (!guard) return;
    auto* removed = rows->takeAt(row); const auto token = removed->identity();
    removed->close(); removed->setParent(nullptr);
    const QPointer<SendRecipient> removed_guard{removed};
    const auto deferred_delete = qScopeGuard([removed_guard] { if (removed_guard) removed_guard->deleteLater(); });
    if (row < m_current || m_current >= count()) --m_current;
    endRemoveRows(); if (!guard) return;
    m_changing_rows = false; if (m_closed) { retireValues(); return; }
    submitIntent(); if (!guard || m_closed) return;
    removeDraft(token);
    Q_EMIT currentIndexChanged(); if (!guard || m_closed) return;
    if (previous != currentRecipient()) { Q_EMIT currentRecipientChanged(); if (!guard || m_closed) return; }
    Q_EMIT countChanged(); if (guard && !m_closed) Q_EMIT validationChanged();
}
void SendRecipientsListModel::retireRecipients(std::shared_ptr<Rows> rows)
{
    if (!m_retired) m_retired = std::make_shared<Retired>();
    m_retired->owner = m_recipient_owner; m_retired->post = m_retirement_post;
    m_retired->rows.push_back(std::move(rows));
    const auto retired = m_retired;
    if (!std::exchange(retired->pumping, true)) retired->post([retired] { retireClosedRows(retired); });
}
void SendRecipientsListModel::clear()
{
    if (importPending()) return;
    if (m_closed || m_changing_rows) return;
    if (m_pending_row_creations >= senddraft::Limits::pending_recipients) {
        rejectCommand(tr("Send is still processing recipient changes. Try again after they finish.")); return;
    }
    const QPointer<SendRecipientsListModel> guard{this};
    const auto opening = m_opening;
    const auto source = m_recipients;
    const auto owner = m_recipient_owner;
    const auto setup = m_setup_revision, epoch = m_epoch, revision = m_revision, clear_generation = m_clear_generation;
    const auto current = [guard, opening, source, owner, setup, epoch, revision, clear_generation] {
        return guard && !guard->m_closed && !guard->m_changing_rows && guard->m_setup_revision == setup &&
            guard->m_epoch == epoch && guard->m_revision == revision && guard->m_clear_generation == clear_generation &&
            guard->m_opening == opening &&
            guard->m_recipients == source && guard->m_recipient_owner == owner;
    };
    const auto make_rows = [] { return std::make_unique<Rows>(); };
    auto replacement = opening ? walletopening::RetainValues<Rows>(opening, make_rows)
        : std::make_shared<Rows>();
    if (!current()) return;
    if (!replacement) { m_error = tr("The recipient could not be reset. Try again."); Q_EMIT validationChanged(); return; }
    const QPointer<SendRecipient> prepared{createRecipient()};
    bool adopted{false};
    const auto discard_unused = qScopeGuard([&] {
        if (!adopted && prepared) { prepared->close(); prepared->deleteLater(); }
    });
    if (!current() || !prepared) return;
    ++m_epoch; ++m_clear_generation; m_clearing = false; m_initializing = false; m_initialize_row = 0;
    m_pending = true; ++m_revision;
    auto old = m_recipients; m_changing_rows = true;
    beginResetModel(); if (!guard) return;
    if (!m_closed) {
        m_recipients = std::move(replacement); m_current = 0;
        m_recipients->append(prepared.data()); adopted = true; ++m_pending_row_creations; retireRecipients(std::move(old));
    }
    endResetModel(); if (!guard) return;
    m_changing_rows = false; if (m_closed) { retireValues(); return; }
    submitIntent(); if (!guard || m_closed) return;
    submitRecipient(currentRecipient()); if (!guard || m_closed) return;
    Q_EMIT countChanged(); if (!guard || m_closed) return;
    Q_EMIT currentRecipientChanged(); if (!guard || m_closed) return;
    Q_EMIT currentIndexChanged(); if (!guard || m_closed) return;
    Q_EMIT validationChanged(); if (!guard || m_closed) return;
    if (m_wallet) m_wallet->clearSelectedCoins();
    if (guard && !m_closed) Q_EMIT listCleared();
}
void SendRecipientsListModel::clearToFront()
{
    if (importPending()) return;
    if (m_closed || m_changing_rows) return;
    const QPointer<SendRecipientsListModel> guard{this};
    m_clearing = count() > 1;
    if (m_clearing) { submitIntent(); if (!guard || m_closed) return; }
    if (m_current != 0) { m_current = 0; Q_EMIT currentRecipientChanged(); if (!guard) return; Q_EMIT currentIndexChanged(); if (!guard) return; }
    m_clearing = count() > 1; trimToFront(++m_clear_generation);
}
void SendRecipientsListModel::trimToFront(quint64 generation)
{
    if (m_closed || generation != m_clear_generation) return;
    const QPointer<SendRecipientsListModel> guard{this};
    if (count() > 1) {
        const int first = std::max(1, count() - 32), last = count() - 1;
        const auto rows = m_recipients;
        std::array<QPointer<SendRecipient>, 32> detached;
        size_t detached_count{0};
        const auto deferred_delete = qScopeGuard([&] {
            for (size_t i{0}; i < detached_count; ++i) if (detached[i]) detached[i]->deleteLater();
        });
        m_changing_rows = true; beginRemoveRows({}, first, last); if (!guard) return;
        while (rows->size() > first) {
            auto* removed = rows->takeAt(rows->size() - 1); const auto token = removed->identity();
            removed->close(); removed->setParent(nullptr);
            detached[detached_count++] = removed; removeDraft(token);
        }
        endRemoveRows(); if (!guard) return;
        m_changing_rows = false; if (m_closed) { retireValues(); return; }
        Q_EMIT countChanged(); if (!guard || m_closed) return;
    }
    if (count() > 1 && m_post) {
        try { m_post([guard, generation] { if (guard) guard->trimToFront(generation); }); }
        catch (...) { m_error = tr("Recipient removal is waiting for GUI delivery."); Q_EMIT validationChanged(); }
        return;
    }
    m_clearing = false; Q_EMIT validationChanged();
}
