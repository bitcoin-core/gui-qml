// Copyright (c) 2024-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/walletqmlcontroller.h>

#include <qml/models/walletqmlmodel.h>
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
#include <wallet/scriptpubkeyman.h>
#include <wallet/wallet.h>
#include <util/translation.h>
#include <util/threadnames.h>

#include <algorithm>
#include <stdexcept>
#include <utility>

#include <QDir>
#include <QDesktopServices>
#include <QFileInfo>
#include <QMetaObject>
#include <QRegularExpression>
#include <QSettings>
#include <QStringList>
#include <QTimer>
#include <QUrl>

namespace {
QString JoinWarnings(const std::vector<bilingual_str>& warnings)
{
    QStringList lines;
    for (const auto& warning : warnings) {
        if (!warning.translated.empty()) {
            lines.append(QString::fromStdString(warning.translated));
        }
    }
    return lines.join('\n');
}

bool IsBackupLikeFile(const QFileInfo& file_info)
{
    const QString file_name = file_info.fileName().toLower();
    return file_name.endsWith(".bak") || file_name.endsWith(".legacy.bak");
}

bool ErrorContains(const QString& error, const QString& needle)
{
    return error.contains(needle, Qt::CaseInsensitive);
}
} // namespace

WalletQmlController::WalletQmlController(interfaces::Node& node, QObject *parent)
    : QObject(parent)
    , m_node(node)
    , m_empty_wallet(new WalletQmlModel(&node, this))
    , m_selected_wallet(m_empty_wallet)
    , m_worker(new QObject)
    , m_worker_thread(new QThread(this))
    , m_open_local_path_fn([](const QString& path) {
        return QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    })
{
    m_worker->moveToThread(m_worker_thread);
    m_worker_thread->start();
    QTimer::singleShot(0, m_worker, []() {
        util::ThreadRename("qml-walletctrl");
    });
}

WalletQmlController::~WalletQmlController()
{
    if (m_handler_load_wallet) {
        m_handler_load_wallet->disconnect();
    }
    m_worker_thread->quit();
    m_worker_thread->wait();
    delete m_worker;
    delete m_empty_wallet;
}

void WalletQmlController::setSelectedWallet(QString path, QString wallet_format)
{
    if (!m_initialized) {
        setWalletLoadError(tr("Wallets are still loading. Try again in a moment."));
        return;
    }

    if (!m_wallets.empty()) {
        for (WalletQmlModel* wallet : m_wallets) {
            if (wallet->name() == path) {
                clearWalletLoadStatus();
                m_selected_wallet = wallet;
                Q_EMIT selectedWalletChanged();
                return;
            }
        }
    }

    startWalletLoad(path, wallet_format);
}

bool WalletQmlController::isWalletOpen(const QString& path)
{
    if (path.trimmed().isEmpty()) {
        return false;
    }

    QMutexLocker locker(&m_wallets_mutex);
    for (WalletQmlModel* wallet : m_wallets) {
        if (wallet->name() == path) {
            return true;
        }
    }
    return false;
}

void WalletQmlController::publishWalletInfo(WalletQmlModel* wallet_model)
{
    if (!wallet_model) return;
    Q_EMIT walletInfoChanged(
        wallet_model->name(),
        wallet_model->balance(),
        static_cast<int>(wallet_model->keySchemeKind()));
}

void WalletQmlController::publishOpenWalletsInfo()
{
    std::vector<WalletQmlModel*> snapshot;
    {
        QMutexLocker locker(&m_wallets_mutex);
        snapshot = m_wallets;
    }
    for (WalletQmlModel* wallet_model : snapshot) {
        publishWalletInfo(wallet_model);
    }
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
}

QString WalletQmlController::homePath() const
{
    return QDir::homePath();
}

QString WalletQmlController::selectedWalletLocationPath() const
{
    if (!m_selected_wallet || m_selected_wallet == m_empty_wallet) {
        return {};
    }

    const QString wallet_name{m_selected_wallet->name().trimmed()};
    if (wallet_name.isEmpty()) {
        return {};
    }

    QFileInfo wallet_path{wallet_name};
    if (!wallet_path.isAbsolute()) {
        const QDir wallet_dir{QString::fromStdString(m_node.walletLoader().getWalletDir())};
        wallet_path = QFileInfo{wallet_dir.filePath(wallet_name)};
    }

    return wallet_path.absoluteFilePath();
}

bool WalletQmlController::openSelectedWalletLocation()
{
    clearWalletLocationOpenError();

    const QString wallet_path{selectedWalletLocationPath()};
    if (wallet_path.isEmpty()) {
        setWalletLocationOpenError(tr("No wallet file is available to view."));
        return false;
    }

    const QFileInfo wallet_location{wallet_path};
    if (!wallet_location.exists()) {
        setWalletLocationOpenError(tr("Wallet file not found: %1").arg(wallet_path));
        return false;
    }

    const QString open_path = wallet_location.isDir()
        ? wallet_location.absoluteFilePath()
        : wallet_location.absolutePath();
    if (!m_open_local_path_fn(open_path)) {
        setWalletLocationOpenError(tr("Could not open wallet file location."));
        return false;
    }

    return true;
}

void WalletQmlController::clearWalletLocationOpenError()
{
    setWalletLocationOpenError({});
}

void WalletQmlController::closeWallet(const QString& path)
{
    if (!m_initialized) {
        setWalletLoadError(tr("Wallets are still loading. Try again in a moment."));
        return;
    }

    if (path.trimmed().isEmpty()) {
        return;
    }

    clearWalletLoadStatus();
    clearWalletMigrationStatus();

    WalletQmlModel* wallet_to_close{nullptr};

    {
        QMutexLocker locker(&m_wallets_mutex);
        const auto wallet_it = std::find_if(m_wallets.begin(), m_wallets.end(), [&](WalletQmlModel* wallet) {
            return wallet->name() == path;
        });
        if (wallet_it == m_wallets.end()) {
            return;
        }

        wallet_to_close = *wallet_it;
    }

    wallet_to_close->removeWallet();
    removeWalletModel(wallet_to_close);
}

QString WalletQmlController::walletDisplayName(const QString& path) const
{
    const QString trimmed_path = path.trimmed();
    if (trimmed_path.isEmpty()) {
        return {};
    }

    QSettings settings;
    const QString display_name = settings.value(walletDisplayNameKey(trimmed_path)).toString().trimmed();
    return display_name.isEmpty() ? trimmed_path : display_name;
}

bool WalletQmlController::setWalletDisplayName(const QString& path, const QString& display_name)
{
    const QString trimmed_path = path.trimmed();
    if (trimmed_path.isEmpty()) {
        return false;
    }

    const QString trimmed_name = display_name.trimmed();
    QSettings settings;
    if (trimmed_name.isEmpty() || trimmed_name == trimmed_path) {
        settings.remove(walletDisplayNameKey(trimmed_path));
    } else {
        settings.setValue(walletDisplayNameKey(trimmed_path), trimmed_name);
    }
    settings.sync();

    {
        QMutexLocker locker(&m_wallets_mutex);
        for (WalletQmlModel* wallet : m_wallets) {
            if (wallet->name() == trimmed_path) {
                applyWalletDisplayName(wallet);
            }
        }
    }

    Q_EMIT walletDisplayNamesChanged();
    return true;
}

WalletQmlModel* WalletQmlController::selectedWallet() const
{
    return m_selected_wallet;
}

void WalletQmlController::unloadWallets()
{
    if (m_handler_load_wallet) {
        m_handler_load_wallet->disconnect();
    }
    m_selected_wallet = m_empty_wallet;
    Q_EMIT selectedWalletChanged();
    QStringList unloaded_wallet_names;
    {
        QMutexLocker locker(&m_wallets_mutex);
        unloaded_wallet_names.reserve(static_cast<qsizetype>(m_wallets.size()));
        for (WalletQmlModel* wallet : m_wallets) {
            unloaded_wallet_names.append(wallet->name());
            delete wallet;
        }
        m_wallets.clear();
    }
    for (const QString& wallet_name : unloaded_wallet_names) {
        Q_EMIT walletLoadStateChanged(wallet_name,
                                      WalletListModel::LoadState::Closed,
                                      QString{});
    }
}

void WalletQmlController::registerWalletModel(WalletQmlModel* wallet_model)
{
    connect(wallet_model, &WalletQmlModel::walletUnloaded, this, [this, wallet_model]() {
        removeWalletModel(wallet_model);
    }, Qt::QueuedConnection);
}

void WalletQmlController::removeWalletModel(WalletQmlModel* wallet_model)
{
    if (!wallet_model || wallet_model == m_empty_wallet) {
        return;
    }

    QString wallet_name;
    WalletQmlModel* next_selected_wallet{nullptr};
    {
        QMutexLocker locker(&m_wallets_mutex);
        const auto wallet_it = std::find(m_wallets.begin(), m_wallets.end(), wallet_model);
        if (wallet_it == m_wallets.end()) {
            return;
        }

        wallet_name = wallet_model->name();
        for (WalletQmlModel* wallet : m_wallets) {
            if (wallet != wallet_model) {
                next_selected_wallet = wallet;
                break;
            }
        }
        m_wallets.erase(wallet_it);
    }

    if (m_selected_wallet == wallet_model) {
        m_selected_wallet = next_selected_wallet ? next_selected_wallet : m_empty_wallet;
        Q_EMIT selectedWalletChanged();
    }

    Q_EMIT walletLoadStateChanged(wallet_name,
                                  WalletListModel::LoadState::Closed,
                                  QString{});
    setWalletLoaded(next_selected_wallet != nullptr);
    delete wallet_model;
}

WalletQmlModel* WalletQmlController::addOrSelectWalletModel(std::unique_ptr<interfaces::Wallet> wallet)
{
    if (!wallet) {
        return nullptr;
    }

    bool selected_existing_wallet{false};
    {
        QMutexLocker locker(&m_wallets_mutex);
        if (!m_wallets.empty()) {
            const QString name = QString::fromStdString(wallet->getWalletName());
            for (WalletQmlModel* wallet_model : m_wallets) {
                if (wallet_model->name() == name) {
                    m_selected_wallet = wallet_model;
                    selected_existing_wallet = true;
                    publishWalletInfo(wallet_model);
                    break;
                }
            }
        }
    }
    if (selected_existing_wallet) {
        applyWalletDisplayName(m_selected_wallet);
        Q_EMIT walletLoadStateChanged(m_selected_wallet->name(),
                                      WalletListModel::LoadState::Open,
                                      QString{});
        Q_EMIT selectedWalletChanged();
        setWalletLoaded(true);
        setNoWalletsFound(false);
        return m_selected_wallet;
    }

    const QString loaded_wallet_name = QString::fromStdString(wallet->getWalletName());
    auto wallet_model = new WalletQmlModel(std::move(wallet), &m_node);
    wallet_model->moveToThread(this->thread());
    registerWalletModel(wallet_model);
    {
        QMutexLocker locker(&m_wallets_mutex);
        applyWalletDisplayName(wallet_model);
        m_selected_wallet = wallet_model;
        m_wallets.push_back(m_selected_wallet);
    }
    subscribeWalletInfo(wallet_model);
    Q_EMIT walletLoadStateChanged(loaded_wallet_name,
                                  WalletListModel::LoadState::Open,
                                  QString{});
    publishWalletInfo(wallet_model);
    Q_EMIT selectedWalletChanged();
    setWalletLoaded(true);
    setNoWalletsFound(false);
    return wallet_model;
}

void WalletQmlController::createWalletAsync(const QString& name,
                                            SecureString passphrase,
                                            uint64_t wallet_creation_flags,
                                            WalletLoadAction load_action,
                                            bool report_create_error,
                                            WalletSetupFn setup_wallet)
{
    const QString wallet_name = name.trimmed();
    if (setup_wallet) {
        m_deferred_wallet_name = wallet_name;
    }
    setWalletLoadInProgress(true);

    QTimer::singleShot(0, m_worker, [this,
                                     wallet_name,
                                     passphrase = std::move(passphrase),
                                     wallet_creation_flags,
                                     load_action,
                                     report_create_error,
                                     setup_wallet = std::move(setup_wallet)]() mutable {
        std::vector<bilingual_str> warning_messages;
        auto wallet = m_node.walletLoader().createWallet(
            wallet_name.toStdString(),
            passphrase,
            wallet_creation_flags,
            warning_messages);
        QmlUtil::ClearSecureString(passphrase);
        const QString warnings = JoinWarnings(warning_messages);

        if (!wallet) {
            const bilingual_str result_error = util::ErrorString(wallet);
            const QString error = QString::fromStdString(
                result_error.translated.empty() ? result_error.original : result_error.translated);
            QMetaObject::invokeMethod(this, [this, wallet_name, error, warnings, report_create_error]() {
                if (m_deferred_wallet_name == wallet_name) {
                    m_deferred_wallet_name.clear();
                }
                setWalletLoadInProgress(false);
                setWalletLoadWarnings(warnings);
                const QString final_error = error.isEmpty() ? tr("Wallet creation failed.") : error;
                if (report_create_error) {
                    setWalletCreateError(final_error);
                } else {
                    setWalletLoadError(final_error);
                }
            });
            return;
        }

        QString setup_error;
        if (setup_wallet) {
            setup_error = setup_wallet(**wallet);
        }
        if (!setup_error.isEmpty()) {
            (*wallet)->remove();
            QMetaObject::invokeMethod(this, [this, wallet_name, setup_error, warnings, report_create_error]() {
                if (m_deferred_wallet_name == wallet_name) {
                    m_deferred_wallet_name.clear();
                }
                setWalletLoadInProgress(false);
                setWalletLoadWarnings(warnings);
                if (report_create_error) {
                    setWalletCreateError(setup_error);
                } else {
                    setWalletLoadError(setup_error);
                }
            });
            return;
        }

        QMetaObject::invokeMethod(this, [this,
                                         wallet_name,
                                         warnings,
                                         load_action,
                                         wallet = std::move(*wallet)]() mutable {
            if (m_deferred_wallet_name == wallet_name) {
                m_deferred_wallet_name.clear();
            }
            setWalletLoadWarnings(warnings);
            addOrSelectWalletModel(std::move(wallet));
            setWalletLoadInProgress(false);
            if (load_action == WalletLoadAction::Load) {
                Q_EMIT walletLoadSucceeded();
            } else if (load_action == WalletLoadAction::Create) {
                Q_EMIT walletCreateSucceeded();
            }
        });
    });
}

void WalletQmlController::createSingleSigWallet(const QString &name, const QString &passphrase)
{
    if (m_wallet_load_in_progress) {
        return;
    }
    clearWalletCreateStatus();
    clearWalletLoadStatus();
    clearWalletMigrationStatus();
    if (!m_initialized) {
        setWalletCreateError(tr("Wallets are still loading. Try again in a moment."));
        return;
    }

    const QString name_error = walletNameAvailabilityError(name);
    if (!name_error.isEmpty()) {
        setWalletCreateError(name_error);
        return;
    }

    SecureString secure_passphrase{QmlUtil::SecureStringFromQString(passphrase)};
    createWalletAsync(name.trimmed(),
                      std::move(secure_passphrase),
                      wallet::WALLET_FLAG_DESCRIPTORS,
                      WalletLoadAction::Create,
                      /*report_create_error=*/true);
}

bool WalletQmlController::createExternalSignerWallet(const QString& name)
{
    clearWalletLoadStatus();
    clearWalletMigrationStatus();
    if (!m_initialized) {
        setWalletLoadError(tr("Wallets are still loading. Try again in a moment."));
        return false;
    }

    const QString wallet_name = name.trimmed();
    const QString name_error = walletNameAvailabilityError(wallet_name);
    if (!name_error.isEmpty()) {
        setWalletLoadError(name_error);
        return false;
    }

    refreshExternalSignerStatus();
    if (!m_external_signer_path_configured) {
        setWalletLoadError(tr("Set an external signer path in Wallet settings first."));
        return false;
    }
    if (!m_external_signer_error.isEmpty()) {
        setWalletLoadError(m_external_signer_error);
        return false;
    }
    if (m_external_signer_count == 0) {
        setWalletLoadError(tr("Connect an external signer and try again."));
        return false;
    }
    if (m_external_signer_count > 1) {
        setWalletLoadError(tr("More than one external signer was found. Connect only one device and try again."));
        return false;
    }

    constexpr uint64_t flags = wallet::WALLET_FLAG_DESCRIPTORS |
        wallet::WALLET_FLAG_DISABLE_PRIVATE_KEYS |
        wallet::WALLET_FLAG_EXTERNAL_SIGNER;
    createWalletAsync(wallet_name,
                      SecureString{},
                      flags,
                      WalletLoadAction::Load,
                      /*report_create_error=*/false);
    return true;
}

void WalletQmlController::createWatchOnlyWallet(const QString &name, const QString &xpub)
{
    if (m_wallet_load_in_progress) {
        return;
    }
    clearWalletLoadStatus();
    clearWalletMigrationStatus();
    if (!m_initialized) {
        setWalletLoadError(tr("Wallets are still loading. Try again in a moment."));
        return;
    }

    const std::string xpub_str = xpub.trimmed().toStdString();
    CExtPubKey ext_pubkey = DecodeExtPubKey(xpub_str);
    if (!ext_pubkey.pubkey.IsValid()) {
        setWalletLoadError(tr("Invalid extended public key."));
        return;
    }

    const QString name_error = walletNameAvailabilityError(name);
    if (!name_error.isEmpty()) {
        setWalletLoadError(name_error);
        return;
    }

    const uint64_t creation_flags = wallet::WALLET_FLAG_DISABLE_PRIVATE_KEYS |
                                    wallet::WALLET_FLAG_DESCRIPTORS |
                                    wallet::WALLET_FLAG_BLANK_WALLET;
    std::vector<std::pair<std::string, bool>> descriptors = {
        {"wpkh(" + xpub_str + "/0/*)", /*internal=*/false},
        {"wpkh(" + xpub_str + "/1/*)", /*internal=*/true},
    };

    createWalletAsync(name.trimmed(),
                      SecureString{},
                      creation_flags,
                      WalletLoadAction::Create,
                      /*report_create_error=*/false,
                      [descriptors = std::move(descriptors)](interfaces::Wallet& wallet) {
        int descriptors_added = 0;
        wallet::CWallet* raw_wallet = wallet.wallet();
        if (!raw_wallet) {
            return WalletQmlController::tr("Failed to import descriptors into watch-only wallet.");
        }

        LOCK(raw_wallet->cs_wallet);
        for (const auto& [desc_str, internal] : descriptors) {
            FlatSigningProvider keys;
            std::string error;
            auto parsed = Parse(desc_str, keys, error, /*require_checksum=*/false);
            if (parsed.empty()) {
                continue;
            }
            wallet::WalletDescriptor w_desc(
                std::move(parsed.at(0)),
                TicksSinceEpoch<std::chrono::seconds>(Now<NodeSeconds>()),
                /*range_start=*/0,
                /*range_end=*/0,
                /*next_index=*/0);
            auto spk_manager_res = raw_wallet->AddWalletDescriptor(w_desc, keys, /*label=*/"", internal);
            if (spk_manager_res) {
                raw_wallet->AddActiveScriptPubKeyMan(
                    spk_manager_res.value().get().GetID(),
                    OutputType::BECH32,
                    internal);
                ++descriptors_added;
            }
        }
        raw_wallet->ConnectScriptPubKeyManNotifiers();

        // A watch-only wallet needs both external and internal descriptors;
        // succeeding with only one would leave a half-created wallet.
        if (descriptors_added != static_cast<int>(descriptors.size())) {
            return WalletQmlController::tr("Failed to import descriptors into watch-only wallet.");
        }
        return QString{};
    });
}

void WalletQmlController::importWallet(const QString& path)
{
    if (!m_initialized) {
        setWalletLoadError(tr("Wallets are still loading. Try again in a moment."));
        return;
    }
    startWalletImport(path);
}

void WalletQmlController::clearWalletCreateStatus()
{
    setWalletCreateError(QString());
}

void WalletQmlController::clearWalletLoadStatus()
{
    setWalletLoadInProgress(false);
    setWalletLoadError(QString());
    setWalletLoadWarnings(QString());
}

void WalletQmlController::migrateWallet(const QString& path, const QString& passphrase)
{
    if (!m_initialized) {
        setWalletMigrationError(tr("Wallets are still loading. Try again in a moment."));
        return;
    }
    startWalletMigration(path, QmlUtil::SecureStringFromQString(passphrase));
}

void WalletQmlController::clearWalletMigrationStatus()
{
    setWalletMigrationInProgress(false);
    setWalletMigrationError(QString());
}

bool WalletQmlController::validateXpub(const QString& xpub) const
{
    CExtPubKey ext_pubkey = DecodeExtPubKey(xpub.trimmed().toStdString());
    return ext_pubkey.pubkey.IsValid();
}

void WalletQmlController::requestOpenWalletSettings()
{
    Q_EMIT openWalletSettingsRequested();
}

void WalletQmlController::refreshExternalSignerStatus()
{
    const QString signer_path = QString::fromStdString(
        SettingToString(m_node.getPersistentSetting("signer"), "")).trimmed();
    const bool path_configured = !signer_path.isEmpty();
    if (path_configured) {
        m_node.forceSetting("signer", signer_path.toStdString());
    } else {
        m_node.forceSetting("signer", common::SettingsValue{});
    }
    int signer_count = 0;
    QString signer_name;
    QString error;

    try {
        auto signers = m_node.listExternalSigners();
        signer_count = static_cast<int>(signers.size());
        if (signer_count == 1) {
            signer_name = QString::fromStdString(signers.front()->getName());
        } else if (signer_count > 1) {
            error = tr("More than one external signer was found. Connect only one device.");
        }
    } catch (const std::runtime_error&) {
        error = tr("The signer command did not return valid output. Check that the path is correct.");
    }

    setExternalSignerStatus(path_configured, signer_count, signer_name, error);
}

void WalletQmlController::requestOpenReceive()
{
    Q_EMIT openReceiveRequested();
}

void WalletQmlController::requestClosePaymentRequestDetail()
{
    Q_EMIT closePaymentRequestDetailRequested();
}

QString WalletQmlController::normalizeWalletPath(const QString& path) const
{
    if (path.isEmpty()) {
        return {};
    }

    const QUrl url(path);
    QString normalized = url.isLocalFile() ? url.toLocalFile() : path;
    return QFileInfo(normalized).absoluteFilePath();
}

bool WalletQmlController::walletPathExists(const QString& path) const
{
    const QString normalized = normalizeWalletPath(path);
    return !normalized.isEmpty() && QFileInfo::exists(normalized);
}

bool WalletQmlController::walletNameExists(const QString& name) const
{
    const QString candidate = name.trimmed();
    if (candidate.isEmpty()) {
        return false;
    }

    for (const auto& [wallet_path, info] : m_node.walletLoader().listWalletDir()) {
        Q_UNUSED(info);
        const QString real_name = QString::fromStdString(wallet_path);
        if (real_name.compare(candidate, Qt::CaseInsensitive) == 0) {
            return true;
        }
        // Reject names that collide with another wallet's stored display alias.
        if (walletDisplayName(real_name).compare(candidate, Qt::CaseInsensitive) == 0) {
            return true;
        }
    }

    QMutexLocker locker(&m_wallets_mutex);
    for (WalletQmlModel* wallet : m_wallets) {
        if (wallet->name().compare(candidate, Qt::CaseInsensitive) == 0) {
            return true;
        }
        if (wallet->displayName().compare(candidate, Qt::CaseInsensitive) == 0) {
            return true;
        }
    }

    return false;
}

QString WalletQmlController::walletNameAvailabilityError(const QString& name) const
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) {
        return tr("Enter a wallet name.");
    }

    if (walletNameExists(trimmed)) {
        return tr("A wallet with this name already exists");
    }

    return {};
}

QString WalletQmlController::walletImportErrorTitle() const
{
    const QString error = m_wallet_load_error.trimmed();
    if (error.isEmpty()) {
        return {};
    }

    if (ErrorContains(error, "legacy wallet") || ErrorContains(error, "migrat")) {
        return tr("This wallet needs to be migrated");
    }
    if (ErrorContains(error, "does not exist")) {
        return tr("We couldn't find that file");
    }
    if (ErrorContains(error, "already exists")) {
        return tr("A wallet with this name already exists");
    }
    if (ErrorContains(error, "verification failed") || ErrorContains(error, "not supported")) {
        return tr("This wallet type is not supported");
    }
    return tr("This wallet couldn't be imported");
}

QString WalletQmlController::walletImportErrorDescription() const
{
    const QString error = m_wallet_load_error.trimmed();
    if (error.isEmpty()) {
        return {};
    }

    if (ErrorContains(error, "legacy wallet") || ErrorContains(error, "migrat")) {
        return tr("The selected backup appears to come from a legacy wallet format. It needs to be migrated before it can be used here.");
    }
    if (ErrorContains(error, "does not exist")) {
        return tr("The selected file is no longer available at that location. Choose the backup file again and retry.");
    }
    if (ErrorContains(error, "already exists")) {
        return tr("Importing this backup would create a wallet that already exists in your wallet directory. Remove or rename the existing wallet first, then try again.");
    }
    if (ErrorContains(error, "verification failed") || ErrorContains(error, "not supported")) {
        return tr("The file looks like a wallet backup, but this application could not verify or open it in a supported format.");
    }
    return tr("The selected file could not be restored as a wallet. Check that it is a valid wallet backup, then try another file.");
}

QString WalletQmlController::walletImportErrorHelpText() const
{
    const QString error = m_wallet_load_error.trimmed();
    if (error.isEmpty()) {
        return {};
    }

    if (ErrorContains(error, "legacy wallet") || ErrorContains(error, "migrat")) {
        return tr("If this is a legacy wallet backup, migrate it with the wallet migration tool before importing it here.");
    }
    return error;
}

QString WalletQmlController::inferWalletLoadTarget(const QString& normalized_path) const
{
    const QFileInfo selected_path(normalized_path);
    if (!selected_path.exists()) {
        return {};
    }

    const QString wallet_dir_path = QDir::cleanPath(
        QFileInfo(QString::fromStdString(m_node.walletLoader().getWalletDir())).absoluteFilePath());
    const QDir wallet_dir(wallet_dir_path);

    auto walletTargetFromDir = [&wallet_dir, &wallet_dir_path](const QString& dir_path) {
        const QString clean_dir_path = QDir::cleanPath(dir_path);
        const QString relative_path = wallet_dir.relativeFilePath(clean_dir_path);
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

    const QString file_name = selected_path.fileName();
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

QString WalletQmlController::resolveManagedWalletReference(const QString& path, QString* wallet_format) const
{
    if (wallet_format) {
        wallet_format->clear();
    }

    if (path.isEmpty()) {
        return {};
    }

    const QString candidate = QDir::cleanPath(path);
    const auto wallet_dir_entries = m_node.walletLoader().listWalletDir();

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

    const QString normalized_path = normalizeWalletPath(path);
    if (normalized_path.isEmpty() || !QFileInfo::exists(normalized_path)) {
        return {};
    }

    const QString load_target = inferWalletLoadTarget(normalized_path);
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

QString WalletQmlController::inferRestoreWalletName(const QString& normalized_path) const
{
    const QFileInfo selected_path(normalized_path);
    if (!selected_path.exists() || selected_path.isDir()) {
        return {};
    }

    QString wallet_name;
    const QString file_name = selected_path.fileName();

    if (file_name.compare("wallet.dat", Qt::CaseInsensitive) == 0) {
        wallet_name = selected_path.dir().dirName();
    } else if (file_name.toLower().endsWith(".legacy.bak")) {
        wallet_name = file_name.left(file_name.size() - QString(".legacy.bak").size());
    } else {
        wallet_name = selected_path.completeBaseName();
    }

    if (wallet_name.isEmpty()) {
        wallet_name = QStringLiteral("restored-wallet");
    }
    return wallet_name;
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
void WalletQmlController::startWalletImport(const QString& path)
{
    const QString normalized_path = normalizeWalletPath(path);
    clearWalletMigrationStatus();
    clearWalletLoadStatus();
    clearLastImportedWalletInfo();

    if (normalized_path.isEmpty()) {
        setWalletLoadError(tr("Choose a wallet backup file."));
        return;
    }

    if (!QFileInfo::exists(normalized_path)) {
        setWalletLoadError(tr("The selected wallet path does not exist."));
        return;
    }

    const QString restore_wallet_name = inferRestoreWalletName(normalized_path);

    setWalletLoadInProgress(true);

    QTimer::singleShot(0, m_worker, [this, normalized_path, restore_wallet_name]() {
        std::vector<bilingual_str> warning_messages;
        // Import is intentionally modeled as restore-from-backup. The user is
        // selecting a wallet file to bring into managed wallet storage, not
        // asking the app to open an arbitrary wallet in place.
        auto wallet = m_node.walletLoader().restoreWallet(
            fs::PathFromString(normalized_path.toStdString()),
            restore_wallet_name.toStdString(),
            warning_messages,
            /*load_after_restore=*/true);
        const QString warnings = JoinWarnings(warning_messages);

        if (!wallet) {
            const bilingual_str result_error = util::ErrorString(wallet);
            const QString error = QString::fromStdString(result_error.translated);
            QMetaObject::invokeMethod(this, [this, error, warnings]() {
                setWalletLoadInProgress(false);
                setWalletLoadWarnings(warnings);
                setWalletLoadError(error.isEmpty() ? tr("Wallet import failed.") : error);
            });
            return;
        }

        QMetaObject::invokeMethod(this, [this, warnings, wallet = std::move(*wallet)]() mutable {
            setWalletLoadWarnings(warnings);
            setLastImportedWalletInfo(
                QString::fromStdString(wallet->getWalletName()),
                describeImportedWalletKeyScheme(*wallet));
            addOrSelectWalletModel(std::move(wallet));
            setWalletLoadInProgress(false);
            Q_EMIT walletImportSucceeded();
        });
    });
}

void WalletQmlController::handleLoadWallet(std::unique_ptr<interfaces::Wallet> wallet)
{
    // Multi-step create flows (currently watch-only) finish setup from the
    // worker result before publishing the wallet model.
    if (wallet && !m_deferred_wallet_name.isEmpty() &&
        QString::fromStdString(wallet->getWalletName()) == m_deferred_wallet_name) {
        return;
    }
    addOrSelectWalletModel(std::move(wallet));
}

void WalletQmlController::initialize()
{
    // wallet_loader is not set when -disablewallet is passed; bail out.
    if (gArgs.GetBoolArg("-disablewallet", false)) {
        return;
    }
    m_handler_load_wallet = m_node.walletLoader().handleLoadWallet([this](std::unique_ptr<interfaces::Wallet> wallet) {
        QMetaObject::invokeMethod(this, [this, wallet = std::move(wallet)]() mutable {
            handleLoadWallet(std::move(wallet));
        });
    });

    auto wallets = m_node.walletLoader().getWallets();
    QStringList loaded_wallet_names;
    loaded_wallet_names.reserve(static_cast<qsizetype>(wallets.size()));
    for (auto& wallet : wallets) {
        loaded_wallet_names.append(QString::fromStdString(wallet->getWalletName()));
        auto* wallet_model = new WalletQmlModel(std::move(wallet), &m_node);
        registerWalletModel(wallet_model);
        applyWalletDisplayName(wallet_model);
        m_wallets.push_back(wallet_model);
        subscribeWalletInfo(wallet_model);
    }
    for (const QString& wallet_name : loaded_wallet_names) {
        Q_EMIT walletLoadStateChanged(wallet_name,
                                      WalletListModel::LoadState::Open,
                                      QString{});
    }
    for (WalletQmlModel* wallet_model : m_wallets) {
        publishWalletInfo(wallet_model);
    }
    if (!m_wallets.empty()) {
        m_selected_wallet = m_wallets.front();
        setWalletLoaded(true);
        setNoWalletsFound(false);
        Q_EMIT selectedWalletChanged();
    }

    refreshExternalSignerStatus();
    m_initialized = true;
    Q_EMIT initializedChanged();
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
    clearWalletMigrationStatus();
    clearWalletLoadStatus();

    if (path.trimmed().isEmpty()) {
        setWalletLoadError(tr("Choose a wallet to open."));
        return;
    }

    // Wallet selection can arrive either as a wallet name from listWalletDir()
    // or as a concrete path under -walletdir. Resolve both into the wallet
    // reference expected by loadWallet() and migrateWallet().
    QString load_target_format = wallet_format;
    const QString load_target = load_target_format.isEmpty()
        ? resolveManagedWalletReference(path, &load_target_format)
        : QDir::cleanPath(path);
    if (load_target.isEmpty()) {
        setWalletLoadError(tr("The selected wallet is not available in the wallet directory."));
        return;
    }

    if (load_target_format == "bdb") {
        Q_EMIT walletMigrationRequired(load_target);
        return;
    }

    setWalletLoadInProgress(true);
    Q_EMIT walletLoadStateChanged(load_target, WalletListModel::LoadState::Loading, QString{});

    QTimer::singleShot(0, m_worker, [this, load_target]() {
        std::vector<bilingual_str> warning_messages;
        // Loading is reserved for already-discovered wallet storage. Import
        // uses restoreWallet() through startWalletImport() instead.
        auto wallet = m_node.walletLoader().loadWallet(load_target.toStdString(), warning_messages);
        const QString warnings = JoinWarnings(warning_messages);

        if (!wallet) {
            const bilingual_str result_error = util::ErrorString(wallet);
            const QString error = QString::fromStdString(result_error.translated);
            QMetaObject::invokeMethod(this, [this, error, warnings, load_target]() {
                setWalletLoadInProgress(false);
                setWalletLoadWarnings(warnings);
                const QString final_error = error.isEmpty() ? tr("Wallet could not be opened.") : error;
                setWalletLoadError(final_error);
                Q_EMIT walletLoadStateChanged(load_target, WalletListModel::LoadState::LoadError, final_error);
            });
            return;
        }

        QMetaObject::invokeMethod(this, [this, warnings, wallet = std::move(*wallet)]() mutable {
            setWalletLoadWarnings(warnings);
            addOrSelectWalletModel(std::move(wallet));
            setWalletLoadInProgress(false);
            Q_EMIT walletLoadSucceeded();
        });
    });
}

void WalletQmlController::startWalletMigration(const QString& path, SecureString passphrase)
{
    clearWalletLoadStatus();
    clearWalletMigrationStatus();

    if (path.trimmed().isEmpty()) {
        QmlUtil::ClearSecureString(passphrase);
        setWalletMigrationError(tr("Choose a wallet to update."));
        Q_EMIT walletMigrationFailed();
        return;
    }

    const QString wallet_reference = resolveManagedWalletReference(path);
    if (wallet_reference.isEmpty()) {
        QmlUtil::ClearSecureString(passphrase);
        setWalletMigrationError(tr("The selected wallet is not available in the wallet directory."));
        Q_EMIT walletMigrationFailed();
        return;
    }

    if (passphrase.empty() && m_node.walletLoader().isEncrypted(wallet_reference.toStdString())) {
        Q_EMIT walletMigrationPassphraseRequired(wallet_reference);
        return;
    }

    setWalletMigrationInProgress(true);

    QTimer::singleShot(0, m_worker, [this, wallet_reference, passphrase = std::move(passphrase)]() mutable {
        auto result = m_node.walletLoader().migrateWallet(wallet_reference.toStdString(), passphrase);
        QmlUtil::ClearSecureString(passphrase);

        if (!result) {
            const QString error = QString::fromStdString(util::ErrorString(result).translated);
            QMetaObject::invokeMethod(this, [this, error]() {
                setWalletMigrationInProgress(false);
                setWalletMigrationError(error.isEmpty() ? tr("Wallet update failed.") : error);
                Q_EMIT walletMigrationFailed();
            });
            return;
        }

        QMetaObject::invokeMethod(this, [this, migration_result = std::move(*result)]() mutable {
            handleLoadWallet(std::move(migration_result.wallet));
            clearWalletMigrationStatus();
            Q_EMIT walletMigrationSucceeded();
        });
    });
}

void WalletQmlController::setWalletCreateError(const QString& error)
{
    if (m_wallet_create_error != error) {
        m_wallet_create_error = error;
        Q_EMIT walletCreateErrorChanged();
    }
}

void WalletQmlController::setWalletLoadInProgress(bool in_progress)
{
    if (m_wallet_load_in_progress != in_progress) {
        m_wallet_load_in_progress = in_progress;
        Q_EMIT walletLoadInProgressChanged();
    }
}

void WalletQmlController::setWalletLoadError(const QString& error)
{
    if (m_wallet_load_error != error) {
        m_wallet_load_error = error;
        Q_EMIT walletLoadErrorChanged();
    }
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
    if (m_wallet_migration_in_progress != in_progress) {
        m_wallet_migration_in_progress = in_progress;
        Q_EMIT walletMigrationInProgressChanged();
    }
}

void WalletQmlController::setWalletMigrationError(const QString& error)
{
    if (m_wallet_migration_error != error) {
        m_wallet_migration_error = error;
        Q_EMIT walletMigrationErrorChanged();
    }
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

QString WalletQmlController::makeSuggestedExternalSignerWalletName(const QString& signer_name) const
{
    QString suggested = signer_name.trimmed();
    suggested.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_]")), QStringLiteral("_"));
    suggested.remove(QRegularExpression(QStringLiteral("^_+")));
    while (suggested.contains(QStringLiteral("__"))) {
        suggested.replace(QStringLiteral("__"), QStringLiteral("_"));
    }
    if (suggested.isEmpty()) {
        suggested = QStringLiteral("external_signer");
    }
    return suggested.left(20);
}

void WalletQmlController::setExternalSignerStatus(bool path_configured, int signer_count, const QString& signer_name, const QString& error)
{
    const QString suggested_name = makeSuggestedExternalSignerWalletName(signer_name);
    if (m_external_signer_path_configured == path_configured &&
        m_external_signer_count == signer_count &&
        m_external_signer_name == signer_name &&
        m_external_signer_error == error &&
        m_suggested_external_signer_wallet_name == suggested_name) {
        return;
    }

    m_external_signer_path_configured = path_configured;
    m_external_signer_count = signer_count;
    m_external_signer_name = signer_name;
    m_external_signer_error = error;
    m_suggested_external_signer_wallet_name = suggested_name;
    Q_EMIT externalSignerStatusChanged();
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
