// Copyright (c) 2024-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/walletlistmodel.h>

#include <interfaces/node.h>
#include <qml/asyncjoin.h>
#include <qml/guipresentation.h>
#include <qml/models/walletmodel.h>

#include <QPointer>

namespace {
QList<int> RolesFor(unsigned fields)
{
    QList<int> roles;
    if (fields & walletcatalog::DisplayName) roles.append({Qt::DisplayRole, WalletListModel::DisplayNameRole});
    if (fields & walletcatalog::Load) roles.append(WalletListModel::LoadStateRole);
    if (fields & walletcatalog::Section) roles.append(WalletListModel::WalletSectionRole);
    if (fields & walletcatalog::Error) roles.append({WalletListModel::ErrorMessageRole, WalletListModel::RowErrorRole});
    if (fields & walletcatalog::Information) roles.append({WalletListModel::BalanceRole, WalletListModel::BalanceSatoshiRole, WalletListModel::BalanceKnownRole, WalletListModel::KeySchemeKindRole});
    else if (fields & walletcatalog::BalanceText) roles.append(WalletListModel::BalanceRole);
    if (fields & walletcatalog::OpenWallet) roles.append(WalletListModel::OpenWalletRole);
    if (fields & walletcatalog::Operation) roles.append(WalletListModel::OperationIdRole);
    return roles;
}
} // namespace

WalletListModel::WalletListModel(interfaces::Node& node, QObject* parent)
    : WalletListModel(node, nullptr, {}, parent) {}
WalletListModel::WalletListModel(interfaces::Node& node, execution::Executor& executor, presentation::Post post, QObject* parent)
    : WalletListModel(node, &executor, std::move(post), parent) {}
WalletListModel::WalletListModel(interfaces::Node& node, execution::Executor* executor, presentation::Post post, QObject* parent)
    : QAbstractListModel(parent), m_post(post ? std::move(post) : presentation::QueuedPost()),
      m_owned_executor(executor ? nullptr : std::make_shared<execution::Executor>(execution::Executor::Config{}))
{
    const QPointer<WalletListModel> guard{this};
    m_reader = std::make_shared<walletcatalog::Reader>(executor ? *executor : *m_owned_executor,
        [&node] { return node.walletLoader().listWalletDir(); },
        [guard, post = m_post] { post([guard] { if (guard) guard->publishSnapshot(); }); });
    observe(m_reader->observation());
}
WalletListModel::~WalletListModel()
{
    close();
    if (m_owned_executor) {
        auto join = JoinThreadAfterDrain([executor = std::move(m_owned_executor)] { executor->stopAndJoin(); },
            QCoreApplication::instance(), [] {});
        m_reader->whenRetired(std::move(join));
    }
}
void WalletListModel::close()
{
    if (std::exchange(m_stopping, true)) return;
    m_pending = false;
    m_reader->close();
    observe(m_reader->observation());
    m_visible.reset();
    Q_EMIT projectionChanged();
}
void WalletListModel::whenRetired(std::function<void()> done)
{
    m_reader->whenRetired(std::move(done));
}
void WalletListModel::listWalletDir() { enqueue(walletcatalog::Input{walletcatalog::Input::Kind::Directory}); }
void WalletListModel::refresh() { listWalletDir(); }
void WalletListModel::setWalletLoadState(const QString& name, LoadState state, const QString& error)
{
    walletcatalog::Input input{walletcatalog::Input::Kind::Load}; input.name = name;
    switch (state) {
    case LoadState::Closed: input.load = walletcatalog::LoadState::Closed; break;
    case LoadState::Open: input.load = walletcatalog::LoadState::Open; break;
    case LoadState::Loading: input.load = walletcatalog::LoadState::Loading; break;
    case LoadState::LoadError: input.load = walletcatalog::LoadState::LoadError; break;
    }
    input.error = error; enqueue(std::move(input));
}
void WalletListModel::setDisplayUnit(int unit) { walletcatalog::Input input{walletcatalog::Input::Kind::Unit}; input.unit = unit; enqueue(std::move(input)); }
void WalletListModel::setWalletInfo(const QString& name, qint64 balance, int keySchemeKind, bool balanceKnown)
{
    walletcatalog::Input input{walletcatalog::Input::Kind::Info}; input.name = name; input.balance = balance; input.scheme = keySchemeKind; input.known = balanceKnown; enqueue(std::move(input));
}
void WalletListModel::refreshDisplayNames() { enqueue(walletcatalog::Input{walletcatalog::Input::Kind::Names}); }
void WalletListModel::setBackendAvailability(BackendAvailability availability)
{
    if (m_stopping || availability == m_availability) return;
    walletcatalog::Input input{walletcatalog::Input::Kind::Availability};
    switch (availability) {
    case BackendAvailability::Uninitialized: input.availability = walletcatalog::Availability::Uninitialized; break;
    case BackendAvailability::Available: input.availability = walletcatalog::Availability::Available; break;
    case BackendAvailability::Disabled: input.availability = walletcatalog::Availability::Disabled; break;
    case BackendAvailability::Unavailable: input.availability = walletcatalog::Availability::Unavailable; break;
    }
    enqueue(std::move(input));
}
void WalletListModel::setOpenWallet(const QString& walletId, WalletModel* wallet)
{
    if (m_stopping) return;
    const auto existing = m_open_wallets.constFind(walletId);
    if (existing != m_open_wallets.cend() && existing->identity == wallet) return;
    if (existing == m_open_wallets.cend() && !wallet) return;
    if (existing != m_open_wallets.cend()) disconnect(existing->destroyed);
    if (!wallet) m_open_wallets.remove(walletId);
    else {
        const auto destroyed = connect(wallet, &QObject::destroyed, this, [this, walletId, identity = wallet] {
            const auto found = m_open_wallets.constFind(walletId);
            if (found != m_open_wallets.cend() && found->identity == identity) setOpenWallet(walletId, nullptr);
        });
        m_open_wallets.insert(walletId, {wallet, wallet, destroyed});
    }
    walletcatalog::Input input{walletcatalog::Input::Kind::OpenWallet}; input.name = walletId; enqueue(std::move(input));
}
void WalletListModel::setOperationId(const QString& walletId, const QString& operationId)
{
    walletcatalog::Input input{walletcatalog::Input::Kind::Operation}; input.name = walletId; input.operation = operationId; enqueue(std::move(input));
}
void WalletListModel::enqueue(walletcatalog::Input input)
{
    if (m_stopping) return;
    m_reader->request(std::move(input));
    observe(m_reader->observation());
    m_pending = m_phase != ReaderPhase::Failed;
    Q_EMIT projectionChanged();
}
void WalletListModel::observe(const walletcatalog::Observation& observation)
{
    switch (observation.phase) {
    case walletcatalog::Phase::Idle: m_phase = ReaderPhase::Idle; break;
    case walletcatalog::Phase::Scheduled: m_phase = ReaderPhase::Scheduled; break;
    case walletcatalog::Phase::Reading: m_phase = ReaderPhase::Reading; break;
    case walletcatalog::Phase::ReadingWithPendingWork: m_phase = ReaderPhase::ReadingWithPendingWork; break;
    case walletcatalog::Phase::Applying: m_phase = ReaderPhase::Applying; break;
    case walletcatalog::Phase::Current: m_phase = ReaderPhase::Current; break;
    case walletcatalog::Phase::Failed: m_phase = ReaderPhase::Failed; break;
    case walletcatalog::Phase::Stopping: m_phase = ReaderPhase::Stopping; break;
    case walletcatalog::Phase::Stopped: m_phase = ReaderPhase::Stopped; break;
    }
    switch (observation.availability) {
    case walletcatalog::Availability::Uninitialized: m_availability = BackendAvailability::Uninitialized; break;
    case walletcatalog::Availability::Available: m_availability = BackendAvailability::Available; break;
    case walletcatalog::Availability::Disabled: m_availability = BackendAvailability::Disabled; break;
    case walletcatalog::Availability::Unavailable: m_availability = BackendAvailability::Unavailable; break;
    }
    m_error = observation.error; m_fresh = observation.fresh; m_revision = observation.revision;
}
void WalletListModel::publishSnapshot()
{
    const auto update = m_reader->take();
    observe(update.observation);
    const auto next = update.snapshot;
    if (m_stopping || !next) {
        if (m_stopping || m_phase == ReaderPhase::Failed || m_phase == ReaderPhase::Idle) m_pending = false;
        Q_EMIT projectionChanged(); return;
    }
    const QPointer<WalletListModel> guard{this};
    const bool count_changed = rowCount() != int(next->rows.size());
    const bool reset = next->structure_changed || next->base.lock() != m_visible;
    if (reset) {
        beginResetModel(); if (!guard) return;
        if (m_stopping) { endResetModel(); return; }
    }
    m_visible = next;
    m_reader->published(next);
    if (reset) { endResetModel(); if (!guard || m_stopping) return; }
    const bool first = !m_wallet_dir_loaded && next->loaded;
    m_wallet_dir_loaded = next->loaded;
    if (next->directory_epoch != m_directory_epoch || first || count_changed) {
        m_directory_epoch = next->directory_epoch;
        Q_EMIT walletListChanged(rowCount() > 0); if (!guard || m_stopping) return;
    }
    if (first) { Q_EMIT walletDirLoadedChanged(); if (!guard || m_stopping) return; }
    m_notice = reset ? next->notices.size() : 0;
    publishNotices(next->revision);
}
void WalletListModel::publishNotices(quint64 revision)
{
    if (m_stopping || !m_visible || m_visible->revision != revision) return;
    const auto snapshot = m_visible; const QPointer<WalletListModel> guard{this};
    for (int budget = 0; budget < 32 && m_notice < snapshot->notices.size(); ++budget) {
        const auto& [row, fields] = snapshot->notices[m_notice++];
        const auto roles = RolesFor(fields);
        Q_EMIT dataChanged(index(row), index(row), roles);
        if (!guard || m_stopping || m_visible != snapshot) return;
    }
    if (m_notice < snapshot->notices.size()) {
        try { m_post([guard, revision] { if (guard) guard->publishNotices(revision); }); }
        catch (...) { m_error = tr("Wallet catalog presentation could not be delivered. Refresh to retry."); m_pending = false; m_fresh = false; m_phase = ReaderPhase::Failed; Q_EMIT projectionChanged(); }
    } else {
        if (revision == m_revision) m_pending = false;
        Q_EMIT projectionChanged();
    }
}
int WalletListModel::rowCount(const QModelIndex& parent) const { return parent.isValid() || !m_visible ? 0 : int(m_visible->rows.size()); }
QVariant WalletListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.model() != this || index.row() < 0 || index.row() >= rowCount()) return {};
    const auto& item = m_visible->rows[index.row()];
    switch (role) {
    case Qt::DisplayRole: case DisplayNameRole: return item.display_name;
    case NameRole: case WalletIdRole: return item.name;
    case FormatRole: return item.format;
    case LoadStateRole:
        switch (item.load_state) {
        case walletcatalog::LoadState::Closed: return static_cast<int>(LoadState::Closed);
        case walletcatalog::LoadState::Open: return static_cast<int>(LoadState::Open);
        case walletcatalog::LoadState::Loading: return static_cast<int>(LoadState::Loading);
        case walletcatalog::LoadState::LoadError: return static_cast<int>(LoadState::LoadError);
        }
        return {};
    case ErrorMessageRole: case RowErrorRole: return item.error;
    case BalanceRole: return item.balance_text;
    case BalanceSatoshiRole: return item.balance;
    case BalanceKnownRole: return item.balance_known;
    case WalletSectionRole: return item.section;
    case KeySchemeKindRole: return item.key_scheme;
    case OperationIdRole: return item.operation;
    case OpenWalletRole: return QVariant::fromValue(m_open_wallets.value(item.name).wallet.data());
    }
    return {};
}
QHash<int, QByteArray> WalletListModel::roleNames() const
{
    return {{NameRole, "name"}, {FormatRole, "format"}, {DisplayNameRole, "displayName"}, {LoadStateRole, "loadState"},
        {ErrorMessageRole, "errorMessage"}, {BalanceRole, "balance"}, {BalanceSatoshiRole, "balanceSatoshi"},
        {BalanceKnownRole, "balanceKnown"}, {KeySchemeKindRole, "keySchemeKind"}, {WalletSectionRole, "walletSection"},
        {WalletIdRole, "walletId"}, {OpenWalletRole, "openWallet"}, {OperationIdRole, "operationId"}, {RowErrorRole, "rowError"}};
}
