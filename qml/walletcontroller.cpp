// Copyright (c) 2024-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/guipresentation.h>
#include <qml/walletcontroller.h>
#include <qml/walletidentity.h>
#include <qml/executor.h>
#include <qml/walletlifecycle.h>
#include <qml/retainedsnapshot.h>

#include <qml/models/walletmodel.h>
#include <qml/models/walletselectionmodel.h>
#include <qml/util.h>

#include <common/args.h>
#include <common/settings.h>
#include <interfaces/node.h>
#include <key_io.h>
#include <script/descriptor.h>
#include <support/allocators/secure.h>
#include <univalue.h>
#include <util/result.h>
#include <util/fs.h>
#include <util/time.h>
#include <wallet/walletutil.h>
#include <wallet/imports.h>
#include <wallet/wallet.h>
#include <util/translation.h>
#include <util/threadnames.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <mutex>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <deque>
#include <limits>
#include <initializer_list>

#include <QDir>
#include <QDesktopServices>
#include <QFileInfo>
#include <QMetaObject>
#include <QQmlEngine>
#include <QRegularExpression>
#include <QSettings>
#include <QScopeGuard>
#include <QStringList>
#include <QTimer>
#include <QUrl>

namespace {
bool IsBackupLikeFile(const QFileInfo& file_info)
{
    QString file_name = file_info.fileName().toLower();
    return file_name.endsWith(".bak") || file_name.endsWith(".legacy.bak");
}

struct ImportErrorText { QString title, description, help; };
ImportErrorText PrepareImportError(const QString& raw)
{
    const QString error = raw.trimmed();
    if (error.isEmpty()) return {};
    const auto contains = [&](const char* needle) { return error.contains(QLatin1String(needle), Qt::CaseInsensitive); };
    if (contains("legacy wallet") || contains("migrat")) return {
        WalletController::tr("This wallet needs to be migrated"),
        WalletController::tr("The selected backup appears to come from a legacy wallet format. It needs to be migrated before it can be used here."),
        WalletController::tr("If this is a legacy wallet backup, migrate it with the wallet migration tool before importing it here.")};
    if (contains("does not exist")) return {WalletController::tr("We couldn't find that file"),
        WalletController::tr("The selected file is no longer available at that location. Choose the backup file again and retry."), error};
    if (contains("already exists")) return {WalletController::tr("A wallet with this name already exists"),
        WalletController::tr("Importing this backup would create a wallet that already exists in your wallet directory. Remove or rename the existing wallet first, then try again."), error};
    if (contains("verification failed") || contains("not supported")) return {WalletController::tr("This wallet type is not supported"),
        WalletController::tr("The file looks like a wallet backup, but this application could not verify or open it in a supported format."), error};
    return {WalletController::tr("This wallet couldn't be imported"),
        WalletController::tr("The selected file could not be restored as a wallet. Check that it is a valid wallet backup, then try another file."), error};
}
QString NormalizeWalletPath(const QString& path)
{
    if (path.isEmpty()) {
        return {};
    }

    const QUrl url(path);
    QString normalized = url.isLocalFile() ? url.toLocalFile() : path;
    return QFileInfo(normalized).absoluteFilePath();
}

//! The database file of every wallet in the wallet folder and of every loaded
//! wallet. A wallet folder holds wallet.dat; a legacy wallet is the file itself.
std::vector<std::string> WalletDatabaseFiles(interfaces::Node& node)
{
    auto& loader = node.walletLoader();
    const fs::path directory{fs::PathFromString(loader.getWalletDir())};
    std::vector<std::string> files;
    const auto add = [&](const std::string& name) {
        // The wallet folder is absolute; an absolute name replaces it.
        const fs::path wallet{directory / fs::PathFromString(name)};
        std::error_code error;
        files.push_back(fs::PathToString(fs::is_directory(wallet, error) ? wallet / "wallet.dat" : wallet));
    };
    for (const auto& [name, format] : loader.listWalletDir()) add(name);
    for (const auto& wallet : loader.getWallets()) add(wallet->getWalletName());
    return files;
}

QString InferWalletLoadTarget(interfaces::Node& node, const QString& normalized_path)
{
    const QFileInfo selected_path(normalized_path);
    if (!selected_path.exists()) {
        return {};
    }

    const QString wallet_dir_path = QDir::cleanPath(
        QFileInfo(QString::fromStdString(node.walletLoader().getWalletDir())).absoluteFilePath());
    const QDir wallet_dir(wallet_dir_path);

    auto walletTargetFromDir = [&wallet_dir, &wallet_dir_path](const QString& dir_path) {
        QString clean_dir_path = QDir::cleanPath(dir_path);
        QString relative_path = wallet_dir.relativeFilePath(clean_dir_path);
        // Wallet loader accepts wallet names relative to -walletdir. Use those
        // when possible so nested wallets resolve to "subdir/name". For the
        // top-level wallet directory, keep the absolute path so it remains a
        // valid load target instead of colliding with the "restore" branch.
        if (!relative_path.startsWith("..") && relative_path != ".") {
            return relative_path;
        }
        if (clean_dir_path == wallet_dir_path) {
            return clean_dir_path;
        }
        return clean_dir_path;
    };

    if (selected_path.isDir()) {
        return walletTargetFromDir(selected_path.absoluteFilePath());
    }

    if (!selected_path.isFile() || IsBackupLikeFile(selected_path)) {
        return {};
    }

    QString file_name = selected_path.fileName();
    const QString parent_dir_path = QDir::cleanPath(selected_path.dir().absolutePath());

    if (file_name.compare("wallet.dat", Qt::CaseInsensitive) == 0) {
        // A wallet.dat selected inside wallet storage should be loaded via its
        // containing wallet directory. The file itself may also just be a
        // copied backup, so only treat it as loadable when it already lives
        // inside the configured wallet directory.
        const QString relative_parent = wallet_dir.relativeFilePath(parent_dir_path);
        if (!relative_parent.startsWith("..") || parent_dir_path == wallet_dir_path) {
            return walletTargetFromDir(parent_dir_path);
        }
        return {};
    }

    // Current Bitcoin Core still supports top-level data files in -walletdir
    // for backwards compatibility. Those should be loaded by file name.
    if (parent_dir_path == wallet_dir_path) {
        return file_name;
    }

    return {};
}

QString ResolveManagedWalletReference(interfaces::Node& node, const QString& path, QString* wallet_format = nullptr)
{
    if (wallet_format) {
        wallet_format->clear();
    }

    const QString candidate = QDir::cleanPath(path);
    const auto wallet_dir_entries = node.walletLoader().listWalletDir();
    // Catalog identities are exact, including the unnamed default wallet.
    // Only non-catalog filesystem input is normalized below.
    for (const auto& [wallet_path, format] : wallet_dir_entries) {
        if (QString::fromStdString(wallet_path) != path) continue;
        if (wallet_format) *wallet_format = QString::fromStdString(format);
        return path;
    }
    if (path.isEmpty()) return {};

    // Wallet selector entries come from listWalletDir(), which reports wallet
    // names relative to -walletdir. Accept those names directly before trying
    // to interpret the value as a filesystem path.
    for (const auto& [wallet_path, format] : wallet_dir_entries) {
        const QString listed_wallet = QDir::cleanPath(QString::fromStdString(wallet_path));
        if (listed_wallet == candidate) {
            if (wallet_format) {
                *wallet_format = QString::fromStdString(format);
            }
            return listed_wallet;
        }
    }

    const QString normalized_path = NormalizeWalletPath(path);
    if (normalized_path.isEmpty() || !QFileInfo::exists(normalized_path)) {
        return {};
    }

    QString load_target = InferWalletLoadTarget(node, normalized_path);
    if (load_target.isEmpty()) {
        return {};
    }

    const QString clean_load_target = QDir::cleanPath(load_target);
    for (const auto& [wallet_path, format] : wallet_dir_entries) {
        const QString listed_wallet = QDir::cleanPath(QString::fromStdString(wallet_path));
        if (listed_wallet == clean_load_target) {
            if (wallet_format) {
                *wallet_format = QString::fromStdString(format);
            }
            break;
        }
    }

    return load_target;
}



} // namespace





void WalletController::describeCommandWallet(CommandResult& result, interfaces::Wallet& wallet,
                                               const QString& fallback, const QString& directory)
{
    result.name = fallback;
    try {
        result.name = walletidentity::Canonical(QString::fromStdString(wallet.getWalletName()), directory);
        result.key_scheme = WalletModel::keySchemeDisplayText(WalletModel::keySchemeForWallet(wallet));
    } catch (...) {
        result.incomplete = true;
        result.error = tr("The wallet operation completed, but wallet metadata could not be read. Check the wallet directory before retrying.");
    }
}

WalletController::WalletOperationState WalletController::walletOperationState() const
{
    if (m_shutting_down && m_command_receipt) { std::lock_guard lock{m_command_receipt->mutex}; if (m_command_receipt->ready) return m_command_receipt->open_intent_state.value_or(m_command_receipt->state); }
    return m_command_state;
}
QString WalletController::walletOperationName() const
{
    if (!m_command_receipt) return {};
    std::lock_guard lock{m_command_receipt->mutex}; return m_command_receipt->name;
}
execution::SubmitResult WalletController::postNative(std::shared_ptr<execution::Task> task) const
{
    if (m_shutting_down) return {{}, execution::Rejection::Closed};
    if (!task) return {{}, execution::Rejection::Invalid};
    return commandServices().post(execution::Lane::Core, std::move(task));
}

walletcreation::Services WalletController::commandServices() const
{
    walletcreation::Services services;
    services.core = m_services.catalog_queues.core;
    services.outcomes = m_services.catalog_queues.outcomes;
    services.value_cleanup = m_registry->valueCleanup();
    return services;
}

bool WalletController::submitWalletCommand(std::function<CommandResult(std::unique_ptr<interfaces::Wallet>&)> effect,
    std::function<void(const CommandResult&)> done, std::function<void(std::exception_ptr)> failed, bool open_intent)
{
    if (m_shutting_down || m_publishing_command || m_creation_admission->creationPending()) return false;
    m_creations->supersede(); m_creation_admission->setLegacyBusy(true);
    const QPointer<WalletController> observer{this};
    const auto id = m_creation_admission->allocate();
    if (!id) { m_creation_admission->setLegacyBusy(false); return false; }
    m_command_id = id;
    startOpening(id);
    m_command_is_open = open_intent;
    m_command_selection_revision = m_admission_selection_revision;
    disconnect(m_result_available_connection); disconnect(m_result_destroyed_connection);
    m_result_wallet.clear();
    m_command_adopted = false;
    m_open_resolution.reset(); m_open_resolution_wallet.clear(); m_open_resolution_rejected = false;
    const auto services = commandServices();
    const auto post = m_services.post;
    auto origin = newAdoptionOrigin();
    if (!observer || observer->m_shutting_down || id != observer->m_command_id) return false;
    const auto result_lifetime = std::make_shared<WalletRegistry::ResultLifetime>();
    if (origin) origin->result_lifetime = result_lifetime;
    m_registry->setCommandOrigin(std::move(origin));
    m_adoption_state = WalletAdoptionState::AdoptionPending;
    m_adoption_error.clear();
    updateNotificationContext();
    if (!observer || observer->m_shutting_down || id != observer->m_command_id) return false;
    auto receipt = std::make_shared<CommandReceipt>(); receipt->name = m_command_requested_target; m_command_receipt = receipt;
    auto weak = std::make_shared<std::weak_ptr<Command>>();
    // The actual effect owns its backend cell through receipt capture,
    // including observer loss. Retained metadata owns no backend after that.
    auto completion_backend = std::make_shared<std::shared_ptr<CommandBackend>>();
    auto command = std::make_shared<Command>([weak, receipt, observer, id, post, completion_backend] {
        if (const auto command = weak->lock()) if (const auto result = command->result()) {
            auto description = result->value ? PrepareImportError(result->value->error).description : QString{};
            if (description.isEmpty()) description = WalletController::tr("The selected file could not be restored as a wallet. Check that it is a valid wallet backup, then try another file.");
            const auto display = result->value ? result->value->error.left(1024) : QString{};
            QString open_warning;
            if (result->value && result->value->open_attempted && !result->value->has_wallet) {
                open_warning = display;
                if (!result->value->warnings.isEmpty()) {
                    if (!open_warning.isEmpty()) open_warning += QLatin1Char('\n');
                    open_warning += result->value->warnings.left(1024);
                }
                open_warning = open_warning.left(1024);
            }
            const bool passphrase = result->value && result->value->error.contains(QLatin1String("passphrase"), Qt::CaseInsensitive);
            std::lock_guard lock{receipt->mutex}; receipt->ready = true; receipt->error_description = std::move(description);
            receipt->error_display = display; receipt->error_needs_passphrase = passphrase;
            receipt->open_intent_warning = std::move(open_warning);
            if (result->error || !result->value) {
                const bool not_started{execution::NotStartedCause(result->error)};
                receipt->state = not_started ? WalletOperationState::WalletOperationFailed : WalletOperationState::WalletOperationUncertain;
                receipt->error_description = WalletController::tr("The wallet may not have opened. Check the wallet directory before trying again.");
            }
            else {
                const auto& value = *result->value;
                if (!value.name.isEmpty()) receipt->name = value.name; else if (!value.reference.isEmpty()) receipt->name = value.reference;
                receipt->state = value.incomplete ? WalletOperationState::WalletOperationIncomplete : value.needs_migration ? WalletOperationState::WalletOperationNeedsMigration
                    : value.needs_passphrase ? WalletOperationState::WalletOperationNeedsPassphrase
                    : value.has_wallet || value.effect_succeeded ? WalletOperationState::WalletOperationSucceeded : WalletOperationState::WalletOperationFailed;
            }
        }
        completion_backend->reset();
        post([observer, id] { if (observer) observer->publishWalletCommand(id); });
    });
    *weak = command;
    auto owner = services.retain<std::shared_ptr<Command>>([&] { return std::make_unique<std::shared_ptr<Command>>(command); });
    const auto backend = services.retain<CommandBackend>([&] {
        auto value = std::make_unique<CommandBackend>(); value->lifetime = result_lifetime; return value;
    });
    *completion_backend = backend;
    if (!backend) result_lifetime->release(); // No Native handle/effect was acquired.
    if (!observer || observer->m_shutting_down || id != observer->m_command_id) return false;
    m_command.reset(); m_command_owner.reset(); m_command_backend.reset();
    if (!owner || !backend || !m_registry->commandOrigin()) {
        m_adoption_state = WalletAdoptionState::AdoptionUnavailable;
        m_adoption_error = tr("The wallet operation could not be started. Try again after pending operations finish.");
        m_command_state = WalletOperationState::WalletOperationFailed;
        failed(std::make_exception_ptr(execution::NotStarted{execution::NotStarted::Cause::Transport}));
        if (observer) Q_EMIT walletOperationChanged();
        return false;
    }
    m_command = command; m_command_owner = std::move(owner); m_command_backend = backend;
    m_command_done = std::move(done); m_command_failed = std::move(failed);
    m_command_state = WalletOperationState::WalletOperationPending;
    auto work = [effect = std::move(effect), backend, subscription = m_load_subscription] {
        std::unique_ptr<interfaces::Wallet> wallet;
        auto result = effect(wallet);
        result.has_wallet = bool(wallet);
        backend->wallet = std::move(wallet);
        result.backend = backend;
        result.reconcile_open = result.open_attempted && !result.has_wallet && !result.incomplete && !result.needs_migration && !result.needs_passphrase;
        if (result.open_attempted && subscription) result.notification_fence = subscription->notificationFence();
        return result;
    };
    const auto submitted = services.start(*command, std::move(work));
    if (!observer || observer->m_shutting_down || id != observer->m_command_id) return bool(submitted);
    if (!submitted) {
        m_command_state = WalletOperationState::WalletOperationFailed;
        m_command_failed(std::make_exception_ptr(execution::NotStarted{execution::NotStarted::Cause::Transport}));
    }
    if (observer) Q_EMIT walletOperationChanged();
    return bool(submitted);
}
void WalletController::publishWalletCommand(quint64 id)
{
    if (m_shutting_down || id != m_command_id || !m_command || m_command_state != WalletOperationState::WalletOperationPending) return;
    if (m_publishing_command || m_registry->adoptionActive() || m_projecting_notifications) {
        m_deferred_command_publication = id;
        return;
    }
    const auto lifetime = m_command_owner;
    const auto result = m_command->result(); if (!result) return;
    const bool failed_open = !result->error && result->value && result->value->reconcile_open;
    if (failed_open && m_load_subscription && m_load_subscription->settlementThrough(result->value->notification_fence) == walletlife::LoadSubscription::Settlement::Pending) {
        m_deferred_command_publication = id;
        return;
    }
    const QPointer<WalletController> observer{this};
    std::optional<OpenIntentResolution> resolution;
    if (failed_open) {
        if (!m_open_resolution) {
            m_open_resolution_wallet = reconciledOpenWallet(*result->value);
            const auto session = m_open_resolution_wallet ? m_open_resolution_wallet->opening() : std::shared_ptr<walletopening::Session>{};
            const OpenIntentFacts facts{true, session ? session->identity() : walletopening::Opening{}, session};
            m_open_resolution = std::make_shared<execution::OwnedCommand<OpenIntentResolution>>(
                [observer, id, post = m_services.post] { post([observer, id] { if (observer) observer->publishWalletCommand(id); }); });
            const auto resolution_owner = m_open_resolution;
            const auto services = commandServices();
            const auto receipt = m_command_receipt;
            execution::SubmitResult submitted{{}, execution::Rejection::Transport};
            try {
                auto work = [facts, receipt] {
                    const auto decision = resolveOpenIntent(facts);
                    { std::lock_guard lock{receipt->mutex}; receipt->open_intent_state = decision.state; }
                    return decision;
                };
                submitted = services.start(*resolution_owner, std::move(work), execution::Lane::Utility);
            } catch (...) { /* Pre-submitted failure has no outcome to await. */ }
            if (!observer || m_shutting_down || id != m_command_id || m_open_resolution != resolution_owner) return;
            m_open_resolution_rejected = !submitted;
            if (submitted) return;
        }
        const auto resolved = m_open_resolution->result();
        if (!resolved && !m_open_resolution_rejected) return;
        if (resolved && resolved->value) resolution = *resolved->value;
    }
    // Keep the public operation Pending through adoption. The immutable Native
    // receipt is already terminal; GUI ownership must be committed separately.
    const auto done = m_command_done;
    const auto failed = m_command_failed;
    const auto previous_publication = std::exchange(m_publishing_command, id);
    const auto previous_origin = m_registry->exchangePublishingOrigin(m_registry->commandOrigin());
    const QPointer<WalletModel> observed{resolution && resolution->state == WalletOperationState::WalletOperationSucceeded ? m_open_resolution_wallet : nullptr};
    if (resolution && resolution->state == WalletOperationState::WalletOperationSucceeded) {
        // The failed Core call remains in the immutable Native receipt. The
        // Open intent is fulfilled by its separately owned, exact notification.
        QString warning;
        { std::lock_guard lock{m_command_receipt->mutex}; warning = m_command_receipt->open_intent_warning; }
        m_adoption_state = WalletAdoptionState::AdoptionAvailable; m_adoption_error.clear();
        recordCommandWallet(observed);
        commitWalletCommandOutcome();
        setWalletLoadError({});
        if (observer && !m_shutting_down && id == m_command_id) setWalletLoadWarnings(warning);
        if (observer && !m_shutting_down && id == m_command_id) setWalletLoadInProgress(false);
        if (observer && !m_shutting_down && id == m_command_id) {
            if (observed && observed->opening()->identity() == resolution->opening && observed == reconciledOpenWallet(*result->value)) {
                offerWalletLoadWarnings(result->value->name, warning);
                if (observer) Q_EMIT walletLoadSucceeded(observed);
            }
            else adoptionUnavailable(true);
        }
    } else if (failed_open && !resolution) {
        commitWalletCommandOutcome();
        m_adoption_state = WalletAdoptionState::AdoptionUnavailable;
        const auto error = tr("Could not check whether the wallet opened. Try again when other wallet actions finish.");
        setWalletLoadInProgress(false);
        if (observer && !m_shutting_down && id == m_command_id) setWalletLoadError(error);
        if (observer && !m_shutting_down && id == m_command_id) reportLoadFailure(walletOperationName(), id, error);
    } else if (result->error || !result->value) { commitWalletCommandOutcome(); failed(result->error); }
    else done(*result->value);
    if (!observer) return;
    if (!m_shutting_down && id == m_command_id) {
        commitWalletCommandOutcome();
        // Metadata/error/warning aliases retain only immutable facts. The
        // separately reserved backend cell can retire after exact adoption.
        bool original_closed{false};
        if (m_command_state == WalletOperationState::WalletOperationSucceeded && result->value && result->value->has_wallet
            && !result->value->incomplete && !result->value->needs_migration && !result->value->needs_passphrase && result->value->error.isEmpty()) {
            const auto origin = m_registry->commandOrigin();
            if (origin) {
                const auto found = origin->bindings.find(result->value->name);
                original_closed = found != origin->bindings.end() && found->second->opening->closed;
            }
        }
        // A consumed exact closed binding has no supported adoption retry.
        // This also applies if close-fence capacity kept its view diagnostic
        // Unavailable: that diagnostic remains unchanged. Live/unknown bindings
        // and failed/incomplete outcomes stay retained for reconciliation.
        // The Native effect has settled before this GUI consumption. An empty
        // cell owns neither a handle nor an adoption-retry resource, even if a
        // notification from that origin already exposed an independent facade.
        const bool empty_backend = m_command_backend && !m_command_backend->wallet;
        if (m_command_adopted || original_closed || empty_backend) m_command_backend.reset();
        m_registry->clearCommandOrigin();
        m_selection->finishSelection(m_command_selection_revision);
        if (!observer) return;
        if (!m_shutting_down) Q_EMIT walletOperationChanged();
    }
    if (!observer) return;
    const bool complete_load = std::exchange(m_complete_load_publication, false);
    m_publishing_command = previous_publication;
    m_registry->exchangePublishingOrigin(previous_origin);
    // The original result, terminal signal and selection completion are owned
    // before an idle notification may synchronously admit a successor command.
    if (!m_shutting_down && id == m_command_id && complete_load) setWalletLoadInProgress(false);
    if (!observer) return;
    if (observer && !m_shutting_down) flushDeferredPublications();
}

void WalletController::commitWalletCommandOutcome()
{
    if (m_shutting_down || !m_publishing_command || m_publishing_command != m_command_id || !m_command_receipt) return;
    std::lock_guard lock{m_command_receipt->mutex};
    m_command_state = m_command_receipt->open_intent_state.value_or(m_command_receipt->state);
}

WalletController::OpenIntentResolution WalletController::resolveOpenIntent(const OpenIntentFacts& facts)
{
    const auto owner = facts.owner.lock();
    return facts.native_failed && facts.observed && owner && owner->identity() == facts.observed && owner->available()
        ? OpenIntentResolution{WalletOperationState::WalletOperationSucceeded, facts.observed}
        : OpenIntentResolution{WalletOperationState::WalletOperationFailed, {}};
}

WalletModel* WalletController::reconciledOpenWallet(const CommandResult& result) const
{
    if (m_shutting_down || !m_initialized || m_initialization_failed || !m_load_subscription
        || m_load_subscription->settlementThrough(result.notification_fence) != walletlife::LoadSubscription::Settlement::Settled) return nullptr;
    const auto origin = m_registry->commandOrigin();
    if (!origin || origin->unavailable) return nullptr;
    const auto found = origin->bindings.find(result.reference);
    if (found == origin->bindings.end()) return nullptr;
    if (!found->second->notification_sequence || found->second->notification_sequence > result.notification_fence) return nullptr;
    const auto& opening = found->second->opening;
    const auto facade = opening->facade;
    if (opening->closed || opening->unloaded->load() || !facade || !facade->available()
        || facade->opening()->identity() != opening->identity || !m_registry->containsExact(facade)) return nullptr;
    return facade;
}

backup::Services WalletController::backupServices() const
{
    backup::Services services;
    services.coordinator = m_backup_installs;
    return services;
}

namespace {
WalletController::Services StandaloneControllerServices()
{
    const auto executor = std::make_shared<execution::Executor>(execution::Executor::Config{});
    WalletController::Services services;
    services.post = presentation::QueuedPost();
    const auto cleanup = std::make_shared<execution::SerialQueue>(executor->makeQueue(execution::Lane::Utility));
    const auto native_cleanup = std::make_shared<execution::SerialQueue>(
        executor->makeQueue(execution::Lane::Utility, execution::UtilityWorker::Cleanup));
    services.make_wallet_services = [executor, cleanup, native_cleanup](bool) {
        WalletModel::Services wallet;
        wallet.core = std::make_shared<execution::SerialQueue>(executor->makeQueue(execution::Lane::Core));
        wallet.outcomes = std::make_shared<execution::SerialQueue>(executor->makeQueue(execution::Lane::Utility));
        wallet.cleanup = cleanup;
        wallet.native_cleanup = native_cleanup;
        return wallet;
    };
    services.executor = executor.get();
    services.owned_executor = executor;
    return services;
}

WalletController::Services ValidateControllerServices(WalletController::Services services)
{
    if (!services.executor && !services.post && !services.shutdown &&
        !services.make_wallet_services && !services.owned_executor && !services.catalog_queues.core && !services.catalog_queues.outcomes) {
        services = StandaloneControllerServices();
    }
    if (!services.executor || !services.post || !services.make_wallet_services || services.shutdown ||
        bool(services.catalog_queues.core) != bool(services.catalog_queues.outcomes))
        throw std::invalid_argument("Incomplete wallet catalog queues");
    if (!services.catalog_queues.core) {
        services.catalog_queues.core = std::make_shared<execution::SerialQueue>(services.executor->makeQueue(execution::Lane::Core));
        services.catalog_queues.outcomes = std::make_shared<execution::SerialQueue>(services.executor->makeQueue(execution::Lane::Utility));
    }
    services.shutdown = [core = services.catalog_queues.core, outcomes = services.catalog_queues.outcomes](auto done) {
        // The registry has drained its retained values, including owners
        // that could still submit a dependent Core stage.
        core->close();
        core->whenDrained([outcomes, done = std::move(done)]() mutable {
            outcomes->close();
            outcomes->whenDrained(std::move(done));
        });
    };
    return services;
}
}

WalletController::WalletController(interfaces::Node& node, Services services, QObject* parent)
    : WalletController(node, {}, {}, std::move(services), parent) {}

WalletController::WalletController(interfaces::Node& node, QObject* parent)
    : WalletController(node, Services{}, parent) {}

WalletController::WalletController(interfaces::Node& node, walletopening::Session::Preferences preferences, QObject* parent)
    : WalletController(node, std::move(preferences), {}, parent) {}

WalletController::WalletController(interfaces::Node& node, walletopening::Session::Preferences preferences,
                                       walletopening::FeePolicy fee_policy, QObject* parent)
    : WalletController(node, std::move(preferences), std::move(fee_policy), Services{}, parent) {}

WalletController::WalletController(interfaces::Node& node, walletopening::Session::Preferences preferences,
                                       walletopening::FeePolicy fee_policy, Services services, QObject* parent)
    : QObject(parent)
    , m_node(node)
    , m_preferences(std::move(preferences))
    , m_fee_policy(std::move(fee_policy))
    , m_services(ValidateControllerServices(std::move(services)))
    , m_empty_wallet(new WalletModel(&node, walletServices(/*detached=*/true), this))
    , m_notification_bridge(presentation::MakeGuiObject())
{
    // Saves run on workers until shutdown drains them; the node outlives that.
    m_backup_installs->wallet_files = [node = &m_node] { return WalletDatabaseFiles(*node); };
    m_services.post = presentation::ReliablePost(std::move(m_services.post));
    auto host = std::make_shared<WalletRegistry::Host>();
    host->owner = this;
    host->stopping = [this] { return m_shutting_down; };
    host->publicationId = [this] { return m_publishing_command; };
    host->publicationCurrent = [this](quint64 operation) { return !operation || operation == m_command_id; };
    host->commitLoaded = [this](bool loaded) { return std::exchange(m_is_wallet_loaded, loaded) != loaded; };
    host->notice = [this](WalletRegistry::Notice notice) {
        switch (notice) {
        case WalletRegistry::Notice::Inventory: Q_EMIT openWalletsChanged(); break;
        case WalletRegistry::Notice::CloseTarget: Q_EMIT closeTargetChanged(); break;
        case WalletRegistry::Notice::Loaded: setWalletLoaded(true); break;
        case WalletRegistry::Notice::Found: setNoWalletsFound(false); break;
        case WalletRegistry::Notice::AdoptionAvailable: m_adoption_state = WalletAdoptionState::AdoptionAvailable; break;
        case WalletRegistry::Notice::PublicationsReleased: flushDeferredPublications(); break;
        case WalletRegistry::Notice::NativeDrained: m_controller_drained = true; m_notifications_drained = true; break;
        case WalletRegistry::Notice::Retired: m_registry_drained = true; maybeFinishShutdown(); break;
        case WalletRegistry::Notice::LoadedChanged: Q_EMIT isWalletLoadedChanged(); break;
        }
    };
    host->adoptionUnavailable = [this](bool closed) { adoptionUnavailable(closed); };
    host->createFacade = [this](walletopening::Source wallet, const QString& name, walletopening::NativeEvents events,
                                std::unique_ptr<interfaces::Handler> handler) {
        return new WalletModel(WalletModel::SharedWallet{std::move(wallet), name, m_preferences, m_fee_policy, m_shutdown_gate, std::move(events), true, std::move(handler), walletServices()}, &m_node, FeeReadServices{}, psbtServices(), backupServices());
    };
    host->information = [this](WalletModel* wallet, WalletRegistry::Information action) {
        switch (action) {
        case WalletRegistry::Information::Subscribe: subscribeWalletInfo(wallet); break;
        case WalletRegistry::Information::ApplyAlias: m_names->apply(wallet); break;
        case WalletRegistry::Information::Initialize: initializeWalletInfo(wallet); break;
        }
    };
    host->loadState = [this](const QString& name, bool open) { Q_EMIT walletLoadStateChanged(name, open ? WalletListModel::LoadState::Open : WalletListModel::LoadState::Closed, QString{}); };
    host->loadError = [this](const QString& error) { setWalletListError(error); };
    host->forgetCloseTarget = [this](WalletModel* wallet) { const bool target = m_close_target == wallet; if (target) m_close_target = nullptr; return target; };
    host->closeQuestionChanged = [this](WalletModel* wallet) {
        if (m_close_target == wallet && !wallet->closeQuestion()) { m_close_target = nullptr; Q_EMIT closeTargetChanged(); }
    };
    host->prepareForget = [this](WalletModel* wallet) { m_selection->prepareForget(wallet); };
    host->forget = [this](WalletModel* wallet, WalletModel* successor) { m_selection->forget(wallet, successor); };
    host->contextChanged = [this] { updateNotificationContext(); };
    WalletNamesController::Services names;
    auto& executor = *m_services.executor;
    auto value_cleanup = std::make_shared<execution::SerialQueue>(executor.makeQueue(execution::Lane::Utility));
    names.utility = std::make_shared<execution::SerialQueue>(executor.makeQueue(execution::Lane::Utility));
    names.outcomes = std::make_shared<execution::SerialQueue>(executor.makeQueue(execution::Lane::Utility));
    auto shutdown = [outcomes = names.outcomes, stop = m_services.shutdown](auto done) {
        outcomes->whenDrained([stop, done = std::move(done)]() mutable { stop(std::move(done)); });
    };
    m_registry = std::make_shared<WalletRegistry>(std::move(host), WalletRegistry::Services{
        m_notification_bridge, m_empty_wallet, m_services.post, std::move(shutdown), std::move(value_cleanup)});
    if (const auto cleanup = m_registry->valueCleanup()) m_empty_wallet->setPsbtValueCleanup(cleanup);
    m_selection = new WalletSelectionModel(m_empty_wallet, [this](WalletModel* wallet) { return m_registry->containsExact(wallet); }, this);
    connect(m_selection, &WalletSelectionModel::selectedWalletChanged, this, [this] {
        const QPointer<WalletController> observer{this};
        updateNotificationContext();
        if (observer) Q_EMIT selectedWalletChanged();
    });
    m_registry->setNotificationOrigin(newAdoptionOrigin());
    names.node = &m_node;
    names.post = m_services.post; names.empty_wallet = m_empty_wallet;
    names.next_wallet = [this](WalletModel* wallet) -> WalletModel* {
        const auto& wallets = m_registry->information();
        const auto next = wallet ? wallets.upper_bound(wallet) : wallets.begin();
        return next == wallets.end() ? nullptr : next->first;
    };
    m_names = new WalletNamesController(std::move(names), this);
    m_names->refresh();
    WalletCreationController::Host creation;
    creation.node = &m_node; creation.registry = m_registry; creation.admission = m_creation_admission;
    creation.names_ready = m_names_ready; creation.shutdown_gate = m_shutdown_gate;
    creation.post = m_services.post;
    creation.catalog_queues = m_services.catalog_queues;
    creation.accepting = [this] { return !m_shutting_down && m_initialized && !walletLoadInProgress() && !m_admitting_command && !m_publishing_command; };
    creation.loading = [this] { return walletLoadInProgress(); };
    creation.ready = [this] { return m_initialized && !m_initialization_failed && !m_shutting_down; };
    creation.unavailable = [this] { return walletDiscoveryError(); };
    creation.context_changed = [this] { updateNotificationContext(); };
    m_creations = new WalletCreationController(std::move(creation), this);
    connect(this, &WalletController::initializedChanged, m_creations, &WalletCreationController::capabilitiesChanged);
    // The current open reports the command's changes as its own.
    const auto opening_changed = [this] { if (m_command_opening) Q_EMIT m_command_opening->changed(); };
    connect(this, &WalletController::walletOperationChanged, this, opening_changed);
    connect(this, &WalletController::walletLoadErrorChanged, this, opening_changed);
    connect(this, &WalletController::walletLoadWarningsChanged, this, opening_changed);
    connect(this, &WalletController::walletLoadInProgressChanged, this, opening_changed);
    connect(this, &WalletController::openWalletsChanged, this, opening_changed);
    connect(this, &WalletController::walletLoadInProgressChanged, m_creations, &WalletCreationController::capabilitiesChanged);
    connect(m_creations, &WalletCreationController::signersDrained, this, &WalletController::maybeFinishShutdown);
    connect(m_creations, &WalletCreationController::busyChanged, this, [this] { if (!m_shutting_down) Q_EMIT walletLoadInProgressChanged(); });
    m_empty_wallet->setNode(&m_node);
    m_notification_bridge->setObjectName(QStringLiteral("wallet-notifications"));
}

WalletController::~WalletController()
{
    blockSignals(true);
    beginShutdown();
    m_registry->detachPresentation();
    m_registry->detachController();
    m_registry.reset();
}

WalletModel::Services WalletController::walletServices(bool detached) const
{
    return m_services.make_wallet_services(detached);
}

void WalletController::setSelectedWallet(QString path, QString wallet_format)
{
    const auto retirement = m_registry->retirement();
    if (retirement->values && retirement->values->retiring_names.contains(path) && !walletForName(path)) {
        QString error = tr("This wallet is still closing.");
        const QPointer<WalletController> observer{this};
        refuseOpening(path, error); if (!observer || m_shutting_down) return;
        reportLoadFailure(path, m_command_id, error); if (!observer || m_shutting_down) return;
        Q_EMIT walletLoadStateChanged(path, WalletListModel::LoadState::LoadError, error);
        return;
    }
    if (!m_initialized) { refuseOpening(path, walletDiscoveryError()); return; }
    if (auto* wallet = walletForName(path)) {
        const QPointer<WalletController> guard{this};
        if (!m_selection->select(wallet)) return;
        if (!guard || m_shutting_down) return;
        clearWalletLoadStatus();
        return;
    }
    if (m_shutting_down || m_admitting_command || m_creations->admitting() || m_publishing_command || m_creation_admission->creationPending() || m_command_state == WalletOperationState::WalletOperationPending) return;
    m_admitting_command = true;
    const QPointer<WalletController> observer{this};
    const auto admission_guard = qScopeGuard([observer] { if (observer) observer->m_admitting_command = false; });
    const auto revision = m_selection->beginSelection(path);
    if (!observer || m_shutting_down) return;
    m_admission_selection_revision = revision;
    updateNotificationContext();
    if (!observer || m_shutting_down) return;
    startWalletLoad(path, wallet_format);
    if (observer && m_command_state != WalletOperationState::WalletOperationPending) m_selection->finishSelection(revision);
}

WalletModel* WalletController::walletForName(const QString& name) const
{ return m_registry->walletForName(name); }

bool WalletController::selectWallet(WalletModel* wallet)
{
    if (m_shutting_down) return false;
    const QPointer<WalletController> observer{this};
    const bool selected = m_selection->select(wallet);
    if (observer && selected) updateNotificationContext();
    return selected;
}

void WalletController::requestCloseWallet(WalletModel* wallet)
{
    if (m_shutting_down || !wallet || !wallet->available() || !m_registry->positions().contains(wallet)) return;
    const QPointer<WalletController> guard{this};
    const QPointer<WalletModel> original{wallet};
    if (m_close_target != wallet) {
        if (m_close_target && m_close_target->closeQuestion()) m_close_target->closeQuestion()->decline();
        if (!guard || !original || !original->available() || m_shutting_down) return;
        m_close_target = wallet;
        Q_EMIT closeTargetChanged();
        if (!guard || !original || m_close_target != original) return;
    }
    if (original && original->available() && m_close_target == original) original->requestClose();
}

bool WalletController::isWalletOpen(const QString& path)
{
    return m_registry->names().contains(path);
}

void WalletController::publishWalletInfo(WalletModel* wallet_model)
{
    const auto found = m_registry->information().find(wallet_model);
    if (found == m_registry->information().end()) return;
    const auto information = found->second;
    Q_EMIT walletInfoChanged(
        information.name,
        wallet_model->displayedBalanceSatoshi(),
        information.key_scheme,
        wallet_model->balanceKnown());
}

void WalletController::initializeWalletInfo(WalletModel* wallet_model)
{
    // Preserve the existing opening-metadata capture/publication point. Its
    // legacy queries belong to the opening migration, not balance callbacks.
    m_registry->rememberInformation(wallet_model, WalletRegistry::WalletInformation{
        wallet_model->name(), static_cast<int>(wallet_model->keySchemeKind())});
    publishWalletInfo(wallet_model);
}

void WalletController::publishOpenWalletsInfo()
{
    if (m_shutting_down) return;
    m_info_cursor.reset(); publishWalletInfoTurn(++m_info_generation);
}
void WalletController::publishWalletInfoTurn(quint64 generation)
{
    if (m_shutting_down || generation != m_info_generation) return;
    const QPointer<WalletController> guard{this};
    for (int budget = 0; budget < 32; ++budget) {
        const auto next = m_info_cursor ? m_registry->information().upper_bound(*m_info_cursor) : m_registry->information().begin();
        if (next == m_registry->information().end()) return;
        auto* wallet = next->first; m_info_cursor = wallet;
        publishWalletInfo(wallet); if (!guard || m_shutting_down || generation != m_info_generation) return;
    }
    QMetaObject::invokeMethod(this, [guard, generation] { if (guard) guard->publishWalletInfoTurn(generation); }, Qt::QueuedConnection);
}

void WalletController::subscribeWalletInfo(WalletModel* wallet_model)
{
    if (!wallet_model) return;
    // Sender is the wallet model, so Qt auto-disconnects when the model is
    // destroyed in closeWallet/unloadWallets.
    QObject::connect(wallet_model, &WalletModel::balanceChanged,
                     this, [this, wallet_model]() {
                         publishWalletInfo(wallet_model);
                     });
    connect(wallet_model, &WalletModel::openingMetadataChanged, this, [this, wallet_model] { initializeWalletInfo(wallet_model); });
}




void WalletController::closeWallet(const QString& path)
{
    if (!m_initialized) {
        setWalletListError(walletDiscoveryError());
        return;
    }

    const QPointer<WalletController> guard{this};
    const auto closing = m_registry->names().find(path);
    if (closing != m_registry->names().end()) closeOpeningAdoption(closing->second);
    if (!guard || m_shutting_down) return;
    clearWalletLoadStatus(); if (!guard || m_shutting_down) return;


    const auto found = m_registry->names().find(path);
    if (found == m_registry->names().end()) return;
    auto* wallet_to_close = found->second;
    removeWalletModel(wallet_to_close, true);
}


WalletModel* WalletController::selectedWallet() const
{ return m_selection->selectedWallet(); }

QList<QObject*> WalletController::openWallets() const
{ return m_registry->openWallets(); }

void WalletController::unloadWallets()
{
    beginShutdown();
}

void WalletController::close()
{
    m_shutdown_gate->store(true); // O(1): fences every original wallet and form owner.
    m_wallet_load_error.clear();
    m_shutting_down = true; m_names_ready->store(false);
    // The compatibility facade lends this one current operation's text model.
    // Close that bounded projection synchronously; the registry still retires
    // every other retained form in its ordinary bounded shutdown batches.
    m_creations->close();
    m_wallet_load_in_progress = false;
    m_command_state = WalletOperationState::WalletOperationClosed;
    m_registry->close();
    m_command.reset(); m_command_owner.reset(); m_command_backend.reset();
    m_open_resolution.reset(); m_open_resolution_wallet.clear();
    m_deferred_command_publication = 0;
    m_deferred_notification_publication = false;
    m_names->close();
    if (m_empty_wallet) m_empty_wallet->close(); // One fixed sentinel, independent of inventory size.
}
void WalletController::setRetirementPostForTesting(std::function<void(std::function<void()>)> post)
{
    if (!m_shutdown_started && post) m_registry->retirement()->post = std::move(post);
}
void WalletController::beginShutdown()
{
    if (std::exchange(m_shutdown_started, true)) return;
    close();
    std::function<void(std::function<void()>)> notification_retirement;
    if (m_load_subscription) m_load_subscription->retire();
    if (m_load_subscription) {
        notification_retirement = [subscription = m_load_subscription](auto done) {
            subscription->whenRetired(std::move(done));
        };
    }
    if (m_load_failure) { m_load_failure->deleteLater(); m_load_failure.clear(); Q_EMIT loadFailureChanged(); }
    const bool loaded_changed = std::exchange(m_is_wallet_loaded, false);
    const bool had_wallets = !m_registry->empty();
    const bool target_changed = m_close_target != nullptr; m_close_target = nullptr;
    m_initialized = false;
    m_empty_wallet->setParent(nullptr);
    m_registry->beginRetirement(std::move(m_services.owned_executor), std::move(notification_retirement));
    const QPointer<WalletController> observer{this};
    m_selection->close(); if (!observer) return;
    m_selection->clear(); if (!observer) return;
    if (target_changed) { Q_EMIT closeTargetChanged(); if (!observer) return; }
    Q_EMIT initializedChanged(); if (!observer) return;
    if (had_wallets) { Q_EMIT openWalletsChanged(); if (!observer) return; }
    if (loaded_changed) Q_EMIT isWalletLoadedChanged();
}
bool WalletController::adoptRetirement(WalletModel* wallet, bool remove)
{ return m_registry->adoptRetirement(wallet, remove); }
void WalletController::retireWallet(WalletModel* model, bool remove)
{
    if (!model || !m_registry->retirement()->values || m_registry->retirement()->values->retiring.contains(model)) return;
    const QPointer<WalletController> observer{this};
    const bool target = adoptRetirement(model, remove);
    model->beginShutdown(remove);
    if (observer && target) Q_EMIT closeTargetChanged();
}
void WalletController::checkShutdownFinished() { if (m_registry->retirement()) m_registry->retirement()->finish(); }

bool WalletController::registerWalletModel(WalletModel* wallet, std::shared_ptr<OpeningAdoption> adoption)
{ return m_registry->registerModel(wallet, std::move(adoption)); }

void WalletController::removeWalletModel(WalletModel* wallet, bool remove)
{ m_registry->removeModel(wallet, remove); }

WalletModel* WalletController::adoptWalletModel(walletpublication::TakeWallet take, const std::optional<QString>& captured_name, std::shared_ptr<AdoptionOrigin> origin, uint64_t ingress_sequence, walletopening::NativeEvents observation)
{
    // Native outcomes and notifications do not express selection intent.
    const QPointer<WalletController> observer{this};
    const auto operation = m_publishing_command;
    auto* adopted = m_registry->adoptWallet(std::move(take), captured_name, std::move(origin), ingress_sequence, std::move(observation));
    if (observer && operation && operation == m_command_id && adopted) recordCommandWallet(adopted);
    return adopted;
}

void WalletController::recordCommandWallet(WalletModel* adopted)
{
    const QPointer<WalletController> observer{this};
    const auto operation = m_publishing_command;
    if (operation && operation == m_command_id && adopted) {
        m_command_adopted = true;
        disconnect(m_result_available_connection); disconnect(m_result_destroyed_connection);
        m_result_wallet = adopted;
        const QPointer<WalletModel> exact{adopted};
        const auto changed = [observer, exact, operation] {
            if (observer && operation == observer->m_command_id && observer->m_result_wallet == exact) Q_EMIT observer->walletOperationChanged();
        };
        m_result_available_connection = connect(adopted, &WalletModel::availableChanged, this, changed);
        m_result_destroyed_connection = connect(adopted, &QObject::destroyed, this, changed);
    }
}





void WalletController::clearWalletLoadStatus()
{
    if (m_creation_admission->creationPending()) return;
    const QPointer<WalletController> guard{this};
    if (m_command_state != WalletOperationState::WalletOperationPending) { setWalletLoadInProgress(false); if (!guard) return; }
    setWalletLoadError({}); if (guard) setWalletLoadWarnings({});
}



void WalletController::requestOpenWalletSettings(WalletModel* wallet)
{
    if (!wallet || !wallet->available() || !m_registry->positions().contains(wallet)) return;
    Q_EMIT openWalletSettingsRequested(wallet);
}

void WalletController::maybeFinishShutdown()
{
    if (m_shutdown_complete || !m_registry_drained || !m_creations->drained()) return;
    m_shutdown_complete = true;
    Q_EMIT walletsDrained();
}




void WalletController::requestOpenReceive(WalletModel* wallet)
{
    if (!wallet || !wallet->available() || !m_registry->positions().contains(wallet)) return;
    Q_EMIT openReceiveRequested(wallet);
}

void WalletController::requestClosePaymentRequestDetail(WalletModel* wallet)
{
    if (!wallet || !wallet->available() || !m_registry->positions().contains(wallet)) return;
    Q_EMIT closePaymentRequestDetailRequested(wallet);
}

QString WalletController::currentCommandErrorDescription() const
{
    if (!m_command_receipt) return {};
    std::lock_guard lock{m_command_receipt->mutex}; return m_command_receipt->error_description;
}



QString WalletController::walletDiscoveryError() const
{
    return m_initialization_failed ? tr("Wallet discovery is unavailable. Restart the app to refresh the wallet list.") :
        tr("Wallets are still loading. Try again in a moment.");
}

void WalletController::consumeWalletNotifications()
{
    if (m_shutting_down || m_initialization_failed || !m_initialized) return;
    if (m_registry->adoptionActive() || m_projecting_notifications) {
        m_deferred_notification_publication = true;
        return;
    }
    const QPointer<WalletController> guard{this};
    m_projecting_notifications = true;
    const auto restore = qScopeGuard([guard] {
        if (!guard) return;
        guard->m_projecting_notifications = false;
        guard->flushDeferredPublications();
    });
    auto handoff = m_load_subscription->takeNotifications();
    if (!handoff) return;
    for (const auto& entry : handoff->entries) {
        // Keep the handoff's shadow until its reserved background disposal.
        // The opaque value is installed only by this controller's context port.
        // Native subscription code retains it without inspecting GUI state.
        auto origin = std::static_pointer_cast<AdoptionOrigin>(entry.adoption_origin);
        if (!origin) {
            setWalletListError(tr("Wallet updates are unavailable. Restart the app to refresh the wallet list."));
            if (guard) m_load_subscription->retire();
            return;
        }
        const auto name = QString::fromStdString(entry.name);
        auto* adopted = adoptWalletModel([source = entry.wallet](auto& handler) { return source->take(handler); },
            name, origin, entry.ingress_sequence, {m_registry->eventClock(), entry.wallet->receipt()->unloaded});
        if (!guard || m_shutting_down) return;
        if (adopted && m_command_is_open && m_command_state == WalletOperationState::WalletOperationPending) {
            m_registry->observeOpening(origin, m_registry->commandOrigin(), name, adopted, entry.notification_sequence, entry.ingress_sequence);
        }
    }
}

void WalletController::flushDeferredPublications()
{
    if (m_shutting_down || m_publishing_command || m_registry->adoptionActive() || m_projecting_notifications) return;
    const auto command = std::exchange(m_deferred_command_publication, 0);
    const bool notifications = std::exchange(m_deferred_notification_publication, false);
    if (!command && !notifications) return;
    const QPointer<WalletController> observer{this};
    m_services.post([observer, command, notifications] {
        if (!observer || observer->m_shutting_down) return;
        if (command) observer->publishWalletCommand(command);
        if (observer && !observer->m_shutting_down && notifications) observer->consumeWalletNotifications();
    });
}

std::shared_ptr<WalletController::AdoptionOrigin> WalletController::newAdoptionOrigin()
{ return m_registry->newOrigin(); }
size_t WalletController::adoptionFenceCount() const
{
    return m_registry->retirement()->values ? m_registry->retirement()->values->closed.size() : 0;
}
void WalletController::updateNotificationContext()
{
    if (!m_load_subscription || m_shutting_down) return;
    const auto subscription = m_load_subscription;
    auto origin = walletLoadInProgress() && m_registry->commandOrigin() ? m_registry->commandOrigin() : m_registry->notificationOrigin();
    if (!origin) {
        subscription->rejectNotificationContext();
        return;
    }
    const auto deferred_name = m_creations->operation() && m_creations->operation()->busy() && m_creations->operation()->creationKind() == walletcreation::Kind::WatchOnly ? m_creations->operation()->targetWalletId() : m_deferred_wallet_name;
    subscription->setNotificationContext(deferred_name, walletLoadInProgress(), m_selection->revision(), std::move(origin), m_creations->operation() && m_creation_admission->creationPending() ? m_creations->operation()->nativeObservation() : nullptr);
}
void WalletController::closeOpeningAdoption(WalletModel* wallet)
{ m_registry->closeOpening(wallet); }
void WalletController::adoptionUnavailable(bool closed)
{
    if (m_shutting_down) return;
    if (!m_publishing_command || m_publishing_command != m_command_id) {
        if (!closed) setWalletListError(tr("The wallet could not be shown. Try again after pending operations finish."));
        return;
    }
    m_adoption_state = closed ? WalletAdoptionState::AdoptionClosed : WalletAdoptionState::AdoptionUnavailable;
    commitWalletCommandOutcome();
    m_adoption_error = closed ? tr("The wallet operation completed after this wallet was closed. Open the wallet again to continue.")
                              : tr("The wallet view is unavailable. Try again after pending operations finish.");
    // This is a view notice. The independent Native receipt is unchanged.
    const QString message = m_adoption_error;
    const QString name = walletOperationName();
    const auto operation = m_command_id;
    const QPointer<WalletController> observer{this};
    setWalletLoadError(message);
    if (observer && !observer->m_shutting_down && operation == observer->m_command_id) reportLoadFailure(name, operation, message);
}

void WalletController::setWalletListError(const QString& error)
{
    if (m_shutting_down || m_wallet_list_error == error) return;
    m_wallet_list_error = error;
    Q_EMIT walletLoadErrorChanged();
}

void WalletController::setOpening(WalletOpenOperation* opening)
{
    // A refused request is released when the next request replaces it.
    if (m_opening && m_opening != m_command_opening && m_opening != m_previous_command_opening) m_opening->deleteLater();
    m_opening = opening;
    Q_EMIT openingChanged();
}

void WalletController::startOpening(quint64 id)
{
    // The previous command keeps its outcome; an older one is released.
    if (m_command_opening) m_command_opening->freeze();
    if (m_previous_command_opening && m_previous_command_opening != m_opening) m_previous_command_opening->deleteLater();
    m_previous_command_opening = m_command_opening;
    // Each field reads the controller's command while it is this open's.
    const QPointer<const WalletController> c{this};
    WalletOpenOperation::Live live;
    live.current = [c, id] { return c && c->m_command_id == id; };
    live.state = [c] { return c->walletOperationState(); };
    live.target = [c] { return c->walletOperationName(); };
    live.error = [c] { return c->walletLoadError(); };
    live.warnings = [c] { return c->walletLoadWarnings(); };
    live.result = [c] { return c->walletOperationResultWallet(); };
    live.can_open = [c] { return c->canOpenResult(); };
    live.adoption = [c] { return c->walletOperationAdoptionState(); };
    live.adoption_error = [c] { return c->walletOperationAdoptionError(); };
    m_command_opening = new WalletOpenOperation(id, std::move(live), this);
    setOpening(m_command_opening);
}

void WalletController::refuseOpening(const QString& target, const QString& error)
{
    if (m_shutting_down) return;
    setOpening(new WalletOpenOperation(target, error, this));
}

void WalletController::reportLoadFailure(const QString& target, quint64 operation, const QString& description)
{
    if (m_shutting_down) return;
    auto* failure = WalletOperationModel::OpenFailure(m_creations->operationServices(), operation, target, description);
    if (const QPointer<WalletOperationModel> previous{m_load_failure}) previous->deleteLater();
    m_load_failure = failure;
    Q_EMIT loadFailureChanged();
}

void WalletController::dismissLoadFailure()
{
    if (!m_load_failure) return;
    m_load_failure->deleteLater();
    m_load_failure.clear();
    Q_EMIT loadFailureChanged();
}

void WalletController::initialize()
{
    if (m_initialized || m_initializing || m_initialization_failed || m_shutting_down) return;
    if (!m_registry->retirement()->retained) { m_initialization_failed = true; setWalletListError(tr("Wallet loading could not be started.")); return; }
    if (!m_registry->openWalletModel() || !m_registry->openWalletModel()->valid()) { m_initialization_failed = true; setWalletListError(tr("Wallet presentation is unavailable.")); return; }
    // wallet_loader is not set when -disablewallet is passed; bail out.
    if (gArgs.GetBoolArg("-disablewallet", false)) return;
    m_initializing = true;
    const QPointer<WalletController> guard{this};
    // Pinned Core notifies before AddWallet. Its initial capture is not a
    // coherent resynchronization capability. Seed once and retain notification
    // handles; the subtree wallet/node interfaces remain unchanged.
    m_load_subscription = std::make_shared<walletlife::LoadSubscription>(
        m_services.executor,
        [node = &m_node](walletlife::LoadSubscription::Callback callback) {
            return node->walletLoader().handleLoadWallet(std::move(callback));
        },
        [node = &m_node] { return node->walletLoader().getWallets(); },
        [guard, post = m_services.post](walletlife::Update update) {
            post([guard, update] { if (guard) guard->handleLifecyclePublication(update); });
        }, [clock = m_registry->eventClock()] { return clock->next(); },
        [node = &m_node](interfaces::Wallet& wallet) { return walletidentity::Canonical(QString::fromStdString(wallet.getWalletName()), QString::fromStdString(node->walletLoader().getWalletDir())).toStdString(); });
    updateNotificationContext();
    if (!guard || m_shutting_down) return;
    if (!m_registry->notificationOrigin() || !m_load_subscription->start()) {
        m_initializing = false;
        m_initialization_failed = true;
        setWalletListError(tr("Wallet loading could not be started."));
    }
}

void WalletController::handleLifecyclePublication(walletlife::Update update)
{
    if (m_shutting_down) return;
    switch (update.kind) {
    case walletlife::Publication::Ready:
    case walletlife::Publication::InitialHandoff:
        finishInitialization();
        break;
    case walletlife::Publication::Changed:
        m_load_subscription->acknowledgeChange();
        consumeWalletNotifications();
        break;
    case walletlife::Publication::Failed:
    case walletlife::Publication::Retired: {
        const QPointer<WalletController> guard{this};
        const bool initialized_changed = m_initialized;
        const QString error = update.reason == walletlife::FailureReason::IngressCapacity ?
            tr("Wallet discovery is unavailable because it could not keep up with wallet notifications. Restart the app to refresh the wallet list.") :
            update.kind == walletlife::Publication::Failed ? tr("Wallet loading failed.") :
            update.cleanup_failed ? tr("Wallet notification cleanup failed.") : tr("Wallet notifications are unavailable.");
        m_initializing = false;
        m_initialized = false; m_names_ready->store(false);
        m_initialization_failed = true;
        setWalletListError(error); if (!guard || m_shutting_down) return;
        if (initialized_changed) {
            Q_EMIT initializedChanged();
            if (!guard || m_shutting_down) return;
        }
        flushDeferredPublications();
        break;
    }
    }
}

void WalletController::finishInitialization()
{
    if (m_shutting_down || m_initialization_failed || !m_initializing) return;
    const QPointer<WalletController> guard{this};
    auto handoff = m_load_subscription->takeInitial();
    if (!handoff) return;
    if (!handoff->entries.empty()) {
        for (auto& entry : handoff->entries) {
            if (entry.unload_observation->unloaded->load()) continue;
            auto adoption = m_registry->retain<OpeningAdoption>([] { return std::make_unique<OpeningAdoption>(); });
            if (!guard || m_shutting_down) return;
            if (!adoption) {
                m_initialization_failed = true;
                m_initializing = false;
                setWalletListError(tr("Wallets could not be shown. Try again after pending operations finish."));
                if (guard) m_load_subscription->retire();
                return;
            }
            adoption->unloaded = entry.unload_observation->unloaded;
            auto* wallet_model = new WalletModel(WalletModel::SharedWallet{
                std::move(entry.initial_wallet), QString::fromStdString(entry.name),
                m_preferences, m_fee_policy, m_shutdown_gate, {m_registry->eventClock(), adoption->unloaded}, true,
                std::move(entry.unload_observation->handler), walletServices()}, &m_node, FeeReadServices{}, psbtServices(), backupServices());
            if (!registerWalletModel(wallet_model, adoption)) {
                if (!guard || m_shutting_down) return;
                retireWallet(wallet_model, false);
                m_initialization_failed = true;
                m_initializing = false;
                setWalletListError(tr("Wallets could not be shown. Try again after pending operations finish."));
                if (!guard || m_shutting_down) return;
                m_load_subscription->retire();
                return;
            }
            if (!wallet_model->available()) { retireWallet(wallet_model, false); continue; }
            m_names->apply(wallet_model);
            m_registry->insert(wallet_model, wallet_model->name());
            m_registry->openWalletModel()->addWallet(wallet_model); if (!guard || m_shutting_down) return;
            subscribeWalletInfo(wallet_model);
            Q_EMIT walletLoadStateChanged(QString::fromStdString(entry.name), WalletListModel::LoadState::Open, QString{});
            if (!guard || m_shutting_down) return;
            initializeWalletInfo(wallet_model);
            if (!guard || m_shutting_down) return;
        }
        // Background disposal publishes the next bounded presentation batch.
        return;
    }
    m_initializing = false;
    m_initialized = true; m_names_ready->store(true);
    // The list is available again; its earlier failure no longer applies.
    setWalletListError({}); if (!guard || m_shutting_down) return;
    if (!m_registry->wallets().empty()) {
        Q_EMIT openWalletsChanged();
        if (!guard || m_shutting_down) return;
        setWalletLoaded(true);
        if (!guard || m_shutting_down) return;
        setNoWalletsFound(false);
        if (!guard || m_shutting_down) return;
        m_selection->select(m_registry->wallets().front());
        if (!guard || m_shutting_down) return;
    }
    m_creations->refreshSigners();
    if (!guard || m_shutting_down) return;
    Q_EMIT initializedChanged();
    if (!guard || m_shutting_down) return;
    consumeWalletNotifications();
}

void WalletController::setWalletLoaded(bool loaded)
{
    if (m_is_wallet_loaded != loaded) {
        m_is_wallet_loaded = loaded;
        Q_EMIT isWalletLoadedChanged();
    }
}

void WalletController::setNoWalletsFound(bool no_wallets_found)
{
    if (m_no_wallets_found != no_wallets_found) {
        m_no_wallets_found = no_wallets_found;
        Q_EMIT noWalletsFoundChanged();
    }
}

void WalletController::startWalletLoad(const QString& path, const QString& wallet_format)
{
    const QPointer<WalletController> admission_observer{this};
    m_command_requested_target = path;
    if (m_shutting_down || m_command_state == WalletOperationState::WalletOperationPending || m_creation_admission->creationPending()) return;
    m_command_state = WalletOperationState::WalletOperationPending;
    clearWalletLoadStatus(); if (!admission_observer || m_shutting_down) return;
    setWalletLoadInProgress(true); if (!admission_observer || m_shutting_down) return;
    const QPointer<WalletController> guard{this};
    Q_EMIT walletLoadStateChanged(path, WalletListModel::LoadState::Loading, {}); if (!guard || m_shutting_down) return;
    submitWalletCommand([node = &m_node, path, wallet_format](std::unique_ptr<interfaces::Wallet>& result_wallet) {
        CommandResult out;
        QString format = wallet_format;
        try { out.reference = format.isEmpty() ? ResolveManagedWalletReference(*node, path, &format) : (path.isEmpty() ? QString{} : QDir::cleanPath(path)); }
        catch (...) { out.error = tr("Wallet directory could not be checked."); return out; }
        if (out.reference.isEmpty() && !(path.isEmpty() && !format.isEmpty())) {
            out.error = path.isEmpty() && wallet_format.isEmpty() ? tr("Choose a wallet to open.") : tr("The selected wallet is not available in the wallet directory.");
            return out;
        }
        if (format == "bdb") { out.needs_migration = true; return out; }
        std::vector<bilingual_str> warnings;
        out.open_attempted = true;
        auto wallet = node->walletLoader().loadWallet(out.reference.toStdString(), warnings);
        out.warnings = walletcreation::JoinWarnings(warnings);
        if (!wallet) out.error = QString::fromStdString(util::ErrorString(wallet).translated);
        else result_wallet = std::move(*wallet);
        const auto directory = QString::fromStdString(node->walletLoader().getWalletDir());
        if (result_wallet) describeCommandWallet(out, *result_wallet, out.reference, directory);
        return out;
    }, [this, path](const CommandResult& out) {
        const QPointer<WalletController> guard{this};
        setWalletLoadInProgress(false); if (!guard) return;
        setWalletLoadWarnings(out.warnings); if (!guard) return;
        if (out.needs_migration) {
            // The wallet list asks its creation controller to migrate; the
            // migration operation is offered to the view like any other.
            commitWalletCommandOutcome(); m_creations->migrate(out.reference); return;
        }
        if (!out.has_wallet || out.incomplete) {
            commitWalletCommandOutcome();
            const auto error = out.error.isEmpty() ? tr("Wallet could not be opened.") : out.error;
            setWalletLoadError(error); if (!guard) return;
            reportLoadFailure(path, m_command_id, currentCommandErrorDescription()); if (!guard || m_shutting_down) return;
            Q_EMIT walletLoadStateChanged(path, WalletListModel::LoadState::LoadError, error); return;
        }
        const auto backend = out.backend.lock();
        if (!backend) return;
        const auto operation = m_publishing_command;
        auto* wallet = m_registry->adoptWallet(backend->wallet, out.name);
        if (guard && wallet) {
            if (operation && operation == m_command_id) recordCommandWallet(wallet);
            commitWalletCommandOutcome(); offerWalletLoadWarnings(out.name, out.warnings);
            if (guard) Q_EMIT walletLoadSucceeded(wallet);
        }
    }, [this, path](std::exception_ptr failure) {
        const QPointer<WalletController> guard{this};
        setWalletLoadInProgress(false); if (!guard) return;
        const auto error = execution::NotStartedCause(failure) ? tr("The wallet could not be opened. Try again.")
            : tr("The wallet may not have opened. Check the wallet directory before trying again.");
        setWalletLoadError(error); if (!guard || m_shutting_down) return;
        reportLoadFailure(path, m_command_id, currentCommandErrorDescription()); if (guard && !m_shutting_down) Q_EMIT walletLoadStateChanged(path, WalletListModel::LoadState::LoadError, error);
    }, true);
}


void WalletController::setWalletLoadInProgress(bool in_progress)
{
    if (!in_progress && m_publishing_command && !m_shutting_down) { m_complete_load_publication = true; return; }
    const QPointer<WalletController> observer{this};
    const auto command = m_command_id;
    const bool changed = m_wallet_load_in_progress != in_progress;
    m_wallet_load_in_progress = in_progress;
    m_creation_admission->setLegacyBusy(m_wallet_load_in_progress);
    if (!in_progress) {
        auto origin = newAdoptionOrigin();
        if (!observer || observer->m_shutting_down || command != observer->m_command_id) return;
        m_registry->setNotificationOrigin(std::move(origin));
    }
    updateNotificationContext();
    if (observer && !observer->m_shutting_down && command == observer->m_command_id && changed) Q_EMIT walletLoadInProgressChanged();
}

void WalletController::setWalletLoadError(const QString& error)
{
    if (m_shutting_down || !commandPublicationCurrent()) return;
    if (error.size() == m_wallet_load_error.size() && error.constData() == m_wallet_load_error.constData()) return;
    m_wallet_load_error = error;
    Q_EMIT walletLoadErrorChanged();
}

void WalletController::offerWalletLoadWarnings(const QString& wallet, const QString& warnings)
{
    if (warnings.isEmpty() || !m_command_owner || m_shutting_down) return;
    const QPointer<WalletController> guard{this};
    auto* notice = WalletOperationModel::OpenNotice(m_creations->operationServices(), m_command_id, wallet, warnings, m_command_owner, m_result_wallet);
    if (!guard) { notice->deleteLater(); return; }
    const QPointer<WalletOperationModel> original{notice};
    m_registry->registerForm(notice, [original] { if (original) original->close(); });
    connect(notice, &QObject::destroyed, this, [this, notice] { m_registry->forgetForm(notice); });
    Q_EMIT walletLoadWarningsOffered(notice);
}

void WalletController::setWalletLoadWarnings(const QString& warnings)
{
    if (m_wallet_load_warnings != warnings) {
        m_wallet_load_warnings = warnings;
        Q_EMIT walletLoadWarningsChanged();
    }
}










WalletModel* WalletController::walletOperationResultWallet() const
{
    return m_result_wallet.data();
}
bool WalletController::canOpenResult() const
{
    return !m_shutting_down && m_result_wallet && m_result_wallet->available() && m_registry->containsExact(m_result_wallet);
}

WalletController::WalletAdoptionState WalletController::walletOperationAdoptionState() const
{
    return m_adoption_state;
}
QString WalletController::walletOperationAdoptionError() const { return m_adoption_error; }
