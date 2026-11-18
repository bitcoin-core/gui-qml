// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/transactionactivitymodel.h>
#include <key_io.h>
#include <qml/models/backuppolicyio.h>
#include <qml/retainedvalue.h>
#include <qml/guipresentation.h>
#include <qml/walletrequests.h>
#include <QPointer>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QClipboard>
#include <mutex>
#include <limits>
#include <stdexcept>

#include <qml/bitcoinunits.h>
#include <qml/models/transactionflow.h>
#include <qml/models/walletqmlmodel.h>

#include <core_io.h>

#include <QDateTime>

#include <exception>
#include <utility>
#include <vector>

namespace {
using Type = TransactionActivity::Type;
using Direction = TransactionAction::Direction;

int LegacyType(Type type)
{
    switch (type) {
    case Type::Send: return Transaction::SendToAddress;
    case Type::Receive: return Transaction::RecvWithAddress;
    case Type::Mined: return Transaction::Generated;
    case Type::Consolidation:
    case Type::Split:
    case Type::InternalTransfer: return Transaction::SendToSelf;
    default: return Transaction::Other;
    }
}

} // namespace

namespace {
QVariantMap RequestMap(const walletrequests::Record& record)
{
    const auto& display = record.display;
    return {{"requestId", display.id}, {"date", display.date}, {"dateIso", display.date_iso},
        {"address", display.address}, {"addressShort", display.address_short}, {"label", display.label},
        {"message", display.message}, {"noteSelf", display.note}, {"amountSat", qint64(display.amount_sats)},
        {"amountDisplay", display.amount}, {"uri", display.uri}, {"paymentReceived", display.payment_received}};
}
QVariantList RequestsForAddress(const std::shared_ptr<const walletrequests::Snapshot>& history, const QString& address)
{
    QVariantList result;
    if (!history) return result;
    const auto found = history->by_address.find(address);
    if (found != history->by_address.end()) for (const auto id : found->second) if (const auto* record = history->find(id)) result.append(RequestMap(*record));
    return result;
}
}

std::shared_ptr<const QVariantList> ActivityValueListModel::values() const
{
    if (!m_snapshot || !m_snapshot->snapshot) return {};
    const auto& value = *m_snapshot->snapshot;
    switch (m_kind) {
    case Kind::Inputs: return value.detail_inputs;
    case Kind::Outputs: return value.detail_outputs;
    case Kind::Actions: return value.detail_actions;
    case Kind::Raw: return {};
    case Kind::EntryDocument: return value.entry_document;
    }
    return {};
}
int ActivityValueListModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid() || !m_snapshot || !m_snapshot->snapshot) return 0;
    if (m_kind == Kind::Raw) return int((m_snapshot->snapshot->selected_raw.size() + 1023) / 1024);
    const auto rows = values(); return rows ? int(rows->size()) : 0;
}
QVariant ActivityValueListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount() || role != Qt::UserRole) return {};
    if (m_kind == Kind::Raw) return m_snapshot->snapshot->selected_raw.mid(qsizetype(index.row()) * 1024, 1024);
    return values()->at(index.row());
}
void ActivityValueListModel::setSnapshot(std::shared_ptr<const activityview::Hold> snapshot, bool quiet)
{
    if (m_snapshot && snapshot && m_snapshot->snapshot == snapshot->snapshot) { m_snapshot = std::move(snapshot); return; }
    const auto revision = ++m_revision;
    const QPointer<ActivityValueListModel> observer{this};
    if (!quiet) beginResetModel();
    if (!observer) return;
    if (revision == m_revision) m_snapshot = std::move(snapshot);
    if (!quiet) endResetModel();
}

namespace {
bool SameRowInput(const activitystore::Record& a, const activitystore::Record& b)
{
    return a.activity == b.activity && a.labels == b.labels && a.status == b.status && a.depth == b.depth &&
        a.blocks_to_maturity == b.blocks_to_maturity && a.block_height == b.block_height && a.status_known == b.status_known && a.in_mempool == b.in_mempool;
}
} // namespace

struct TransactionActivityModel::Owner {
    enum class ProjectionStep {
        Initialize,
        IndexRequests,
        BuildTransactionRows,
        BuildRequestRows,
        ReconcileRows,
        RemoveMissingRows,
        PrepareRowPatches,
        Complete,
    };
    std::shared_ptr<const Snapshot> input, result;
    std::shared_ptr<const activityview::Hold> previous;
    std::shared_ptr<const walletrequests::Snapshot> requests;
    std::shared_ptr<const activityview::Snapshot> view;
    std::shared_ptr<const QVariantMap> preview;
    std::shared_ptr<const activityview::Snapshot> preview_view;
    std::shared_ptr<const QString> copied;
    std::vector<activityview::Patch> patches;
    QMap<QString, QVariantList> request_matches;
    QSet<QString> fulfilled;
    QMap<QString, Record>::const_iterator next_record;
    activityview::Map<activityview::SharedRow>::const_iterator next_row;
    activityview::Map<activityview::SharedRow>::const_iterator previous_row, new_row;
    std::shared_ptr<activityview::Snapshot> building;
    activityview::Tree tree;
    size_t request_position{0};
    ProjectionStep step{ProjectionStep::Initialize};
    bool counts_changed{false};
    QString error;
    void clear() {
        input.reset(); result.reset(); previous.reset(); requests.reset(); view.reset(); preview.reset(); preview_view.reset(); copied.reset(); patches.clear(); request_matches.clear(); fulfilled.clear(); building.reset(); tree.reset(); previous_row = new_row = next_row = {};
    }
};
struct TransactionActivityModel::Job {
    std::shared_ptr<Owner> owner;
    std::shared_ptr<void> lease;
    std::shared_ptr<walletopening::Session> opening;
    std::shared_ptr<Mailbox> mailbox;
    std::shared_ptr<std::atomic_bool> cancelled;
    Work work;
    QString selected;
    int unit{0};
    quint64 generation{0}, detail_generation{0}, projection_generation{0}, revision{0}, native_revision{0};
    ~Job() { owner.reset(); lease.reset(); }
};
struct TransactionActivityModel::Mailbox {
    std::mutex mutex;
    bool closed{false};
    std::shared_ptr<Job> preview, ready, entry;
    std::function<void()> notify;
    void publish(std::shared_ptr<Job> job, bool is_preview = false) {
        {
            std::lock_guard lock{mutex};
            if (closed) return;
            (is_preview ? preview : ready) = std::move(job);
        }
        try { notify(); } catch (...) { /* The retained result can be consumed by the next GUI poll or close. */ }
    }
};

TransactionActivityModel::TransactionActivityModel(WalletQmlModel* wallet)
    : TransactionActivityModel(wallet, Services{}) {}

TransactionActivityModel::TransactionActivityModel(WalletQmlModel* wallet, Services services)
    : QAbstractListModel(wallet), m_wallet_model(wallet), m_display_unit(wallet->displayUnit()),
      m_opening(wallet->opening()), m_services(std::move(services)),
      m_export_installs(wallet->fileInstallCoordinator() ? wallet->fileInstallCoordinator() : std::make_shared<backup::InstallCoordinator>()), m_mailbox(std::make_shared<Mailbox>())
{
    const QPointer<TransactionActivityModel> observer{this};
    if (!m_services.post) m_services.post = presentation::QueuedPost();
    m_mailbox->notify = [observer, post = m_services.post] { post([observer] { if (observer) observer->receive(); }); };
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, &TransactionActivityModel::poll);
    if (!m_services.schedule_poll) m_services.schedule_poll = [this](auto delay, auto) { m_timer.start(int(delay.count())); };
    if (!m_services.cancel_poll) m_services.cancel_poll = [this] { m_timer.stop(); };
    m_apply_timer.setSingleShot(true);
    connect(&m_apply_timer, &QTimer::timeout, this, &TransactionActivityModel::applyPatches);
    connect(wallet, &WalletQmlModel::transactionChanged, this, &TransactionActivityModel::updateTransaction);
    connect(wallet, &WalletQmlModel::addressListChanged, this, [this] {
        ++m_generation; ++m_reader_revisions.labels; m_pending.labels = true; schedule();
    });
    auto* requests = wallet->receiveRequests();
    const auto rebuild = [this] { rebuildRows(); };
    connect(requests, &QAbstractItemModel::modelReset, this, rebuild);
    connect(requests, &QAbstractItemModel::rowsInserted, this, rebuild);
    connect(requests, &QAbstractItemModel::rowsRemoved, this, rebuild);
    connect(requests, &QAbstractItemModel::dataChanged, this, rebuild);
    connect(wallet, &WalletQmlModel::displayUnitChanged, this, &TransactionActivityModel::setDisplayUnit);
    connect(wallet, &WalletQmlModel::walletUnloaded, this, &TransactionActivityModel::stop);
    reload(); armPoll();
}

TransactionActivityModel::~TransactionActivityModel() { m_in_model_update = false; close(); finishClose(); }

int TransactionActivityModel::rowCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : activityview::Size(m_root); }
QVariant TransactionActivityModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.model() != this || index.column() != 0) return {};
    const auto* row = activityview::At(m_root, index.row());
    return row ? row->row->value(role) : QVariant{};
}
QHash<int, QByteArray> TransactionActivityModel::roleNames() const { return roles(); }

void TransactionActivityModel::close()
{
    if (std::exchange(m_stopped, true)) return;
    m_services.cancel_poll(); m_apply_timer.stop(); m_cancelled->store(true);
    std::shared_ptr<Job> preview, ready, entry;
    { std::lock_guard lock{m_mailbox->mutex}; m_mailbox->closed = true; preview = std::exchange(m_mailbox->preview, {}); ready = std::exchange(m_mailbox->ready, {}); entry = std::exchange(m_mailbox->entry, {}); }
    finishClose();
}
void TransactionActivityModel::finishClose()
{
    if (m_in_model_update) return;
    // Every large reference is dropped before its pre-reserved native lease.
    m_detail_inputs.setSnapshot({}, true); m_detail_outputs.setSnapshot({}, true);
    m_detail_actions.setSnapshot({}, true); m_raw_lines.setSnapshot({}, true); m_entry_document.setSnapshot({}, true); m_entry_active.reset();
    m_root.reset(); m_core_state.reset(); m_core_lease.reset(); m_preview.reset(); m_detail_hold.reset(); m_published.reset(); m_active.reset();
}
void TransactionActivityModel::stop()
{
    if (m_stopped) return;
    close(); m_loading = m_details_loading = false;
    Q_EMIT loadingChanged();
}
void TransactionActivityModel::reload()
{
    ++m_generation; ++m_projection_generation; ++m_reader_revisions.history;
    m_pending.reload = true; schedule();
}
void TransactionActivityModel::updateTransaction(const QString& txid, int change)
{
    Q_UNUSED(change);
    if (m_stopped) return;
    ++m_generation; ++m_reader_revisions.transactions; m_pending.transactions.insert(txid);
    if (m_pending.transactions.size() > 256) { m_pending.transactions.clear(); m_pending.reload = true; }
    schedule();
}
void TransactionActivityModel::refreshStatuses()
{
    if (!m_stopped) { ++m_reader_revisions.status; m_pending.statuses = true; schedule(); }
}
void TransactionActivityModel::poll()
{
    const QPointer<TransactionActivityModel> observer{this};
    receive();
    if (!observer || m_stopped) return;
    // A timer tick requests another observation; it is not itself a change to
    // Core state. Keep the captured status revision stable while a read is in
    // flight so repeated ticks coalesce instead of continually invalidating it.
    m_pending.poll = true; schedule(); armPoll();
}
void TransactionActivityModel::armPoll()
{
    if (m_stopped || !m_opening) return;
    const QPointer<TransactionActivityModel> observer{this};
    m_services.schedule_poll(std::chrono::milliseconds{1000}, [observer] { if (observer) observer->poll(); });
}
void TransactionActivityModel::setDisplayUnit(int unit)
{
    if (m_display_unit == unit) return;
    m_display_unit = unit; rebuildRows();
}
void TransactionActivityModel::rebuildRows() { if (!m_stopped) { ++m_projection_generation; m_pending.projection = true; schedule(); } }
void TransactionActivityModel::requestTransactionDetails(const QString& txid)
{
    if (m_stopped || (!txid.isEmpty() && txid.size() != 64)) return;
    ++m_detail_generation;
    ++m_reader_revisions.details;
    const QPointer<TransactionActivityModel> observer{this};
    if (m_detail_txid != txid) {
        m_preview.reset(); m_detail_hold.reset();
        if (!publishDetailModels()) return;
        m_entry_document.setSnapshot({}); if (!observer || m_stopped) return; m_entry_active.reset();
    }
    m_detail_txid = txid; m_details_loading = !txid.isEmpty();
    m_pending.details = !txid.isEmpty(); m_pending.projection = true;
    schedulePreview(); schedule(); if (observer && !m_stopped) Q_EMIT transactionDetailsChanged();
}
bool TransactionActivityModel::publishDetailModels()
{
    const QPointer<TransactionActivityModel> observer{this};
    m_detail_inputs.setSnapshot(m_detail_hold); if (!observer || m_stopped) return false;
    m_detail_outputs.setSnapshot(m_detail_hold); if (!observer || m_stopped) return false;
    m_detail_actions.setSnapshot(m_detail_hold); if (!observer || m_stopped) return false;
    m_raw_lines.setSnapshot(m_detail_hold); return observer && !m_stopped;
}
void TransactionActivityModel::schedulePreview()
{
    if (m_detail_txid.isEmpty() || !m_core_state || !m_published || !m_opening || !m_opening->available()) return;
    auto owner = std::make_shared<Owner>(); auto held = m_opening->holdValues([owner] { owner->clear(); }); if (!held) return;
    auto job = std::make_shared<Job>(); job->owner = owner; job->opening = m_opening; job->mailbox = m_mailbox; job->cancelled = m_cancelled;
    job->lease = std::move(held); job->selected = m_detail_txid; job->unit = m_display_unit;
    job->detail_generation = m_detail_generation; job->projection_generation = m_projection_generation;
    owner->input = m_core_state; owner->previous = m_published; owner->requests = m_wallet_model->receiveRequests()->snapshot();
    m_opening->postDomain([job] {
        if (job->cancelled->load()) return;
        const auto& snapshot = *job->owner->input; const auto found = snapshot.records.constFind(job->selected);
        const auto row = job->owner->previous->snapshot->rows.find(QStringLiteral("tx:") + job->selected);
        if (found == snapshot.records.cend() || row == job->owner->previous->snapshot->rows.end()) return;
        Snapshot value = snapshot; value.detail_txid = job->selected; value.raw_transaction.clear(); value.flow_resolved = false; value.can_bump = false;
        interfaces::WalletTx cached{}; cached.tx = found->transaction; cached.debit = found->activity.wallet_debit;
        cached.txin_is_mine = found->inputs_mine; cached.txout_is_mine = found->outputs_mine; cached.txout_is_change = found->outputs_change;
        value.flow = BuildTransactionFlow(cached, {}, true);
        auto view = std::make_shared<activityview::Snapshot>(); view->selected_txid = job->selected; view->raw = job->owner->previous->snapshot->raw;
        prepareDetails(*view, buildDetails(job->selected, *row->second, value, job->owner->requests, job->unit, true), job->unit, *row->second);
        job->owner->preview = view->selected_details; job->owner->preview_view = std::move(view); job->mailbox->publish(job, true);
    }, [] {}, {});
}
void TransactionActivityModel::schedule()
{
    if (m_stopped || m_pending.empty()) return;
    if (!m_opening || !m_opening->available()) {
        m_loading = false; m_load_error = tr("This wallet opening is no longer available."); Q_EMIT loadingChanged(); return;
    }
    const QPointer<TransactionActivityModel> observer{this};
    if (!m_loading) { m_loading = true; Q_EMIT loadingChanged(); if (!observer || m_stopped) return; }
    if (m_work_pending || m_scheduled) return;
    m_scheduled = true; QTimer::singleShot(0, this, &TransactionActivityModel::startWork);
}
activityexport::Files activityexport::LocalFiles(std::shared_ptr<backup::InstallCoordinator> coordinator)
{
    Files files;
    files.inspect = [coordinator](const QString& path) {
        const auto bytes = path.toUtf8();
        return backup::Screen(backup::Inspect({bytes.constData(), size_t(bytes.size())}, "activity", ".csv"), *coordinator);
    };
    files.install = [coordinator = std::move(coordinator)](const backup::Proposal& target, bool approved, const Files::Producer& producer) {
        return backup::Write(target, approved, producer, *coordinator);
    };
    return files;
}

activityexport::Files TransactionActivityModel::activityFiles() const
{
    auto files = activityexport::LocalFiles(m_export_installs);
    const std::weak_ptr<walletopening::Session> opening = m_opening;
    files.available = [opening] { const auto original = opening.lock(); return original && original->available(); };
    if (m_opening) files.utility = m_opening->utilityQueue();
    if (files.utility) {
        files.value_cleanup = [opening](auto cleanup) -> std::shared_ptr<void> {
            const auto original = opening.lock();
            return original ? original->holdValues(std::move(cleanup)) : nullptr;
        };
    }
    return files;
}
void TransactionActivityModel::startWork()
{
    m_scheduled = false;
    if (m_stopped || m_work_pending || m_pending.empty()) return;
    auto owner = std::make_shared<Owner>();
    auto held = m_opening->holdValues([owner] { owner->clear(); });
    if (!held) { m_loading = false; m_load_error = tr("Activity presentation is unavailable."); Q_EMIT loadingChanged(); return; }
    auto job = std::make_shared<Job>();
    job->owner = owner; job->lease = std::move(held);
    job->opening = m_opening; job->mailbox = m_mailbox; job->cancelled = m_cancelled;
    job->work = std::exchange(m_pending, {}); job->work.revisions = m_reader_revisions; job->selected = m_detail_txid;
    job->unit = m_display_unit; job->generation = m_generation; job->detail_generation = m_detail_generation;
    job->projection_generation = m_projection_generation; job->revision = ++m_work_revision;
    owner->input = m_core_state; owner->previous = m_published;
    owner->requests = m_wallet_model->receiveRequests()->snapshot();
    m_active = job; m_work_pending = true;
    const auto admitted = m_opening->postDomain([job] { planned(job); }, [] {},
        [job](std::exception_ptr) { job->owner->error = tr("Activity planning could not be completed."); job->mailbox->publish(job); });
    if (!admitted) { owner->error = tr("Activity could not be updated. Try again."); m_mailbox->publish(job); }
}
void TransactionActivityModel::planned(const std::shared_ptr<Job>& job)
{
    auto& owner = *job->owner;
    if (job->cancelled->load() || !job->opening->available()) { owner.error = tr("This wallet opening is no longer available."); job->mailbox->publish(job); return; }
    Snapshot input = owner.input ? *owner.input : Snapshot{};
    if (input.detail_txid != job->selected) { input.flow.clear(); input.raw_transaction.clear(); input.detail_labels.clear(); input.flow_resolved = false; }
    input.detail_txid = job->selected;
    if (!input.history_loaded) job->work.reload = true;
    if (job->work.details && !job->selected.isEmpty() && input.flow.isEmpty()) {
        const auto found = input.records.constFind(job->selected);
        if (found != input.records.cend()) {
            interfaces::WalletTx cached{}; cached.tx = found->transaction; cached.debit = found->activity.wallet_debit;
            cached.txin_is_mine = found->inputs_mine; cached.txout_is_mine = found->outputs_mine; cached.txout_is_change = found->outputs_change;
            input.flow = BuildTransactionFlow(cached, {}, true); input.can_bump = false;
            if (owner.previous && owner.previous->snapshot) {
                const auto row = owner.previous->snapshot->rows.find(QStringLiteral("tx:") + job->selected);
                if (row != owner.previous->snapshot->rows.end()) {
                    auto view = std::make_shared<activityview::Snapshot>();
                    view->raw = [snapshot = owner.input](const QString& id) { const auto found = snapshot->records.constFind(id); return found != snapshot->records.cend() && found->transaction ? QString::fromStdString(EncodeHexTx(*found->transaction)) : QString{}; };
                    view->selected_txid = job->selected;
                    prepareDetails(*view, buildDetails(job->selected, *row->second, input, owner.requests, job->unit, true), job->unit, *row->second);
                    owner.preview = view->selected_details; owner.preview_view = std::move(view);
                    job->mailbox->publish(job, true);
                }
            }
        }
    }
    const bool needs_read = job->work.reload || job->work.labels || job->work.statuses || job->work.poll || job->work.details || !job->work.transactions.isEmpty();
    if (!needs_read) { owner.result = std::make_shared<const Snapshot>(std::move(input)); project(job); return; }
    job->native_revision = job->opening->transactionRevision();
    const auto admitted = job->opening->read([job, input = std::move(input)](interfaces::Wallet& wallet) mutable {
        return std::make_shared<const Snapshot>(activitystore::Reader::read(wallet, std::move(input), job->work,
                                                                            *job->cancelled, job->native_revision));
    }, [job](std::shared_ptr<const Snapshot> result) {
        job->owner->result = std::move(result); readCompleted(job);
    }, [job](std::exception_ptr) { job->owner->error = tr("Wallet activity could not be loaded. Try again."); job->mailbox->publish(job); });
    if (!admitted) { owner.error = tr("Wallet activity could not be loaded. Try again."); job->mailbox->publish(job); }
}
void TransactionActivityModel::readCompleted(const std::shared_ptr<Job>& job)
{
    try { project(job); }
    catch (const std::exception&) { job->owner->error = tr("Activity could not be updated. Try again."); job->mailbox->publish(job); }
}
void TransactionActivityModel::project(const std::shared_ptr<Job>& job)
{
    using Step = Owner::ProjectionStep;
    auto& owner = *job->owner;
    if (job->cancelled->load()) return;
    int budget{128};
    if (owner.step == Step::Initialize) {
        if (!owner.result->rows_changed && !owner.result->details_changed && !job->work.projection && owner.previous) {
            owner.view = owner.previous->snapshot;
            owner.input.reset(); owner.previous.reset(); job->mailbox->publish(job); return;
        }
        owner.building = std::make_shared<activityview::Snapshot>();
        owner.building->display_unit = job->unit;
        auto detail_source = walletopening::RetainValues<DetailSource>(job->opening,[&owner] {
            return std::make_unique<DetailSource>(DetailSource{owner.result,owner.requests});
        });
        if (!detail_source) {
            owner.error = tr("Activity could not be loaded right now. Try again after pending operations finish.");
            job->mailbox->publish(job);
            return;
        }
        owner.building->detail_source = std::move(detail_source);
        owner.building->revision = job->revision; owner.building->opening = job->opening->identity();
        if (owner.previous && owner.previous->snapshot) {
            owner.building->delta_base = owner.previous->snapshot;
            owner.building->rows = owner.previous->snapshot->rows;
            owner.building->details = owner.previous->snapshot->details;
        }
        owner.building->canonical_address = [](const QString& search) {
            auto compact = search; compact.remove(QLatin1Char(' '));
            return IsValidDestination(DecodeDestination(compact.toStdString())) ? compact : search;
        };
        owner.building->raw = [snapshot = owner.result](const QString& txid) {
            const auto found = snapshot->records.constFind(txid);
            return found != snapshot->records.cend() && found->transaction ? QString::fromStdString(EncodeHexTx(*found->transaction)) : QString{};
        };
        owner.tree = owner.previous && owner.previous->snapshot ? owner.previous->snapshot->presentation : activityview::Tree{};
        owner.step = Step::IndexRequests;
    }
    while (budget > 0 && owner.step < Step::Complete) {
        if (job->cancelled->load()) return;
        if (owner.step == Step::IndexRequests) {
            if (!owner.requests || owner.request_position == owner.requests->records.size()) {
                owner.building->request_context = std::make_shared<const QMap<QString, QVariantList>>(owner.request_matches);
                owner.next_record = owner.result->records.cbegin();
                owner.step = Step::BuildTransactionRows;
                continue;
            }
            const auto& request = owner.requests->records[owner.request_position++];
            owner.request_matches[request->display.address].append(RequestMap(*request));
            if (!request->display.payment_received) owner.building->request_uris.emplace(request->display.id, request->display.uri);
            --budget;
        } else if (owner.step == Step::BuildTransactionRows) {
            if (owner.next_record == owner.result->records.cend()) {
                owner.request_position = 0;
                owner.step = Step::BuildRequestRows;
                continue;
            }
            const auto& record = owner.next_record.value();
            ++owner.next_record;
            const QString id = QStringLiteral("tx:") + record.activity.txid;
            const auto cached = owner.building->rows.find(id);
            const auto basis = owner.previous && owner.previous->snapshot->detail_source
                ? std::static_pointer_cast<const DetailSource>(owner.previous->snapshot->detail_source) : nullptr;
            bool reuse{false};
            if (basis && owner.previous->snapshot->display_unit == job->unit && cached != owner.building->rows.end()) {
                const auto old = basis->core->records.constFind(record.activity.txid);
                reuse = old != basis->core->records.cend() && SameRowInput(old.value(), record);
                if (reuse && basis->requests != owner.requests) {
                    const auto prior = owner.previous->snapshot->request_context
                        ? std::static_pointer_cast<const QMap<QString, QVariantList>>(owner.previous->snapshot->request_context) : nullptr;
                    reuse = bool(prior);
                    if (prior) for (const auto& action : record.activity.actions) {
                        if (action.output_index >= 0 && action.direction != Direction::Send && action.amount > 0 &&
                            prior->value(action.address) != owner.request_matches.value(action.address)) { reuse = false; break; }
                    }
                }
            }
            if (reuse) {
                if (cached->second->value(StatusKnownRole).toBool() && !cached->second->value(IsInactiveRole).toBool())
                    for (const auto& request : cached->second->value(PaymentRequestsRole).toList()) owner.fulfilled.insert(request.toMap().value("requestId").toString());
            } else {
                const auto value = buildTransactionRow(record, owner.request_matches, job->unit, owner.fulfilled);
                ++owner.building->transformed_rows;
                if (cached == owner.building->rows.end() || *cached->second != value.second) {
                    owner.building->rows.insert_or_assign(value.first, std::make_shared<const Row>(value.second));
                    owner.building->changed_ids.push_back(value.first);
                }
            }
            --budget;
        } else if (owner.step == Step::BuildRequestRows) {
            if (!owner.requests || owner.request_position == owner.requests->records.size()) {
                owner.next_row = owner.building->rows.begin();
                owner.step = Step::ReconcileRows;
                continue;
            }
            const auto row = buildRequestRow(RequestMap(*owner.requests->records[owner.request_position++]), job->unit, owner.fulfilled);
            if (row) {
                const auto cached = owner.building->rows.find(row->first);
                if (cached == owner.building->rows.end() || *cached->second != row->second) {
                    owner.building->rows.insert_or_assign(row->first, std::make_shared<const Row>(row->second));
                    owner.building->changed_ids.push_back(row->first); ++owner.building->transformed_rows;
                }
            }
            --budget;
        } else if (owner.step == Step::ReconcileRows) {
            if (owner.next_row == owner.building->rows.end()) {
                owner.building->selected_txid = job->selected;
                const auto selected = owner.building->rows.find(QStringLiteral("tx:") + job->selected);
                if (selected != owner.building->rows.end()) {
                    prepareDetails(*owner.building, buildDetails(job->selected, *selected->second, *owner.result, owner.requests, job->unit, true), job->unit, *selected->second);
                } else if (!job->selected.isEmpty()) owner.building->selected_details = std::make_shared<const QVariantMap>(QVariantMap{
                    {"txid", job->selected}, {"detailsLoading", false}, {"detailsError", tr("This transaction is no longer available.")}});
                if (owner.result->flow_resolved && !owner.result->detail_missing) owner.building->selected_raw = owner.result->raw_transaction;
                if (owner.previous && owner.previous->snapshot) owner.previous_row = owner.previous->snapshot->rows.begin();
                owner.step = Step::RemoveMissingRows;
                continue;
            }
            const auto& [id, full] = *owner.next_row++;
            bool kept{true};
            if (full->value(IsPendingRequestRole).toBool()) {
                bool valid{false}; const auto request_id = full->value(RequestIdRole).toString(); const auto number = request_id.toLongLong(&valid);
                kept = valid && owner.requests && owner.requests->find(number) && !owner.fulfilled.contains(request_id);
            } else kept = owner.result->records.contains(full->value(TxidRole).toString());
            if (!kept) {
                owner.building->rows.erase(id); owner.building->details.erase(full->value(TxidRole).toString());
                owner.building->changed_ids.push_back(id); --budget; continue;
            }
            if (full->value(IsPendingRequestRole).toBool()) ++owner.building->requests;
            else {
                ++owner.building->transactions;
                const auto txid = full->value(TxidRole).toString();
                const auto previous = owner.previous ? owner.previous->snapshot->rows.find(id) : owner.building->rows.end();
                const auto basis = owner.previous && owner.previous->snapshot->detail_source
                    ? std::static_pointer_cast<const DetailSource>(owner.previous->snapshot->detail_source) : nullptr;
                bool reuse = basis && previous != owner.previous->snapshot->rows.end() && previous->second == full;
                if (reuse) {
                    const auto old = basis->core->records.constFind(txid); const auto current = owner.result->records.constFind(txid);
                    reuse = old != basis->core->records.cend() && current != owner.result->records.cend() && SameRowInput(old.value(), current.value());
                }
                if (!reuse) owner.building->details.insert_or_assign(txid, std::make_shared<const QVariantMap>(buildDetails(txid, *full, *owner.result, owner.requests, job->unit, false)));
            }
            --budget;
        } else if (owner.step == Step::RemoveMissingRows) {
            if (!owner.previous || !owner.previous->snapshot || owner.previous_row == owner.previous->snapshot->rows.end()) {
                owner.new_row = owner.building->rows.begin();
                owner.step = Step::PrepareRowPatches;
                continue;
            }
            const auto& [id, row] = *owner.previous_row++;
            if (!owner.building->rows.contains(id)) {
                const int position = activityview::Rank(owner.tree, id); owner.tree = activityview::Erase(owner.tree, id);
                const bool request = row->value(IsPendingRequestRole).toBool();
                owner.counts_changed = true;
                owner.patches.push_back({activityview::Patch::Kind::Remove, position, owner.tree, {}, request ? 0 : -1, request ? -1 : 0});
            }
            --budget;
        } else {
            if (owner.new_row == owner.building->rows.end()) {
                owner.step = Step::Complete;
                continue;
            }
            const auto& [id, full] = *owner.new_row++;
            const auto old = owner.previous ? owner.previous->snapshot->rows.find(id) : owner.building->rows.end();
            if (owner.previous && old != owner.previous->snapshot->rows.end() && old->second == full) { --budget; continue; }
            auto row = std::make_shared<const Row>(presentationRow(*full));
            const auto amount = full->value(NetAmountSatRole).toLongLong();
            const bool request = full->value(IsPendingRequestRole).toBool();
            const activityview::Measure measure{request ? 0 : 1, request ? 1 : 0,
                !request && full->value(IsPendingRole).toBool() ? amount : 0,
                amount == std::numeric_limits<qint64>::min() ? std::numeric_limits<qint64>::max() : amount < 0 ? -amount : amount};
            const int position = activityview::Rank(owner.tree, id);
            const auto* previous = activityview::At(owner.tree, position);
            if (!previous || previous->id != id) {
                owner.tree = activityview::Insert(owner.tree, id, row, measure);
                const bool request = row->value(IsPendingRequestRole).toBool();
                owner.counts_changed = true;
                owner.patches.push_back({activityview::Patch::Kind::Insert, position, owner.tree, {}, request ? 0 : 1, request ? 1 : 0});
            } else if (*previous->row != *row) {
                QList<int> changed;
                for (auto role = row->cbegin(); role != row->cend(); ++role) if (previous->row->value(role.key()) != role.value()) changed.append(role.key());
                owner.tree = activityview::Insert(owner.tree, id, row, measure);
                owner.patches.push_back({activityview::Patch::Kind::Change, position, owner.tree, changed});
            }
            --budget;
        }
    }
    if (owner.step == Step::Complete) {
        owner.building->presentation = std::move(owner.tree); owner.building->statistics_valid = true; owner.view = std::move(owner.building);
        owner.input.reset(); owner.previous.reset(); owner.previous_row = owner.new_row = {};
        owner.request_matches.clear(); owner.fulfilled.clear(); owner.next_row = {};
        job->mailbox->publish(job); return;
    }
    const auto admitted = job->opening->postDomain([job] { project(job); }, [] {},
        [job](std::exception_ptr) { job->owner->error = tr("Activity could not be updated. Try again."); job->mailbox->publish(job); });
    if (!admitted) { owner.error = tr("Activity could not be updated. Try again."); job->mailbox->publish(job); }
}
void TransactionActivityModel::receive()
{
    std::shared_ptr<Job> preview, ready, entry;
    {
        std::lock_guard lock{m_mailbox->mutex};
        preview = std::exchange(m_mailbox->preview, {}); ready = std::exchange(m_mailbox->ready, {}); entry = std::exchange(m_mailbox->entry, {});
    }
    if (m_stopped || (m_opening && !m_opening->observationOpen())) return;
    const QPointer<TransactionActivityModel> observer{this};
    if (entry && entry == m_entry_active) {
        m_entry_error = entry->owner->error;
        if (entry->detail_generation == m_detail_generation && entry->owner->view) {
            auto hold = std::make_shared<activityview::Hold>(); hold->snapshot = entry->owner->view; hold->lease = entry->lease;
            m_entry_document.setSnapshot(std::move(hold)); if (!observer || m_stopped) return;
        }
        m_entry_active.reset(); Q_EMIT entryDetailsChanged(); if (!observer || m_stopped) return;
    }
    if (preview && m_details_loading && preview->detail_generation == m_detail_generation && preview->projection_generation == m_projection_generation) {
        auto hold = std::make_shared<activityview::Hold>(); hold->snapshot = preview->owner->preview_view; hold->lease = preview->lease;
        m_detail_hold = std::move(hold); m_preview = preview->owner->preview;
        if (!publishDetailModels()) return;
        Q_EMIT transactionDetailsChanged();
        if (!observer || m_stopped) return;
    }
    if (!ready || ready != m_active) return;
    const quint64 native_revision = ready->owner->result ? ready->owner->result->transaction_revision : ready->native_revision;
    const bool native_snapshot_changed = m_opening && native_revision != m_opening->transactionRevision();
    if (ready->work.supersededBy(m_reader_revisions) || native_snapshot_changed) {
        m_pending.merge(ready->work);
        if (native_snapshot_changed) m_pending.reload = true;
        m_active.reset(); m_work_pending = false;
        m_loading = true;
        schedule();
        return;
    }
    if (!ready->owner->error.isEmpty()) {
        if (ready->detail_generation != m_detail_generation || ready->projection_generation != m_projection_generation) {
            m_pending.merge(ready->work);
            m_pending.projection = true; m_active.reset(); m_work_pending = false; schedule(); return;
        }
        const bool newer = !m_pending.empty();
        m_pending.merge(ready->work);
        m_load_error = ready->owner->error; m_details_loading = false; m_loading = false; m_work_pending = false;
        m_preview.reset(); m_active.reset(); Q_EMIT loadingChanged(); Q_EMIT transactionDetailsChanged(); if (newer) schedule(); return;
    }
    // Keep completed native facts as the next read's basis even if a newer UI
    // intent requires a fresh projection. An active notification stream never
    // forces a new full wallet read merely to reach a quiet interval.
    m_core_state = ready->owner->result; m_core_lease = ready->lease;
    if (ready->projection_generation != m_projection_generation || ready->detail_generation != m_detail_generation) {
        m_pending.projection = true; m_active.reset(); m_work_pending = false; schedule(); return;
    }
    m_patch_position = 0; applyPatches();
}
void TransactionActivityModel::applyPatches()
{
    if (m_stopped || !m_active || (m_opening && !m_opening->observationOpen())) return;
    const auto job = m_active;
    const auto& patches = job->owner->patches;
    int budget{16};
    const QPointer<TransactionActivityModel> observer{this};
    while (budget-- && m_patch_position < patches.size()) {
        const auto& patch = patches[m_patch_position++];
        m_in_model_update = true;
        if (patch.kind == activityview::Patch::Kind::Insert) {
            beginInsertRows({}, patch.position, patch.position);
            if (!observer) return;
            m_root = patch.root; m_transaction_count += patch.transaction_delta; m_request_count += patch.request_delta; endInsertRows();
        } else if (patch.kind == activityview::Patch::Kind::Remove) {
            beginRemoveRows({}, patch.position, patch.position);
            if (!observer) return;
            m_root = patch.root; m_transaction_count += patch.transaction_delta; m_request_count += patch.request_delta; endRemoveRows();
        } else { m_root = patch.root; Q_EMIT dataChanged(index(patch.position), index(patch.position), patch.roles); }
        if (!observer) return;
        m_in_model_update = false;
        if (m_stopped) { finishClose(); return; }
    }
    if (m_patch_position < patches.size()) { m_apply_timer.start(0); return; }
    auto hold = std::make_shared<activityview::Hold>(); hold->snapshot = job->owner->view; hold->lease = job->lease;
    m_preview.reset(); m_published = std::move(hold); m_detail_hold = m_published;
    if (!publishDetailModels()) return;
    m_active.reset();
    // Intermediate persistent roots are no longer observed after the final
    // GUI patch. Optional native eager cleanup avoids keeping every version.
    m_opening->retireValues([owner = job->owner] { owner->patches.clear(); });
    m_work_pending = false; m_details_loading = false; m_load_error.clear(); m_loading = !m_pending.empty();
    if (job->owner->counts_changed) Q_EMIT countChanged();
    if (!observer || m_stopped) return;
    Q_EMIT snapshotChanged(); if (!observer || m_stopped) return;
    Q_EMIT transactionDetailsChanged(); if (!observer || m_stopped) return;
    Q_EMIT loadingChanged(); if (!observer || m_stopped) return;
    schedule();
}
QVariantMap TransactionActivityModel::transactionDetails(const QString& txid, bool full) const
{
    QVariantMap result;
    if (m_published && m_published->snapshot) {
        const auto& view = *m_published->snapshot;
        const auto base = view.details.find(txid);
        if (base != view.details.end()) result = *base->second;
        if (full && txid == m_detail_txid) {
            if (m_preview) result = *m_preview;
            else if (view.selected_txid == txid && view.selected_details) result = *view.selected_details;
        }
    }
    if (full && txid == m_detail_txid && !txid.isEmpty()) {
        result.insert("txid", txid); result.insert("detailsLoading", m_details_loading);
        if (!m_load_error.isEmpty()) { result.insert("detailsError", m_load_error); result.remove("rawTransaction"); }
        if (m_details_loading || m_loading || !m_load_error.isEmpty()) result.insert("canBump", false);
    }
    return result;
}
QString TransactionActivityModel::rawTransaction(const QString& txid) const
{
    return m_detail_hold && m_detail_hold->snapshot && txid == m_detail_hold->snapshot->selected_txid
        ? m_detail_hold->snapshot->selected_raw : QString{};
}
QString TransactionActivityModel::paymentRequestUri(const QString& id) const
{
    if (!m_published || !m_published->snapshot || m_wallet_model->receiveRequestReconciliationPending()) return {};
    const auto found = m_published->snapshot->request_uris.find(id);
    return found == m_published->snapshot->request_uris.end() ? QString{} : found->second;
}
quint64 TransactionActivityModel::requestCopyRawTransaction(const QString& txid)
{
    if (m_stopped || !m_opening || !m_opening->available() || txid.size() != 64 || !m_detail_hold || !m_detail_hold->snapshot->raw) return 0;
    auto owner = std::make_shared<Owner>();
    auto held = m_opening->holdValues([owner] { owner->clear(); });
    if (!held) return 0;
    auto job = std::make_shared<Job>(); job->owner = owner; job->lease = std::move(held);
    owner->previous = m_detail_hold;
    const auto ticket = ++m_copy_ticket;
    const QPointer<TransactionActivityModel> observer{this};
    const auto complete = [job, observer, ticket](std::shared_ptr<const QString> text) {
        job->owner->copied = std::move(text);
        QMetaObject::invokeMethod(QCoreApplication::instance(), [job, observer, ticket] {
            const bool copied = job->owner->copied && !job->owner->copied->isEmpty() && QGuiApplication::clipboard();
            if (copied) QGuiApplication::clipboard()->setText(*job->owner->copied);
            if (observer && !observer->m_stopped && observer->m_opening->observationOpen()) Q_EMIT observer->copyFinished(ticket, copied, copied ? QString{} : tr("The raw transaction could not be copied."));
        }, Qt::QueuedConnection);
    };
    const auto submitted = m_opening->postDomain([job, txid] {
        return std::make_shared<const QString>(job->owner->previous->snapshot->raw(txid));
    }, complete, [complete](std::exception_ptr) { complete({}); }, execution::WorkClass::Action);
    return submitted ? ticket : 0;
}

quint64 TransactionActivityModel::requestEntryDetails(const QString& side, int row)
{
    if (m_stopped || !m_opening || !m_opening->available() || !m_detail_hold || row < 0 || (side != QLatin1String("inputs") && side != QLatin1String("outputs") && side != QLatin1String("actions"))) return 0;
    const auto& snapshot = *m_detail_hold->snapshot;
    const auto entries = side == QLatin1String("inputs") ? snapshot.full_inputs : side == QLatin1String("outputs") ? snapshot.full_outputs : snapshot.full_actions;
    if (!entries || row >= entries->size()) return 0;
    auto owner = std::make_shared<Owner>();
    auto held = m_opening->holdValues([owner] { owner->clear(); }); if (!held) return 0;
    auto job = std::make_shared<Job>(); job->owner = owner; job->opening = m_opening; job->mailbox = m_mailbox;
    job->lease = std::move(held); job->detail_generation = m_detail_generation;
    owner->previous = m_detail_hold; m_entry_active = job; m_entry_error.clear();
    const auto ticket = ++m_entry_ticket;
    const auto publish = [job] {
        { std::lock_guard lock{job->mailbox->mutex}; if (job->mailbox->closed) return; job->mailbox->entry = job; }
        try { job->mailbox->notify(); } catch (...) { }
    };
    const auto admitted = m_opening->postDomain([job, side, row] {
        const auto& snapshot = *job->owner->previous->snapshot;
        const auto entries = side == QLatin1String("inputs") ? snapshot.full_inputs : side == QLatin1String("outputs") ? snapshot.full_outputs : snapshot.full_actions;
        auto lines = std::make_shared<const QVariantList>(FlowEntryDocument(entries->at(row)));
        auto view = std::make_shared<activityview::Snapshot>(); view->entry_document = std::move(lines); job->owner->view = std::move(view);
    }, publish, [job, publish](std::exception_ptr) { job->owner->error = tr("The entry details could not be prepared."); publish(); });
    if (!admitted) { m_entry_active.reset(); m_entry_error = tr("Entry details could not be loaded. Try again."); Q_EMIT entryDetailsChanged(); return 0; }
    Q_EMIT entryDetailsChanged(); return ticket;
}

QHash<int, QByteArray> TransactionActivityModel::roles()
{
    return {
        {AddressRole, "address"}, {AmountRole, "amount"},
        {DateTimeRole, "date"}, {DepthRole, "depth"},
        {LabelRole, "label"}, {StatusRole, "status"},
        {TypeRole, "type"}, {TxidRole, "txid"},
        {CanBumpRole, "canBump"}, {PaymentRequestCountRole, "paymentRequestCount"}, {PresentationLimitedRole, "presentationLimited"}, {ReplacesTxidRole, "replacesTxid"},
        {ReplacedByTxidRole, "replacedByTxid"}, {TimestampRole, "timestamp"},
        {IsPendingRequestRole, "isPendingRequest"}, {RequestIdRole, "requestId"},
        {NetAmountSatRole, "netAmountSat"},
        {IdRole, "activityId"}, {ActivityTypeRole, "activityType"},
        {ActionsRole, "actions"}, {ActionCountRole, "actionCount"},
        {WalletDebitSatRole, "walletDebitSat"}, {WalletCreditSatRole, "walletCreditSat"},
        {FeeSatRole, "feeSat"}, {FeeKnownRole, "feeKnown"},
        {StatusKnownRole, "statusKnown"}, {IsPendingRole, "isPending"},
        {IsInactiveRole, "isInactive"}, {PaymentRequestsRole, "paymentRequests"},
        {HasPaymentRequestRole, "hasPaymentRequest"}, {SearchTextRole, "searchText"},
        {BlocksToMaturityRole, "blocksToMaturity"},
    };
}


std::pair<QString, TransactionActivityModel::Row> TransactionActivityModel::buildTransactionRow(
    const Record& record, const QMap<QString, QVariantList>& requests_by_address, int unit, QSet<QString>& fulfilled)
{
        const auto& tx = record.activity;
        // Replacement metadata is historical. An original transaction that
        // subsequently confirms, or a reorg returns to the mempool, is active
        // even if the marker is still stored. A depth-zero transaction outside
        // the mempool (an evicted replacement, an expired payment or history
        // the node has not synced) is not pending (#1072).
        const bool unrelayed = record.status_known && record.depth == 0 && !record.in_mempool;
        const bool replaced = !tx.replaced_by_txid.isEmpty() && (!record.status_known || record.depth < 0 || unrelayed);
        const bool inactive = replaced || unrelayed || (record.status_known &&
            (record.status == Transaction::Conflicted || record.status == Transaction::Abandoned || record.status == Transaction::NotAccepted));
        QVariantList actions;
        QVariantList requests;
        QSet<QString> request_ids;
        QStringList search{tx.txid};
        for (size_t i = 0; i < tx.actions.size(); ++i) {
            const auto& action = tx.actions[i];
            const bool receive = action.direction == Direction::Receive;
            // Associate requests with owned outputs, never an external send or
            // the aggregate wallet-input contribution of a mixed transaction.
            const QVariantList matches = action.output_index >= 0 && action.direction != Direction::Send && action.amount > 0
                ? requests_by_address.value(action.address) : QVariantList{};
            // A request's public Name is not a private activity note. An
            // associated request owns the note, including an intentionally
            // empty one; never fall back to its name/address-book label.
            const QString label = !matches.isEmpty() ? matches.first().toMap().value("noteSelf").toString()
                : action.direction == Direction::Send ? record.labels.at(i) : QString{};
            for (const auto& match : matches) {
                const auto request = match.toMap();
                const QString id = request.value("requestId").toString();
                if (!request_ids.contains(id)) {
                    request_ids.insert(id);
                    requests.append(match);
                    search << request.value("label").toString() << request.value("message").toString()
                           << request.value("noteSelf").toString();
                }
                if (record.status_known && !inactive) fulfilled.insert(id);
            }
            QVariantList inputs;
            for (const auto& input : action.inputs) {
                inputs.append(QVariantMap{{"txid", QString::fromStdString(input.hash.ToString())}, {"outputIndex", input.n}});
            }
            actions.append(QVariantMap{
                {"actionId", action.id}, {"direction", static_cast<int>(action.direction)},
                {"source", static_cast<int>(action.source)}, {"outputIndex", action.output_index},
                {"inputs", inputs}, {"address", action.address}, {"label", label},
                {"amountSat", qint64(action.amount)}, {"amount", formatAmount(action.amount, receive, unit)},
                {"paymentRequests", matches}, {"hasPaymentRequest", !matches.isEmpty()},
            });
            search << action.address << label << record.labels.at(i);
        }
        const bool single = actions.size() == 1;
        const QVariantMap first = single ? actions.first().toMap() : QVariantMap{};
        const QString label = single ? first.value("label").toString() : QString{};
        search << label;
        const QString id = QStringLiteral("tx:") + tx.txid;
        return {id, Row{
            {IdRole, id}, {TxidRole, tx.txid}, {LabelRole, label},
            {AddressRole, single ? first.value("address") : QVariant{QString{}}},
            {TimestampRole, tx.timestamp},
            {DateTimeRole, QDateTime::fromSecsSinceEpoch(tx.timestamp)},
            {NetAmountSatRole, qint64(tx.walletImpact())},
            {AmountRole, formatAmount(tx.walletImpact(), tx.walletImpact() > 0, unit)},
            {TypeRole, LegacyType(tx.type)}, {ActivityTypeRole, static_cast<int>(tx.type)},
            {DepthRole, record.depth}, {StatusRole, record.status},
            {StatusKnownRole, record.status_known}, {BlocksToMaturityRole, record.blocks_to_maturity},
            {CanBumpRole, false}, {IsInactiveRole, inactive},
            {ReplacesTxidRole, tx.replaces_txid}, {ReplacedByTxidRole, tx.replaced_by_txid},
            {IsPendingRole, record.status_known && !inactive && record.depth == 0},
            {IsPendingRequestRole, false}, {RequestIdRole, QString{}},
            {WalletDebitSatRole, qint64(tx.wallet_debit)}, {WalletCreditSatRole, qint64(tx.wallet_credit)},
            {FeeKnownRole, tx.fee.has_value()}, {FeeSatRole, tx.fee ? QVariant{qint64(*tx.fee)} : QVariant{}},
            {ActionsRole, actions}, {ActionCountRole, actions.size()},
            {PaymentRequestsRole, requests}, {HasPaymentRequestRole, !requests.isEmpty()},
            {SearchTextRole, search.join(QLatin1Char('\n'))},
        }};

}
std::optional<std::pair<QString, TransactionActivityModel::Row>> TransactionActivityModel::buildRequestRow(
    const QVariantMap& request, int unit, const QSet<QString>& fulfilled)
{
            const QString request_id = request.value("requestId").toString();
            if (fulfilled.contains(request_id)) return std::nullopt;
            const QString id = QStringLiteral("request:") + request_id;
            const QString address = request.value("address").toString();
            const QString label = request.value("noteSelf").toString();
            const auto date = QDateTime::fromString(request.value("dateIso").toString(), Qt::ISODate);
            const qint64 amount = request.value("amountSat").toLongLong();
            return std::pair<QString, Row>{id, Row{
                {IdRole, id}, {TxidRole, QString{}}, {RequestIdRole, request_id},
                {AddressRole, address}, {LabelRole, label},
                {TimestampRole, date.toSecsSinceEpoch()}, {DateTimeRole, date.toLocalTime()},
                {AmountRole, formatAmount(amount, false, unit)}, {NetAmountSatRole, amount},
                {IsPendingRequestRole, true}, {IsPendingRole, true}, {IsInactiveRole, false},
                {TypeRole, int(Transaction::RecvWithAddress)}, {ActivityTypeRole, int(Receive)},
                {DepthRole, 0}, {StatusRole, int(Transaction::Unconfirmed)}, {StatusKnownRole, false},
                {CanBumpRole, false}, {FeeKnownRole, false},
                {BlocksToMaturityRole, 0},
                {ReplacesTxidRole, QString{}}, {ReplacedByTxidRole, QString{}},
                {ActionsRole, QVariantList{}}, {ActionCountRole, 0},
                {PaymentRequestsRole, QVariantList{request}}, {HasPaymentRequestRole, true},
                {SearchTextRole, QStringList{address, label, request.value("label").toString(), request.value("message").toString()}.join(QLatin1Char('\n'))},
            }};
}

void TransactionActivityModel::prepareDetails(activityview::Snapshot& view, QVariantMap details, int unit, const Row& original_row)
{
    const auto bounded = BoundedFlowEntry;
    auto flow = details.value("flow").toMap();
    flow["presentation"] = PrepareTransactionFlowPresentation(flow);
    view.full_inputs = std::make_shared<const QVariantList>(flow.value("inputs").toList());
    view.full_outputs = std::make_shared<const QVariantList>(flow.value("outputs").toList());
    view.full_actions = std::make_shared<const QVariantList>(original_row.value(ActionsRole).toList());
    for (const auto& side : {QStringLiteral("inputs"), QStringLiteral("outputs")}) {
        const auto full = flow.value(side).toList();
        auto values = std::make_shared<QVariantList>();
        values->reserve(full.size());
        for (const auto& entry : full) values->append(bounded(entry.toMap()));
        if (side == QLatin1String("inputs")) view.detail_inputs = values;
        else view.detail_outputs = values;
        QVariantList graph;
        for (int i = 0; i < std::min(15, int(values->size())); ++i) graph.append(values->at(i));
        if (values->size() > 15) {
            bool known{true}; CAmount amount{0};
            for (int i = 15; i < values->size(); ++i) {
                const auto entry = values->at(i).toMap();
                known &= entry.value("amountKnown").toBool();
                if (!known) continue;
                const auto value = entry.value("amountSat").toLongLong();
                if (value < 0 || amount > std::numeric_limits<CAmount>::max() - value) throw std::runtime_error("Activity graph amount is outside the supported range");
                amount += value;
            }
            const bool input = side == QLatin1String("inputs");
            graph.append(QVariantMap{{"id", input ? "remaining-inputs" : "remaining-outputs"},
                {"kind", input ? "input-group" : "output-group"},
                {"label", input ? tr("%1 other inputs").arg(values->size() - 15) : tr("%1 other outputs").arg(values->size() - 15)},
                {"amountKnown", known}, {"amountSat", known ? QVariant{qint64(amount)} : QVariant{}},
                {"amount", known ? formatAmount(amount, false, unit) : QString{}},
                {"ownership", "mixed"}, {"outputCount", values->size() - 15}, {"inputCount", values->size() - 15}});
        }
        flow[side] = graph;
        flow[side + QStringLiteral("ListedCount")] = values->size();
    }
    const auto& actions = *view.full_actions;
    auto action_values = std::make_shared<QVariantList>();
    for (const auto& action : actions) {
        const Row single{{ActionsRole, QVariantList{action}}};
        action_values->append(presentationRow(single).value(ActionsRole).toList().front());
    }
    view.detail_actions = std::move(action_values);
    flow["presentationLimited"] = (view.detail_inputs && view.detail_inputs->size() > 15) || (view.detail_outputs && view.detail_outputs->size() > 15);
    details["flow"] = flow;
    view.selected_details = std::make_shared<const QVariantMap>(std::move(details));
}

TransactionActivityModel::Row TransactionActivityModel::presentationRow(const Row& source)
{
    Row row = source;
    bool limited{false};
    const auto clipped = [&limited](QVariantMap map) {
        bool text_limited{false};
        for (auto field = map.begin(); field != map.end(); ++field) if (field->metaType().id() == QMetaType::QString) {
            const auto text = field->toString();
            if (text.size() > 1024) {
                const QString clipped_text = text.left(1023) + QStringLiteral("…");
                *field = clipped_text; text_limited = true; limited = true;
            }
        }
        if (text_limited) map["textTruncated"] = true;
        return map;
    };
    const auto requests = [&](const QVariantList& values) {
        QVariantList visible;
        if (values.size() > 16) limited = true;
        for (int i = 0; i < std::min(16, int(values.size())); ++i) visible.append(clipped(values[i].toMap()));
        return visible;
    };
    for (const auto role : {AddressRole, LabelRole, SearchTextRole}) {
        const auto text = row.value(role).toString();
        if (text.size() > 1024) {
            const QString clipped_text = text.left(1023) + QStringLiteral("…");
            row[role] = clipped_text; limited = true;
        }
    }
    QVariantList actions;
    const auto full = source.value(ActionsRole).toList();
    if (full.size() > 64) limited = true;
    for (int i = 0; i < std::min(64, int(full.size())); ++i) {
        auto action = clipped(full[i].toMap());
        const auto inputs = action.value("inputs").toList();
        if (inputs.size() > 64) limited = true;
        action["inputCount"] = inputs.size(); action["inputs"] = inputs.mid(0, 64);
        const auto matches = action.value("paymentRequests").toList();
        action["paymentRequestCount"] = matches.size(); action["paymentRequests"] = requests(matches);
        actions.append(action);
    }
    row[ActionsRole] = actions;
    const auto matches = source.value(PaymentRequestsRole).toList();
    row[PaymentRequestCountRole] = matches.size();
    row[PaymentRequestsRole] = requests(matches);
    row[PresentationLimitedRole] = limited;
    return row;
}

QVariantMap TransactionActivityModel::buildDetails(const QString& txid, const Row& row,
    const Snapshot& snapshot, const std::shared_ptr<const walletrequests::Snapshot>& history, int unit, bool full)
{
    // The role names never change; build their strings once (thread-safe static).
    static const std::vector<std::pair<int, QString>> names = [] {
        std::vector<std::pair<int, QString>> result;
        const auto all = roles();
        for (auto it = all.cbegin(); it != all.cend(); ++it) result.emplace_back(it.key(), QString::fromUtf8(it.value()));
        return result;
    }();
    const auto visible = presentationRow(row);
        QVariantMap result;
        for (const auto& [role, name] : names) result.insert(name, visible.value(role));
        const auto record_it = snapshot.records.constFind(txid);
        if (record_it == snapshot.records.cend()) return result;
        const auto& record = record_it.value();
        result.insert("blockHeight", record.status_known && record.depth > 0 ? QVariant{record.block_height} : QVariant{});
        if (!full) return result;
        if (snapshot.detail_txid != txid) return result;
        result.insert("detailsLoading", !snapshot.flow_resolved);
        result.insert("detailsError", snapshot.detail_missing ? tr("This transaction is no longer available.") : QString{});
        result.insert("canBump", snapshot.can_bump && !result.value("isInactive").toBool());
        if (snapshot.flow.isEmpty()) return result;
        QVariantMap flow = snapshot.flow;
        for (const QString& side : {QStringLiteral("inputs"), QStringLiteral("outputs")}) {
            QVariantList entries;
            for (const auto& value : flow.value(side).toList()) {
                QVariantMap entry = value.toMap();
                const QString address = entry.value("address").toString();
                const bool output = side == "outputs";
                const bool mine = entry.value("ownership") == "wallet";
                const auto requests = output && mine && entry.value("amountSat").toLongLong() > 0
                    ? RequestsForAddress(history, address) : QVariantList{};
                entry.insert("paymentRequests", requests);
                entry.insert("label", !requests.isEmpty() ? requests.first().toMap().value("noteSelf").toString()
                    : !output || !mine ? snapshot.detail_labels.value(address) : QString{});
                entry.insert("amount", entry.value("amountKnown").toBool()
                    ? formatAmount(entry.value("amountSat").toLongLong(), false, unit) : QString{});
                entries.append(entry);
            }
            flow.insert(side, entries);
        }
        const bool fee_known = flow.value("feeKnown").toBool();
        const qint64 vsize = flow.value("virtualSize").toLongLong();
        flow.insert("feeAmount", fee_known ? formatAmount(flow.value("feeSat").toLongLong(), false, unit) : QString{});
        result.insert("flow", flow);
        result.insert("rawAvailable", snapshot.flow_resolved && !snapshot.raw_transaction.isEmpty());
        result.insert("virtualSize", vsize);
        result.insert("size", flow.value("size"));
        result.insert("weight", flow.value("weight"));
        result.insert("version", flow.value("version"));
        result.insert("lockTime", flow.value("lockTime"));
        result.insert("feeKnown", fee_known);
        result.insert("feeSat", flow.value("feeSat"));
        result.insert("feeRateSatPerVb", fee_known && vsize > 0
            ? QVariant{double(flow.value("feeSat").toLongLong()) / vsize} : QVariant{});
        result.insert("signalsRbf", flow.value("signalsRbf"));
        return result;
}


QString TransactionActivityModel::formatAmount(CAmount amount, bool receive, int display_unit)
{
    const auto unit = QmlBitcoinUnits::fromDisplayUnit(display_unit);
    if (amount == std::numeric_limits<CAmount>::min()) throw std::runtime_error("Activity amount is outside the supported range");
    return QmlBitcoinUnits::formatForDisplay(unit, qAbs(amount), receive) + QLatin1Char(' ')
        + QmlBitcoinUnits::label(unit, amount);
}
