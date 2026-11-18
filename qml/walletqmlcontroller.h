// Copyright (c) 2024-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_WALLETQMLCONTROLLER_H
#define BITCOIN_QML_WALLETQMLCONTROLLER_H

#include <qml/models/signerdiscoverymodel.h>
#include <qml/models/walletlistmodel.h>
#include <qml/models/walletqmlmodel.h>
#include <qml/models/walletselectionmodel.h>
#include <qml/models/walletprovisioningcheck.h>
#include <qml/models/walletcreationmodel.h>
#include <qml/execution.h>
#include <qml/secretinput.h>
#include <qml/guipresentation.h>
#include <qml/walletcreationcontroller.h>
#include <qml/walletnamescontroller.h>
#include <qml/walletopenoperation.h>
#include <qml/walletregistry.h>

#include <interfaces/handler.h>
#include <interfaces/node.h>
#include <interfaces/wallet.h>
#include <support/allocators/secure.h>

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <mutex>
#include <set>
#include <list>

#include <QMutex>
#include <QObject>
#include <QThread>
#include <QSet>
#include <QUrl>

namespace walletlife { class LoadSubscription; struct Update; }


class WalletQmlController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(WalletQmlModel* selectedWallet READ selectedWallet NOTIFY selectedWalletChanged)
    Q_PROPERTY(WalletSelectionModel* selection READ selection CONSTANT)
    //! Wallet display names, the home folder and wallet file locations.
    Q_PROPERTY(WalletNamesController* names READ names CONSTANT)
    //! Creating, restoring and migrating wallets.
    Q_PROPERTY(WalletCreationController* creation READ creation CONSTANT)
    Q_PROPERTY(WalletQmlModel* closeTarget READ closeTarget NOTIFY closeTargetChanged)
    Q_PROPERTY(QAbstractItemModel* openWalletModel READ openWalletModel NOTIFY openWalletsChanged)
    Q_PROPERTY(bool initialized READ initialized NOTIFY initializedChanged)
    Q_PROPERTY(bool isWalletLoaded READ isWalletLoaded NOTIFY isWalletLoadedChanged)
    Q_PROPERTY(bool noWalletsFound READ noWalletsFound NOTIFY noWalletsFoundChanged)
    Q_PROPERTY(bool walletLoadInProgress READ walletLoadInProgress NOTIFY walletLoadInProgressChanged)
    //! The latest wallet open that failed, until dismissed.
    Q_PROPERTY(WalletOperationModel* loadFailure READ loadFailure NOTIFY loadFailureChanged)

public:
    using WalletOperationState = WalletOpenOperation::State;
    WalletSelectionModel* selection() const { return m_selection; }
    WalletNamesController* names() const { return m_names; }
    WalletCreationController* creation() const { return m_creations; }
    using WalletAdoptionState = WalletOpenOperation::Adoption;
    //! The latest wallet open request, run or refused; null before one.
    WalletOpenOperation* opening() const { return m_opening; }
    //! The latest open that ran; it follows its command until another starts.
    WalletOpenOperation* commandOpening() const { return m_command_opening; }
    //! Why the wallet list itself is unavailable, if it is.
    QString walletListError() const { return m_shutting_down ? QString{} : m_wallet_list_error; }
    bool acceptingOperations() const { return !m_shutting_down && m_initialized && !walletLoadInProgress() && !m_admitting_command && !m_publishing_command; }

    struct Services {
        // One original controller admission domain, shared by its wallet openings.
        // The supplied shutdown callback drains this controller's accepted work.
        std::shared_ptr<execution::Scheduler> scheduler;
        presentation::Post post; // Enqueue on GUI; never invoke a Native observer inline.
        std::function<void(std::function<void()>)> shutdown;
    };
    explicit WalletQmlController(interfaces::Node& node, QObject *parent = nullptr);
    WalletQmlController(interfaces::Node& node, Services services, QObject* parent = nullptr);
    WalletQmlController(interfaces::Node& node, execution::Options limits, QObject* parent = nullptr);
    WalletQmlController(interfaces::Node& node, walletopening::Session::Preferences preferences, QObject* parent = nullptr);
    WalletQmlController(interfaces::Node& node, walletopening::Session::Preferences preferences,
                        walletopening::FeePolicy fee_policy, QObject* parent = nullptr);
    WalletQmlController(interfaces::Node& node, walletopening::Session::Preferences preferences,
                        walletopening::FeePolicy fee_policy, Services services, QObject* parent = nullptr);
    ~WalletQmlController();
    void setPsbtPolicyRead(psbtpolicy::Read read) { m_psbt_policy = std::move(read); }

    Q_INVOKABLE void setSelectedWallet(QString path, QString wallet_format = QString());
    Q_INVOKABLE bool isWalletOpen(const QString& path);
    Q_INVOKABLE void closeWallet(const QString& path);
    Q_INVOKABLE bool selectWallet(WalletQmlModel* wallet);
    Q_INVOKABLE WalletQmlModel* walletForName(const QString& name) const;
    Q_INVOKABLE void requestCloseWallet(WalletQmlModel* wallet);
    WalletQmlModel* closeTarget() const { return m_close_target; }
    Q_INVOKABLE void clearWalletLoadStatus();
    WalletOperationModel* loadFailure() const { return m_load_failure; }
    //! Forgets the shown load failure.
    Q_INVOKABLE void dismissLoadFailure();
    Q_INVOKABLE void requestOpenWalletSettings(WalletQmlModel* wallet);
    Q_INVOKABLE void requestOpenReceive(WalletQmlModel* wallet);
    Q_INVOKABLE void requestClosePaymentRequestDetail(WalletQmlModel* wallet);

    std::shared_ptr<execution::Scheduler> backgroundScheduler() const { return m_services.scheduler; }
    WalletQmlModel* selectedWallet() const;
    QAbstractItemModel* openWalletModel() const { return m_shutting_down ? nullptr : m_registry->openWalletModel(); }
    Q_INVOKABLE int rowForWallet(WalletQmlModel* wallet) const {
        // A paired Qt row removal follows selection publication. Its old row
        // must not expose a facade after the registry committed its retirement.
        if (!m_registry->containsExact(wallet)) return -1;
        auto* rows = m_registry->openWalletModel();
        return rows ? rows->rowForWallet(wallet) : -1;
    }
    // C++ test/setup compatibility only. Production binds openWalletModel.
    QList<QObject*> openWallets() const;
    void unloadWallets();
    void beginShutdown();
    void close();
    void setRetirementPostForTesting(std::function<void(std::function<void()>)> post);
    bool initialized() const { return m_initialized; }
    bool isWalletLoaded() const { return m_is_wallet_loaded; }
    void setWalletLoaded(bool loaded);
    bool noWalletsFound() const { return m_no_wallets_found; }
    void setNoWalletsFound(bool no_wallets_found);
    bool walletLoadInProgress() const { return m_wallet_load_in_progress || m_creation_admission->creationPending(); }

Q_SIGNALS:
    void walletsDrained();
    void walletOperationChanged();
    void selectedWalletChanged();
    void closeTargetChanged();
    void openWalletsChanged();
    void initializedChanged();
    void isWalletLoadedChanged();
    void noWalletsFoundChanged();
    void walletLoadStateChanged(const QString& name,
                                WalletListModel::LoadState state,
                                const QString& error);
    void walletLoadInProgressChanged();
    void walletLoadErrorChanged();
    void loadFailureChanged();
    void openingChanged();
    void walletLoadWarningsChanged();
    void walletLoadWarningsOffered(WalletOperationModel* operation);
    void walletInfoChanged(const QString& name,
                           qint64 balance,
                           int keySchemeKind,
                           bool balanceKnown);
    // Each carries the wallet that was opened, or null if it could not be.
    void walletLoadSucceeded(WalletQmlModel* wallet);
    void openWalletSettingsRequested(WalletQmlModel* wallet);
    void openReceiveRequested(WalletQmlModel* wallet);
    void closePaymentRequestDetailRequested(WalletQmlModel* wallet);

public Q_SLOTS:
    void initialize();
    void publishOpenWalletsInfo();

private:
    friend class WalletQmlControllerTests;
    size_t adoptionFenceCount() const;
    QString currentCommandErrorDescription() const;
    struct CommandBackend {
        std::unique_ptr<interfaces::Wallet> wallet;
        std::shared_ptr<WalletRegistry::ResultLifetime> lifetime;
        ~CommandBackend() { wallet.reset(); lifetime->release(); }
    };
    struct CommandResult {
        // Metadata can outlive the worker-owned wallet.
        std::weak_ptr<CommandBackend> backend;
        bool has_wallet{false};
        bool open_attempted{false};
        bool reconcile_open{false};
        uint64_t notification_fence{0};
        QString error, warnings, name, key_scheme, reference;
        bool needs_migration{false}, needs_passphrase{false}, effect_succeeded{false}, incomplete{false};
    };
    static void describeCommandWallet(CommandResult& result, interfaces::Wallet& wallet, const QString& fallback, const QString& directory);
    struct CommandReceipt {
        std::mutex mutex;
        bool ready{false};
        WalletOperationState state{WalletOperationState::WalletOperationPending};
        // Native state/error remain immutable facts. An Open intent can be
        // fulfilled by its independently captured exact load notification.
        std::optional<WalletOperationState> open_intent_state;
        QString name, error_description, error_display, open_intent_warning;
        bool error_needs_passphrase{false};
    };
    using Command = execution::OwnedCommand<CommandResult>;
    bool submitWalletCommand(std::function<CommandResult(std::unique_ptr<interfaces::Wallet>&)> effect,
        std::function<void(const CommandResult&)> done, std::function<void(std::exception_ptr)> failed,
        bool open_intent = false);
    void publishWalletCommand(quint64 id);
    void commitWalletCommandOutcome();
    void offerWalletLoadWarnings(const QString& wallet, const QString& warnings);
    WalletQmlModel* reconciledOpenWallet(const CommandResult& result) const;
    void recordCommandWallet(WalletQmlModel* wallet);
    struct OpenIntentFacts { bool native_failed{false}; walletopening::Opening observed; std::weak_ptr<walletopening::Session> owner; };
    struct OpenIntentResolution { WalletOperationState state; walletopening::Opening opening; };
    static OpenIntentResolution resolveOpenIntent(const OpenIntentFacts& facts);
    using OpeningAdoption = WalletRegistry::OpeningAdoption;
    using AdoptionOrigin = WalletRegistry::AdoptionOrigin;
    WalletAdoptionState m_adoption_state{WalletAdoptionState::AdoptionNone};
    QString m_adoption_error;
    std::shared_ptr<AdoptionOrigin> newAdoptionOrigin();
    void updateNotificationContext();
    void closeOpeningAdoption(WalletQmlModel* wallet);
    void adoptionUnavailable(bool closed);
    void flushDeferredPublications();
    quint64 m_deferred_command_publication{0};
    bool m_deferred_notification_publication{false};
    std::shared_ptr<WalletRegistry> m_registry;
    const std::shared_ptr<std::atomic_bool> m_shutdown_gate{std::make_shared<std::atomic_bool>(false)};
    std::shared_ptr<std::atomic_bool> m_names_ready{std::make_shared<std::atomic_bool>(false)};
    std::shared_ptr<Command> m_command;
    std::shared_ptr<CommandBackend> m_command_backend;
    bool m_command_adopted{false};
    bool m_command_is_open{false};
    std::shared_ptr<execution::OwnedCommand<OpenIntentResolution>> m_open_resolution;
    QPointer<WalletQmlModel> m_open_resolution_wallet;
    bool m_open_resolution_rejected{false};
    std::shared_ptr<std::shared_ptr<Command>> m_command_owner;
    std::shared_ptr<CommandReceipt> m_command_receipt;
    std::function<void(const CommandResult&)> m_command_done;
    std::function<void(std::exception_ptr)> m_command_failed;
    quint64 m_command_id{0}, m_publishing_command{0}, m_command_selection_revision{0};
    bool m_complete_load_publication{false};
    bool commandPublicationCurrent() const { return !m_publishing_command || m_publishing_command == m_command_id; }
    QString m_command_requested_target;
    WalletOperationState m_command_state{WalletOperationState::WalletOperationIdle};
    enum class WalletLoadAction {
        Load,
        Create,
    };

    void consumeWalletNotifications();
    void finishInitialization();
    QString walletDiscoveryError() const;
    void handleLifecyclePublication(walletlife::Update update);
    WalletQmlModel* adoptWalletModel(walletpublication::TakeWallet take, const std::optional<QString>& captured_name = {}, std::shared_ptr<AdoptionOrigin> origin = {}, uint64_t ingress_sequence = 0, walletopening::NativeEvents observation = {});
    bool registerWalletModel(WalletQmlModel* wallet_model, std::shared_ptr<OpeningAdoption> adoption = {});
    void removeWalletModel(WalletQmlModel* wallet_model, bool remove = false);
    bool adoptRetirement(WalletQmlModel* model, bool remove);
    void retireWallet(WalletQmlModel* wallet_model, bool remove);
    void checkShutdownFinished();
    std::shared_ptr<walletcreation::Admission> m_creation_admission{std::make_shared<walletcreation::Admission>()};
    void startWalletLoad(const QString& path, const QString& wallet_format = QString());
    void setWalletLoadInProgress(bool in_progress);
    void setWalletLoadError(const QString& error);
    void setWalletLoadWarnings(const QString& warnings);
    void maybeFinishShutdown();
    void publishWalletInfo(WalletQmlModel* wallet_model);
    void subscribeWalletInfo(WalletQmlModel* wallet_model);
    void initializeWalletInfo(WalletQmlModel* wallet_model);
    void publishWalletInfoTurn(quint64 generation);
    quint64 m_info_generation{0};
    std::optional<WalletQmlModel*> m_info_cursor;

    backup::Services backupServices() const;
    std::shared_ptr<backup::InstallCoordinator> m_backup_installs{std::make_shared<backup::InstallCoordinator>()};
    bool m_admitting_command{false};
    quint64 m_admission_selection_revision{0};
    bool m_initialized{false};
    bool m_initializing{false};
    bool m_initialization_failed{false};
    bool m_projecting_notifications{false};
    bool m_shutting_down{false};
    bool m_shutdown_started{false};
    bool m_controller_drained{false};
    bool m_notifications_drained{false};
    bool m_shutdown_complete{false};
    interfaces::Node& m_node;
    walletopening::Session::Preferences m_preferences;
    walletopening::FeePolicy m_fee_policy;
    psbtpolicy::Read m_psbt_policy;
    psbtfee::Services psbtServices() const { psbtfee::Services services; services.policy = m_psbt_policy; return services; }
    Services m_services;
    WalletQmlModel* m_empty_wallet;
    WalletSelectionModel* m_selection{nullptr};
    WalletNamesController* m_names{nullptr};
    WalletCreationController* m_creations{nullptr};
    QPointer<WalletQmlModel> m_result_wallet;
    QMetaObject::Connection m_result_available_connection, m_result_destroyed_connection;
    WalletQmlModel* m_close_target{nullptr};
    std::shared_ptr<walletlife::LoadSubscription> m_load_subscription;
    QPointer<WalletOperationModel> m_load_failure;
    QPointer<WalletOpenOperation> m_opening, m_command_opening, m_previous_command_opening;
    QString m_wallet_list_error;
    WalletAdoptionState walletOperationAdoptionState() const;
    QString walletOperationAdoptionError() const;
    WalletQmlModel* walletOperationResultWallet() const;
    bool canOpenResult() const;
    WalletOperationState walletOperationState() const;
    QString walletOperationName() const;
    QString walletLoadError() const { return m_shutting_down ? QString{} : m_wallet_load_error; }
    QString walletLoadWarnings() const { return m_shutting_down ? QString{} : m_wallet_load_warnings; }
    void startOpening(quint64 id);
    void refuseOpening(const QString& target, const QString& error);
    void setOpening(WalletOpenOperation* opening);
    void setWalletListError(const QString& error);
    //! The open of \p target failed with \p description; QML shows it.
    void reportLoadFailure(const QString& target, quint64 operation, const QString& description);
    std::shared_ptr<QObject> m_notification_bridge;
    bool m_is_wallet_loaded{false};
    bool m_no_wallets_found{false};
    bool m_wallet_load_in_progress{false};
    QString m_wallet_load_error;
    QString m_wallet_load_warnings;
    bool m_registry_drained{false};

    // Suppress the global wallet-loaded notification while a multi-step create
    // flow (watch-only) finishes setup. The worker result publishes the wallet
    // through adoptWalletModel() after setup completes.
    QString m_deferred_wallet_name;

};

#endif // BITCOIN_QML_WALLETQMLCONTROLLER_H
