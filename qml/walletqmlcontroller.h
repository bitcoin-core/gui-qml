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
#include <qml/backendexecutor.h>
#include <qml/secretinput.h>
#include <qml/guipresentation.h>
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

class WalletImportErrorLines final : public QAbstractListModel
{
public:
    explicit WalletImportErrorLines(QObject* parent) : QAbstractListModel(parent) {}
    int rowCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : int((m_text.size() + 1023) / 1024); }
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override { return index.isValid() && index.row() >= 0 && index.row() < rowCount() && role == Qt::UserRole ? QVariant{m_text.mid(qsizetype(index.row()) * 1024, 1024)} : QVariant{}; }
    QHash<int, QByteArray> roleNames() const override { return {{Qt::UserRole, "content"}}; }
    void setValue(QString text, std::shared_ptr<const void> lifetime, bool quiet = false);
private:
    std::shared_ptr<const void> m_lifetime;
    QString m_text;
    quint64 m_revision{0};
    bool m_resetting{false};
};

class WalletQmlController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(WalletOperationModel* creationOperation READ creationOperation NOTIFY walletOperationChanged)
    Q_PROPERTY(bool acceptingOperations READ acceptingOperations NOTIFY walletLoadInProgressChanged)
    Q_PROPERTY(WalletQmlModel* selectedWallet READ selectedWallet NOTIFY selectedWalletChanged)
    Q_PROPERTY(WalletSelectionModel* selection READ selection CONSTANT)
    Q_PROPERTY(WalletQmlModel* closeTarget READ closeTarget NOTIFY closeTargetChanged)
    Q_PROPERTY(QAbstractItemModel* openWalletModel READ openWalletModel NOTIFY openWalletsChanged)
    Q_PROPERTY(bool initialized READ initialized NOTIFY initializedChanged)
    Q_PROPERTY(bool isWalletLoaded READ isWalletLoaded NOTIFY isWalletLoadedChanged)
    Q_PROPERTY(bool noWalletsFound READ noWalletsFound NOTIFY noWalletsFoundChanged)
    Q_PROPERTY(bool walletLoadInProgress READ walletLoadInProgress NOTIFY walletLoadInProgressChanged)
    Q_PROPERTY(QString walletLoadError READ walletLoadError NOTIFY walletLoadErrorChanged)
    Q_PROPERTY(bool walletLoadHasError READ walletLoadHasError NOTIFY walletLoadErrorChanged)
    Q_PROPERTY(QString walletLoadErrorDisplay READ walletLoadErrorDisplay NOTIFY walletImportErrorChanged)
    Q_PROPERTY(QString walletLoadWarnings READ walletLoadWarnings NOTIFY walletLoadWarningsChanged)
    Q_PROPERTY(QString walletImportErrorTitle READ walletImportErrorTitle NOTIFY walletImportErrorChanged)
    Q_PROPERTY(QString walletImportErrorDescription READ walletImportErrorDescription NOTIFY walletImportErrorChanged)
    Q_PROPERTY(QString walletImportErrorHelpText READ walletImportErrorHelpText NOTIFY walletImportErrorChanged)
    Q_PROPERTY(QAbstractItemModel* walletImportErrorHelpModel READ walletImportErrorHelpModel NOTIFY walletImportErrorChanged)
    Q_PROPERTY(bool walletImportErrorPending READ walletImportErrorPending NOTIFY walletImportErrorChanged)
    Q_PROPERTY(bool walletImportErrorUnavailable READ walletImportErrorUnavailable NOTIFY walletImportErrorChanged)
    Q_PROPERTY(QString walletImportErrorProjectionError READ walletImportErrorProjectionError NOTIFY walletImportErrorChanged)
    Q_PROPERTY(bool walletImportErrorRetryAvailable READ walletImportErrorRetryAvailable NOTIFY walletImportErrorChanged)
    Q_PROPERTY(QString walletCreateError READ walletCreateError NOTIFY walletCreateErrorChanged)
    Q_PROPERTY(bool walletCreateHasError READ walletCreateHasError NOTIFY walletCreateErrorChanged)
    Q_PROPERTY(QString walletCreateErrorDisplay READ walletCreateErrorDisplay NOTIFY walletCreateErrorChanged)
    Q_PROPERTY(QAbstractItemModel* walletCreateErrorHelpModel READ walletCreateErrorHelpModel CONSTANT)
    Q_PROPERTY(bool walletMigrationInProgress READ walletMigrationInProgress NOTIFY walletMigrationInProgressChanged)
    Q_PROPERTY(QString walletMigrationError READ walletMigrationError NOTIFY walletMigrationErrorChanged)
    Q_PROPERTY(QString lastImportedWalletName READ lastImportedWalletName NOTIFY lastImportedWalletInfoChanged)
    Q_PROPERTY(QString lastImportedWalletKeyScheme READ lastImportedWalletKeyScheme NOTIFY lastImportedWalletInfoChanged)
    Q_PROPERTY(SignerDiscoveryModel* signers READ signers NOTIFY externalSignerStatusChanged)
    Q_PROPERTY(bool canCreateExternalSignerWallet READ canCreateExternalSignerWallet NOTIFY externalSignerStatusChanged)
    Q_PROPERTY(bool externalSignerPending READ externalSignerPending NOTIFY externalSignerStatusChanged)
    Q_PROPERTY(QString externalSignerName READ externalSignerName NOTIFY externalSignerStatusChanged)
    Q_PROPERTY(QString externalSignerError READ externalSignerError NOTIFY externalSignerStatusChanged)
    Q_PROPERTY(QString suggestedExternalSignerWalletName READ suggestedExternalSignerWalletName NOTIFY externalSignerStatusChanged)
    Q_PROPERTY(WalletQmlModel* resultWallet READ walletOperationResultWallet NOTIFY walletOperationChanged)
    Q_PROPERTY(bool canOpenResult READ canOpenResult NOTIFY walletOperationChanged)
    Q_PROPERTY(quint64 walletOperationId READ walletOperationId NOTIFY walletOperationChanged)
    Q_PROPERTY(WalletOperationState walletOperationState READ walletOperationState NOTIFY walletOperationChanged)
    Q_PROPERTY(QString walletOperationName READ walletOperationName NOTIFY walletOperationChanged)
    Q_PROPERTY(bool walletDisplayNameWritePending READ walletDisplayNameWritePending NOTIFY walletDisplayNameWriteChanged)
    Q_PROPERTY(bool walletDisplayNameWriteSucceeded READ walletDisplayNameWriteSucceeded NOTIFY walletDisplayNameWriteChanged)
    Q_PROPERTY(QString walletDisplayNameWriteError READ walletDisplayNameWriteError NOTIFY walletDisplayNameWriteChanged)
    Q_PROPERTY(bool walletLocationPending READ walletLocationPending NOTIFY walletLocationOpenErrorChanged)
    Q_PROPERTY(QString walletLocationOpenError READ walletLocationOpenError NOTIFY walletLocationOpenErrorChanged)

public:
    enum class WalletOperationState { WalletOperationIdle, WalletOperationPending, WalletOperationSucceeded, WalletOperationIncomplete, WalletOperationFailed, WalletOperationUncertain, WalletOperationNeedsMigration, WalletOperationNeedsPassphrase, WalletOperationClosed };
    Q_ENUM(WalletOperationState)
    WalletSelectionModel* selection() const { return m_selection; }
    enum class WalletAdoptionState { AdoptionNone, AdoptionPending, AdoptionAvailable, AdoptionClosed, AdoptionUnavailable };
    Q_ENUM(WalletAdoptionState)
    Q_PROPERTY(WalletAdoptionState walletOperationAdoptionState READ walletOperationAdoptionState NOTIFY walletOperationChanged)
    Q_PROPERTY(QString walletOperationAdoptionError READ walletOperationAdoptionError NOTIFY walletOperationChanged)
    WalletAdoptionState walletOperationAdoptionState() const;
    QString walletOperationAdoptionError() const;
    WalletQmlModel* walletOperationResultWallet() const;
    bool canOpenResult() const;
    quint64 walletOperationId() const { return m_creation ? m_creation->sequence() : m_command_id; }
    Q_INVOKABLE WalletCreationModel* newCreation();
    WalletOperationModel* creationOperation() const { return m_creation; }
    bool acceptingOperations() const { return !m_shutting_down && m_initialized && !walletLoadInProgress() && !m_admitting_command && !m_publishing_command; }
    WalletOperationState walletOperationState() const;
    QString walletOperationName() const;
    using OpenLocalPathFn = std::function<bool(const QString&)>;

    struct Services {
        // One original controller admission domain, shared by its wallet openings.
        // An injected graph supplies its own physical shutdown/drain owner.
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
    ~WalletQmlController();
    void setPsbtPolicyRead(psbtpolicy::Read read) { m_psbt_policy = std::move(read); }

    Q_INVOKABLE void setSelectedWallet(QString path, QString wallet_format = QString());
    Q_INVOKABLE bool isWalletOpen(const QString& path);
    Q_INVOKABLE void closeWallet(const QString& path);
    Q_INVOKABLE bool selectWallet(WalletQmlModel* wallet);
    Q_INVOKABLE WalletQmlModel* walletForName(const QString& name) const;
    Q_INVOKABLE void requestCloseWallet(WalletQmlModel* wallet);
    WalletQmlModel* closeTarget() const { return m_close_target; }
    Q_INVOKABLE QString walletDisplayName(const QString& path) const;
    Q_INVOKABLE bool setWalletDisplayName(const QString& path, const QString& display_name);
    Q_INVOKABLE void createSingleSigWallet(const QString &name, const QString &passphrase);
    Q_INVOKABLE bool createExternalSignerWallet(const QString& name);
    Q_INVOKABLE void createWatchOnlyWallet(const QString &name, const QString &xpub);
    Q_INVOKABLE void importWallet(const QString& path);
    Q_INVOKABLE WalletOperationModel* restore(const QUrl& source);
    Q_INVOKABLE WalletOperationModel* restoreLocalFile(const QString& path);
    Q_INVOKABLE WalletOperationModel* migrate(const QString& wallet_id);
    Q_INVOKABLE void clearWalletCreateStatus();
    Q_INVOKABLE void clearWalletLoadStatus();
    Q_INVOKABLE void migrateWallet(const QString& path, const QString& passphrase = QString());
    Q_INVOKABLE void clearWalletMigrationStatus();
    Q_INVOKABLE WalletProvisioningCheck* createProvisioningCheck();
    Q_INVOKABLE QString homePath() const;
    Q_PROPERTY(QUrl homeUrl READ homeUrl NOTIFY walletDisplayNamesChanged)
    QUrl homeUrl() const { return QUrl::fromLocalFile(m_home_path); }
    Q_INVOKABLE bool openWalletLocation(WalletQmlModel* wallet);
    Q_INVOKABLE void clearWalletLocationOpenError();
    Q_INVOKABLE void requestOpenWalletSettings(WalletQmlModel* wallet);
    Q_INVOKABLE void refreshExternalSignerStatus();
    void setExternalSignerServices(execution::SnapshotServices services);
    void setSignerDiscoveryPorts(signerdiscovery::Ports ports);
    SignerDiscoveryModel* signers() const { return m_signers; }
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
    void closeAdmission();
    void setRetirementPostForTesting(std::function<void(std::function<void()>)> post);
    bool initialized() const { return m_initialized; }
    bool isWalletLoaded() const { return m_is_wallet_loaded; }
    void setWalletLoaded(bool loaded);
    bool noWalletsFound() const { return m_no_wallets_found; }
    void setNoWalletsFound(bool no_wallets_found);
    bool walletLoadInProgress() const { return m_wallet_load_in_progress || m_creation_admission->creationPending(); }
    QString walletLoadError() const { return m_shutting_down ? QString{} : m_creation ? m_creation->errorText() : m_import_error_unavailable ? m_import_error_projection_error : m_wallet_load_error; }
    QString walletLoadWarnings() const { return m_shutting_down ? QString{} : m_creation ? m_creation->warningText() : m_wallet_load_warnings; }
    QString walletImportErrorTitle() const;
    QString walletImportErrorDescription() const;
    QString walletImportErrorHelpText() const;
    QAbstractItemModel* walletImportErrorHelpModel() { return m_creation && m_creation->kind() == WalletOperationModel::Restore ? m_creation->errorDetails() : &m_import_error_lines; }
    bool walletImportErrorPending() const { return m_creation && m_creation->kind() == WalletOperationModel::Restore ? false : m_import_error_pending; }
    bool walletImportErrorUnavailable() const { return m_creation && m_creation->kind() == WalletOperationModel::Restore ? false : m_import_error_unavailable; }
    QString walletImportErrorProjectionError() const { return m_creation && m_creation->kind() == WalletOperationModel::Restore ? QString{} : m_import_error_projection_error; }
    bool walletImportErrorRetryAvailable() const { return (!m_import_error_unavailable && !m_import_error_projection_error.isEmpty()) || bool(m_import_error_retry_owner) || m_import_error_retry_fixed.has_value(); }
    Q_INVOKABLE bool retryWalletImportErrorPresentation();
    void setImportErrorServicesForTesting(execution::SnapshotServices services);
    bool walletLoadHasError() const { return !walletLoadError().isEmpty(); }
    bool walletCreateHasError() const { return !walletCreateError().isEmpty(); }
    QString walletLoadErrorDisplay() const { return m_shutting_down ? QString{} : m_creation ? m_creation->error().message : m_import_error_unavailable ? m_import_error_projection_error : m_import_error_display; }
    QString walletCreateError() const { return m_shutting_down ? QString{} : m_creation ? m_creation->errorText() : m_wallet_create_error; }
    QString walletCreateErrorDisplay() const { return m_shutting_down ? QString{} : m_creation ? m_creation->error().message : m_create_error_display; }
    QAbstractItemModel* walletCreateErrorHelpModel() { return m_creation ? m_creation->errorDetails() : &m_create_error_lines; }
    bool walletMigrationInProgress() const { return m_wallet_migration_in_progress || (m_creation && m_creation->kind() == WalletOperationModel::Migrate && m_creation_admission->creationPending()); }
    QString walletMigrationError() const { return m_shutting_down ? QString{} : m_creation && m_creation->kind() == WalletOperationModel::Migrate ? m_creation->errorText() : m_wallet_migration_error; }
    Q_INVOKABLE QAbstractItemModel* captureMigrationErrorDetails(quint64 operation, int slot);
    QString lastImportedWalletName() const { return m_last_imported_wallet_name; }
    QString lastImportedWalletKeyScheme() const { return m_last_imported_wallet_key_scheme; }
    bool canCreateExternalSignerWallet() const { return !m_shutting_down && m_signers && !m_signers->pending() && m_signers->fresh() && !m_signers->uniqueSignerId().isEmpty() && m_signers->error().code == SignerDiscoveryError::None; }
    bool externalSignerPending() const { return m_signers && m_signers->pending(); }
    QString externalSignerName() const { return m_signers ? m_signers->uniqueSignerName() : QString{}; }
    QString externalSignerError() const { return m_signers ? m_signers->error().message : QString{}; }
    QString suggestedExternalSignerWalletName() const { return m_signers ? m_signers->suggestedWalletName() : QStringLiteral("external_signer"); }
    QString walletLocationOpenError() const { return m_wallet_location_open_error; }
    bool walletLocationPending() const { return m_location_pending; }
    bool walletDisplayNameWritePending() const { return m_names_write_pending; }
    bool walletDisplayNameWriteSucceeded() const;
    QString walletDisplayNameWriteError() const;
    void setOpenLocalPathFnForTesting(OpenLocalPathFn fn);

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
    void walletImportErrorChanged();
    void walletLoadErrorNotice(const QString& name, quint64 operation, const QString& description);
    void walletLoadWarningsChanged();
    void walletLoadWarningsOffered(WalletOperationModel* operation);
    void walletCreateErrorChanged();
    void walletInfoChanged(const QString& name,
                           qint64 balance,
                           int keySchemeKind,
                           bool balanceKnown);
    // Each carries the wallet that was opened, or null if it could not be.
    void walletLoadSucceeded(WalletQmlModel* wallet);
    void walletImportSucceeded(WalletQmlModel* wallet);
    void migrationOperationOffered(WalletOperationModel* operation);
    void walletCreateSucceeded(WalletQmlModel* wallet);
    void walletMigrationInProgressChanged();
    void walletMigrationErrorChanged();
    void walletMigrationRequired(const QString& path);
    void walletMigrationPassphraseRequired(const QString& path);
    void walletMigrationSucceeded();
    void walletMigrationFailed();
    void walletMigrationErrorNotice(const QString& name, quint64 operation, bool needsPassphrase, const QString& description);
    void lastImportedWalletInfoChanged();
    void openWalletSettingsRequested(WalletQmlModel* wallet);
    void externalSignerStatusChanged();
    void walletDisplayNamesChanged();
    void walletDisplayNameWriteChanged();
    void openReceiveRequested(WalletQmlModel* wallet);
    void closePaymentRequestDetailRequested(WalletQmlModel* wallet);
    void walletLocationOpenErrorChanged();

public Q_SLOTS:
    void initialize();
    void publishOpenWalletsInfo();

private:
    friend class WalletQmlControllerTests;
    size_t adoptionFenceCount() const;
    using ImportErrorInput = WalletRegistry::ImportErrorInput;
    using ImportErrorFrame = WalletRegistry::ImportErrorFrame;
    struct ImportErrorProjection;
    void prepareImportErrorProjection();
    void publishImportErrorProjection();
    bool captureWalletLoadError(const QString& error);
    QString currentCommandErrorDescription() const;
    struct DisplayAliases { std::map<QString, QString> names; QString home; };
    struct SettingsResult { QString error; bool saved{false}; };
    struct SettingsReceipt { std::mutex mutex; bool ready{false}, saved{false}; QString error; };
    struct LocationResult { QString path, error; };
    void refreshDisplayAliases();
    void publishDisplayAliases();
    void applyDisplayAliases(quint64 generation);
    void publishDisplayNameWrite(quint64 id);
    void publishLocation(quint64 id);
    std::shared_ptr<execution::SnapshotQuery<std::shared_ptr<const DisplayAliases>>> m_names_query;
    std::shared_ptr<const DisplayAliases> m_names;
    std::optional<WalletQmlModel*> m_names_cursor;
    quint64 m_names_generation{0}, m_names_write_id{0}, m_location_id{0};
    std::shared_ptr<execution::OwnedCommand<SettingsResult>> m_names_write;
    std::shared_ptr<SettingsReceipt> m_names_write_receipt;
    bool m_names_write_pending{false};
    QString m_names_write_error;
    std::shared_ptr<execution::OwnedCommand<LocationResult>> m_location_operation;
    QPointer<WalletQmlModel> m_location_wallet;
    bool m_location_pending{false};
    QString m_home_path;
    struct CommandBackend {
        std::shared_ptr<interfaces::Wallet> wallet;
        std::shared_ptr<WalletRegistry::ResultLifetime> lifetime;
        ~CommandBackend() { wallet.reset(); lifetime->release(); }
    };
    struct CommandResult {
        // Only the Core effect uses this transient field. Its wrapper moves the
        // handle into the independently retained backend cell before Domain.
        std::shared_ptr<interfaces::Wallet> wallet;
        std::weak_ptr<CommandBackend> backend;
        bool has_wallet{false};
        bool open_attempted{false};
        bool reconcile_open{false};
        uint64_t notification_fence{0};
        std::shared_ptr<interfaces::Wallet> walletForAdoption() const {
            const auto owner = backend.lock();
            if (!owner || !owner->wallet) return {};
            // Every consumer holds the Native cell lease. Its last GUI release
            // only activates reserved Cleanup, never destroys the adapter.
            return {owner, owner->wallet.get()};
        }
        QString error, warnings, name, key_scheme, reference;
        bool needs_migration{false}, needs_passphrase{false}, effect_succeeded{false}, incomplete{false};
    };
    static void describeCommandWallet(CommandResult& result, const QString& fallback, const QString& directory = {});
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
    bool submitWalletCommand(std::function<CommandResult()> effect,
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
    std::shared_ptr<std::shared_ptr<Command>> m_import_error_retry_owner;
    std::optional<QString> m_import_error_retry_fixed;
    execution::SnapshotServices m_import_error_services;
    std::shared_ptr<ImportErrorInput> m_import_error_input;
    std::shared_ptr<const ImportErrorFrame> m_import_error_frame;
    std::shared_ptr<const ImportErrorProjection> m_import_error_projection;
    std::shared_ptr<execution::SnapshotQuery<std::shared_ptr<const ImportErrorProjection>>> m_import_error_query;
    WalletImportErrorLines m_import_error_lines{this};
    QString m_import_error_title, m_import_error_description, m_import_error_help, m_import_error_projection_error, m_import_error_display;
    quint64 m_import_error_revision{0};
    bool m_import_error_pending{false}, m_import_error_unavailable{false}, m_import_error_visibility_changed{false};
    std::shared_ptr<CommandReceipt> m_command_receipt;
    std::function<void(const CommandResult&)> m_command_done;
    std::function<void(std::exception_ptr)> m_command_failed;
    quint64 m_command_id{0}, m_publishing_command{0}, m_command_selection_revision{0};
    bool m_complete_load_publication{false}, m_complete_migration_publication{false};
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
    WalletQmlModel* adoptWalletModel(std::shared_ptr<interfaces::Wallet> wallet, const std::optional<QString>& captured_name = {}, std::shared_ptr<AdoptionOrigin> origin = {}, uint64_t ingress_sequence = 0, walletopening::NativeEvents observation = {});
    bool registerWalletModel(WalletQmlModel* wallet_model, std::shared_ptr<OpeningAdoption> adoption = {});
    void removeWalletModel(WalletQmlModel* wallet_model, bool remove = false);
    bool adoptRetirement(WalletQmlModel* model, bool remove);
    void retireWallet(WalletQmlModel* wallet_model, bool remove);
    void checkShutdownFinished();
    WalletCreationModel::Services creationServices();
    void trackCreation(WalletOperationModel* operation);
    WalletOperationModel* startCreation(WalletCreationModel::Kind kind, const QString& name, const QString& key, const QString& passphrase);
    std::shared_ptr<walletcreation::Admission> m_creation_admission{std::make_shared<walletcreation::Admission>()};
    QPointer<WalletOperationModel> m_creation;
    const QChar* m_creation_error_data{nullptr};
    const QChar* m_creation_warning_data{nullptr};
    bool m_creation_busy{false};
    WalletOperationModel* startRestoration(walletrestoration::Request request);
    void startWalletImport(const QString& path);
    void startWalletLoad(const QString& path, const QString& wallet_format = QString());
    QString walletDisplayNameKey(const QString& path) const;
    void applyWalletDisplayName(WalletQmlModel* wallet_model) const;
    QString describeImportedWalletKeyScheme(interfaces::Wallet& wallet) const;
    void setWalletCreateError(const QString& error);
    void setWalletLoadInProgress(bool in_progress);
    void setWalletLoadError(const QString& error);
    void setWalletLoadWarnings(const QString& warnings);
    void setWalletMigrationInProgress(bool in_progress);
    quint64 setWalletMigrationError(const QString& error);
    void setLastImportedWalletInfo(const QString& wallet_name, const QString& key_scheme);
    void clearLastImportedWalletInfo();
    void maybeFinishShutdown();
    void publishWalletInfo(WalletQmlModel* wallet_model);
    void subscribeWalletInfo(WalletQmlModel* wallet_model);
    void initializeWalletInfo(WalletQmlModel* wallet_model);
    void publishWalletInfoTurn(quint64 generation);
    quint64 m_info_generation{0};
    std::optional<WalletQmlModel*> m_info_cursor;
    void setWalletLocationOpenError(const QString& error);

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
    WalletQmlModel* m_empty_wallet;
    WalletSelectionModel* m_selection{nullptr};
    QPointer<WalletQmlModel> m_result_wallet;
    QMetaObject::Connection m_result_available_connection, m_result_destroyed_connection;
    WalletQmlModel* m_close_target{nullptr};
    WalletQmlController(interfaces::Node& node, execution::Options limits, Services services, QObject* parent);
    std::shared_ptr<BackendExecutor> m_wallet_command_executor;
    Services m_services;
    std::shared_ptr<walletlife::LoadSubscription> m_load_subscription;
    std::shared_ptr<QObject> m_notification_bridge;
    bool m_is_wallet_loaded{false};
    bool m_no_wallets_found{false};
    bool m_wallet_load_in_progress{false};
    QString m_wallet_load_error;
    QString m_wallet_load_warnings;
    std::shared_ptr<const void> m_create_error_owner;
    QString m_wallet_create_error, m_create_error_display;
    WalletImportErrorLines m_create_error_lines{this};
    bool m_wallet_migration_in_progress{false};
    std::shared_ptr<const void> m_migration_error_owner;
    QString m_wallet_migration_error, m_migration_error_display;
    quint64 m_migration_error_operation{0}, m_migration_error_revision{0};
    bool m_migration_error_needs_passphrase{false};
    WalletImportErrorLines m_migration_required_details{this}, m_migration_passphrase_details{this};
    QString m_last_imported_wallet_name;
    QString m_last_imported_wallet_key_scheme;
    SignerDiscoveryModel* m_signers{nullptr};
    execution::SnapshotServices m_signer_services;
    signerdiscovery::Ports m_signer_ports;
    bool m_registry_drained{false};
    QString m_wallet_location_open_error;
    OpenLocalPathFn m_open_local_path_fn;

    // Suppress the global wallet-loaded notification while a multi-step create
    // flow (watch-only) finishes setup. The worker result publishes the wallet
    // through adoptWalletModel() after setup completes.
    QString m_deferred_wallet_name;

};

#endif // BITCOIN_QML_WALLETQMLCONTROLLER_H
