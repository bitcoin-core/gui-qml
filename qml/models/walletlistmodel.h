// Copyright (c) 2024 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_WALLETLISTMODEL_H
#define BITCOIN_QML_MODELS_WALLETLISTMODEL_H

#include <qml/guipresentation.h>
#include <qml/walletcatalog.h>
#include <QAbstractListModel>
#include <QHash>
#include <QList>
#include <QPointer>
#include <QString>

class WalletQmlModel;
Q_MOC_INCLUDE("qml/models/walletqmlmodel.h")

namespace interfaces {
class Node;
}

class WalletListModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(bool walletDirLoaded READ walletDirLoaded NOTIFY walletDirLoadedChanged)
    Q_PROPERTY(bool projectionPending READ projectionPending NOTIFY projectionChanged)
    Q_PROPERTY(QString readError READ readError NOTIFY projectionChanged)
    Q_PROPERTY(ReaderPhase phase READ phase NOTIFY projectionChanged)
    Q_PROPERTY(bool fresh READ fresh NOTIFY projectionChanged)
    Q_PROPERTY(BackendAvailability backendAvailability READ backendAvailability NOTIFY projectionChanged)

public:
    enum class LoadState {
        Closed    = 0,
        Open      = 1,
        Loading   = 2,
        LoadError = 3,
    };
    Q_ENUM(LoadState)
    enum class ReaderPhase { Idle, Scheduled, Reading, ReadingWithPendingWork, Applying, Current, Failed, Stopping, Stopped };
    Q_ENUM(ReaderPhase)
    enum class BackendAvailability { Uninitialized, Available, Disabled, Unavailable };
    Q_ENUM(BackendAvailability)

    WalletListModel(interfaces::Node& node, QObject *parent = nullptr);
    WalletListModel(interfaces::Node& node, execution::Executor& executor, presentation::Post post, QObject* parent = nullptr);
    ~WalletListModel() override;
    void close();
    void whenRetired(std::function<void()> done);
    bool projectionPending() const { return m_pending; }
    QString readError() const { return m_error; }
    ReaderPhase phase() const { return m_phase; }
    bool fresh() const { return m_fresh; }
    BackendAvailability backendAvailability() const { return m_availability; }

    enum Roles {
        NameRole = Qt::UserRole + 1,
        FormatRole,
        DisplayNameRole,
        LoadStateRole,
        ErrorMessageRole,
        BalanceRole,
        KeySchemeKindRole,
        WalletSectionRole,
        BalanceSatoshiRole,
        BalanceKnownRole,
        WalletIdRole,
        OpenWalletRole,
        OperationIdRole,
        RowErrorRole,
    };

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;
    bool walletDirLoaded() const { return m_wallet_dir_loaded; }

Q_SIGNALS:
    void walletListChanged(bool has_wallets);
    void walletDirLoadedChanged();
    void projectionChanged();

public Q_SLOTS:
    void listWalletDir();
    void setWalletLoadState(const QString& name, LoadState state, const QString& error = {});
    void setDisplayUnit(int unit);
    void setWalletInfo(const QString& name, qint64 balance, int keySchemeKind, bool balanceKnown = true);
    void refreshDisplayNames();
    void refresh();
    void setBackendAvailability(BackendAvailability availability);
    void setOpenWallet(const QString& walletId, WalletQmlModel* wallet);
    void setOperationId(const QString& walletId, const QString& operationId);

private:
    WalletListModel(interfaces::Node& node, execution::Executor* executor, presentation::Post post, QObject* parent);
    struct OpenWalletBinding {
        QPointer<WalletQmlModel> wallet;
        const WalletQmlModel* identity{nullptr};
        QMetaObject::Connection destroyed;
    };
    void enqueue(walletcatalog::Input input);
    void observe(const walletcatalog::Observation& observation);
    void publishSnapshot();
    void publishNotices(quint64 revision);
    std::shared_ptr<walletcatalog::Reader> m_reader;
    std::shared_ptr<const walletcatalog::Snapshot> m_visible;
    QHash<QString, OpenWalletBinding> m_open_wallets;
    presentation::Post m_post;
    std::shared_ptr<execution::Executor> m_owned_executor;
    quint64 m_revision{0}, m_directory_epoch{0};
    size_t m_notice{0};
    bool m_pending{false}, m_stopping{false}, m_wallet_dir_loaded{false};
    bool m_fresh{false};
    ReaderPhase m_phase{ReaderPhase::Idle};
    BackendAvailability m_availability{BackendAvailability::Available};
    QString m_error;
};

#endif // BITCOIN_QML_MODELS_WALLETLISTMODEL_H
