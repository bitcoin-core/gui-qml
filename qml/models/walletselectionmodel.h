// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_WALLETSELECTIONMODEL_H
#define BITCOIN_QML_MODELS_WALLETSELECTIONMODEL_H

#include <qml/models/walletqmlmodel.h>

#include <QObject>
#include <QPointer>
#include <QString>

#include <functional>

/** One window's selection. Registry membership and facade availability are
 * cached GUI facts; this owner never reads Core or starts a wallet operation.
 * A completed load may finish its pending indicator, but only an explicit
 * select of the original facade changes the selected wallet.
 */
class WalletSelectionModel final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(WalletQmlModel* selectedWallet READ selectedWallet NOTIFY selectedWalletChanged)
    Q_PROPERTY(QString pendingTargetId READ pendingTargetId NOTIFY pendingChanged)
    Q_PROPERTY(bool pending READ pending NOTIFY pendingChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
public:
    using Contains = std::function<bool(WalletQmlModel*)>;
    WalletSelectionModel(WalletQmlModel* empty, Contains contains, QObject* parent = nullptr);
    WalletQmlModel* selectedWallet() const { return m_selected ? m_selected.data() : m_empty.data(); }
    quint64 revision() const { return m_revision; }
    QString pendingTargetId() const { return m_pending_target; }
    bool pending() const { return m_pending; }
    QString error() const { return m_error; }
    Q_INVOKABLE bool select(WalletQmlModel* wallet);
    Q_INVOKABLE void clear();
    quint64 beginSelection(const QString& target_id);
    void finishSelection(quint64 expected_revision);
    // The registry calls this before fencing a selected facade, then commits
    // forget() after its row/retirement state is coherent. A newer select wins.
    void prepareForget(WalletQmlModel* wallet);
    void forget(WalletQmlModel* wallet, WalletQmlModel* successor = nullptr);
    void closeAdmission();
Q_SIGNALS:
    void selectedWalletChanged();
    void pendingChanged();
    void errorChanged();
    void commandRejected(const QString& error);
private:
    bool available(WalletQmlModel* wallet) const;
    void observeSelected();
    void setError(QString error);
    const QPointer<WalletQmlModel> m_empty;
    const Contains m_contains;
    QPointer<WalletQmlModel> m_selected;
    QMetaObject::Connection m_available_connection, m_destroyed_connection;
    QString m_pending_target, m_error;
    quint64 m_revision{0}, m_pending_revision{0};
    bool m_pending{false}, m_closed{false};
};

#endif // BITCOIN_QML_MODELS_WALLETSELECTIONMODEL_H
