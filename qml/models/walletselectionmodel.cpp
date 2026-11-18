// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/walletselectionmodel.h>

#include <utility>

WalletSelectionModel::WalletSelectionModel(WalletQmlModel* empty, Contains contains, QObject* parent)
    : QObject(parent), m_empty(empty), m_contains(std::move(contains)) {}

bool WalletSelectionModel::available(WalletQmlModel* wallet) const
{
    return wallet && wallet != m_empty && wallet->available() && m_contains && m_contains(wallet);
}

void WalletSelectionModel::setError(QString error)
{
    if (m_error == error) return;
    m_error = std::move(error);
    Q_EMIT errorChanged();
}

void WalletSelectionModel::observeSelected()
{
    disconnect(m_available_connection);
    disconnect(m_destroyed_connection);
    if (!m_selected) return;
    auto* wallet = m_selected.data();
    m_available_connection = connect(wallet, &WalletQmlModel::availableChanged, this, [this, wallet] {
        if (m_selected == wallet && !available(wallet)) clear();
    });
    m_destroyed_connection = connect(wallet, &QObject::destroyed, this, [this] {
        // QPointer has already cleared when destroyed() is delivered.
        ++m_revision;
        observeSelected();
        Q_EMIT selectedWalletChanged();
    });
}

bool WalletSelectionModel::select(WalletQmlModel* wallet)
{
    if (m_closed || !available(wallet)) {
        const auto error = m_closed ? tr("Wallet selection is stopping.") : tr("This wallet opening is no longer available.");
        const QPointer<WalletSelectionModel> guard{this};
        setError(error);
        if (guard) Q_EMIT commandRejected(error);
        return false;
    }
    // Reaffirming B is still a newer user intent than an earlier load of A.
    ++m_revision;
    const bool changed = m_selected != wallet;
    m_selected = wallet;
    if (changed) observeSelected();
    const auto revision = m_revision;
    const QPointer<WalletSelectionModel> guard{this};
    setError({});
    if (guard && revision == m_revision && changed) Q_EMIT selectedWalletChanged();
    return true;
}

void WalletSelectionModel::clear()
{
    ++m_revision;
    const bool changed = bool(m_selected);
    m_selected.clear();
    observeSelected();
    const auto revision = m_revision;
    const QPointer<WalletSelectionModel> guard{this};
    setError({});
    if (guard && revision == m_revision && changed) Q_EMIT selectedWalletChanged();
}

quint64 WalletSelectionModel::beginSelection(const QString& target_id)
{
    if (m_closed) return 0;
    const auto revision = ++m_revision;
    m_pending_revision = revision;
    m_pending = true;
    m_pending_target = target_id; // Empty is the valid default-wallet identity.
    const QPointer<WalletSelectionModel> guard{this};
    setError({});
    if (guard && m_pending_revision == revision) Q_EMIT pendingChanged();
    return revision;
}

void WalletSelectionModel::finishSelection(quint64 expected_revision)
{
    if (!m_pending || m_pending_revision != expected_revision) return;
    m_pending = false;
    m_pending_target.clear();
    Q_EMIT pendingChanged();
}

void WalletSelectionModel::prepareForget(WalletQmlModel* wallet)
{
    if (m_selected != wallet) return;
    disconnect(m_available_connection);
    disconnect(m_destroyed_connection);
}

void WalletSelectionModel::forget(WalletQmlModel* wallet, WalletQmlModel* successor)
{
    if (m_selected != wallet) return;
    if (!m_closed && available(successor)) select(successor);
    else clear();
}

void WalletSelectionModel::closeAdmission()
{
    if (m_closed) return;
    m_closed = true;
    ++m_revision;
    if (!m_pending) return;
    m_pending = false;
    m_pending_target.clear();
    Q_EMIT pendingChanged();
}
