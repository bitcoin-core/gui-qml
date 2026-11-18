// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_QML_MODELS_WALLETPROVISIONINGCHECK_H
#define BITCOIN_QML_MODELS_WALLETPROVISIONINGCHECK_H
#include <qml/snapshotquery.h>
#include <qml/walletopening.h>
#include <qml/walletnames.h>
#include <QObject>
#include <QString>
#include <memory>
#include <mutex>
namespace interfaces { class Node; }
class WalletProvisioningCheck : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString name READ name WRITE setName NOTIFY inputChanged)
    Q_PROPERTY(QString xpub READ xpub WRITE setXpub NOTIFY inputChanged)
    Q_PROPERTY(QString path READ path WRITE setPath NOTIFY inputChanged)
    Q_PROPERTY(bool pending READ pending NOTIFY changed)
    Q_PROPERTY(bool ready READ ready NOTIFY changed)
    Q_PROPERTY(QString nameError READ nameError NOTIFY changed)
    Q_PROPERTY(bool xpubValid READ xpubValid NOTIFY changed)
    Q_PROPERTY(bool pathExists READ pathExists NOTIFY changed)
    Q_PROPERTY(QString normalizedPath READ normalizedPath NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
public:
    WalletProvisioningCheck(interfaces::Node& node, execution::SnapshotServices services, QObject* parent = nullptr, std::shared_ptr<std::atomic_bool> names_ready = {}, std::shared_ptr<WalletNameRegistry> registry = {}, std::shared_ptr<std::atomic_bool> shutdown_gate = {}, bool strict_name = false);
    ~WalletProvisioningCheck() override;
    QString name() const { return m_name; }
    QString xpub() const { return m_xpub; }
    QString path() const { return m_path; }
    void setName(const QString& value);
    void setXpub(const QString& value);
    void setPath(const QString& value);
    bool pending() const { return m_pending; }
    bool ready() const { return !m_closed && !m_pending && m_completed == m_revision && m_error.isEmpty(); }
    QString nameError() const { return m_name_error; }
    bool xpubValid() const { return ready() && m_xpub_valid; }
    bool pathExists() const { return ready() && m_path_exists; }
    QString normalizedPath() const { return ready() ? m_normalized_path : QString{}; }
    QString error() const { return m_error; }
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void closeAdmission();
Q_SIGNALS:
    void inputChanged();
    void changed();
private:
    struct Input { std::mutex mutex; QString name, xpub, path; quint64 revision{0}; };
    struct Result { QString name_error, normalized_path; bool xpub_valid{false}, path_exists{false}; quint64 revision{0}; };
    void publish();
    interfaces::Node& m_node;
    std::shared_ptr<QObject> m_bridge;
    std::shared_ptr<std::atomic_bool> m_names_ready;
    std::shared_ptr<WalletNameRegistry> m_registry;
    execution::SnapshotServices m_services;
    std::shared_ptr<Input> m_input{std::make_shared<Input>()};
    std::shared_ptr<execution::SnapshotQuery<Result>> m_query;
    QString m_name, m_xpub, m_path, m_name_error, m_normalized_path, m_error;
    quint64 m_revision{0}, m_completed{0};
    bool m_pending{false}, m_xpub_valid{false}, m_path_exists{false};
    walletopening::AdmissionFence m_closed;
    const bool m_strict_name;
};

#endif // BITCOIN_QML_MODELS_WALLETPROVISIONINGCHECK_H
