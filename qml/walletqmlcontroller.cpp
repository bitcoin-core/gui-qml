// Copyright (c) 2024-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/guipresentation.h>
#include <qml/walletqmlcontroller.h>
#include <qml/walletidentity.h>
#include <qml/backendexecutor.h>
#include <qml/walletlifecycle.h>
#include <qml/retainedsnapshot.h>

#include <qml/models/walletqmlmodel.h>
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
        WalletQmlController::tr("This wallet needs to be migrated"),
        WalletQmlController::tr("The selected backup appears to come from a legacy wallet format. It needs to be migrated before it can be used here."),
        WalletQmlController::tr("If this is a legacy wallet backup, migrate it with the wallet migration tool before importing it here.")};
    if (contains("does not exist")) return {WalletQmlController::tr("We couldn't find that file"),
        WalletQmlController::tr("The selected file is no longer available at that location. Choose the backup file again and retry."), error};
    if (contains("already exists")) return {WalletQmlController::tr("A wallet with this name already exists"),
        WalletQmlController::tr("Importing this backup would create a wallet that already exists in your wallet directory. Remove or rename the existing wallet first, then try again."), error};
    if (contains("verification failed") || contains("not supported")) return {WalletQmlController::tr("This wallet type is not supported"),
        WalletQmlController::tr("The file looks like a wallet backup, but this application could not verify or open it in a supported format."), error};
    return {WalletQmlController::tr("This wallet couldn't be imported"),
        WalletQmlController::tr("The selected file could not be restored as a wallet. Check that it is a valid wallet backup, then try another file."), error};
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

void WalletImportErrorLines::setValue(QString text, std::shared_ptr<const void> lifetime, bool quiet)
{
    const auto revision = ++m_revision;
    if (quiet || m_resetting) { m_text = std::move(text); m_lifetime = std::move(lifetime); return; }
    const QPointer<WalletImportErrorLines> observer{this};
    m_resetting = true;
    beginResetModel(); if (!observer) return;
    if (revision == m_revision) { m_text = std::move(text); m_lifetime = std::move(lifetime); }
    m_resetting = false;
    endResetModel();
}


struct WalletQmlController::ImportErrorProjection {
    std::shared_ptr<const ImportErrorFrame> frame, previous;
    ImportErrorText text;
    QString details, display;
    bool changed{false}, failed{false};
    quint64 revision{0};
};

WalletProvisioningCheck* WalletQmlController::createProvisioningCheck()
{
    if (m_shutting_down || !m_registry->retirement()->retained) return nullptr;
    auto* form = new WalletProvisioningCheck(m_node, {backgroundScheduler(), m_services.post}, nullptr, m_names_ready, m_registry->nameRegistry(), m_shutdown_gate);
    QQmlEngine::setObjectOwnership(form, QQmlEngine::JavaScriptOwnership);
    const QPointer<WalletProvisioningCheck> original{form};
    m_registry->registerForm(form, [original] { if (original) original->closeAdmission(); });
    connect(form, &QObject::destroyed, this, [this, form] { m_registry->forgetForm(form); });
    connect(this, &WalletQmlController::initializedChanged, form, &WalletProvisioningCheck::refresh);
    return form;
}

void WalletQmlController::describeCommandWallet(CommandResult& result, const QString& fallback, const QString& directory)
{
    if (!result.wallet) return;
    result.name = fallback;
    try {
        result.name = walletidentity::Canonical(QString::fromStdString(result.wallet->getWalletName()), directory);
        result.key_scheme = WalletQmlModel::keySchemeDisplayText(WalletQmlModel::keySchemeForWallet(*result.wallet));
    } catch (...) {
        result.incomplete = true;
        result.error = tr("The wallet operation completed, but wallet metadata could not be read. Check the wallet directory before retrying.");
    }
}

WalletQmlController::WalletOperationState WalletQmlController::walletOperationState() const
{
    if (m_creation) {
        switch (m_creation->phase()) {
        case WalletOperationModel::AwaitingConfirmation: return WalletOperationState::WalletOperationNeedsMigration;
        case WalletOperationModel::AwaitingPassphrase: return WalletOperationState::WalletOperationNeedsPassphrase;
        case WalletOperationModel::Succeeded: return WalletOperationState::WalletOperationSucceeded;
        case WalletOperationModel::Partial: return WalletOperationState::WalletOperationIncomplete;
        case WalletOperationModel::Uncertain: return WalletOperationState::WalletOperationUncertain;
        case WalletOperationModel::Failed: case WalletOperationModel::Rejected: case WalletOperationModel::Cancelled: return WalletOperationState::WalletOperationFailed;
        default: return WalletOperationState::WalletOperationPending;
        }
    }
    if (m_shutting_down && m_command_receipt) { std::lock_guard lock{m_command_receipt->mutex}; if (m_command_receipt->ready) return m_command_receipt->open_intent_state.value_or(m_command_receipt->state); }
    return m_command_state;
}
QString WalletQmlController::walletOperationName() const
{
    if (m_creation) return m_creation->targetWalletId();
    if (!m_command_receipt) return {};
    std::lock_guard lock{m_command_receipt->mutex}; return m_command_receipt->name;
}
bool WalletQmlController::submitWalletCommand(std::function<CommandResult()> effect,
    std::function<void(const CommandResult&)> done, std::function<void(std::exception_ptr)> failed, bool open_intent)
{
    if (m_shutting_down || m_publishing_command || m_creation_admission->creationPending()) return false;
    m_creation.clear(); m_creation_admission->setLegacyBusy(true);
    const QPointer<WalletQmlController> observer{this};
    const auto id = m_creation_admission->allocate();
    if (!id) { m_creation_admission->setLegacyBusy(false); return false; }
    m_command_id = id;
    m_command_is_open = open_intent;
    m_command_selection_revision = m_admission_selection_revision;
    disconnect(m_result_available_connection); disconnect(m_result_destroyed_connection);
    m_result_wallet.clear();
    m_command_adopted = false;
    m_open_resolution.reset(); m_open_resolution_wallet.clear(); m_open_resolution_rejected = false;
    const auto scheduler = backgroundScheduler();
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
    // The actual effect owns its backend cell through Domain receipt capture,
    // including observer loss. Retained metadata owns no backend after that.
    auto completion_backend = std::make_shared<std::shared_ptr<CommandBackend>>();
    auto command = std::make_shared<Command>(scheduler, [weak, receipt, observer, id, post, completion_backend] {
        if (const auto command = weak->lock()) if (const auto result = command->result()) {
            auto description = result->value ? PrepareImportError(result->value->error).description : QString{};
            if (description.isEmpty()) description = WalletQmlController::tr("The selected file could not be restored as a wallet. Check that it is a valid wallet backup, then try another file.");
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
                receipt->error_description = WalletQmlController::tr("The wallet open outcome is uncertain. Check the wallet directory before retrying.");
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
    auto owner = execution::RetainSnapshot<std::shared_ptr<Command>>(scheduler, [&] { return std::make_unique<std::shared_ptr<Command>>(command); });
    const auto backend = execution::RetainSnapshot<CommandBackend>(scheduler, [&] {
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
    const auto admission = command->start([effect = std::move(effect), backend, subscription = m_load_subscription] {
        auto result = effect();
        result.has_wallet = bool(result.wallet);
        backend->wallet = std::exchange(result.wallet, {});
        result.backend = backend;
        result.reconcile_open = result.open_attempted && !result.has_wallet && !result.incomplete && !result.needs_migration && !result.needs_passphrase;
        if (result.open_attempted && subscription) result.notification_fence = subscription->notificationFence();
        return result;
    },
        {execution::Lane::Core, scheduler->allocateKey(), execution::WorkClass::Action});
    if (!observer || observer->m_shutting_down || id != observer->m_command_id) return bool(admission);
    if (!admission) {
        m_command_state = WalletOperationState::WalletOperationFailed;
        m_command_failed(std::make_exception_ptr(execution::NotStarted{execution::NotStarted::Cause::Transport}));
    }
    if (observer) Q_EMIT walletOperationChanged();
    return bool(admission);
}
void WalletQmlController::publishWalletCommand(quint64 id)
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
    const QPointer<WalletQmlController> observer{this};
    std::optional<OpenIntentResolution> resolution;
    if (failed_open) {
        if (!m_open_resolution) {
            m_open_resolution_wallet = reconciledOpenWallet(*result->value);
            const auto session = m_open_resolution_wallet ? m_open_resolution_wallet->opening() : std::shared_ptr<walletopening::Session>{};
            const OpenIntentFacts facts{true, session ? session->identity() : walletopening::Opening{}, session};
            m_open_resolution = std::make_shared<execution::OwnedCommand<OpenIntentResolution>>(backgroundScheduler(),
                [observer, id, post = m_services.post] { post([observer, id] { if (observer) observer->publishWalletCommand(id); }); });
            const auto resolution_owner = m_open_resolution;
            const auto scheduler = backgroundScheduler();
            const auto receipt = m_command_receipt;
            execution::Admission admission{{}, execution::Rejection::Transport};
            try {
                admission = resolution_owner->start([facts, receipt] {
                    const auto decision = resolveOpenIntent(facts);
                    { std::lock_guard lock{receipt->mutex}; receipt->open_intent_state = decision.state; }
                    return decision;
                }, {execution::Lane::Domain, scheduler->allocateKey(), execution::WorkClass::Action});
            } catch (...) { /* Pre-admission failure has no Domain outcome to await. */ }
            if (!observer || m_shutting_down || id != m_command_id || m_open_resolution != resolution_owner) return;
            m_open_resolution_rejected = !admission;
            if (admission) return;
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
    const QPointer<WalletQmlModel> observed{resolution && resolution->state == WalletOperationState::WalletOperationSucceeded ? m_open_resolution_wallet : nullptr};
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
        const auto error = tr("The wallet open result could not be reconciled. Try again after pending operations finish.");
        setWalletLoadInProgress(false);
        if (observer && !m_shutting_down && id == m_command_id) setWalletLoadError(error);
        if (observer && !m_shutting_down && id == m_command_id) Q_EMIT walletLoadErrorNotice(walletOperationName(), id, error);
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
    const bool complete_migration = std::exchange(m_complete_migration_publication, false);
    m_publishing_command = previous_publication;
    m_registry->exchangePublishingOrigin(previous_origin);
    // The original result, terminal signal and selection completion are owned
    // before an idle notification may synchronously admit a successor command.
    if (!m_shutting_down && id == m_command_id && complete_load) setWalletLoadInProgress(false);
    if (!observer) return;
    if (!m_shutting_down && id == m_command_id && complete_migration) setWalletMigrationInProgress(false);
    if (observer && !m_shutting_down) flushDeferredPublications();
}

void WalletQmlController::commitWalletCommandOutcome()
{
    if (m_shutting_down || !m_publishing_command || m_publishing_command != m_command_id || !m_command_receipt) return;
    std::lock_guard lock{m_command_receipt->mutex};
    m_command_state = m_command_receipt->open_intent_state.value_or(m_command_receipt->state);
}

WalletQmlController::OpenIntentResolution WalletQmlController::resolveOpenIntent(const OpenIntentFacts& facts)
{
    const auto owner = facts.owner.lock();
    return facts.native_failed && facts.observed && owner && owner->identity() == facts.observed && owner->available()
        ? OpenIntentResolution{WalletOperationState::WalletOperationSucceeded, facts.observed}
        : OpenIntentResolution{WalletOperationState::WalletOperationFailed, {}};
}

WalletQmlModel* WalletQmlController::reconciledOpenWallet(const CommandResult& result) const
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

backup::Services WalletQmlController::backupServices() const
{
    backup::Services services;
    services.coordinator = m_backup_installs;
    return services;
}

namespace {
WalletQmlController::Services ValidateControllerServices(WalletQmlController::Services services)
{
    if (bool(services.scheduler) != bool(services.shutdown) || bool(services.scheduler) != bool(services.post))
        throw std::invalid_argument("Controller scheduler, GUI post, and physical shutdown owner must be supplied together");
    return services;
}
}

WalletQmlController::WalletQmlController(interfaces::Node& node, Services services, QObject* parent)
    : WalletQmlController(node, execution::Options{}, ValidateControllerServices(std::move(services)), parent) {}

WalletQmlController::WalletQmlController(interfaces::Node& node, QObject* parent)
    : WalletQmlController(node, execution::Options{}, parent) {}

WalletQmlController::WalletQmlController(interfaces::Node& node, walletopening::Session::Preferences preferences, QObject* parent)
    : WalletQmlController(node, execution::Options{}, parent)
{
    m_preferences = std::move(preferences);
}

WalletQmlController::WalletQmlController(interfaces::Node& node, walletopening::Session::Preferences preferences,
                                       walletopening::FeePolicy fee_policy, QObject* parent)
    : WalletQmlController(node, std::move(preferences), parent)
{
    m_fee_policy = std::move(fee_policy);
}

WalletQmlController::WalletQmlController(interfaces::Node& node, execution::Options limits, QObject* parent)
    : WalletQmlController(node, limits, Services{}, parent) {}

WalletQmlController::WalletQmlController(interfaces::Node& node, execution::Options limits, Services services, QObject* parent)
    : QObject(parent)
    , m_node(node)
    , m_empty_wallet(new WalletQmlModel(&node, this))
    , m_wallet_command_executor(services.scheduler ? nullptr : std::make_shared<BackendExecutor>(limits))
    , m_services(std::move(services))
    , m_notification_bridge(presentation::MakeGuiObject())
    , m_open_local_path_fn([](const QString& path) {
        return QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    })
{
    if (!m_services.scheduler) m_services.scheduler = m_wallet_command_executor->scheduler();
    if (!m_services.post) m_services.post = [bridge = m_notification_bridge](auto callback) { QMetaObject::invokeMethod(bridge.get(), std::move(callback), Qt::QueuedConnection); };
    m_services.post = presentation::ReliablePost(std::move(m_services.post));
    if (!m_services.shutdown) m_services.shutdown = [executor = m_wallet_command_executor, bridge = m_notification_bridge](auto drained) {
        executor->whenDrained(bridge.get(), std::move(drained));
        executor->shutdown();
    };
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
    host->createFacade = [this](std::shared_ptr<interfaces::Wallet> wallet, const QString& name, walletopening::NativeEvents events) {
        return new WalletQmlModel(WalletQmlModel::SharedWallet{std::move(wallet), backgroundScheduler(), name, m_preferences, m_fee_policy, m_shutdown_gate, std::move(events), true}, &m_node, FeeReadServices{}, psbtServices(), backupServices());
    };
    host->information = [this](WalletQmlModel* wallet, WalletRegistry::Information action) {
        switch (action) {
        case WalletRegistry::Information::Subscribe: subscribeWalletInfo(wallet); break;
        case WalletRegistry::Information::ApplyAlias: applyWalletDisplayName(wallet); break;
        case WalletRegistry::Information::Initialize: initializeWalletInfo(wallet); break;
        }
    };
    host->loadState = [this](const QString& name, bool open) { Q_EMIT walletLoadStateChanged(name, open ? WalletListModel::LoadState::Open : WalletListModel::LoadState::Closed, QString{}); };
    host->loadError = [this](const QString& error) { setWalletLoadError(error); };
    host->forgetCloseTarget = [this](WalletQmlModel* wallet) { const bool target = m_close_target == wallet; if (target) m_close_target = nullptr; return target; };
    host->closeQuestionChanged = [this](WalletQmlModel* wallet) {
        if (m_close_target == wallet && wallet->closeQuestionToken().isEmpty()) { m_close_target = nullptr; Q_EMIT closeTargetChanged(); }
    };
    host->prepareForget = [this](WalletQmlModel* wallet) { m_selection->prepareForget(wallet); };
    host->forget = [this](WalletQmlModel* wallet, WalletQmlModel* successor) { m_selection->forget(wallet, successor); };
    host->contextChanged = [this] { updateNotificationContext(); };
    m_registry = std::make_shared<WalletRegistry>(std::move(host), WalletRegistry::Services{backgroundScheduler(), m_wallet_command_executor, m_notification_bridge, m_empty_wallet, m_services.post, m_services.shutdown});
    m_import_error_input = m_registry->importErrors();
    m_import_error_services = {backgroundScheduler(), m_services.post};
    m_selection = new WalletSelectionModel(m_empty_wallet, [this](WalletQmlModel* wallet) { return m_registry->containsExact(wallet); }, this);
    connect(m_selection, &WalletSelectionModel::selectedWalletChanged, this, [this] {
        const QPointer<WalletQmlController> observer{this};
        updateNotificationContext();
        if (observer) Q_EMIT selectedWalletChanged();
    });
    m_home_path = qEnvironmentVariable("HOME");
    m_registry->setNotificationOrigin(newAdoptionOrigin());
    refreshDisplayAliases();
    m_empty_wallet->setNode(&m_node);
    if (m_wallet_command_executor) m_wallet_command_executor->setObjectName(QStringLiteral("wallet-commands"));
    m_notification_bridge->setObjectName(QStringLiteral("wallet-notifications"));
}

WalletQmlController::~WalletQmlController()
{
    blockSignals(true);
    beginShutdown();
    m_registry->detachPresentation();
    m_registry->detachController();
    m_registry.reset();
}

void WalletQmlController::setSelectedWallet(QString path, QString wallet_format)
{
    const auto retirement = m_registry->retirement();
    if (retirement->values && retirement->values->retiring_names.contains(path) && !walletForName(path)) {
        QString error = tr("This wallet is still closing.");
        const QPointer<WalletQmlController> observer{this};
        setWalletLoadError(error); if (!observer || m_shutting_down) return;
        Q_EMIT walletLoadErrorNotice(path, m_command_id, error); if (!observer || m_shutting_down) return;
        Q_EMIT walletLoadStateChanged(path, WalletListModel::LoadState::LoadError, error);
        return;
    }
    if (!m_initialized) { setWalletLoadError(walletDiscoveryError()); return; }
    if (auto* wallet = walletForName(path)) {
        const QPointer<WalletQmlController> guard{this};
        if (!m_selection->select(wallet)) return;
        if (!guard || m_shutting_down) return;
        clearWalletLoadStatus();
        return;
    }
    if (m_shutting_down || m_admitting_command || m_publishing_command || m_creation_admission->creationPending() || m_command_state == WalletOperationState::WalletOperationPending) return;
    m_admitting_command = true;
    const QPointer<WalletQmlController> observer{this};
    const auto admission_guard = qScopeGuard([observer] { if (observer) observer->m_admitting_command = false; });
    const auto revision = m_selection->beginSelection(path);
    if (!observer || m_shutting_down) return;
    m_admission_selection_revision = revision;
    updateNotificationContext();
    if (!observer || m_shutting_down) return;
    startWalletLoad(path, wallet_format);
    if (observer && m_command_state != WalletOperationState::WalletOperationPending) m_selection->finishSelection(revision);
}

WalletQmlModel* WalletQmlController::walletForName(const QString& name) const
{ return m_registry->walletForName(name); }

bool WalletQmlController::selectWallet(WalletQmlModel* wallet)
{
    if (m_shutting_down) return false;
    const QPointer<WalletQmlController> observer{this};
    const bool selected = m_selection->select(wallet);
    if (observer && selected) updateNotificationContext();
    return selected;
}

void WalletQmlController::requestCloseWallet(WalletQmlModel* wallet)
{
    if (m_shutting_down || !wallet || !wallet->available() || !m_registry->positions().contains(wallet)) return;
    const QPointer<WalletQmlController> guard{this};
    const QPointer<WalletQmlModel> original{wallet};
    if (m_close_target != wallet) {
        if (m_close_target) m_close_target->answerClose(m_close_target->closeQuestionToken(), false);
        if (!guard || !original || !original->available() || m_shutting_down) return;
        m_close_target = wallet;
        Q_EMIT closeTargetChanged();
        if (!guard || !original || m_close_target != original) return;
    }
    if (original && original->available() && m_close_target == original) original->requestClose();
}

bool WalletQmlController::isWalletOpen(const QString& path)
{
    return m_registry->names().contains(path);
}

void WalletQmlController::publishWalletInfo(WalletQmlModel* wallet_model)
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

void WalletQmlController::initializeWalletInfo(WalletQmlModel* wallet_model)
{
    // Preserve the existing opening-metadata capture/publication point. Its
    // legacy queries belong to the opening migration, not balance callbacks.
    m_registry->rememberInformation(wallet_model, WalletRegistry::WalletInformation{
        wallet_model->name(), static_cast<int>(wallet_model->keySchemeKind())});
    publishWalletInfo(wallet_model);
}

void WalletQmlController::publishOpenWalletsInfo()
{
    if (m_shutting_down) return;
    m_info_cursor.reset(); publishWalletInfoTurn(++m_info_generation);
}
void WalletQmlController::publishWalletInfoTurn(quint64 generation)
{
    if (m_shutting_down || generation != m_info_generation) return;
    const QPointer<WalletQmlController> guard{this};
    for (int budget = 0; budget < 32; ++budget) {
        const auto next = m_info_cursor ? m_registry->information().upper_bound(*m_info_cursor) : m_registry->information().begin();
        if (next == m_registry->information().end()) return;
        auto* wallet = next->first; m_info_cursor = wallet;
        publishWalletInfo(wallet); if (!guard || m_shutting_down || generation != m_info_generation) return;
    }
    QMetaObject::invokeMethod(this, [guard, generation] { if (guard) guard->publishWalletInfoTurn(generation); }, Qt::QueuedConnection);
}

void WalletQmlController::subscribeWalletInfo(WalletQmlModel* wallet_model)
{
    if (!wallet_model) return;
    // Sender is the wallet model, so Qt auto-disconnects when the model is
    // destroyed in closeWallet/unloadWallets.
    QObject::connect(wallet_model, &WalletQmlModel::balanceChanged,
                     this, [this, wallet_model]() {
                         publishWalletInfo(wallet_model);
                     });
    connect(wallet_model, &WalletQmlModel::openingMetadataChanged, this, [this, wallet_model] { initializeWalletInfo(wallet_model); });
}

QString WalletQmlController::homePath() const
{
    return m_home_path;
}

bool WalletQmlController::openWalletLocation(WalletQmlModel* wallet)
{
    if (m_shutting_down || m_location_pending) return false;
    if (!wallet || wallet == m_empty_wallet) {
        setWalletLocationOpenError(tr("No wallet file is available to view."));
        return false;
    }
    if (!wallet->available()) return false;
    const auto action = wallet->opening()->startAction(); if (!action) return false;
    const auto name = wallet->name();
    const auto id = ++m_location_id;
    m_location_wallet = wallet; m_location_pending = true; m_wallet_location_open_error.clear();
    const QPointer<WalletQmlController> guard{this};
    auto operation = std::make_shared<execution::OwnedCommand<LocationResult>>(backgroundScheduler(), [guard, id, post = m_services.post] {
        post([guard, id] { if (guard) guard->publishLocation(id); });
    });
    m_location_operation = operation;
    const auto admitted = operation->start([name, node = &m_node, action] {
        (void)action; // Original opening remains retained through this Native handoff.
        LocationResult result;
        QFileInfo path{name.isEmpty() ? QString::fromStdString(node->walletLoader().getWalletDir()) : name};
        if (!path.isAbsolute()) path = QFileInfo{QDir{QString::fromStdString(node->walletLoader().getWalletDir())}.filePath(name)};
        if (!path.exists()) { result.error = tr("Wallet file not found: %1").arg(path.absoluteFilePath()); return result; }
        result.path = path.isDir() ? path.absoluteFilePath() : path.absolutePath();
        return result;
    });
    if (!admitted) { m_location_pending = false; m_wallet_location_open_error = tr("Wallet location could not be checked."); }
    Q_EMIT walletLocationOpenErrorChanged(); return bool(admitted);
}
void WalletQmlController::publishLocation(quint64 id)
{
    if (m_shutting_down || id != m_location_id || !m_location_operation) return;
    const auto result = m_location_operation->result(); if (!result) return;
    m_location_pending = false;
    m_wallet_location_open_error = result->value ? result->value->error : tr("Wallet location could not be checked.");
    const QPointer<WalletQmlController> guard{this};
    if (m_wallet_location_open_error.isEmpty() && m_location_wallet && m_location_wallet->available()) {
        if (!m_open_local_path_fn(result->value->path)) { if (!guard) return; m_wallet_location_open_error = tr("Could not open wallet file location."); }
    } else if (m_wallet_location_open_error.isEmpty()) m_wallet_location_open_error = tr("This wallet is closing.");
    if (guard) Q_EMIT walletLocationOpenErrorChanged();
}

void WalletQmlController::clearWalletLocationOpenError()
{
    setWalletLocationOpenError({});
}

void WalletQmlController::closeWallet(const QString& path)
{
    if (!m_initialized) {
        setWalletLoadError(walletDiscoveryError());
        return;
    }

    const QPointer<WalletQmlController> guard{this};
    const auto closing = m_registry->names().find(path);
    if (closing != m_registry->names().end()) closeOpeningAdoption(closing->second);
    if (!guard || m_shutting_down) return;
    clearWalletLoadStatus(); if (!guard || m_shutting_down) return;
    clearWalletMigrationStatus(); if (!guard || m_shutting_down) return;

    const auto found = m_registry->names().find(path);
    if (found == m_registry->names().end()) return;
    auto* wallet_to_close = found->second;
    removeWalletModel(wallet_to_close, true);
}

QString WalletQmlController::walletDisplayName(const QString& path) const
{
    const auto& name = path;
    if (!m_names) return walletidentity::Display(name);
    const auto found = m_names->names.find(name);
    return found != m_names->names.end() && !found->second.isEmpty() ? found->second : walletidentity::Display(name);
}
bool WalletQmlController::setWalletDisplayName(const QString& path, const QString& display_name)
{
    if (m_shutting_down || m_names_write_pending || path.size() > 32768 || display_name.size() > 32768) return false;
    const auto& name = path;
    const auto alias = display_name.trimmed();
    const auto id = ++m_names_write_id;
    m_names_write_pending = true; m_names_write_error.clear();
    auto receipt = std::make_shared<SettingsReceipt>(); m_names_write_receipt = receipt;
    auto weak = std::make_shared<std::weak_ptr<execution::OwnedCommand<SettingsResult>>>();
    const QPointer<WalletQmlController> guard{this};
    auto operation = std::make_shared<execution::OwnedCommand<SettingsResult>>(backgroundScheduler(), [weak, receipt, guard, id, post = m_services.post] {
        if (const auto operation = weak->lock()) if (const auto result = operation->result()) {
            std::lock_guard lock{receipt->mutex}; receipt->ready = true;
            receipt->saved = result->value && result->value->saved;
            receipt->error = result->value ? result->value->error : WalletQmlController::tr("Wallet display name write outcome is uncertain.");
        }
        post([guard, id] { if (guard) guard->publishDisplayNameWrite(id); });
    });
    *weak = operation; m_names_write = operation;
    const auto admitted = operation->start([name, alias] {
        QSettings settings;
        walletidentity::WriteAlias(settings, name, alias);
        settings.sync();
        SettingsResult result; result.saved = settings.status() == QSettings::NoError;
        if (!result.saved) result.error = tr("Wallet display name could not be saved.");
        return result;
    });
    if (!admitted) { m_names_write_pending = false; m_names_write_error = tr("Wallet display name write could not be queued."); }
    Q_EMIT walletDisplayNameWriteChanged(); return bool(admitted);
}
bool WalletQmlController::walletDisplayNameWriteSucceeded() const
{
    if (!m_names_write_receipt) return false;
    std::lock_guard lock{m_names_write_receipt->mutex}; return m_names_write_receipt->ready && m_names_write_receipt->saved;
}
QString WalletQmlController::walletDisplayNameWriteError() const
{
    if (m_names_write_receipt) { std::lock_guard lock{m_names_write_receipt->mutex}; if (m_names_write_receipt->ready) return m_names_write_receipt->error; }
    return m_names_write_error;
}
void WalletQmlController::publishDisplayNameWrite(quint64 id)
{
    if (m_shutting_down || id != m_names_write_id || !m_names_write) return;
    const auto result = m_names_write->result(); if (!result) return;
    m_names_write_pending = false; m_names_write_error = walletDisplayNameWriteError();
    const QPointer<WalletQmlController> guard{this};
    if (walletDisplayNameWriteSucceeded()) refreshDisplayAliases();
    if (guard) Q_EMIT walletDisplayNameWriteChanged();
}
void WalletQmlController::refreshDisplayAliases()
{
    if (m_shutting_down) return;
    if (!m_names_query) {
        const QPointer<WalletQmlController> guard{this};
        m_names_query = std::make_shared<execution::SnapshotQuery<std::shared_ptr<const DisplayAliases>>>(backgroundScheduler(),
            [scheduler = backgroundScheduler()] {
                auto aliases = execution::RetainSnapshot<DisplayAliases>(scheduler, [] {
                    auto result = std::make_unique<DisplayAliases>(); QSettings settings;
                    result->names = walletidentity::ReadAliases(settings);
                    if (settings.status() != QSettings::NoError) throw std::runtime_error("Wallet aliases could not be read");
                    result->home = QDir::homePath(); return result;
                });
                if (!aliases) throw std::runtime_error("Wallet display name is unavailable");
                return std::shared_ptr<const DisplayAliases>{std::move(aliases)};
            }, [guard, post = m_services.post] { post([guard] { if (guard) guard->publishDisplayAliases(); }); });
    }
    m_names_query->request();
}
void WalletQmlController::publishDisplayAliases()
{
    if (m_shutting_down || !m_names_query) return;
    const auto result = m_names_query->take(); if (!result) return;
    if (!result->snapshot || !*result->snapshot) { Q_EMIT walletDisplayNamesChanged(); return; }
    m_names = *result->snapshot; m_home_path = m_names->home;
    m_names_cursor.reset(); applyDisplayAliases(++m_names_generation);
}
void WalletQmlController::applyDisplayAliases(quint64 generation)
{
    if (m_shutting_down || generation != m_names_generation) return;
    const QPointer<WalletQmlController> guard{this};
    int budget{32};
    while (budget--) {
        const auto next = m_names_cursor ? m_registry->information().upper_bound(*m_names_cursor) : m_registry->information().begin();
        if (next == m_registry->information().end()) { Q_EMIT walletDisplayNamesChanged(); return; }
        auto* wallet = next->first; m_names_cursor = wallet;
        applyWalletDisplayName(wallet); if (!guard || m_shutting_down) return;
    }
    QMetaObject::invokeMethod(this, [guard, generation] { if (guard) guard->applyDisplayAliases(generation); }, Qt::QueuedConnection);
}

WalletQmlModel* WalletQmlController::selectedWallet() const
{ return m_selection->selectedWallet(); }

QList<QObject*> WalletQmlController::openWallets() const
{ return m_registry->openWallets(); }

void WalletQmlController::unloadWallets()
{
    beginShutdown();
}

void WalletQmlController::closeAdmission()
{
    m_shutdown_gate->store(true); // O(1): fences every original wallet and form owner.
    if (m_import_error_query) m_import_error_query->close();
    m_import_error_query.reset();
    m_import_error_lines.setValue({}, {}, true);
    m_import_error_help.clear(); m_wallet_load_error.clear(); m_import_error_frame.reset(); m_import_error_projection.reset();
    m_import_error_retry_fixed.reset(); m_import_error_retry_owner.reset(); m_import_error_input.reset();
    m_import_error_pending = false;
    m_create_error_lines.setValue({}, {}, true); m_wallet_create_error.clear(); m_create_error_owner.reset();
    m_migration_required_details.setValue({}, {}, true); m_migration_passphrase_details.setValue({}, {}, true);
    m_wallet_migration_error.clear(); m_migration_error_owner.reset();
    m_shutting_down = true; m_names_ready->store(false);
    // The compatibility facade lends this one current operation's text model.
    // Close that bounded projection synchronously; the registry still retires
    // every other retained form in its ordinary bounded shutdown batches.
    if (m_creation) m_creation->closeAdmission();
    m_wallet_load_in_progress = false; m_wallet_migration_in_progress = false;
    m_command_state = WalletOperationState::WalletOperationClosed;
    m_registry->closeAdmission();
    m_command.reset(); m_command_owner.reset(); m_command_backend.reset();
    m_open_resolution.reset(); m_open_resolution_wallet.clear();
    m_deferred_command_publication = 0;
    m_deferred_notification_publication = false;
    if (m_names_query) m_names_query->close();
    m_names_query.reset(); m_names.reset();
    m_names_write.reset(); m_names_write_pending = false;
    m_location_operation.reset(); m_location_pending = false;
    if (m_signers) m_signers->closeAdmission();
    if (m_empty_wallet) m_empty_wallet->closeAdmission(); // One fixed sentinel, independent of inventory size.
}
void WalletQmlController::setRetirementPostForTesting(std::function<void(std::function<void()>)> post)
{
    if (!m_shutdown_started && post) m_registry->retirement()->post = std::move(post);
}
void WalletQmlController::beginShutdown()
{
    if (std::exchange(m_shutdown_started, true)) return;
    closeAdmission();
    if (m_load_subscription) m_load_subscription->retire();
    const bool loaded_changed = std::exchange(m_is_wallet_loaded, false);
    const bool had_wallets = !m_registry->empty();
    const bool target_changed = m_close_target != nullptr; m_close_target = nullptr;
    m_initialized = false;
    m_empty_wallet->setParent(nullptr);
    m_registry->beginRetirement();
    const QPointer<WalletQmlController> observer{this};
    m_selection->closeAdmission(); if (!observer) return;
    m_selection->clear(); if (!observer) return;
    if (target_changed) { Q_EMIT closeTargetChanged(); if (!observer) return; }
    Q_EMIT initializedChanged(); if (!observer) return;
    if (had_wallets) { Q_EMIT openWalletsChanged(); if (!observer) return; }
    if (loaded_changed) Q_EMIT isWalletLoadedChanged();
}
bool WalletQmlController::adoptRetirement(WalletQmlModel* wallet, bool remove)
{ return m_registry->adoptRetirement(wallet, remove); }
void WalletQmlController::retireWallet(WalletQmlModel* model, bool remove)
{
    if (!model || !m_registry->retirement()->values || m_registry->retirement()->values->retiring.contains(model)) return;
    const QPointer<WalletQmlController> observer{this};
    const bool target = adoptRetirement(model, remove);
    model->beginShutdown(remove);
    if (observer && target) Q_EMIT closeTargetChanged();
}
void WalletQmlController::checkShutdownFinished() { if (m_registry->retirement()) m_registry->retirement()->finish(); }

bool WalletQmlController::registerWalletModel(WalletQmlModel* wallet, std::shared_ptr<OpeningAdoption> adoption)
{ return m_registry->registerModel(wallet, std::move(adoption)); }

void WalletQmlController::removeWalletModel(WalletQmlModel* wallet, bool remove)
{ m_registry->removeModel(wallet, remove); }

WalletQmlModel* WalletQmlController::adoptWalletModel(std::shared_ptr<interfaces::Wallet> wallet, const std::optional<QString>& captured_name, std::shared_ptr<AdoptionOrigin> origin, uint64_t ingress_sequence, walletopening::NativeEvents observation)
{
    // Native outcomes and notifications do not express selection intent.
    const QPointer<WalletQmlController> observer{this};
    const auto operation = m_publishing_command;
    auto* adopted = m_registry->adoptWallet(std::move(wallet), captured_name, std::move(origin), ingress_sequence, std::move(observation));
    if (observer && operation && operation == m_command_id && adopted) recordCommandWallet(adopted);
    return adopted;
}

void WalletQmlController::recordCommandWallet(WalletQmlModel* adopted)
{
    const QPointer<WalletQmlController> observer{this};
    const auto operation = m_publishing_command;
    if (operation && operation == m_command_id && adopted) {
        m_command_adopted = true;
        disconnect(m_result_available_connection); disconnect(m_result_destroyed_connection);
        m_result_wallet = adopted;
        const QPointer<WalletQmlModel> exact{adopted};
        const auto changed = [observer, exact, operation] {
            if (observer && operation == observer->m_command_id && observer->m_result_wallet == exact) Q_EMIT observer->walletOperationChanged();
        };
        m_result_available_connection = connect(adopted, &WalletQmlModel::availableChanged, this, changed);
        m_result_destroyed_connection = connect(adopted, &QObject::destroyed, this, changed);
    }
}

WalletCreationModel::Services WalletQmlController::creationServices()
{
    WalletCreationModel::Services services;
    services.operation.native = {&m_node, {backgroundScheduler(), m_services.post}, m_creation_admission, m_registry->nameRegistry(), m_names_ready, m_shutdown_gate};
    services.operation.publisher = m_registry; services.operation.projection_parent = this;
    const QPointer<WalletQmlController> controller{this};
    services.signer = [controller] {
        if (!controller || !controller->m_signers) return walletcreation::SignerSnapshot{};
        const auto* signers = controller->m_signers;
        return walletcreation::SignerSnapshot{signers->pending(), !signers->effectivePath().isEmpty(), signers->deviceCount(),
            signers->uniqueSignerName(), signers->error().message, signers->uniqueSignerId(), signers->effectiveConfigurationId(), signers->versionGate(), signers->admissionVersion()};
    };
    return services;
}
WalletCreationModel* WalletQmlController::newCreation()
{
    if (m_shutting_down || !m_registry->retained()) return nullptr;
    auto services = creationServices();
    const QPointer<WalletQmlController> owner{this};
    auto* editor = new WalletCreationModel(std::move(services));
    if (!owner) { editor->discard(); editor->deleteLater(); return nullptr; }
    QQmlEngine::setObjectOwnership(editor, QQmlEngine::JavaScriptOwnership);
    const QPointer<WalletCreationModel> original{editor};
    m_registry->registerForm(editor, [original] { if (original) original->discard(); });
    connect(editor, &QObject::destroyed, this, [this, editor] { m_registry->forgetForm(editor); });
    connect(this, &WalletQmlController::initializedChanged, editor, &WalletCreationModel::refreshCapabilities);
    connect(this, &WalletQmlController::walletLoadInProgressChanged, editor, &WalletCreationModel::refreshCapabilities);
    connect(this, &WalletQmlController::externalSignerStatusChanged, editor, &WalletCreationModel::refreshCapabilities);
    connect(editor, &WalletCreationModel::operationCreated, this, &WalletQmlController::trackCreation);
    return editor;
}
void WalletQmlController::trackCreation(WalletOperationModel* operation)
{
    if (!operation || !operation->acceptedForDelivery() || m_shutting_down) return;
    m_creation = operation;
    m_creation_error_data = operation->errorText().constData(); m_creation_warning_data = operation->warningText().constData();
    m_creation_busy = true;
    const QPointer<WalletOperationModel> original{operation};
    m_registry->registerForm(operation, [original] { if (original) original->closeAdmission(); });
    connect(operation, &QObject::destroyed, this, [this, operation] { m_registry->forgetForm(operation); });
    connect(operation, &WalletOperationModel::activated, this, [this](WalletOperationModel* active) {
        if (m_creation == active && m_creation_busy) return;
        m_creation = active; m_creation_busy = true;
        const QPointer<WalletQmlController> guard{this};
        updateNotificationContext();
        if (guard && !m_shutting_down) Q_EMIT walletLoadInProgressChanged();
    });
    connect(operation, &WalletOperationModel::changed, this, [this, original] {
        if (!original || m_shutting_down || m_creation != original) return;
        const QPointer<WalletQmlController> guard{this};
        updateNotificationContext();
        if (!guard || m_shutting_down || !original || m_creation != original) return;
        Q_EMIT walletOperationChanged(); if (!guard || !original) return;
        const auto error = original->errorText().constData();
        const auto warning = original->warningText().constData();
        if (m_creation_error_data != error) { m_creation_error_data = error; Q_EMIT walletCreateErrorChanged(); if (!guard) return; Q_EMIT walletLoadErrorChanged(); if (!guard) return; }
        if (m_creation_warning_data != warning) { m_creation_warning_data = warning; Q_EMIT walletLoadWarningsChanged(); if (!guard) return; }
        const bool busy = walletLoadInProgress();
        if (std::exchange(m_creation_busy, busy) != busy) { Q_EMIT walletLoadInProgressChanged(); if (!guard) return; }
        if (original->kind() == WalletOperationModel::Restore) Q_EMIT walletImportErrorChanged();
        if (!guard) return;
        if (original->kind() == WalletOperationModel::Migrate) {
            Q_EMIT walletMigrationErrorChanged(); if (!guard) return;
            Q_EMIT walletMigrationInProgressChanged(); if (!guard) return;
            if (original->phase() == WalletOperationModel::AwaitingPassphrase) Q_EMIT walletMigrationPassphraseRequired(original->targetWalletId());
        }
    });
    connect(operation, &WalletOperationModel::finished, this, [this, original] {
        if (!original || m_shutting_down || m_creation != original) return;
        // Admission remains held through these reentrant terminal observers.
        // Selection is an independent explicit user intent.
        const QPointer<WalletQmlController> guard{this};
        if (original->kind() == WalletOperationModel::Restore) {
            if (original->phase() == WalletOperationModel::Succeeded) {
                setLastImportedWalletInfo(original->targetWalletId(), original->keyScheme());
                if (guard) Q_EMIT walletImportSucceeded(original->resultWallet());
            }
            return;
        }
        if (original->kind() == WalletOperationModel::Migrate) {
            if (original->phase() == WalletOperationModel::Succeeded) Q_EMIT walletMigrationSucceeded();
            else {
                Q_EMIT walletMigrationErrorNotice(original->targetWalletId(), original->sequence(), false, original->error().message);
                if (guard) Q_EMIT walletMigrationFailed();
            }
            return;
        }
        if (original->phase() == WalletOperationModel::Succeeded && original->canOpenResult()) {
            if (original->creationKind() == walletcreation::Kind::ExternalSigner) Q_EMIT walletLoadSucceeded(original->resultWallet());
            else Q_EMIT walletCreateSucceeded(original->resultWallet());
        }
    });
    const QPointer<WalletQmlController> guard{this};
    updateNotificationContext();
    if (!guard || m_shutting_down) return;
    Q_EMIT walletOperationChanged(); if (guard) Q_EMIT walletLoadInProgressChanged();
}
WalletOperationModel* WalletQmlController::startCreation(WalletCreationModel::Kind kind, const QString& name, const QString& key, const QString& passphrase)
{
    if (!acceptingOperations()) return nullptr;
    const QPointer<WalletQmlController> guard{this};
    m_admitting_command = true;
    const auto release = qScopeGuard([guard] { if (guard) guard->m_admitting_command = false; });
    walletcreation::Request request;
    request.kind = static_cast<walletcreation::Kind>(kind); request.submitted_name = name; request.extended_public_key = key;
    request.encrypt = !passphrase.isEmpty(); request.legacy_normalize = true;
    auto services = creationServices(); request.signer = services.signer();
    if (name.size() > 32768 || key.size() > 4096) request.transport_error = {QStringLiteral("TransportBudget"), tr("This wallet input is too long. No wallet was created."), {}, {}};
    if (kind == WalletCreationModel::Software && request.encrypt && request.transport_error.empty()) {
        auto captured = secretinput::Capture(backgroundScheduler(), passphrase);
        if (!captured) request.transport_error = {QStringLiteral("InputUnavailable"), tr("The password input could not be queued. No wallet was created."), QStringLiteral("credentials"), {}};
        else request.passphrase = request.confirmation = std::move(captured.input);
    }
    auto* operation = WalletOperationModel::Make(services.operation, std::move(request));
    if (!guard) { operation->deleteLater(); return nullptr; }
    if (!operation->acceptedForDelivery()) {
        m_creation = operation;
        Q_EMIT walletOperationChanged(); if (!guard) return nullptr;
        Q_EMIT walletCreateErrorChanged(); if (!guard) return nullptr;
        Q_EMIT walletLoadErrorChanged();
    } else {
        trackCreation(operation);
        if (guard) operation->start();
    }
    return operation;
}
void WalletQmlController::createSingleSigWallet(const QString& name, const QString& passphrase)
{
    if (!m_initialized) { setWalletCreateError(walletDiscoveryError()); return; }
    startCreation(WalletCreationModel::Software, name, {}, passphrase);
}
bool WalletQmlController::createExternalSignerWallet(const QString& name)
{
    if (!m_initialized) { setWalletLoadError(walletDiscoveryError()); return false; }
    if (!canCreateExternalSignerWallet()) {
        const auto message = externalSignerPending() ? tr("Wait for the external signer check to finish.")
            : !externalSignerError().isEmpty() ? externalSignerError()
            : tr("Connect an external signer and try again.");
        setWalletLoadError(message); return false;
    }
    auto* operation = startCreation(WalletCreationModel::ExternalSigner, name, {}, {});
    return operation && operation->acceptedForDelivery();
}
void WalletQmlController::createWatchOnlyWallet(const QString& name, const QString& xpub)
{
    if (!m_initialized) { setWalletLoadError(walletDiscoveryError()); return; }
    startCreation(WalletCreationModel::WatchOnly, name, xpub, {});
}

WalletOperationModel* WalletQmlController::startRestoration(walletrestoration::Request request)
{
    const QPointer<WalletQmlController> guard{this};
    if (!m_initialized || m_initialization_failed || m_shutting_down) request.transport_error = {QStringLiteral("Unavailable"), walletDiscoveryError(), {}, {}};
    const bool previous_busy = walletLoadInProgress();
    const auto operation = QPointer<WalletOperationModel>{WalletOperationModel::Make(creationServices().operation, std::move(request))};
    if (!guard) { if (operation) operation->deleteLater(); return nullptr; }
    if (!operation->acceptedForDelivery()) {
        if (!previous_busy) {
            m_creation = operation; Q_EMIT walletOperationChanged(); if (!guard || !operation) return nullptr;
            Q_EMIT walletLoadErrorChanged(); if (!guard || !operation) return nullptr;
            Q_EMIT walletImportErrorChanged(); if (!guard || !operation) return nullptr;
            if (operation->kind() == WalletOperationModel::Migrate) { Q_EMIT walletMigrationErrorChanged(); if (guard) Q_EMIT walletMigrationFailed(); }
        }
        return operation;
    }
    trackCreation(operation); if (guard && operation) operation->start();
    return operation;
}
WalletOperationModel* WalletQmlController::restore(const QUrl& source)
{
    walletrestoration::Request request; request.kind = walletrestoration::Kind::Restore; request.source = source.toString(QUrl::FullyEncoded);
    return startRestoration(std::move(request));
}
WalletOperationModel* WalletQmlController::restoreLocalFile(const QString& path) { return restore(QUrl::fromLocalFile(path)); }
WalletOperationModel* WalletQmlController::migrate(const QString& wallet_id)
{
    walletrestoration::Request request; request.kind = walletrestoration::Kind::Migrate; request.source = wallet_id;
    const QPointer<WalletQmlController> guard{this}; auto* operation = startRestoration(std::move(request));
    if (guard && operation && operation->acceptedForDelivery()) Q_EMIT migrationOperationOffered(operation);
    return operation;
}
void WalletQmlController::importWallet(const QString& path)
{
    walletrestoration::Request request; request.kind = walletrestoration::Kind::Restore; request.source = path;
    startRestoration(std::move(request));
}

void WalletQmlController::clearWalletCreateStatus()
{
    if (m_creation && m_creation->busy()) return;
    m_creation.clear();
    setWalletCreateError(QString());
}

void WalletQmlController::clearWalletLoadStatus()
{
    if (m_creation_admission->creationPending()) return;
    const QPointer<WalletQmlController> guard{this};
    if (m_command_state != WalletOperationState::WalletOperationPending) { setWalletLoadInProgress(false); if (!guard) return; }
    setWalletLoadError({}); if (guard) setWalletLoadWarnings({});
}

void WalletQmlController::migrateWallet(const QString& path, const QString& passphrase)
{
    walletrestoration::Request request; request.kind = walletrestoration::Kind::Migrate; request.source = path; request.confirmed = true;
    if (!m_initialized || m_initialization_failed || m_shutting_down) request.transport_error = {QStringLiteral("Unavailable"), walletDiscoveryError(), {}, {}};
    if (path.size() > 32768) request.transport_error = {QStringLiteral("TransportBudget"), tr("This wallet input is too long. Nothing was changed."), {}, {}};
    if (!passphrase.isEmpty() && request.transport_error.empty()) {
        auto input = secretinput::Capture(backgroundScheduler(), passphrase);
        if (!input) request.transport_error = {QStringLiteral("InputUnavailable"), tr("The password input could not be queued. No wallet update was started."), {}, {}};
        else request.passphrase = std::move(input.input);
    }
    startRestoration(std::move(request));
}

void WalletQmlController::clearWalletMigrationStatus()
{
    const QPointer<WalletQmlController> guard{this};
    if (m_command_state != WalletOperationState::WalletOperationPending) { setWalletMigrationInProgress(false); if (!guard) return; }
    setWalletMigrationError({});
}

void WalletQmlController::requestOpenWalletSettings(WalletQmlModel* wallet)
{
    if (!wallet || !wallet->available() || !m_registry->positions().contains(wallet)) return;
    Q_EMIT openWalletSettingsRequested(wallet);
}

void WalletQmlController::setExternalSignerServices(execution::SnapshotServices services)
{
    Q_ASSERT(!m_signers);
    m_signer_services = std::move(services);
}
void WalletQmlController::setSignerDiscoveryPorts(signerdiscovery::Ports ports)
{
    Q_ASSERT(!m_signers);
    m_signer_ports = std::move(ports);
}
void WalletQmlController::maybeFinishShutdown()
{
    if (m_shutdown_complete || !m_registry_drained || (m_signers && !m_signers->drained())) return;
    m_shutdown_complete = true;
    Q_EMIT walletsDrained();
}

void WalletQmlController::refreshExternalSignerStatus()
{
    if (m_shutting_down) return;
    if (!m_signers) {
        SignerDiscoveryModel::Services services;
        services.discovery = m_signer_services; // Default owns a separate bounded process lane.
        services.configuration = {backgroundScheduler(), m_services.post};
        services.retire = m_services.post;
        services.ports = m_signer_ports.configuration ? m_signer_ports : signerdiscovery::PersistentPorts(m_node);
        m_signers = new SignerDiscoveryModel(std::move(services), this);
        connect(m_signers, &SignerDiscoveryModel::changed, this, [this] { if (!m_shutting_down) Q_EMIT externalSignerStatusChanged(); });
        connect(m_signers, &SignerDiscoveryModel::shutdownFinished, this, &WalletQmlController::maybeFinishShutdown);
    }
    m_signers->refresh();
}



void WalletQmlController::requestOpenReceive(WalletQmlModel* wallet)
{
    if (!wallet || !wallet->available() || !m_registry->positions().contains(wallet)) return;
    Q_EMIT openReceiveRequested(wallet);
}

void WalletQmlController::requestClosePaymentRequestDetail(WalletQmlModel* wallet)
{
    if (!wallet || !wallet->available() || !m_registry->positions().contains(wallet)) return;
    Q_EMIT closePaymentRequestDetailRequested(wallet);
}

QString WalletQmlController::walletImportErrorTitle() const { return m_creation && m_creation->kind() == WalletOperationModel::Restore ? m_creation->errorTitle() : m_import_error_title; }
QString WalletQmlController::walletImportErrorDescription() const { return m_creation && m_creation->kind() == WalletOperationModel::Restore ? m_creation->errorDescription() : m_import_error_description; }
QString WalletQmlController::walletImportErrorHelpText() const { return m_creation && m_creation->kind() == WalletOperationModel::Restore ? m_creation->errorText() : m_import_error_help; }
QString WalletQmlController::currentCommandErrorDescription() const
{
    if (!m_command_receipt) return {};
    std::lock_guard lock{m_command_receipt->mutex}; return m_command_receipt->error_description;
}

QString WalletQmlController::walletDisplayNameKey(const QString& path) const
{
    return QStringLiteral("walletDisplayNames/%1").arg(path);
}

void WalletQmlController::applyWalletDisplayName(WalletQmlModel* wallet_model) const
{
    if (!wallet_model) {
        return;
    }
    wallet_model->setDisplayName(walletDisplayName(wallet_model->name()));
}
void WalletQmlController::startWalletImport(const QString& path) { importWallet(path); }

QString WalletQmlController::walletDiscoveryError() const
{
    return m_initialization_failed ? tr("Wallet discovery is unavailable. Restart the app to refresh the wallet list.") :
        tr("Wallets are still loading. Try again in a moment.");
}

void WalletQmlController::consumeWalletNotifications()
{
    if (m_shutting_down || m_initialization_failed || !m_initialized) return;
    if (m_registry->adoptionActive() || m_projecting_notifications) {
        m_deferred_notification_publication = true;
        return;
    }
    const QPointer<WalletQmlController> guard{this};
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
            setWalletLoadError(tr("Wallet updates are unavailable. Restart the app to refresh the wallet list."));
            if (guard) m_load_subscription->retire();
            return;
        }
        const auto name = QString::fromStdString(entry.name);
        auto* adopted = adoptWalletModel(entry.wallet, name, origin, entry.ingress_sequence,
            {m_registry->eventClock(), entry.unload_observation ? entry.unload_observation->unloaded : nullptr, entry.unload_observation});
        if (!guard || m_shutting_down) return;
        if (adopted && m_command_is_open && m_command_state == WalletOperationState::WalletOperationPending) {
            m_registry->observeOpening(origin, m_registry->commandOrigin(), name, adopted, entry.notification_sequence, entry.ingress_sequence);
        }
    }
}

void WalletQmlController::flushDeferredPublications()
{
    if (m_shutting_down || m_publishing_command || m_registry->adoptionActive() || m_projecting_notifications) return;
    const auto command = std::exchange(m_deferred_command_publication, 0);
    const bool notifications = std::exchange(m_deferred_notification_publication, false);
    if (!command && !notifications) return;
    const QPointer<WalletQmlController> observer{this};
    m_services.post([observer, command, notifications] {
        if (!observer || observer->m_shutting_down) return;
        if (command) observer->publishWalletCommand(command);
        if (observer && !observer->m_shutting_down && notifications) observer->consumeWalletNotifications();
    });
}

std::shared_ptr<WalletQmlController::AdoptionOrigin> WalletQmlController::newAdoptionOrigin()
{ return m_registry->newOrigin(); }
size_t WalletQmlController::adoptionFenceCount() const
{
    return m_registry->retirement()->values ? m_registry->retirement()->values->closed.size() : 0;
}
void WalletQmlController::updateNotificationContext()
{
    if (!m_load_subscription || m_shutting_down) return;
    const auto subscription = m_load_subscription;
    auto origin = walletLoadInProgress() && m_registry->commandOrigin() ? m_registry->commandOrigin() : m_registry->notificationOrigin();
    if (!origin) {
        subscription->rejectNotificationContext();
        return;
    }
    const auto deferred_name = m_creation && m_creation->busy() && m_creation->creationKind() == walletcreation::Kind::WatchOnly ? m_creation->targetWalletId() : m_deferred_wallet_name;
    subscription->setNotificationContext(deferred_name, walletLoadInProgress(), m_selection->revision(), std::move(origin), m_creation && m_creation_admission->creationPending() ? m_creation->nativeObservation() : nullptr);
}
void WalletQmlController::closeOpeningAdoption(WalletQmlModel* wallet)
{ m_registry->closeOpening(wallet); }
void WalletQmlController::adoptionUnavailable(bool closed)
{
    if (m_shutting_down) return;
    if (!m_publishing_command || m_publishing_command != m_command_id) {
        if (!closed) setWalletLoadError(tr("The wallet could not be shown. Try again after pending operations finish."));
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
    const QPointer<WalletQmlController> observer{this};
    setWalletLoadError(message);
    if (observer && !observer->m_shutting_down && operation == observer->m_command_id) Q_EMIT walletLoadErrorNotice(name, operation, message);
}

void WalletQmlController::initialize()
{
    if (m_initialized || m_initializing || m_initialization_failed || m_shutting_down) return;
    if (!m_registry->retirement()->retained) { m_initialization_failed = true; setWalletLoadError(tr("Wallet loading could not be started.")); return; }
    if (!m_registry->openWalletModel() || !m_registry->openWalletModel()->valid()) { m_initialization_failed = true; setWalletLoadError(tr("Wallet presentation is unavailable.")); return; }
    // wallet_loader is not set when -disablewallet is passed; bail out.
    if (gArgs.GetBoolArg("-disablewallet", false)) return;
    m_initializing = true;
    const QPointer<WalletQmlController> guard{this};
    // Pinned Core notifies before AddWallet. Its initial capture is not a
    // coherent resynchronization capability. Seed once and retain notification
    // handles; the subtree wallet/node interfaces remain unchanged.
    m_load_subscription = std::make_shared<walletlife::LoadSubscription>(
        backgroundScheduler(),
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
        setWalletLoadError(tr("Wallet loading could not be started."));
    }
}

void WalletQmlController::handleLifecyclePublication(walletlife::Update update)
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
        const QPointer<WalletQmlController> guard{this};
        const bool initialized_changed = m_initialized;
        const QString error = update.reason == walletlife::FailureReason::IngressCapacity ?
            tr("Wallet discovery is unavailable because it could not keep up with wallet notifications. Restart the app to refresh the wallet list.") :
            update.kind == walletlife::Publication::Failed ? tr("Wallet loading failed.") :
            update.cleanup_failed ? tr("Wallet notification cleanup failed.") : tr("Wallet notifications are unavailable.");
        m_initializing = false;
        m_initialized = false; m_names_ready->store(false);
        m_initialization_failed = true;
        setWalletLoadError(error); if (!guard || m_shutting_down) return;
        if (initialized_changed) {
            Q_EMIT initializedChanged();
            if (!guard || m_shutting_down) return;
        }
        flushDeferredPublications();
        break;
    }
    }
}

void WalletQmlController::finishInitialization()
{
    if (m_shutting_down || m_initialization_failed || !m_initializing) return;
    const QPointer<WalletQmlController> guard{this};
    auto handoff = m_load_subscription->takeInitial();
    if (!handoff) return;
    if (!handoff->entries.empty()) {
        for (const auto& entry : handoff->entries) {
            if (entry.unload_observation && entry.unload_observation->unloaded->load()) continue;
            auto adoption = WalletRegistry::retain<OpeningAdoption>(backgroundScheduler(), [] { return std::make_unique<OpeningAdoption>(); });
            if (!guard || m_shutting_down) return;
            if (!adoption) {
                m_initialization_failed = true;
                m_initializing = false;
                setWalletLoadError(tr("Wallets could not be shown. Try again after pending operations finish."));
                if (guard) m_load_subscription->retire();
                return;
            }
            if (entry.unload_observation) adoption->unloaded = entry.unload_observation->unloaded;
            auto* wallet_model = new WalletQmlModel(WalletQmlModel::SharedWallet{entry.wallet, backgroundScheduler(), QString::fromStdString(entry.name), m_preferences, m_fee_policy, m_shutdown_gate, {m_registry->eventClock(), adoption->unloaded, entry.unload_observation}, true}, &m_node, FeeReadServices{}, psbtServices(), backupServices());
            if (!registerWalletModel(wallet_model, adoption)) {
                if (!guard || m_shutting_down) return;
                retireWallet(wallet_model, false);
                m_initialization_failed = true;
                m_initializing = false;
                setWalletLoadError(tr("Wallets could not be shown. Try again after pending operations finish."));
                if (!guard || m_shutting_down) return;
                m_load_subscription->retire();
                return;
            }
            if (!wallet_model->available()) { retireWallet(wallet_model, false); continue; }
            applyWalletDisplayName(wallet_model);
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
    refreshExternalSignerStatus();
    if (!guard || m_shutting_down) return;
    Q_EMIT initializedChanged();
    if (!guard || m_shutting_down) return;
    consumeWalletNotifications();
}

void WalletQmlController::setWalletLoaded(bool loaded)
{
    if (m_is_wallet_loaded != loaded) {
        m_is_wallet_loaded = loaded;
        Q_EMIT isWalletLoadedChanged();
    }
}

void WalletQmlController::setNoWalletsFound(bool no_wallets_found)
{
    if (m_no_wallets_found != no_wallets_found) {
        m_no_wallets_found = no_wallets_found;
        Q_EMIT noWalletsFoundChanged();
    }
}

void WalletQmlController::startWalletLoad(const QString& path, const QString& wallet_format)
{
    const QPointer<WalletQmlController> admission_observer{this};
    m_command_requested_target = path;
    if (m_shutting_down || m_command_state == WalletOperationState::WalletOperationPending || m_creation_admission->creationPending()) return;
    m_command_state = WalletOperationState::WalletOperationPending;
    clearWalletMigrationStatus(); if (!admission_observer || m_shutting_down) return; clearWalletLoadStatus(); if (!admission_observer || m_shutting_down) return;
    setWalletLoadInProgress(true); if (!admission_observer || m_shutting_down) return;
    const QPointer<WalletQmlController> guard{this};
    Q_EMIT walletLoadStateChanged(path, WalletListModel::LoadState::Loading, {}); if (!guard || m_shutting_down) return;
    submitWalletCommand([node = &m_node, path, wallet_format] {
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
        else out.wallet = std::move(*wallet);
        describeCommandWallet(out, out.reference, QString::fromStdString(node->walletLoader().getWalletDir()));
        return out;
    }, [this, path](const CommandResult& out) {
        const QPointer<WalletQmlController> guard{this};
        setWalletLoadInProgress(false); if (!guard) return;
        setWalletLoadWarnings(out.warnings); if (!guard) return;
        if (out.needs_migration) { commitWalletCommandOutcome(); Q_EMIT walletMigrationRequired(out.reference); return; }
        if (!out.has_wallet || out.incomplete) {
            commitWalletCommandOutcome();
            const auto error = out.error.isEmpty() ? tr("Wallet could not be opened.") : out.error;
            setWalletLoadError(error); if (!guard) return;
            Q_EMIT walletLoadErrorNotice(path, m_command_id, currentCommandErrorDescription()); if (!guard || m_shutting_down) return;
            Q_EMIT walletLoadStateChanged(path, WalletListModel::LoadState::LoadError, error); return;
        }
        auto* wallet = adoptWalletModel(out.walletForAdoption(), out.name);
        if (guard && wallet) {
            commitWalletCommandOutcome(); offerWalletLoadWarnings(out.name, out.warnings);
            if (guard) Q_EMIT walletLoadSucceeded(wallet);
        }
    }, [this, path](std::exception_ptr failure) {
        const QPointer<WalletQmlController> guard{this};
        setWalletLoadInProgress(false); if (!guard) return;
        const auto error = execution::NotStartedCause(failure) ? tr("Wallet open could not be queued. No wallet was opened.")
            : tr("Wallet open outcome is uncertain. Check the wallet directory before retrying.");
        setWalletLoadError(error); if (!guard || m_shutting_down) return;
        Q_EMIT walletLoadErrorNotice(path, m_command_id, currentCommandErrorDescription()); if (guard && !m_shutting_down) Q_EMIT walletLoadStateChanged(path, WalletListModel::LoadState::LoadError, error);
    }, true);
}

void WalletQmlController::setWalletCreateError(const QString& error)
{
    if (m_shutting_down || !commandPublicationCurrent()) return;
    if ((error.isEmpty() && m_wallet_create_error.isEmpty()) ||
        (error.size() == m_wallet_create_error.size() && error.constData() == m_wallet_create_error.constData())) return;
    std::shared_ptr<const void> owner;
    QString display;
    if (m_command && m_command_owner) {
        const auto actual = m_command->result();
        if (actual && actual->value && actual->value->error.size() == error.size() && actual->value->error.constData() == error.constData()) {
            owner = m_command_owner;
            std::lock_guard lock{m_command_receipt->mutex}; display = m_command_receipt->error_display;
        }
    }
    if (!owner && error.size() > 1024) {
        m_wallet_create_error = tr("Wallet error details could not be retained. Check the latest wallet operation before retrying.");
        display = m_wallet_create_error;
    } else {
        m_wallet_create_error = error;
        if (!owner) display = error;
    }
    m_create_error_owner = std::move(owner); m_create_error_display = std::move(display);
    const QPointer<WalletQmlController> observer{this};
    const auto original = m_wallet_create_error.constData();
    m_create_error_lines.setValue(m_wallet_create_error, m_create_error_owner);
    if (!observer || m_shutting_down || original != m_wallet_create_error.constData()) return;
    Q_EMIT walletCreateErrorChanged();
}

void WalletQmlController::setWalletLoadInProgress(bool in_progress)
{
    if (!in_progress && m_publishing_command && !m_shutting_down) { m_complete_load_publication = true; return; }
    const QPointer<WalletQmlController> observer{this};
    const auto command = m_command_id;
    const bool changed = m_wallet_load_in_progress != in_progress;
    m_wallet_load_in_progress = in_progress;
    m_creation_admission->setLegacyBusy(m_wallet_load_in_progress || m_wallet_migration_in_progress);
    if (!in_progress) {
        auto origin = newAdoptionOrigin();
        if (!observer || observer->m_shutting_down || command != observer->m_command_id) return;
        m_registry->setNotificationOrigin(std::move(origin));
    }
    updateNotificationContext();
    if (observer && !observer->m_shutting_down && command == observer->m_command_id && changed) Q_EMIT walletLoadInProgressChanged();
}

void WalletQmlController::setImportErrorServicesForTesting(execution::SnapshotServices services)
{
    if (m_shutting_down || m_import_error_query) return;
    if (services.scheduler) m_import_error_services.scheduler = std::move(services.scheduler);
    if (services.post) m_import_error_services.post = std::move(services.post);
}
void WalletQmlController::setWalletLoadError(const QString& error) { captureWalletLoadError(error); }
bool WalletQmlController::captureWalletLoadError(const QString& error)
{
    if (m_shutting_down || !commandPublicationCurrent() || !m_import_error_input) return false;
    if (!m_import_error_unavailable && ((error.isEmpty() && m_wallet_load_error.isEmpty()) ||
        (error.size() == m_wallet_load_error.size() && error.constData() == m_wallet_load_error.constData()))) return true;
    constexpr size_t MAX_FRAMES{128}, MAX_BYTES{16 * 1024 * 1024};
    const auto input = m_import_error_input;
    const auto revision = ++m_import_error_revision;
    if (m_import_error_query) m_import_error_query->take(); // Retire an older immediate transport failure before accepting a new origin.
    bool accepted{false};
    {
        std::lock_guard lock{input->mutex};
        const size_t bytes = error.size() <= qsizetype(MAX_BYTES / sizeof(QChar)) ? size_t(error.size()) * sizeof(QChar) : MAX_BYTES + 1;
        if (input->budget->frames < MAX_FRAMES && bytes <= MAX_BYTES - input->budget->bytes.load()) {
            ++input->budget->frames; input->budget->bytes += bytes;
            std::shared_ptr<const ImportErrorFrame> frame;
            try { frame = std::make_shared<ImportErrorFrame>(input->budget, error, revision); }
            catch (...) { --input->budget->frames; input->budget->bytes -= bytes; throw; }
            if (input->current) input->retired.emplace(input->current.get(), input->current);
            input->current = frame;
            // Native cannot retire an old frame until every GUI alias has
            // moved. No callback/signal runs while this bounded handoff locks.
            m_import_error_frame = frame; m_wallet_load_error = frame->error;
            m_import_error_title = error.isEmpty() ? QString{} : tr("This wallet couldn't be imported");
            m_import_error_display = error.isEmpty() ? QString{} : tr("Preparing wallet error details…");
            m_import_error_description = error.isEmpty() ? QString{} : tr("Preparing wallet error details…");
            m_import_error_help.clear(); m_import_error_projection.reset();
            accepted = true;
        }
    }
    const QPointer<WalletQmlController> observer{this};
    if (!accepted) {
        const bool hidden = m_import_error_unavailable;
        m_import_error_unavailable = true; m_import_error_pending = false; m_import_error_visibility_changed = true;
        m_import_error_projection_error = tr("Error details could not be retained. Retry showing the details after background work finishes.");
        m_import_error_title = tr("Wallet error details are unavailable");
        m_import_error_description = tr("Check the latest wallet operation before trying another wallet action.");
        m_import_error_help.clear(); m_import_error_projection.reset();
        m_import_error_retry_fixed.reset(); m_import_error_retry_owner.reset();
        if (error.size() <= 4096) m_import_error_retry_fixed = error; // Bounded local diagnostic, never a large GUI capture.
        else if (m_command && m_command_owner) {
            const auto actual = m_command->result();
            if (actual && actual->value && actual->value->error.size() == error.size() && actual->value->error.constData() == error.constData()) m_import_error_retry_owner = m_command_owner;
        }
        m_import_error_lines.setValue({}, {}); if (!observer || m_shutting_down || revision != m_import_error_revision) return false;
        prepareImportErrorProjection(); // Dispose older input frames on Native even while latest capture failed.
        if (!hidden) { Q_EMIT walletLoadErrorChanged(); if (!observer || m_shutting_down) return false; }
        Q_EMIT walletImportErrorChanged(); return false;
    }
    m_import_error_visibility_changed |= m_import_error_unavailable;
    m_import_error_unavailable = false; m_import_error_pending = true; m_import_error_projection_error.clear();
    m_import_error_retry_owner.reset(); m_import_error_retry_fixed.reset();
    m_import_error_lines.setValue({}, {}); if (!observer || m_shutting_down || revision != m_import_error_revision) return true;
    prepareImportErrorProjection();
    Q_EMIT walletImportErrorChanged(); return true;
}
bool WalletQmlController::retryWalletImportErrorPresentation()
{
    if (m_shutting_down) return false;
    if (!m_import_error_unavailable) {
        if (m_import_error_projection_error.isEmpty()) return false;
        m_import_error_pending = true; m_import_error_projection_error.clear(); prepareImportErrorProjection(); Q_EMIT walletImportErrorChanged(); return true;
    }
    const auto owner = m_import_error_retry_owner;
    if (owner && *owner) {
        const auto actual = (*owner)->result();
        if (actual && actual->value) return captureWalletLoadError(actual->value->error);
    }
    if (m_import_error_retry_fixed) { const auto fixed = *m_import_error_retry_fixed; return captureWalletLoadError(fixed); }
    return false; // Unknown source lifetime cannot be recreated as a false new native error.
}
void WalletQmlController::prepareImportErrorProjection()
{
    if (m_shutting_down || !m_import_error_input) return;
    if (!m_import_error_query) {
        const QPointer<WalletQmlController> observer{this};
        m_import_error_query = std::make_shared<execution::SnapshotQuery<std::shared_ptr<const ImportErrorProjection>>>(m_import_error_services.scheduler,
            [input = m_import_error_input, scheduler = m_import_error_services.scheduler] {
                std::shared_ptr<const ImportErrorFrame> frame, previous;
                std::map<const ImportErrorFrame*, std::shared_ptr<const ImportErrorFrame>> retired;
                { std::lock_guard lock{input->mutex}; frame = input->current; previous = input->notified; retired.swap(input->retired); }
                retired.clear(); // Every large retired last reference is released on Native.
                if (!frame) throw execution::NotStarted{execution::NotStarted::Cause::Cancelled};
                try {
                    auto result = execution::RetainSnapshot<ImportErrorProjection>(scheduler, [&] {
                        auto value = std::make_unique<ImportErrorProjection>(); value->frame = frame; value->previous = previous; value->revision = frame->revision;
                        value->text = PrepareImportError(frame->error);
                        value->display = frame->error.left(1024);
                        value->details = frame->error;
                        if (!value->text.help.isEmpty() && value->text.help != frame->error.trimmed()) value->details.prepend(value->text.help + QStringLiteral("\n\n"));
                        value->changed = previous ? previous->error != frame->error : !frame->error.isEmpty();
                        return value;
                    });
                    if (result) return std::shared_ptr<const ImportErrorProjection>{std::move(result)};
                } catch (const std::exception&) { }
                // A failed projection carries only bounded scalar context. Its
                // actual large source remains in the Native input channel.
                auto failed = std::make_shared<ImportErrorProjection>(); failed->revision = frame->revision; failed->failed = true;
                return std::shared_ptr<const ImportErrorProjection>{std::move(failed)};
            }, [observer, post = m_import_error_services.post] { post([observer] { if (observer) observer->publishImportErrorProjection(); }); }, execution::Lane::Domain);
    }
    m_import_error_query->request();
}
void WalletQmlController::publishImportErrorProjection()
{
    if (m_shutting_down || !m_import_error_query) return;
    const auto result = m_import_error_query->take(); if (!result) return;
    if (!result->snapshot || !*result->snapshot) {
        if (m_import_error_unavailable) return;
        m_import_error_pending = false; m_import_error_projection_error = tr("Wallet error details could not be prepared. Retry showing the details.");
        Q_EMIT walletImportErrorChanged(); return;
    }
    const auto projection = *result->snapshot;
    if (m_import_error_unavailable || projection->revision != m_import_error_revision) return;
    if (projection->failed) {
        m_import_error_pending = false; m_import_error_projection_error = tr("Wallet error details could not be prepared. Retry showing the details.");
        Q_EMIT walletImportErrorChanged(); return;
    }
    {
        std::lock_guard lock{m_import_error_input->mutex};
        // A newer GUI acknowledgement can overtake an already-running read.
        // Recompute the Native equality decision instead of comparing text here.
        if (m_import_error_input->notified != projection->previous) { m_import_error_query->request(); return; }
        m_import_error_input->notified = projection->frame;
    }
    m_import_error_display = projection->display;
    m_import_error_title = projection->text.title; m_import_error_description = projection->text.description; m_import_error_help = projection->text.help;
    m_import_error_projection = projection; m_import_error_pending = false; m_import_error_projection_error.clear();
    const bool raw_changed = projection->changed || std::exchange(m_import_error_visibility_changed, false);
    const QPointer<WalletQmlController> observer{this};
    m_import_error_lines.setValue(projection->details, projection); if (!observer || m_shutting_down || projection->revision != m_import_error_revision) return;
    if (raw_changed) { Q_EMIT walletLoadErrorChanged(); if (!observer || m_shutting_down) return; }
    Q_EMIT walletImportErrorChanged();
}

void WalletQmlController::offerWalletLoadWarnings(const QString& wallet, const QString& warnings)
{
    if (warnings.isEmpty() || !m_command_owner || m_shutting_down) return;
    const QPointer<WalletQmlController> guard{this};
    auto* notice = WalletOperationModel::OpenNotice(creationServices().operation, m_command_id, wallet, warnings, m_command_owner, m_result_wallet);
    if (!guard) { notice->deleteLater(); return; }
    const QPointer<WalletOperationModel> original{notice};
    m_registry->registerForm(notice, [original] { if (original) original->closeAdmission(); });
    connect(notice, &QObject::destroyed, this, [this, notice] { m_registry->forgetForm(notice); });
    Q_EMIT walletLoadWarningsOffered(notice);
}

void WalletQmlController::setWalletLoadWarnings(const QString& warnings)
{
    if (m_wallet_load_warnings != warnings) {
        m_wallet_load_warnings = warnings;
        Q_EMIT walletLoadWarningsChanged();
    }
}

void WalletQmlController::setWalletMigrationInProgress(bool in_progress)
{
    if (!in_progress && m_publishing_command && !m_shutting_down) { m_complete_migration_publication = true; return; }
    if (m_wallet_migration_in_progress != in_progress) {
        m_wallet_migration_in_progress = in_progress;
    m_creation_admission->setLegacyBusy(m_wallet_load_in_progress || m_wallet_migration_in_progress);
        Q_EMIT walletMigrationInProgressChanged();
    }
}

QAbstractItemModel* WalletQmlController::captureMigrationErrorDetails(quint64 operation, int slot)
{
    if (m_creation && m_creation->kind() == WalletOperationModel::Migrate && operation == m_creation->sequence() && (slot == 0 || slot == 1)) return m_creation->errorDetails();
    if (m_shutting_down || operation != m_migration_error_operation || (slot != 0 && slot != 1)) return nullptr;
    const auto revision = m_migration_error_revision;
    auto* lines = slot == 0 ? &m_migration_required_details : &m_migration_passphrase_details;
    const QPointer<WalletQmlController> observer{this};
    lines->setValue(m_wallet_migration_error, m_migration_error_owner);
    return observer && !m_shutting_down && revision == m_migration_error_revision && operation == m_migration_error_operation ? lines : nullptr;
}
quint64 WalletQmlController::setWalletMigrationError(const QString& error)
{
    if (m_shutting_down || !commandPublicationCurrent()) return m_migration_error_revision;
    if ((error.isEmpty() && m_wallet_migration_error.isEmpty()) ||
        (m_migration_error_operation == m_command_id && error.size() == m_wallet_migration_error.size() && error.constData() == m_wallet_migration_error.constData())) return m_migration_error_revision;
    std::shared_ptr<const void> owner;
    QString display;
    bool passphrase{false};
    if (m_command && m_command_owner) {
        const auto actual = m_command->result();
        if (actual && actual->value && actual->value->error.size() == error.size() && actual->value->error.constData() == error.constData()) {
            owner = m_command_owner;
            std::lock_guard lock{m_command_receipt->mutex};
            display = m_command_receipt->error_display; passphrase = m_command_receipt->error_needs_passphrase;
        }
    }
    // All non-command callers supply fixed bounded diagnostics. An unknown
    // large producer cannot lend unowned text to a GUI cache.
    if (!owner && error.size() > 1024) {
        m_wallet_migration_error = tr("Wallet error details could not be retained. Check the latest wallet operation before retrying.");
        display = m_wallet_migration_error;
    } else {
        m_wallet_migration_error = error;
        if (!owner) display = error;
    }
    m_migration_error_owner = std::move(owner); m_migration_error_display = std::move(display);
    m_migration_error_needs_passphrase = passphrase; m_migration_error_operation = m_command_id;
    const auto revision = ++m_migration_error_revision;
    Q_EMIT walletMigrationErrorChanged();
    return revision;
}

QString WalletQmlController::describeImportedWalletKeyScheme(interfaces::Wallet& imported_wallet) const
{
    return WalletQmlModel::keySchemeDisplayText(WalletQmlModel::keySchemeForWallet(imported_wallet));
}

void WalletQmlController::setLastImportedWalletInfo(const QString& wallet_name, const QString& key_scheme)
{
    if (m_last_imported_wallet_name == wallet_name &&
        m_last_imported_wallet_key_scheme == key_scheme) {
        return;
    }

    m_last_imported_wallet_name = wallet_name;
    m_last_imported_wallet_key_scheme = key_scheme;
    Q_EMIT lastImportedWalletInfoChanged();
}

void WalletQmlController::clearLastImportedWalletInfo()
{
    setLastImportedWalletInfo(QString(), QString());
}



void WalletQmlController::setWalletLocationOpenError(const QString& error)
{
    if (m_wallet_location_open_error == error) {
        return;
    }

    m_wallet_location_open_error = error;
    Q_EMIT walletLocationOpenErrorChanged();
}

void WalletQmlController::setOpenLocalPathFnForTesting(OpenLocalPathFn fn)
{
    m_open_local_path_fn = std::move(fn);
}

WalletQmlModel* WalletQmlController::walletOperationResultWallet() const
{
    return m_creation ? m_creation->resultWallet() : m_result_wallet.data();
}
bool WalletQmlController::canOpenResult() const
{
    if (m_creation) return !m_shutting_down && m_creation->canOpenResult();
    return !m_shutting_down && m_result_wallet && m_result_wallet->available() && m_registry->containsExact(m_result_wallet);
}

WalletQmlController::WalletAdoptionState WalletQmlController::walletOperationAdoptionState() const
{
    if (!m_creation) return m_adoption_state;
    return m_creation->canOpenResult() ? WalletAdoptionState::AdoptionAvailable : m_creation->busy() ? WalletAdoptionState::AdoptionPending
        : m_creation->resultWallet() && !m_creation->resultWallet()->available() ? WalletAdoptionState::AdoptionClosed : WalletAdoptionState::AdoptionUnavailable;
}
QString WalletQmlController::walletOperationAdoptionError() const { return m_creation ? m_creation->error().message : m_adoption_error; }
