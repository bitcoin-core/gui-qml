// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_QML_MODELS_WALLETOPERATIONPRESENTATIONMODEL_H
#define BITCOIN_QML_MODELS_WALLETOPERATIONPRESENTATIONMODEL_H
#include <qml/walletoperationpresentation.h>
#include <QAbstractListModel>
#include <QPointer>
#include <vector>

// One window's bounded presentation queue. Each row keeps the exact operation
// object through its own native Popup teardown; it can never retarget a dialog.
class WalletOperationPresentationModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(bool showTerminalResults READ showTerminalResults WRITE setShowTerminalResults NOTIFY showTerminalResultsChanged)
    Q_PROPERTY(WalletOperationError error READ error NOTIFY errorChanged)
public:
    enum Role { OperationRole = Qt::UserRole + 1, ActiveRole };
    explicit WalletOperationPresentationModel(QObject* parent = nullptr);
    ~WalletOperationPresentationModel() override;
    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    bool showTerminalResults() const { return m_terminals; }
    void setShowTerminalResults(bool value);
    WalletOperationError error() const { return m_error; }
    Q_INVOKABLE bool offer(QObject* operation);
    //! Dismisses \p operation if it is the dialog shown now.
    Q_INVOKABLE bool dismiss(QObject* operation);
    //! The dialog for \p prompt of \p operation closed; a stale prompt changes nothing.
    Q_INVOKABLE void reportClosed(QObject* operation, QObject* prompt);
Q_SIGNALS:
    void errorChanged();
    void showTerminalResultsChanged();
    void commandRejected(const QString& operation, const WalletOperationError& error);
private:
    struct Entry { QPointer<QObject> object; WalletOperationPresentation* operation; QString id; QMetaObject::Connection changed, destroyed; };
    bool refuse(const QString& id, const QString& reason);
    bool erase(size_t row);
    void refresh();
    std::vector<Entry> m_entries;
    WalletOperationError m_error;
    bool m_terminals{true}, m_mutating{false}, m_refresh_pending{false};
};
#endif // BITCOIN_QML_MODELS_WALLETOPERATIONPRESENTATIONMODEL_H
