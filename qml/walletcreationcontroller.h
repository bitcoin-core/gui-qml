// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_WALLETCREATIONCONTROLLER_H
#define BITCOIN_QML_WALLETCREATIONCONTROLLER_H

#include <qml/guipresentation.h>
#include <qml/models/signerdiscoverymodel.h>
#include <qml/models/walletcreationmodel.h>
#include <qml/models/walletoperationmodel.h>
#include <qml/models/walletprovisioningcheck.h>
#include <qml/walletrestoration.h>

#include <atomic>
#include <functional>
#include <memory>

#include <QObject>
#include <QPointer>
#include <QString>
#include <QUrl>

class WalletModel;
class WalletRegistry;
namespace interfaces { class Node; }

/** Creating, restoring and migrating wallets. Each action is a
 * WalletOperationModel the caller binds to; the controller keeps the active
 * one, admits one at a time, and owns external signer discovery for creation.
 * Wallets it opens are adopted by the wallet list. */
class WalletCreationController final : public QObject
{
    Q_OBJECT
    //! The latest operation the controller started, or null.
    Q_PROPERTY(WalletOperationModel* operation READ operation NOTIFY operationChanged)
    Q_PROPERTY(SignerDiscoveryModel* signers READ signers NOTIFY signersChanged)
    //! One connected signer was found and can create a wallet.
    Q_PROPERTY(bool canCreateExternalSignerWallet READ canCreateExternalSignerWallet NOTIFY signersChanged)

public:
    struct Host {
        interfaces::Node* node{nullptr};
        std::shared_ptr<WalletRegistry> registry;
        std::shared_ptr<walletcreation::Admission> admission;
        std::shared_ptr<std::atomic_bool> names_ready, shutdown_gate;
        presentation::Post post;
        WalletProvisioningCheck::Queues catalog_queues{};
        //! The wallet list admits a new operation now.
        std::function<bool()> accepting;
        //! A wallet load or creation is in progress.
        std::function<bool()> loading;
        //! The wallet list is ready; otherwise unavailable() says why not.
        std::function<bool()> ready;
        std::function<QString()> unavailable;
        //! The active operation changed what the wallet list must observe.
        std::function<void()> context_changed;
    };
    WalletCreationController(Host host, QObject* parent);

    WalletOperationModel* operation() const { return m_operation; }
    //! An admitted operation is still running.
    bool busy() const;
    //! Inside an admission: the wallet list must not start a load.
    bool admitting() const { return m_admitting; }
    SignerDiscoveryModel* signers() const { return m_signers; }
    bool canCreateExternalSignerWallet() const;

    //! A creation form for C++ callers, who own it. QML declares a
    //! WalletCreationModel and binds its creation property instead.
    WalletCreationModel* newCreation();
    //! Supplies \p editor's services and follows it until it is destroyed.
    void adopt(WalletCreationModel* editor);
    Q_INVOKABLE WalletOperationModel* restore(const QUrl& source);
    Q_INVOKABLE WalletOperationModel* restoreLocalFile(const QString& path) { return restore(QUrl::fromLocalFile(path)); }
    //! Offers to migrate legacy wallet \p wallet_id; the operation asks first.
    Q_INVOKABLE WalletOperationModel* migrate(const QString& wallet_id);
    Q_INVOKABLE WalletProvisioningCheck* createProvisioningCheck();
    Q_INVOKABLE void refreshSigners();

    //! Starts creating a wallet without a form.
    WalletOperationModel* create(WalletCreationModel::Kind kind, const QString& name, const QString& key = {}, const QString& passphrase = {});
    //! Starts a confirmed migration of \p path with \p passphrase.
    WalletOperationModel* migrateConfirmed(const QString& path, const QString& passphrase = {});
    //! Starts restoring from \p path without the URL form.
    WalletOperationModel* restorePath(const QString& path);
    //! Forgets a finished active operation.
    void clear();
    //! The wallet list's load superseded the active operation.
    void supersede() { m_operation.clear(); }
    WalletOperationModel::Services operationServices();

    void setSignerExecutor(execution::Executor& executor, presentation::Post post = {});
    void setSignerPorts(signerdiscovery::Ports ports);
    void close();
    //! Signer discovery finished its background work.
    bool drained() const { return !m_signers || m_signers->drained(); }

Q_SIGNALS:
    void operationChanged();
    //! The active operation started or finished running.
    void busyChanged();
    void signersChanged();
    void signersDrained();
    //! Forms re-check what they can create.
    void capabilitiesChanged();
    void migrationOffered(WalletOperationModel* operation);

private:
    friend class WalletQmlControllerTests;
    WalletCreationModel::Services creationServices();
    void track(WalletOperationModel* operation);
    //! Keeps a refused operation as the active one so its error shows.
    void showRefused(WalletOperationModel* operation);
    WalletOperationModel* startRestoration(walletrestoration::Request request);

    Host m_host;
    bool m_closed{false};
    bool m_admitting{false};
    bool m_busy{false};
    QPointer<WalletOperationModel> m_operation;
    SignerDiscoveryModel* m_signers{nullptr};
    presentation::Post m_signer_post;
    execution::Executor* m_signer_executor{nullptr};
    signerdiscovery::Ports m_signer_ports;
};

#endif // BITCOIN_QML_WALLETCREATIONCONTROLLER_H
