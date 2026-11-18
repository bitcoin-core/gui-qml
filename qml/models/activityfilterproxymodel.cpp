// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/activityfilterproxymodel.h>
#include <qml/bitcoinunits.h>
#include <qml/guipresentation.h>
#include <qml/models/transaction.h>
#include <qml/models/transactionactivitymodel.h>
#include <qml/walletopening.h>

#include <QClipboard>
#include <QCoreApplication>
#include <QDateTime>
#include <QGuiApplication>
#include <QLocale>
#include <QPointer>
#include <QSaveFile>
#include <QStringList>
#include <QTextStream>
#include <QUrl>

#include <algorithm>
#include <atomic>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace {
using A = TransactionActivityModel;
using Row = activityview::Row;
CAmount Magnitude(CAmount value) { return value == std::numeric_limits<CAmount>::min() ? std::numeric_limits<CAmount>::max() : value < 0 ? -value : value; }
void WriteCsvValue(QTextStream& stream, QString value) { value.replace('"', "\"\""); stream << '"' << value << '"'; }
QString GuardedCsvText(QString value) {
    if (!value.isEmpty() && QStringLiteral("=+-@\t\r\n").contains(value.at(0))) value.prepend(QLatin1Char('\''));
    return value;
}
void WriteCsvRow(QTextStream& stream, const QStringList& values) {
    for (int i = 0; i < values.size(); ++i) { if (i) stream << ','; WriteCsvValue(stream, values.at(i)); } stream << '\n';
}
QmlBitcoinUnits::Unit ExportDisplayUnit(int unit) { return QmlBitcoinUnits::fromDisplayUnit(unit); }
QString ExportDisplayUnitLabel(int unit) { return QmlBitcoinUnits::label(ExportDisplayUnit(unit)); }
}

struct ActivityFilterProxyModel::Parameters {
    QString search;
    DateFilter date{DateAll};
    QList<int> types;
    int unit{0};
    CAmount minimum{-1}, maximum{-1};
    QDate today, start, end;
    GroupBy group{Month};
    QLocale locale;
    QTimeZone timezone;
    bool operator==(const Parameters&) const = default;
};
struct ActivityFilterProxyModel::Owner {
    enum class ProjectionStep {
        PrepareRows,
        RemoveMissingRows,
        PrepareRowPatches,
        Complete,
    };
    std::shared_ptr<const activityview::Hold> input, previous, previous_input;
    std::shared_ptr<activityview::Snapshot> result;
    activityview::Tree tree;
    std::vector<activityview::Patch> patches;
    activityview::Map<activityview::SharedRow>::const_iterator next, old, fresh;
    Parameters parameters;
    CAmount available{0}, pending{0};
    QString pending_text, error;
    ProjectionStep step{ProjectionStep::PrepareRows};
    size_t changed_position{0};
    WorkReport work;
    void clear() { input.reset(); previous.reset(); previous_input.reset(); result.reset(); tree.reset(); patches.clear(); parameters = {}; next = old = fresh = {}; }
};
struct ActivityFilterProxyModel::Mailbox {
    std::mutex mutex;
    bool closed{false};
    std::shared_ptr<Job> ready;
    std::function<void()> notify;
    void publish(const std::shared_ptr<Job>& job) {
        { std::lock_guard lock{mutex}; if (closed) return; ready = job; }
        try { notify(); } catch (...) { /* The mailbox retains the actual outcome until the next presentation turn or close. */ }
    }
};
struct ActivityFilterProxyModel::Job {
    std::shared_ptr<Owner> owner;
    std::shared_ptr<execution::ResourceLease> lease;
    std::shared_ptr<execution::Scheduler> scheduler;
    std::shared_ptr<Mailbox> mailbox;
    std::shared_ptr<std::atomic_bool> cancelled{std::make_shared<std::atomic_bool>(false)};
    quint64 generation{0}, settings{0};
    ~Job() { owner.reset(); lease.reset(); }
};

ActivityFilterProxyModel::ActivityFilterProxyModel(QObject* parent) : ActivityFilterProxyModel(Services{}, parent) {}
ActivityFilterProxyModel::ActivityFilterProxyModel(Services services, QObject* parent)
    : QAbstractProxyModel(parent), m_services(std::move(services)), m_mailbox(std::make_shared<Mailbox>())
{
    if (!m_services.execution.post) m_services.execution.post = presentation::QueuedPost();
    if (!m_services.today) m_services.today = [] { return QDate::currentDate(); };
    if (!m_services.write) m_services.write = WriteCsv;
    m_date_observed = m_services.today();
    if (!m_services.locale) m_services.locale = [] { return QLocale{}; };
    if (!m_services.timezone) m_services.timezone = QTimeZone::systemTimeZone;
    const QPointer<ActivityFilterProxyModel> observer{this};
    m_mailbox->notify = [post = m_services.execution.post, observer] { post([observer] { if (observer) observer->receive(); }); };
    m_start_timer.setSingleShot(true); m_apply_timer.setSingleShot(true);
    connect(&m_start_timer, &QTimer::timeout, this, &ActivityFilterProxyModel::start);
    connect(&m_apply_timer, &QTimer::timeout, this, &ActivityFilterProxyModel::apply);
    connect(&m_date_timer, &QTimer::timeout, this, &ActivityFilterProxyModel::refreshDate);
    // A clock wakeup only compares bounded dates and requests Domain work.
    // No history scan, transformation, or blocking native call occurs here.
    m_date_timer.setInterval(1000);
}
ActivityFilterProxyModel::~ActivityFilterProxyModel() { close(); finishClose(); }
void ActivityFilterProxyModel::close()
{
    if (std::exchange(m_closed, true)) return;
    m_start_timer.stop(); m_apply_timer.stop(); m_date_timer.stop(); m_scheduled = false;
    for (const auto& connection : m_source_connections) disconnect(connection);
    m_source_connections.clear();
    if (m_job) m_job->cancelled->store(true);
    { std::lock_guard lock{m_mailbox->mutex}; m_mailbox->closed = true; m_mailbox->ready.reset(); }
    if (!m_in_update) finishClose();
}
void ActivityFilterProxyModel::finishClose()
{
    m_root.reset(); m_current.reset(); m_input.reset(); m_applied_input.reset(); m_context.reset(); m_context_txid.clear(); m_job.reset();
    // OwnedCommand retains accepted file effects and their actual outcomes.
    m_chooser_snapshot.reset(); m_chooser_source.clear(); m_chooser_token.clear();
    if (m_export_operation) m_export_operation->retire();
}
int ActivityFilterProxyModel::rowCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : activityview::Size(m_root); }
QModelIndex ActivityFilterProxyModel::index(int row, int column, const QModelIndex& parent) const {
    return !parent.isValid() && column == 0 && row >= 0 && row < rowCount() ? createIndex(row, column) : QModelIndex{};
}
QVariant ActivityFilterProxyModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.model() != this || index.column()) return {};
    const auto* value = activityview::At(m_root, index.row()); return value ? value->row->value(role) : QVariant{};
}
QHash<int, QByteArray> ActivityFilterProxyModel::roleNames() const { return m_roles; }
QModelIndex ActivityFilterProxyModel::mapToSource(const QModelIndex& proxy) const
{
    const auto* provider = qobject_cast<activityview::Source*>(sourceModel());
    if (!provider || proxy.model() != this || !proxy.isValid()) return {};
    const QString id = data(proxy, A::IdRole).toString();
    const int row = provider->activityRowForId(id);
    return row >= 0 ? sourceModel()->index(row, 0) : QModelIndex{};
}
QModelIndex ActivityFilterProxyModel::mapFromSource(const QModelIndex& source) const
{
    if (!source.isValid() || source.model() != sourceModel() || !m_current) return {};
    const auto found = m_current->snapshot->order_keys.find(source.data(A::IdRole).toString());
    if (found == m_current->snapshot->order_keys.end()) return {};
    const int row = activityview::Rank(m_root, found->second);
    const auto* value = activityview::At(m_root, row);
    return value && value->id == found->second ? index(row) : QModelIndex{};
}
void ActivityFilterProxyModel::setSourceModel(QAbstractItemModel* model)
{
    if (m_closed) return;
    if (m_source_intent == std::numeric_limits<quint64>::max()) { close(); return; }
    const auto source_intent = ++m_source_intent;
    const bool target_requested = model != nullptr;
    const QPointer<QAbstractItemModel> target{model};
    if (m_in_update) {
        m_deferred_source = model;
        if (!std::exchange(m_source_deferred, true)) {
            const QPointer<ActivityFilterProxyModel> observer{this};
            QTimer::singleShot(0, this, [observer] { if (!observer || observer->m_closed) return; observer->m_source_deferred = false; observer->setSourceModel(observer->m_deferred_source); });
        }
        return;
    }
    if (sourceModel() == model) return;
    const QPointer<ActivityFilterProxyModel> alive{this}; cancelExport();
    if (!alive || m_closed || m_source_intent != source_intent || (target_requested && !target)) return;
    for (const auto& connection : m_source_connections) disconnect(connection);
    m_source_connections.clear();
    ++m_generation; if (m_job) m_job->cancelled->store(true);
    m_apply_timer.stop(); m_job.reset();
    const QPointer<ActivityFilterProxyModel> observer{this};
    m_in_update = true; beginResetModel(); if (!observer) return;
    m_root.reset(); m_current.reset(); m_input.reset(); m_applied_input.reset(); m_context.reset(); m_context_txid.clear();
    m_transactions = m_requests = 0; m_pending_total = m_available_max_amount = 0; m_pending_text.clear();
    QAbstractProxyModel::setSourceModel(target);
    m_roles = target ? target->roleNames() : QHash<int, QByteArray>{};
    m_roles.insert(SectionKeyRole, "sectionKey"); m_roles.insert(SectionLabelRole, "sectionLabel"); m_roles.insert(DateTimeLabelRole, "dateTimeLabel");
    endResetModel(); if (!observer) return; m_in_update = false;
    if (m_closed) { finishClose(); return; }
    if (auto* provider = qobject_cast<activityview::Source*>(target.data())) {
        m_source_connections = provider->observeActivity(this, [this] { sourceChanged(); });
        m_source_connections << connect(target, &QObject::destroyed, this, [this] { sourceChanged(); });
    }
    sourceChanged(); if (!observer || m_closed) return;
    Q_EMIT countChanged(); if (!observer || m_closed) return;
    Q_EMIT availableMaxAmountChanged(); if (!observer || m_closed) return;
    Q_EMIT pendingBalanceChanged();
}
void ActivityFilterProxyModel::sourceChanged()
{
    if (m_closed) return;
    const QPointer<ActivityFilterProxyModel> alive{this};
    if (exportChoosing() && (!m_chooser_source || m_chooser_source != sourceModel())) cancelExport();
    if (!alive || m_closed) return;
    auto* provider = qobject_cast<activityview::Source*>(sourceModel());
    if (!provider && (m_current || m_job || m_input)) {
        if (m_in_update) {
            const QPointer<ActivityFilterProxyModel> observer{this}; QTimer::singleShot(0, this, [observer] { if (observer) observer->sourceChanged(); }); return;
        }
        if (m_job) m_job->cancelled->store(true);
        m_apply_timer.stop(); m_start_timer.stop(); m_scheduled = false;
        const QPointer<ActivityFilterProxyModel> observer{this};
        m_in_update = true; beginResetModel(); if (!observer) return;
        m_root.reset(); m_current.reset(); m_input.reset(); m_applied_input.reset(); m_context.reset(); m_context_txid.clear(); m_job.reset();
        m_transactions = m_requests = 0; m_pending_total = m_available_max_amount = 0; m_pending_text.clear();
        endResetModel(); if (!observer) return; m_in_update = false;
        if (m_closed) { finishClose(); return; }
        Q_EMIT countChanged(); if (!observer || m_closed) return;
        Q_EMIT availableMaxAmountChanged(); if (!observer || m_closed) return;
        Q_EMIT pendingBalanceChanged(); if (!observer || m_closed) return;
        m_source_loading = false; m_source_error = sourceModel() ? tr("Activity cannot be exported from this view.") : QString{};
        Q_EMIT stateChanged(); return;
    }
    m_source_loading = provider && provider->activityLoading();
    m_source_error = provider ? provider->activityError() : sourceModel() ? tr("Activity cannot be exported from this view.") : QString{};
    auto next = provider ? provider->preparedSnapshot() : nullptr;
    const bool same = next && m_input && next->snapshot == m_input->snapshot;
    m_input = std::move(next); // Also refresh the cleanup lease when the immutable value was reused.
    if (provider) { m_execution = m_services.execution; if (!m_execution.scheduler) m_execution.scheduler = provider->activityServices().scheduler; }
    if (!same || m_settings_generation != m_applied_settings) reload();
    Q_EMIT stateChanged();
}
void ActivityFilterProxyModel::reload()
{
    if (m_closed) return;
    ++m_generation; m_scheduled = true;
    if (!m_job) m_start_timer.start(0);
    const QPointer<ActivityFilterProxyModel> observer{this};
    m_services.execution.post([observer] { if (observer && !observer->m_closed) Q_EMIT observer->stateChanged(); });
}
void ActivityFilterProxyModel::start()
{
    if (m_closed || m_job || !m_scheduled) return;
    m_scheduled = false;
    if (!m_input || !m_execution.scheduler) { Q_EMIT stateChanged(); return; }
    auto owner = std::make_shared<Owner>();
    auto* provider = qobject_cast<activityview::Source*>(sourceModel());
    const auto opening = provider ? provider->opening() : nullptr;
    auto held = opening ? opening->holdValues([owner] { owner->clear(); }) : activityview::HoldValues(m_execution.scheduler, [owner] { owner->clear(); });
    if (!held) { m_error = tr("Activity could not be filtered. Try again."); Q_EMIT stateChanged(); return; }
    auto job = std::make_shared<Job>(); job->owner = owner; job->lease = std::make_shared<execution::ResourceLease>(std::move(*held.lease));
    job->scheduler = m_execution.scheduler; job->mailbox = m_mailbox; job->generation = m_generation; job->settings = m_settings_generation;
    owner->input = m_input; owner->previous = m_current; owner->previous_input = m_applied_input;
    owner->parameters = {m_search_text, m_date_filter, m_type_filters, m_display_unit, m_min_amount, m_max_amount, m_services.today(), m_range_start, m_range_end, m_group_by, {}, {}};
    // Locale/timezone lookup and address normalization are Domain work.
    const auto locale = m_services.locale; const auto timezone = m_services.timezone;
    m_job = job;
    const auto admitted = execution::Submit(*job->scheduler, [job, locale, timezone] { job->owner->parameters.locale = locale(); job->owner->parameters.timezone = timezone(); step(job); }, [] {},
        [job](std::exception_ptr) { job->owner->error = tr("Activity filtering failed."); job->mailbox->publish(job); },
        {execution::Lane::Domain, {}, execution::WorkClass::Observation});
    if (!admitted) { owner->error = tr("Activity could not be filtered. Try again."); m_mailbox->publish(job); }
}
void ActivityFilterProxyModel::patchRow(Owner& owner, const QString& id, activityview::SharedRow row)
{
    const auto found = owner.result->rows.find(id);
    const bool existing = found != owner.result->rows.end();
    const auto previous = existing ? found->second : activityview::SharedRow{};
    const auto old_key = existing ? owner.result->order_keys.at(id) : QString{};
    const auto key = row ? orderKey(*row) : QString{};
    const bool old_request = previous && previous->value(A::IsPendingRequestRole).toBool();
    const bool request = row && row->value(A::IsPendingRequestRole).toBool();
    const int tx_delta = int(bool(row) && !request) - int(existing && !old_request);
    const int request_delta = int(request) - int(existing && old_request);
    if (!row) {
        if (!existing) return;
        const int position = activityview::Rank(owner.tree, old_key);
        owner.tree = activityview::Erase(owner.tree, old_key);
        owner.result->rows.erase(id); owner.result->order_keys.erase(id);
        owner.patches.push_back({activityview::Patch::Kind::Remove, position, owner.tree, {}, tx_delta, request_delta});
        return;
    }
    if (existing && (*previous == *row)) return;
    owner.result->rows.insert_or_assign(id, row); owner.result->order_keys.insert_or_assign(id, key);
    const auto amount = row->value(A::NetAmountSatRole).toLongLong();
    const activityview::Measure measure{request ? 0 : 1, request ? 1 : 0,
        !request && row->value(A::IsPendingRole).toBool() ? amount : 0, Magnitude(amount)};
    const auto preview = std::make_shared<const Row>(A::presentationRow(*row));
    if (!existing) {
        const int position = activityview::Rank(owner.tree, key);
        owner.tree = activityview::Insert(owner.tree, key, preview, measure);
        owner.patches.push_back({activityview::Patch::Kind::Insert, position, owner.tree, {}, tx_delta, request_delta});
    } else if (key != old_key) {
        const int position = activityview::Rank(owner.tree, old_key);
        auto without = activityview::Erase(owner.tree, old_key);
        const int destination = activityview::Rank(without, key);
        owner.tree = activityview::Insert(without, key, preview, measure);
        owner.patches.push_back({activityview::Patch::Kind::Move, position, owner.tree, {}, tx_delta, request_delta, destination});
    } else {
        QList<int> roles;
        for (auto value = row->cbegin(); value != row->cend(); ++value) if (previous->value(value.key()) != value.value()) roles.append(value.key());
        owner.tree = activityview::Insert(owner.tree, key, preview, measure);
        owner.patches.push_back({activityview::Patch::Kind::Change, activityview::Rank(owner.tree, key), owner.tree, roles, tx_delta, request_delta});
    }
}
void ActivityFilterProxyModel::step(const std::shared_ptr<Job>& job)
{
    if (job->cancelled->load()) return;
    using Step = Owner::ProjectionStep;
    auto& owner = *job->owner;
    if (!owner.result) {
        if (owner.parameters.date == DateAll || owner.parameters.date == CustomRange) owner.parameters.today = {};
        owner.parameters.search = owner.parameters.search.trimmed();
        // Grouped display addresses are canonicalized only after validation.
        // Ordinary notes retain their internal whitespace semantics.
        if (owner.input->snapshot->canonical_address) owner.parameters.search = owner.input->snapshot->canonical_address(owner.parameters.search);
        owner.result = std::make_shared<activityview::Snapshot>(); owner.result->revision = job->generation;
        owner.result->canonical_address = owner.input->snapshot->canonical_address;
        owner.result->opening = owner.input->snapshot->opening; owner.result->raw = owner.input->snapshot->raw;
        owner.result->presentation_context = std::make_shared<const Parameters>(owner.parameters);
        owner.tree = owner.previous ? owner.previous->snapshot->presentation : activityview::Tree{};
        const auto policy = owner.previous && owner.previous->snapshot->presentation_context
            ? std::static_pointer_cast<const Parameters>(owner.previous->snapshot->presentation_context) : nullptr;
        owner.work.delta = policy && *policy == owner.parameters && owner.previous_input &&
            owner.input->snapshot->delta_base.lock() == owner.previous_input->snapshot && owner.input->snapshot->statistics_valid;
        if (owner.work.delta) {
            owner.result->rows = owner.previous->snapshot->rows;
            owner.result->order_keys = owner.previous->snapshot->order_keys;
        }
        owner.next = owner.input->snapshot->rows.begin();
    }
    constexpr size_t TURN_BUDGET{128};
    size_t used{0}; ++owner.work.turns;
    while (used < TURN_BUDGET) {
        if (owner.step == Step::PrepareRows) {
            if (owner.work.delta) {
                if (owner.changed_position == owner.input->snapshot->changed_ids.size()) {
                    owner.step = Step::Complete;
                    continue;
                }
                const auto& id = owner.input->snapshot->changed_ids[owner.changed_position++];
                const auto found = owner.input->snapshot->rows.find(id);
                ++used; ++owner.work.examined;
                activityview::SharedRow prepared;
                if (found != owner.input->snapshot->rows.end() && matches(*found->second, owner.parameters)) {
                    prepared = std::make_shared<const Row>(present(*found->second, owner.parameters)); ++owner.work.prepared;
                }
                patchRow(owner, id, std::move(prepared));
            } else {
                if (owner.next == owner.input->snapshot->rows.end()) {
                    owner.step = Step::RemoveMissingRows;
                    if (owner.previous) owner.old = owner.previous->snapshot->rows.begin();
                    continue;
                }
                const auto& [id, row] = *owner.next++; ++used; ++owner.work.examined;
                owner.available = std::max(owner.available, Magnitude(row->value(A::NetAmountSatRole).toLongLong()));
                if (!matches(*row, owner.parameters)) continue;
                owner.result->rows.emplace(id, std::make_shared<const Row>(present(*row, owner.parameters))); ++owner.work.prepared;
            }
        } else if (owner.step == Step::RemoveMissingRows) {
            if (!owner.previous || owner.old == owner.previous->snapshot->rows.end()) {
                owner.step = Step::PrepareRowPatches;
                owner.fresh = owner.result->rows.begin();
                continue;
            }
            const auto& [id, row] = *owner.old++; ++used;
            if (owner.result->rows.contains(id)) continue;
            const auto key = owner.previous->snapshot->order_keys.at(id);
            const auto position = activityview::Rank(owner.tree, key); owner.tree = activityview::Erase(owner.tree, key);
            const bool request = row->value(A::IsPendingRequestRole).toBool();
            owner.patches.push_back({activityview::Patch::Kind::Remove, position, owner.tree, {}, request ? 0 : -1, request ? -1 : 0});
        } else if (owner.step == Step::PrepareRowPatches) {
            if (owner.fresh == owner.result->rows.end()) {
                owner.step = Step::Complete;
                continue;
            }
            const auto& [id, row] = *owner.fresh++; ++used;
            // The fresh map is an immutable traversal root; patchRow mutates a
            // separate persistent root without invalidating this cursor.
            const auto old = owner.previous ? owner.previous->snapshot->rows.find(id) : owner.result->rows.end();
            const bool existing = owner.previous && old != owner.previous->snapshot->rows.end();
            owner.result->rows.erase(id);
            if (existing) {
                owner.result->rows.emplace(id, old->second);
                owner.result->order_keys.emplace(id, owner.previous->snapshot->order_keys.at(id));
            }
            patchRow(owner, id, row);
            // Unchanged full rows also need their original ordering entry.
            if (!owner.result->order_keys.contains(id)) owner.result->order_keys.emplace(id, orderKey(*row));
        } else {
            owner.result->presentation = owner.tree;
            const auto total = owner.tree ? owner.tree->total : activityview::Measure{};
            owner.result->transactions = total.transactions; owner.result->requests = total.requests; owner.pending = total.pending;
            if (owner.input->snapshot->statistics_valid) owner.available = owner.input->snapshot->presentation ? owner.input->snapshot->presentation->total.maximum : 0;
            const auto unit = ExportDisplayUnit(owner.parameters.unit);
            owner.pending_text = QmlBitcoinUnits::formatForDisplay(unit, owner.pending, true) + QLatin1Char(' ') + QmlBitcoinUnits::displayLabel(unit, owner.pending);
            owner.work.largestTurn = std::max(owner.work.largestTurn, used); owner.work.patches = owner.patches.size();
            // A committed frame must not retain the preceding frame's cleanup
            // lease and form an ever-growing history of owners.
            owner.previous.reset(); owner.previous_input.reset(); owner.next = owner.old = owner.fresh = {};
            job->mailbox->publish(job); return;
        }
    }
    owner.work.largestTurn = std::max(owner.work.largestTurn, used);
    const auto admitted = execution::Submit(*job->scheduler, [job] { step(job); }, [] {},
        [job](std::exception_ptr) { job->owner->error = tr("Activity filtering could not continue."); job->mailbox->publish(job); },
        {execution::Lane::Domain, {}, execution::WorkClass::Observation});
    if (!admitted) { owner.error = tr("Activity filtering could not continue."); job->mailbox->publish(job); }
}
void ActivityFilterProxyModel::receive()
{
    if (m_closed) return;
    std::shared_ptr<Job> ready;
    { std::lock_guard lock{m_mailbox->mutex}; ready = std::exchange(m_mailbox->ready, {}); }
    if (!ready || ready != m_job) return;
    if (ready->owner->parameters.date >= Today && ready->owner->parameters.date <= ThisYear && ready->owner->parameters.today != m_services.today()) refreshDate();
    if (ready->generation != m_generation) { m_job.reset(); if (m_scheduled) m_start_timer.start(0); return; }
    if (!ready->owner->error.isEmpty()) { m_error = ready->owner->error; m_job.reset(); Q_EMIT stateChanged(); return; }
    m_patch_position = 0; apply();
}
void ActivityFilterProxyModel::apply()
{
    if (m_closed || !m_job) return;
    const auto job = m_job; const auto& patches = job->owner->patches;
    const QPointer<ActivityFilterProxyModel> observer{this};
    int budget{16};
    while (budget-- > 0 && m_patch_position < patches.size()) {
        const auto& patch = patches[m_patch_position++]; m_in_update = true;
        if (patch.kind == activityview::Patch::Kind::Insert) { beginInsertRows({}, patch.position, patch.position); if (!observer) return; m_root = patch.root; m_transactions += patch.transaction_delta; m_requests += patch.request_delta; endInsertRows(); }
        else if (patch.kind == activityview::Patch::Kind::Remove) { beginRemoveRows({}, patch.position, patch.position); if (!observer) return; m_root = patch.root; m_transactions += patch.transaction_delta; m_requests += patch.request_delta; endRemoveRows(); }
        else if (patch.kind == activityview::Patch::Kind::Move) {
            m_transactions += patch.transaction_delta; m_requests += patch.request_delta;
            if (patch.position != patch.destination) { beginMoveRows({}, patch.position, patch.position, {}, patch.destination > patch.position ? patch.destination + 1 : patch.destination); if (!observer) return; m_root = patch.root; endMoveRows(); }
            else m_root = patch.root;
            Q_EMIT dataChanged(index(patch.destination), index(patch.destination), {});
        } else { m_root = patch.root; m_transactions += patch.transaction_delta; m_requests += patch.request_delta; Q_EMIT dataChanged(index(patch.position), index(patch.position), patch.roles); }
        if (!observer) return;
        m_in_update = false;
        if (m_closed) { finishClose(); return; }
    }
    if (m_patch_position < patches.size()) { m_apply_timer.start(0); return; }
    const auto& owner = *job->owner;
    m_applied_input = owner.input; m_last_work = owner.work;
    auto hold = std::make_shared<activityview::Hold>(); hold->snapshot = owner.result; hold->lease = job->lease; m_current = std::move(hold);
    const bool maximum_changed = m_available_max_amount != owner.available;
    const bool pending_changed = m_pending_total != owner.pending || m_pending_text != owner.pending_text;
    m_available_max_amount = owner.available; m_pending_total = owner.pending; m_pending_text = owner.pending_text;
    m_applied_settings = job->settings; m_error.clear(); m_job.reset();
    // Drop transient persistent roots on Native after the last GUI patch. If
    // ordinary work is closed, the already-reserved owner retirement remains
    // the fallback; no last large vector is cleared on GUI. Retain the job's
    // cleanup lease until this task finishes: a source switch may drop the
    // committed GUI hold while transient disposal is still queued or running.
    if (auto* provider = qobject_cast<activityview::Source*>(sourceModel()); provider && provider->opening()) {
        provider->opening()->retireValues([job] { job->owner->patches.clear(); });
    } else {
        execution::Submit(*job->scheduler, [job] { job->owner->patches.clear(); }, [] {}, {},
            {execution::Lane::Domain, {}, execution::WorkClass::Observation});
    }
    Q_EMIT countChanged(); if (!observer || m_closed) return;
    if (maximum_changed) Q_EMIT availableMaxAmountChanged();
    if (!observer || m_closed) return;
    if (pending_changed) Q_EMIT pendingBalanceChanged();
    if (!observer || m_closed) return;
    Q_EMIT stateChanged(); if (!observer || m_closed) return;
    if (m_scheduled) m_start_timer.start(0);
}

QString ActivityFilterProxyModel::searchText() const { return m_search_text; }
void ActivityFilterProxyModel::setSearchText(const QString& value) { if (value == m_search_text) return; m_search_text = value; ++m_settings_generation; reload(); Q_EMIT searchTextChanged(); }
ActivityFilterProxyModel::DateFilter ActivityFilterProxyModel::dateFilter() const { return m_date_filter; }
void ActivityFilterProxyModel::setDateFilter(DateFilter value) { if (value < DateAll || value > CustomRange) return; if (value == m_date_filter) { refreshDate(); return; } m_date_filter = value;
    if (m_services.automaticDateRefresh && value >= Today && value <= ThisYear) m_date_timer.start(); else m_date_timer.stop();
    ++m_settings_generation; reload(); Q_EMIT dateFilterChanged(); }
void ActivityFilterProxyModel::refreshDate()
{
    if (m_closed) return;
    const auto today = m_services.today(); if (today == m_date_observed) return;
    m_date_observed = today;
    if (m_date_filter >= Today && m_date_filter <= ThisYear) { ++m_settings_generation; reload(); }
}
void ActivityFilterProxyModel::clearDateFilter() { setDateFilter(DateAll); }
ActivityFilterProxyModel::TypeFilter ActivityFilterProxyModel::typeFilter() const { return m_type_filters.isEmpty() ? TypeAll : TypeFilter(m_type_filters.first()); }
void ActivityFilterProxyModel::setTypeFilter(TypeFilter value) { setTypeFilters(value == TypeAll ? QList<int>{} : QList<int>{value}); }
void ActivityFilterProxyModel::setTypeFilters(const QList<int>& values) {
    QList<int> normalized; for (int type = Received; type <= Split; ++type) if (values.contains(type)) normalized.append(type);
    if (normalized == m_type_filters) return;
    m_type_filters = normalized; ++m_settings_generation; reload(); const QPointer<ActivityFilterProxyModel> observer{this}; Q_EMIT typeFilterChanged(); if (!observer || m_closed) return; Q_EMIT typeFiltersChanged();
}
int ActivityFilterProxyModel::displayUnit() const { return m_display_unit; }
void ActivityFilterProxyModel::setDisplayUnit(int value) { if (value == m_display_unit) return; m_display_unit = value; ++m_settings_generation; reload(); Q_EMIT displayUnitChanged(); }
CAmount ActivityFilterProxyModel::minAmount() const { return m_min_amount; }
void ActivityFilterProxyModel::setMinAmount(CAmount value) { setAmountRange(value, m_max_amount); }
void ActivityFilterProxyModel::setMaxAmount(CAmount value) { setAmountRange(m_min_amount, value); }
bool ActivityFilterProxyModel::setAmountRange(qint64 minimum, qint64 maximum) {
    minimum = minimum < 0 ? -1 : minimum; maximum = maximum < 0 ? -1 : maximum;
    if (minimum >= 0 && maximum >= 0 && minimum > maximum) return false;
    const bool low = minimum != m_min_amount, high = maximum != m_max_amount; if (!low && !high) return true;
    m_min_amount = minimum; m_max_amount = maximum; ++m_settings_generation; reload(); const QPointer<ActivityFilterProxyModel> observer{this}; if (low) Q_EMIT minAmountChanged(); if (!observer || m_closed) return true; if (high) Q_EMIT maxAmountChanged(); return true;
}
QDate ActivityFilterProxyModel::rangeStart() const { return m_range_start; }
QDate ActivityFilterProxyModel::rangeEnd() const { return m_range_end; }
bool ActivityFilterProxyModel::applyCustomRange(const QString& start_iso, const QString& end_iso) {
    if (start_iso.size() != 10 || end_iso.size() != 10) return false;
    const auto start = QDate::fromString(start_iso, Qt::ISODate), end = QDate::fromString(end_iso, Qt::ISODate);
    if (!start.isValid() || !end.isValid() || start > end) return false;
    const bool range = m_range_start != start || m_range_end != end, filter = m_date_filter != CustomRange; if (!range && !filter) return true;
    m_range_start = start; m_range_end = end; m_date_filter = CustomRange; m_date_timer.stop(); ++m_settings_generation; reload(); const QPointer<ActivityFilterProxyModel> observer{this}; if (range) Q_EMIT rangeChanged(); if (!observer || m_closed) return true; if (filter) Q_EMIT dateFilterChanged(); return true;
}
int ActivityFilterProxyModel::count() const { return rowCount(); }
int ActivityFilterProxyModel::transactionCount() const { return m_transactions; }
int ActivityFilterProxyModel::requestCount() const { return m_requests; }
CAmount ActivityFilterProxyModel::pendingBalanceSat() const { return m_pending_total; }
QString ActivityFilterProxyModel::pendingBalance() const { return m_pending_text; }
void ActivityFilterProxyModel::setGroupBy(GroupBy value) { if ((value != Month && value != Day) || value == m_group_by) return; m_group_by = value; ++m_settings_generation; reload(); Q_EMIT groupByChanged(); }

bool ActivityFilterProxyModel::typeMatches(const Row& row, TypeFilter filter)
{
    if (filter == TypeAll) return true;
    if (filter == PaymentRequest) return row.value(A::HasPaymentRequestRole).toBool();
    if (row.value(A::IsPendingRequestRole).toBool()) return false;
    const auto type = A::ActivityType(row.value(A::ActivityTypeRole).toInt());
    switch (filter) {
    case Multiple: return type == A::Multiple;
    case Consolidation: return type == A::Consolidation;
    case Split: return type == A::Split;
    case SentToSelf: return type == A::InternalTransfer || type == A::Consolidation || type == A::Split;
    case Mined: return type == A::Mined;
    case Other: return type == A::Other;
    case Received: case Sent:
        if (type == A::Mined) return false;
        for (const auto& value : row.value(A::ActionsRole).toList()) if (value.toMap().value("direction").toInt() == (filter == Received ? A::ReceiveAction : A::SendAction)) return true;
        return false;
    default: return false;
    }
}
bool ActivityFilterProxyModel::matches(const Row& row, const Parameters& parameters)
{
    if (!dateMatches(row.value(A::TimestampRole).toLongLong(), parameters)) return false;
    if (!parameters.types.isEmpty() && !std::any_of(parameters.types.cbegin(), parameters.types.cend(), [&](int type) { return typeMatches(row, TypeFilter(type)); })) return false;
    const auto amount = Magnitude(row.value(A::NetAmountSatRole).toLongLong());
    if ((parameters.minimum >= 0 && amount < parameters.minimum) || (parameters.maximum >= 0 && amount > parameters.maximum)) return false;
    if (parameters.search.isEmpty()) return true;
    for (const auto role : {A::AddressRole, A::LabelRole, A::TxidRole, A::SearchTextRole}) if (row.value(role).toString().contains(parameters.search, Qt::CaseInsensitive)) return true;
    return false;
}
bool ActivityFilterProxyModel::dateMatches(qint64 timestamp, const Parameters& parameters)
{
    if (parameters.date == DateAll) return true;
    if (timestamp <= 0) return false;
    QDate start, end;
    switch (parameters.date) {
    case Today: start = end = parameters.today; break;
    case ThisWeek: start = parameters.today.addDays(-((parameters.today.dayOfWeek() - int(parameters.locale.firstDayOfWeek()) + 7) % 7)); end = start.addDays(6); break;
    case ThisMonth: start = QDate(parameters.today.year(), parameters.today.month(), 1); end = start.addMonths(1).addDays(-1); break;
    case ThisYear: start = QDate(parameters.today.year(), 1, 1); end = start.addYears(1).addDays(-1); break;
    case CustomRange: start = parameters.start; end = parameters.end; break;
    case DateAll: return true;
    }
    const auto date = QDateTime::fromSecsSinceEpoch(timestamp, parameters.timezone).date();
    return (!start.isValid() || date >= start) && (!end.isValid() || date <= end);
}
QString ActivityFilterProxyModel::orderKey(const Row& row)
{
    const auto timestamp = quint64(row.value(A::TimestampRole).toLongLong()) ^ (quint64(1) << 63);
    QString key = row.value(A::IsPendingRole).toBool() ? QStringLiteral("0") : QStringLiteral("1");
    key += QString::number(std::numeric_limits<quint64>::max() - timestamp).rightJustified(20, QLatin1Char('0'));
    for (const auto ch : row.value(A::IdRole).toString()) key += QChar(ushort(0xffffU - ch.unicode()));
    key += QChar(ushort(0xffff)); return key;
}
Row ActivityFilterProxyModel::present(const Row& original, const Parameters& parameters)
{
    Row row = original;
    const bool pending = row.value(A::IsPendingRole).toBool();
    const auto date = QDateTime::fromSecsSinceEpoch(row.value(A::TimestampRole).toLongLong(), parameters.timezone);
    const auto time = parameters.locale.toString(date.time(), QLocale::ShortFormat);
    row[DateTimeLabelRole] = parameters.group == Day && !pending ? time
        : parameters.locale.toString(date.date(), QStringLiteral("MMM d")) + QStringLiteral(", ") + time;
    row[SectionKeyRole] = pending ? QStringLiteral("pending") : date.toString(parameters.group == Day ? "yyyy-MM-dd" : "yyyy-MM");
    row[SectionLabelRole] = pending ? tr("Pending") : parameters.locale.toString(date.date(), parameters.group == Day ? "MMMM d, yyyy" : "MMMM yyyy");
    return row;
}
QString ActivityFilterProxyModel::typeLabel(const Row& row)
{
    if (row.value(A::IsPendingRequestRole).toBool()) return tr("Payment request");
    switch (A::ActivityType(row.value(A::ActivityTypeRole).toInt())) {
    case A::Send: return tr("Sent"); case A::Receive: return tr("Received"); case A::Multiple: return tr("Multiple actions");
    case A::Consolidation: return tr("Consolidation"); case A::Split: return tr("Split"); case A::InternalTransfer: return tr("Sent to yourself");
    case A::Mined: return tr("Mined"); case A::Other: return tr("Other");
    }
    return tr("Other");
}

ActivityFilterProxyModel::CsvResult ActivityFilterProxyModel::WriteCsv(const CsvRequest& request)
{
    const QUrl url{request.path};
    const QString filename = url.isLocalFile() ? url.toLocalFile() : request.path;
    if (filename.isEmpty() || !request.snapshot || !request.snapshot->snapshot) return {false, filename, tr("There is no activity to export yet.")};
    auto destination = request.destination ? request.destination(filename) : std::make_unique<QSaveFile>(filename);
    if (!destination) return {false, filename, tr("The Activity export destination is unavailable.")};
    auto& file = *destination;
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return {false, filename, file.errorString()};
    QTextStream stream(&file);
    QStringList header{
        tr("Confirmed"),
        tr("Date"),
        tr("Type"),
        tr("Label"),
        tr("Address"),
        tr("Amount") + QStringLiteral(" (%1)").arg(ExportDisplayUnitLabel(request.unit)),
        tr("ID"),
    };
    header << tr("Record") << tr("Action ID") << tr("Status")
                        << tr("Action amount") + QStringLiteral(" (%1)").arg(ExportDisplayUnitLabel(request.unit));
    WriteCsvRow(stream, header);

    for (int position = 0; position < activityview::Size(request.snapshot->snapshot->presentation); ++position) {
        const auto* node = activityview::At(request.snapshot->snapshot->presentation, position);
        const auto found = request.snapshot->snapshot->rows.find(node->row->value(A::IdRole).toString());
        if (found == request.snapshot->snapshot->rows.end()) continue;
        const auto& row = *found->second;
        const qint64 timestamp = row.value(TransactionActivityModel::TimestampRole).toLongLong();
        const auto status = static_cast<Transaction::Status>(row.value(TransactionActivityModel::StatusRole).toInt());
        const bool confirmed = status == Transaction::Confirming || status == Transaction::Confirmed;
        // The source decides inactivity from Core's mempool membership; a
        // stored marker alone does not make a returned original replaced.
        const bool inactive = row.value(TransactionActivityModel::IsInactiveRole).toBool();
        const bool replaced = inactive && !row.value(TransactionActivityModel::ReplacedByTxidRole).toString().isEmpty();
        const CAmount amount = replaced ? 0 : row.value(TransactionActivityModel::NetAmountSatRole).toLongLong();
        const bool pending_request = row.value(TransactionActivityModel::IsPendingRequestRole).toBool();

        QString status_label;
        if (pending_request) status_label = tr("Awaiting payment");
        else if (replaced) status_label = tr("Replaced");
        else if (!row.value(TransactionActivityModel::StatusKnownRole).toBool()) status_label = tr("Unknown");
        else {
            switch (status) {
            case Transaction::Confirmed: status_label = tr("Confirmed"); break;
            case Transaction::Confirming: status_label = tr("Confirming"); break;
            case Transaction::Unconfirmed: status_label = inactive ? tr("Not in mempool") : tr("Unconfirmed"); break;
            case Transaction::Conflicted: status_label = tr("Conflicted"); break;
            case Transaction::Abandoned: status_label = tr("Abandoned"); break;
            case Transaction::Immature: status_label = tr("Immature"); break;
            case Transaction::NotAccepted: status_label = tr("Not accepted"); break;
            }
        }

        QStringList values{
            confirmed ? QStringLiteral("true") : QStringLiteral("false"),
            QDateTime::fromSecsSinceEpoch(timestamp).toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")),
            typeLabel(row),
            GuardedCsvText(row.value(TransactionActivityModel::LabelRole).toString()),
            GuardedCsvText(row.value(TransactionActivityModel::AddressRole).toString()),
            QmlBitcoinUnits::format(ExportDisplayUnit(request.unit), amount, false, QmlBitcoinUnits::SeparatorStyle::NEVER),
            pending_request ? QString{} : row.value(TransactionActivityModel::TxidRole).toString(),
        };
        values << (pending_request ? tr("Payment request") : tr("Transaction")) << QString{} << status_label << QString{};
        WriteCsvRow(stream, values);

        // Keep the wallet impact (including fees) only on the parent. Action
        // amounts describe individual movements and are not wallet balance deltas.
        const auto actions = row.value(TransactionActivityModel::ActionsRole).toList();
        if (pending_request || actions.size() <= 1) continue;
        for (const auto& value : actions) {
            const auto action = value.toMap();
            const int direction = action.value("direction").toInt();
            const CAmount action_amount = action.value("amountSat").toLongLong();
            WriteCsvRow(stream, {
                values[0], values[1],
                direction == TransactionActivityModel::SendAction ? tr("Sent")
                    : direction == TransactionActivityModel::ReceiveAction ? tr("Received") : tr("Internal"),
                GuardedCsvText(action.value("label").toString()),
                GuardedCsvText(action.value("address").toString()),
                QString{}, values[6], tr("Action"), GuardedCsvText(action.value("actionId").toString()), status_label,
                QmlBitcoinUnits::format(ExportDisplayUnit(request.unit),
                    direction == TransactionActivityModel::SendAction ? -action_amount : action_amount,
                    false, QmlBitcoinUnits::SeparatorStyle::NEVER),
            });
        }
    }

    stream.flush();
    if (stream.status() != QTextStream::Ok || file.error() != QFileDevice::NoError) { file.cancelWriting(); return {false, filename, tr("The activity export could not be written.")}; }
    if (!file.commit()) return {false, filename, file.errorString()};
    return {true, filename, {}};
}
bool ActivityFilterProxyModel::beginExport()
{
    if (m_export_result_shown) { m_export_result_shown = false; Q_EMIT exportChanged(); }
    refreshDate();
    auto* provider = qobject_cast<activityview::Source*>(sourceModel());
    if (m_closed || exportBusy() || busy() || m_source_deferred || !sourceModel() || !m_current || !m_execution.scheduler ||
        (provider && provider->opening() && !provider->opening()->available()) || m_chooser_sequence == std::numeric_limits<quint64>::max()) {
        m_export_rejected = true; m_export_error = tr("Activity is not ready to export yet."); Q_EMIT exportChanged(); return false;
    }
    m_chooser_snapshot = m_current; m_chooser_source = sourceModel(); m_chooser_unit = m_display_unit;
    m_chooser_token = QString::number(++m_chooser_sequence); m_export_rejected = false; m_export_error.clear();
    const auto token = m_chooser_token; const QPointer<ActivityFilterProxyModel> guard{this};
    Q_EMIT exportChanged(); return guard && m_chooser_token == token;
}
void ActivityFilterProxyModel::cancelExport()
{
    if (m_chooser_token.isEmpty()) return;
    m_chooser_snapshot.reset(); m_chooser_source.clear(); m_chooser_token.clear(); Q_EMIT exportChanged();
}
bool ActivityFilterProxyModel::chooseExportFile(const QString& path)
{
    // The capture ends when the source changes, so a file chosen for an
    // earlier source finds no capture.
    if (m_chooser_token.isEmpty() || !m_chooser_snapshot || !m_chooser_source || m_chooser_source != sourceModel() ||
        (m_source_deferred && m_deferred_source != m_chooser_source)) {
        m_export_rejected = true; m_export_error = tr("This export selection is no longer current.");
        m_export_result_shown = true; m_export_succeeded = false; Q_EMIT exportChanged(); return false;
    }
    auto original = std::move(m_chooser_snapshot); const auto unit = m_chooser_unit;
    m_chooser_source.clear(); m_chooser_token.clear();
    // Starting can retire this proxy through its scheduler.
    const QPointer<ActivityFilterProxyModel> guard{this};
    const bool started = startExport(path, std::move(original), unit, true) != 0;
    if (!started && guard && !m_closed) { m_export_result_shown = true; m_export_succeeded = false; Q_EMIT exportChanged(); }
    return started;
}
quint64 ActivityFilterProxyModel::requestExportCsv(const QString& path)
{
    refreshDate(); // A file command may arrive before the next clock wakeup.
    if (exportChoosing()) { m_export_rejected = true; m_export_error = tr("Finish or cancel the current export selection first."); Q_EMIT exportChanged(); return 0; }
    const QPointer<ActivityFilterProxyModel> guard{this};
    const auto ticket = startExport(path, m_current, m_display_unit, false);
    if (!ticket && guard && !m_closed) { m_export_result_shown = true; m_export_succeeded = false; Q_EMIT exportChanged(); }
    return ticket;
}
void ActivityFilterProxyModel::dismissExportResult()
{
    if (!m_export_result_shown) return;
    m_export_result_shown = false;
    Q_EMIT exportChanged();
}
QString ActivityFilterProxyModel::exportResultTitle() const
{
    return m_export_succeeded ? tr("Export complete") : tr("Export failed");
}
QString ActivityFilterProxyModel::exportResultText() const
{
    if (m_export_rejected) return m_export_error;
    if (m_export_result.present && !m_export_result.outcomeKnown)
        return tr("The export may not have finished. Check this file before trying again: %1").arg(m_export_result.finalPath);
    if (m_export_succeeded) return tr("Your Activity CSV has been saved.");
    if (m_export_result.present) return m_export_result.errorText;
    return m_export_error.isEmpty() ? tr("The Activity CSV could not be saved. Check the file path and try again.") : m_export_error;
}
quint64 ActivityFilterProxyModel::startExport(const QString& path, std::shared_ptr<const activityview::Hold> snapshot, int unit, bool captured)
{
    if (m_closed || exportBusy() || (!captured && busy()) || !snapshot || !m_execution.scheduler || path.isEmpty() || path.size() > 4096 || m_export_ticket == std::numeric_limits<quint64>::max()) {
        m_export_rejected = true; m_export_error = tr("Activity is not ready to export yet."); Q_EMIT exportChanged(); return 0;
    }
    auto* provider = qobject_cast<activityview::Source*>(sourceModel());
    if (provider && provider->opening() && !provider->opening()->available()) { m_export_rejected = true; m_export_error = tr("The wallet is closing."); Q_EMIT exportChanged(); return 0; }
    const auto files = provider ? provider->activityFiles() : activityexport::Files{};
    if (!files.inspect || !files.install) { m_export_rejected = true; m_export_error = tr("Activity is not ready to export yet."); Q_EMIT exportChanged(); return 0; }
    const auto ticket = ++m_export_ticket; m_export_rejected = false;
    const QPointer<ActivityFilterProxyModel> observer{this};
    if (m_export_operation) m_export_operation->deleteLater();
    // Reserving Native ownership may reenter close through an injected
    // scheduler. Attach the child only after the observer survives it.
    auto* operation = new ActivityExportOperation({path, snapshot, unit}, files,
        {m_execution.scheduler, m_services.execution.post}, m_services.write);
    if (!observer || observer->m_closed) { delete operation; return 0; }
    operation->setParent(this);
    m_export_operation = operation; m_export_error.clear(); m_exported_path.clear();
    auto finished = std::make_shared<bool>(false);
    connect(operation, &ActivityExportOperation::changed, this, [observer, original = QPointer<ActivityExportOperation>{operation}, ticket, finished] {
        if (!observer || observer->m_closed || !original || observer->m_export_operation != original) return;
        const auto receipt = original->result(); // Bounded immutable metadata survives reentrant observer destruction/successors.
        observer->m_export_rejected = false; observer->m_export_error = receipt.errorText; observer->m_exported_path = receipt.saved ? receipt.finalPath : QString{};
        if (receipt.present && !std::exchange(*finished, true)) {
            observer->m_export_result = receipt;
            if (!receipt.cancelled) { observer->m_export_result_shown = true; observer->m_export_succeeded = receipt.saved; }
            Q_EMIT observer->exportChanged(); if (!observer || observer->m_closed) return;
            Q_EMIT observer->exportFinished(ticket, receipt.saved, receipt.finalPath, receipt.errorText);
        } else Q_EMIT observer->exportChanged();
    });
    const bool admitted = operation->start();
    if (!observer || observer->m_closed) return 0;
    if (!admitted) {
        m_export_rejected = true;
        if (m_export_error.isEmpty()) m_export_error = tr("The Activity export could not start. Try again.");
    }
    Q_EMIT exportChanged(); return admitted ? ticket : 0;
}
bool ActivityFilterProxyModel::openRowMenu(int row)
{
    const auto* value = activityview::At(m_root, row);
    if (m_closed || !value || !m_current || busy()) return false;
    m_context = m_current; m_context_txid = value->row->value(A::TxidRole).toString();
    return true;
}
void ActivityFilterProxyModel::closeRowMenu()
{
    m_context.reset(); m_context_txid.clear();
}
bool ActivityFilterProxyModel::copyRowMenuRawTransaction()
{
    if (m_closed || !m_context || m_context_txid.size() != 64 || !m_context->snapshot->raw || !m_execution.scheduler) return false;
    auto* provider = qobject_cast<activityview::Source*>(sourceModel());
    if (provider && provider->opening() && !provider->opening()->available()) return false;
    struct Copy { std::shared_ptr<const activityview::Hold> snapshot; std::shared_ptr<const QString> text; void clear() { snapshot.reset(); text.reset(); } };
    struct Lifetime { std::shared_ptr<Copy> value; std::shared_ptr<execution::ResourceLease> lease; ~Lifetime() { value.reset(); lease.reset(); } };
    auto value = std::make_shared<Copy>();
    const auto opening = provider ? provider->opening() : nullptr;
    auto held = opening ? opening->holdValues([value] { value->clear(); }) : activityview::HoldValues(m_execution.scheduler, [value] { value->clear(); });
    if (!held) return false;
    auto lifetime = std::make_shared<Lifetime>(); lifetime->value = value; lifetime->lease = std::make_shared<execution::ResourceLease>(std::move(*held.lease));
    value->snapshot = m_context;
    const auto txid = m_context_txid;
    const QPointer<ActivityFilterProxyModel> observer{this};
    const auto complete = [lifetime, observer](std::shared_ptr<const QString> text) {
        lifetime->value->text = std::move(text);
        QMetaObject::invokeMethod(QCoreApplication::instance(), [lifetime, observer] {
            const bool copied = lifetime->value->text && !lifetime->value->text->isEmpty() && QGuiApplication::clipboard();
            if (copied) QGuiApplication::clipboard()->setText(*lifetime->value->text);
            if (observer && !observer->m_closed) Q_EMIT observer->copyFinished(copied, copied ? QString{} : tr("The raw transaction could not be copied."));
        }, Qt::QueuedConnection);
    };
    const auto admitted = execution::Submit(*m_execution.scheduler, [lifetime, txid] { return std::make_shared<const QString>(lifetime->value->snapshot->snapshot->raw(txid)); }, complete,
        [complete](std::exception_ptr) { complete({}); }, {execution::Lane::Domain, {}, execution::WorkClass::Action});
    return bool(admitted);
}
