// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_BANLISTMODEL_H
#define BITCOIN_QML_MODELS_BANLISTMODEL_H

#include <net_types.h>
#include <netaddress.h>

#include <QAbstractListModel>
#include <QList>

#include <memory>

class BackendExecutor;

namespace interfaces { class Node; }

struct BanListEntry
{
    CSubNet subnet;
    CBanEntry ban_entry;
};

class BanListModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(bool ready READ ready NOTIFY refreshStateChanged)
    Q_PROPERTY(bool refreshPending READ refreshPending NOTIFY refreshStateChanged)
    Q_PROPERTY(QString refreshError READ refreshError NOTIFY refreshStateChanged)
    Q_PROPERTY(bool actionPending READ actionPending NOTIFY actionStateChanged)
    Q_PROPERTY(QString actionError READ actionError NOTIFY actionStateChanged)

public:
    enum class BanRoles {
        AddressRole = Qt::UserRole,
        BanUntilRole
    };

    explicit BanListModel(interfaces::Node& node, QObject* parent = nullptr, bool backend_ready = true,
                          std::shared_ptr<BackendExecutor> executor = {});
    ~BanListModel() override;
    void backendInitialized();

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_PROPERTY(int count READ count NOTIFY countChanged)
    int count() const { return m_ban_list.size(); }
    bool ready() const { return m_ready; }
    bool refreshPending() const { return m_refresh_running; }
    QString refreshError() const { return m_refresh_error; }
    bool actionPending() const { return m_action_pending || m_external_action_pending; }
    QString actionError() const { return m_action_error; }

    /** Return whether a request was accepted; completion is delivered separately. */
    Q_INVOKABLE bool unbanAt(int row);
    void beginShutdown();
    bool isDrained() const;
    void setExternalActionPending(bool pending);
    void finishExternalAction(bool refresh_required);

public Q_SLOTS:
    void refresh();

Q_SIGNALS:
    void countChanged();
    void refreshStateChanged();
    void actionStateChanged();
    void unbanFinished(bool success, const QString& error);
    void drained();

private:
    void finishRefresh(QList<BanListEntry> entries, const QString& error);
    void finishUnban(bool success, const QString& error);
    interfaces::Node& m_node;
    std::shared_ptr<BackendExecutor> m_executor;
    QList<BanListEntry> m_ban_list;
    bool m_stopping{false};
    bool m_backend_ready;
    bool m_ready{false};
    bool m_refresh_running{false};
    bool m_refresh_requested{false};
    bool m_action_pending{false};
    bool m_external_action_pending{false};
    QString m_refresh_error;
    QString m_action_error;
};

#endif // BITCOIN_QML_MODELS_BANLISTMODEL_H
