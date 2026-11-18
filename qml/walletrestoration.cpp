// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/walletrestoration.h>
#include <qml/walletidentity.h>
#include <qml/util.h>
#include <interfaces/node.h>
#include <interfaces/wallet.h>
#include <util/result.h>
#include <util/translation.h>
#include <QDateTime>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QScopeGuard>
#include <algorithm>
#include <stdexcept>
#include <utility>

namespace walletrestoration {
namespace {
using Phase = walletcreation::Phase;
QString Text(const char* text) { return QCoreApplication::translate("WalletOperationModel", text); }
void Append(QString& destination, const QString& value)
{
    if (value.isEmpty()) return;
    if (!destination.isEmpty()) destination += QLatin1Char('\n');
    destination += value;
}
Failure Reason(const std::string& original)
{
    // Context comes from the actual public load/migration error. Never search
    // wallet paths for broad words such as "migrat" or "not supported".
    if (original.starts_with("Wallet loading failed. Prune: last wallet synchronisation goes beyond pruned data")) return Failure::Pruned;
    if (original == "Error: The wallet passphrase was not provided") return Failure::CredentialsRequired;
    if (original == "The passphrase entered for the wallet decryption was incorrect." || original == "Error: The wallet passphrase entered was incorrect." || original.starts_with("Error: The wallet passphrase entered is incorrect. It contains a null character")) return Failure::Credentials;
    return Failure::Backend;
}
bool SettingsException(const std::exception& error)
{
    return std::string_view{error.what()} == "Attempt to write settings file when dynamic settings are disabled.";
}
QString Metadata(interfaces::Wallet& wallet)
{
    return wallet.hasExternalSigner() ? Text("External signer") : wallet.privateKeysDisabled() ? Text("Watch-only") : Text("Software wallet");
}
struct ReleaseGroup {
    std::mutex mutex;
    size_t remaining{0}; bool sealed{false};
    std::shared_ptr<walletpublication::Release> original;
    void add() { std::lock_guard lock{mutex}; ++remaining; }
    void finished() { bool release; { std::lock_guard lock{mutex}; --remaining; release = sealed && !remaining; } if (release && original) original->release(); }
    void seal() { bool release; { std::lock_guard lock{mutex}; sealed = true; release = !remaining; } if (release && original) original->release(); }
};
} // namespace

FileStamp ReadStamp(const QString& path)
{
    const QFileInfo info{path};
    if (!info.isFile()) return {};
    QFile file{info.canonicalFilePath()};
    if (!file.open(QIODevice::ReadOnly)) throw std::runtime_error("The wallet backup could not be read");
    return {info.canonicalFilePath(), info.size(), info.lastModified().toMSecsSinceEpoch(), file.read(72)};
}
Format Hint(const FileStamp& stamp)
{
    if (stamp.size >= 512 && stamp.header.startsWith(QByteArray{"SQLite format 3\0", 16})) return Format::SQLite;
    const auto magic = stamp.header.mid(12, 4);
    if (stamp.size >= 4096 && (magic == QByteArray::fromHex("00053162") || magic == QByteArray::fromHex("62310500"))) return Format::Berkeley;
    return Format::Unknown;
}
QString RestoreName(const QString& source, Format hint)
{
    const QFileInfo info{source}; const auto filename = info.fileName();
    QString name;
    if (filename.compare(QLatin1String("wallet.dat"), Qt::CaseInsensitive) == 0) name = info.dir().dirName();
    else if (hint == Format::Berkeley && filename.endsWith(QLatin1String(".legacy.bak"), Qt::CaseInsensitive)) name = filename.left(filename.size() - 11);
    else name = info.completeBaseName();
    return name.isEmpty() ? QStringLiteral("restored-wallet") : name;
}
Facts Inspect(interfaces::Node& node, const Request& request, const QString& captured_target, bool copied)
{
    Facts facts; facts.wallet_directory = QString::fromStdString(node.walletLoader().getWalletDir());
    if (request.kind == Kind::Restore && !copied) {
        const QUrl url{request.source};
        if (!url.isLocalFile() && !url.scheme().isEmpty() && !QDir::isAbsolutePath(request.source)) {
            facts.failure = Failure::InvalidFormat; facts.error = Text("Choose a local wallet backup file."); return facts;
        }
        const auto path = url.isLocalFile() ? url.toLocalFile() : request.source;
        facts.stamp = ReadStamp(QFileInfo(path).absoluteFilePath()); facts.source_path = facts.stamp.path;
        if (facts.source_path.isEmpty()) { facts.failure = Failure::Missing; facts.error = Text("The selected backup file does not exist."); return facts; }
        facts.format = Hint(facts.stamp);
        if (facts.format == Format::Unknown) { facts.failure = Failure::InvalidFormat; facts.error = Text("The selected file does not have a supported wallet database header. The original file was not changed."); return facts; }
        facts.target = RestoreName(QFileInfo(path).absoluteFilePath(), facts.format);
        if (facts.target.size() > 32768 || facts.target == QLatin1String(".") || facts.target == QLatin1String("..") || facts.target.contains(QLatin1Char('/')) || facts.target.contains(QLatin1Char('\\'))) {
            facts.failure = Failure::InvalidFormat; facts.error = Text("Choose a backup with an unambiguous wallet filename."); return facts;
        }
        facts.target_path = QDir(facts.wallet_directory).filePath(facts.target);
        facts.target_exists = QFileInfo::exists(facts.target_path);
        if (facts.target_exists) { facts.failure = Failure::TargetExists; facts.error = Text("A wallet database already exists at the restore destination. Nothing was overwritten."); return facts; }
        facts.encrypted = facts.format == Format::Berkeley && node.walletLoader().isEncrypted(facts.source_path.toStdString());
        facts.exists = true; return facts;
    }
    const auto candidate = walletidentity::Canonical(copied ? captured_target : request.source, facts.wallet_directory);
    const QFileInfo selected{request.source};
    const auto parent = selected.fileName().compare(QLatin1String("wallet.dat"), Qt::CaseInsensitive) == 0 ? walletidentity::Canonical(selected.dir().absolutePath(), facts.wallet_directory) : candidate;
    // The public getter allocates its catalog on Native. Retain only the
    // original candidates in the outcome, never a second unbounded catalog.
    for (const auto& [name, format] : node.walletLoader().listWalletDir()) {
        const auto id = walletidentity::Canonical(QString::fromStdString(name), facts.wallet_directory);
        if ((id == candidate || id == parent) && facts.wallets.size() < 2) facts.wallets.emplace_back(id, QString::fromStdString(format));
    }
    bool found{false};
    for (const auto& [id, format] : facts.wallets) {
        if (id != candidate) continue;
        facts.target = id; facts.format = format == QLatin1String("bdb") ? Format::Berkeley : format == QLatin1String("sqlite") ? Format::SQLite : Format::Unknown;
        found = true; break;
    }
    if (!found) {
        for (const auto& [id, format] : facts.wallets) if (id == parent) { facts.target = id; facts.format = format == QLatin1String("bdb") ? Format::Berkeley : Format::SQLite; found = true; break; }
    }
    if (!found) { facts.failure = Failure::Missing; facts.error = Text("This wallet is no longer in the wallet directory."); return facts; }
    facts.target_path = QDir::isAbsolutePath(facts.target) ? facts.target : QDir(facts.wallet_directory).filePath(facts.target);
    if (QFileInfo(facts.target_path).isDir()) facts.target_path = QDir(facts.target_path).filePath(QStringLiteral("wallet.dat"));
    facts.source_path = facts.target_path; facts.stamp = ReadStamp(facts.source_path); facts.exists = true;
    facts.encrypted = facts.format == Format::Berkeley && node.walletLoader().isEncrypted(facts.target.toStdString());
    return facts;
}
walletcreation::Frame FailureFrame(Failure reason, const QString& error, const QString& target, bool effect)
{
    walletcreation::Frame frame; frame.target_id = target;
    frame.error.message = error.left(1024); frame.error.recovery = effect ? Text("Keep the original backup and the copied wallet files. Do not repeat the restore or migration blindly.") : Text("The original backup and existing wallet files were not changed.");
    switch (reason) {
    case Failure::TargetExists:
        frame.error.code = QStringLiteral("TargetExists"); frame.error_title = Text("A wallet with this name already exists");
        frame.error_description = Text("The restore destination already contains a wallet. Choose a different backup name; no existing wallet was overwritten."); break;
    case Failure::Pruned:
        frame.error.code = QStringLiteral("PrunedDataUnavailable"); frame.error_title = Text("This node is missing older blocks");
        frame.error_description = Text("Core could not rescan this backup because the required blocks were pruned. Keep the backup. Re-download the required blocks or restore it on a node with the full history."); break;
    case Failure::Missing:
        frame.error.code = QStringLiteral("SourceUnavailable"); frame.error_title = Text("The original wallet file is unavailable"); frame.error_description = error.left(1024); break;
    case Failure::InvalidFormat:
        frame.error.code = QStringLiteral("UnsupportedBackup"); frame.error_title = Text("This wallet backup could not be opened"); frame.error_description = error.left(1024); break;
    case Failure::CredentialsRequired:
        frame.error.code = QStringLiteral("CredentialsRequired"); frame.error_title = Text("Wallet password required"); frame.error_description = Text("Enter the password for the original wallet to continue. Its files and recovery backup were kept."); break;
    case Failure::Credentials:
        frame.error.code = QStringLiteral("CredentialsRejected"); frame.error_title = Text("The wallet password was rejected"); frame.error_description = Text("Enter the password for the original wallet. Its files and recovery backup were kept."); break;
    case Failure::Changed:
        frame.error.code = QStringLiteral("SourceChanged"); frame.error_title = Text("The original wallet file changed"); frame.error_description = Text("The source changed after validation. Choose and confirm the intended wallet again."); break;
    case Failure::Settings:
        frame.error.code = QStringLiteral("StartupPreferenceUnavailable"); frame.error_title = Text("Wallet startup preference could not be saved"); frame.error_description = error.left(1024); break;
    case Failure::Exception:
        frame.error.code = QStringLiteral("OutcomeUnknown"); frame.error_title = Text("Check the wallet operation outcome"); frame.error_description = Text("Core did not report whether the update finished. Keep the original backup and check the recovery paths before trying again."); break;
    case Failure::Backend:
        frame.error.code = QStringLiteral("CoreRejected"); frame.error_title = Text("Core could not complete this wallet operation"); frame.error_description = Text("Read Core's reason below. This message does not imply that the original backup is invalid or that its keys were lost."); break;
    case Failure::None: break;
    }
    return frame;
}

Owner::Owner(walletcreation::Services services, Request request, Notify notify,
    std::shared_ptr<walletpublication::Release> release, std::shared_ptr<walletpublication::NativeObservation> observation, secretinput::Captured secret)
    : m_services(std::move(services)), m_request(std::move(request)), m_notify(std::move(notify)), m_secret(std::move(secret)), m_release(std::move(release)), m_observation(std::move(observation)), m_confirmed(m_request.confirmed) { m_frame.target_id = m_request.kind == Kind::Migrate ? m_request.source : QString{}; }
Owner::~Owner() { if (!m_backend_retained && m_release) m_release->release(); if (m_detached.load() || !m_started) m_services.admission->presentationConsumed(m_request.id); }
std::shared_ptr<Owner> Owner::Make(walletcreation::Services services, Request request, Notify notify,
    std::shared_ptr<walletpublication::Release> release, std::shared_ptr<walletpublication::NativeObservation> observation)
{
    auto secret = std::move(request.passphrase);
    return services.retain<Owner>([&] {
        return std::unique_ptr<Owner>{new Owner(std::move(services), std::move(request), std::move(notify),
            std::move(release), std::move(observation), std::move(secret))};
    });
}
walletcreation::Frame Owner::snapshot() const { std::lock_guard lock{m_mutex}; return m_frame; }
QString Owner::errorText() const { std::lock_guard lock{m_mutex}; return m_actual.error.isEmpty() ? m_frame.error.message : m_actual.error; }
QString Owner::warningText() const { std::lock_guard lock{m_mutex}; return m_actual.warnings; }
void Owner::notify() { if (!m_detached.load()) m_notify(); }
void Owner::update(Phase phase) { { std::lock_guard lock{m_mutex}; m_frame.phase = phase; ++m_frame.revision; } notify(); }
void Owner::start()
{
    if (m_started) return;
    m_started = true;
    m_keep_alive = shared_from_this();
    if (m_request.kind == Kind::Migrate && !m_confirmed) {
        const auto self = shared_from_this();
        if (!m_services.post(execution::Lane::Utility, [self] { self->prompt(Phase::AwaitingConfirmation); })) complete(Phase::Rejected, Failure::Backend, Text("The update question could not be shown. No wallet was changed."));
    } else inspect();
}
void Owner::inspect()
{
    update(Phase::Validating); const auto weak = weak_from_this();
    m_inspect = std::make_shared<execution::OwnedCommand<Facts>>([weak] { if (const auto owner = weak.lock()) owner->inspected(); });
    if (!m_services.start(*m_inspect, [self = shared_from_this()] {
            if (self->m_cancelled.load() || (self->m_services.shutdown && self->m_services.shutdown->load())) throw execution::NotStarted{execution::NotStarted::Cause::Cancelled};
            return Inspect(*self->m_services.node, self->m_request, self->m_target, self->m_copied);
        })) complete(Phase::Rejected, Failure::Backend, Text("The wallet check could not start. No wallet was changed."));
}
void Owner::inspected()
{
    const auto outcome = m_inspect->result(); if (!outcome) return;
    if (m_cancelled.load() || m_detached.load()) { complete(Phase::Cancelled); return; }
    if (!outcome->value) { complete(Phase::Failed, Failure::Backend, Text("Wallet details could not be read. No restore or migration was started.")); return; }
    m_facts = *outcome->value;
    if (m_reload_only && m_facts.stamp != m_actual.target_stamp) { complete(Phase::Partial, Failure::Changed, Text("The copied wallet changed before it could be checked. It was not reopened.")); return; }
    if (m_facts.failure != Failure::None) { complete(m_copied ? Phase::Partial : Phase::Failed, m_facts.failure, m_facts.error); return; }
    if (!m_target.isEmpty() && m_target != m_facts.target) { complete(Phase::Failed, Failure::Changed, Text("The draft changed.")); return; }
    m_target = m_facts.target;
    { std::lock_guard lock{m_mutex}; m_frame.target_id = m_target; }
    if (m_facts.format == Format::Berkeley) {
        if (!m_confirmed) { prompt(Phase::AwaitingConfirmation); return; }
        if (m_facts.encrypted && !m_secret) { prompt(Phase::AwaitingPassphrase); return; }
    } else if (m_request.kind == Kind::Migrate && !m_reload_only) {
        complete(Phase::Failed, Failure::Backend, Text("This wallet is already in a modern format; no migration was started.")); return;
    }
    perform();
}
void Owner::prompt(Phase phase, QString error, Failure reason)
{
    if (m_prompt_sequence >= 64) { complete(Phase::Partial, Failure::Backend, Text("The prompt retry limit was reached. Keep the wallet files and begin a new explicit operation.")); return; }
    {
        std::lock_guard lock{m_mutex};
        m_frame.phase = phase; ++m_frame.revision;
        m_frame.prompt_id = QString::number(m_request.id) + QLatin1Char(':') + QString::number(++m_prompt_sequence);
        if (m_copied || m_actual.effect_started) { m_frame.result.recovery_paths = m_actual.recovery; m_frame.result.effect = m_copied ? WalletOperationResult::BackupCopied : WalletOperationResult::PossibleEffect; }
        if (!error.isEmpty()) { const auto prepared = FailureFrame(reason, error, m_target, m_copied || m_actual.effect_started); m_frame.error = prepared.error; m_frame.error_title = prepared.error_title; m_frame.error_description = prepared.error_description; }
    }
    if (m_release) m_release->release();
    notify();
}
void Owner::answer(QString command_id, QString expected_prompt, std::shared_ptr<walletpublication::Release> release,
    std::shared_ptr<walletpublication::NativeObservation> observation, secretinput::Captured secret)
{
    {
        std::lock_guard lock{m_mutex};
        if (m_frame.prompt_id != expected_prompt || (m_frame.phase != Phase::AwaitingConfirmation && m_frame.phase != Phase::AwaitingPassphrase) || m_cancelled.load()) {
            m_frame.rejected_command = std::move(command_id); m_frame.command_error = {QStringLiteral("StalePrompt"), Text("This answer is for a question that is no longer open."), {}, {}}; ++m_frame.revision;
            // Release the refused transport only after leaving this lock.
        } else {
            m_frame.rejected_command.clear(); m_frame.command_error = {};
        }
    }
    if (!snapshot().rejected_command.isEmpty()) { m_services.admission->presentationConsumed(m_request.id); if (release) release->release(); notify(); return; }
    m_release = std::move(release); m_observation = std::move(observation); m_secret = std::move(secret); m_confirmed = true; m_backend_retained = false;
    { std::lock_guard lock{m_mutex}; m_frame.prompt_id.clear(); m_frame.error = {}; m_frame.error_title.clear(); m_frame.error_description.clear(); }
    inspect();
}
void Owner::perform()
{
    update(Phase::Creating);
    const auto group = std::make_shared<ReleaseGroup>(); group->original = m_release;
    const auto seal = qScopeGuard([group] { group->seal(); });
    for (auto& backend : m_backends) {
        group->add(); m_backend_retained = true;
        const auto child = std::make_shared<walletpublication::Release>(); child->whenReleased([group] { group->finished(); });
        backend = m_services.retain<walletcreation::Backend>([child] { auto value = std::make_unique<walletcreation::Backend>(); value->release = child; return value; });
        if (!backend) { child->release(); complete(Phase::Rejected, Failure::Backend, Text("The wallet action could not start. Nothing was changed.")); return; }
    }
    const auto weak = weak_from_this(); m_effect = std::make_shared<execution::OwnedCommand<NativeResult>>([weak] { if (const auto owner = weak.lock()) owner->performed(); });
    if (!m_services.start(*m_effect, [self = shared_from_this()] { return self->effect(); })) complete(Phase::Rejected, Failure::Backend, Text("The wallet action could not start."));
}
void Owner::loadResult(NativeResult& out, const QString& name, size_t index)
{
    std::vector<bilingual_str> warnings;
    walletopening::Source source;
    try {
        auto loaded = m_services.node->walletLoader().loadWallet(name.toStdString(), warnings); Append(out.warnings, walletcreation::JoinWarnings(warnings));
        if (loaded) source = std::move(*loaded);
        else { const auto& error = util::ErrorString(loaded); out.failure = Reason(error.original); Append(out.error, QString::fromStdString(error.translated)); }
    } catch (const std::exception& error) {
        Append(out.warnings, walletcreation::JoinWarnings(warnings));
        if (SettingsException(error) && m_observation && !m_observation->failed()) {
            auto original = m_observation->find(name, [directory = m_facts.wallet_directory](interfaces::Wallet& value) { return walletidentity::Canonical(QString::fromStdString(value.getWalletName()), directory); });
            if (original.events.unloaded) {
                Append(out.warnings, Text("The wallet opened, but its startup preference could not be saved. Its wallet file was preserved."));
                out.names[index] = QString::fromStdString(original.facts.name);
                out.observations[index] = std::move(original.events);
                if (out.key_scheme.isEmpty()) out.key_scheme = original.facts.external ? Text("External signer") : original.facts.watch_only ? Text("Watch-only") : Text("Software wallet");
                { std::lock_guard lock{m_mutex}; m_backends[index]->recover(std::move(original.wallet)); }
                ++out.count;
                return;
            }
        }
        out.failure = SettingsException(error) ? Failure::Settings : Failure::Exception;
        Append(out.error, QString::fromUtf8(error.what()));
        out.uncertain |= !SettingsException(error);
    }
    auto* wallet = std::visit([](const auto& value) { return value.get(); }, source);
    if (!wallet) return;
    const auto actual = walletidentity::Canonical(QString::fromStdString(wallet->getWalletName()), m_facts.wallet_directory);
    out.names[index] = actual;
    if (!out.observations[index].unloaded && m_observation) out.observations[index] = m_observation->resolve(actual, [directory = m_facts.wallet_directory](interfaces::Wallet& value) { return walletidentity::Canonical(QString::fromStdString(value.getWalletName()), directory); });
    if (out.key_scheme.isEmpty()) out.key_scheme = Metadata(*wallet);
    { std::lock_guard lock{m_mutex}; m_backends[index]->wallet = std::move(source); }
    ++out.count;
}
NativeResult Owner::effect()
{
    auto out = performEffect();
    if (out.copied) {
        try { const auto path = QFileInfo(m_facts.target_path).isDir() ? QDir(m_facts.target_path).filePath(QStringLiteral("wallet.dat")) : m_facts.target_path; out.target_stamp = ReadStamp(path); }
        catch (...) { out.target_stamp = {}; }
    }
    return out;
}
NativeResult Owner::performEffect()
{
    NativeResult out; out.copied = m_copied; out.migrated = m_migrated; out.primary = m_target;
    try {
        if (m_cancelled.load()) throw execution::NotStarted{execution::NotStarted::Cause::Cancelled};
        if (ReadStamp(m_facts.source_path) != m_facts.stamp) { out.failure = Failure::Changed; out.error = Text("The wallet file changed before the action started."); return out; }
        if (m_reload_only) {
            out.recovery = m_actual.recovery; out.auxiliary = m_actual.auxiliary;
            loadResult(out, m_target, 0); if (out.count) out.primary_index = 0;
            return out;
        }
        if (m_request.kind == Kind::Restore && !m_copied) {
            std::vector<bilingual_str> warnings; out.effect_started = true;
            auto restored = m_services.node->walletLoader().restoreWallet(fs::PathFromString(m_facts.source_path.toStdString()), m_target.toStdString(), warnings, false);
            Append(out.warnings, walletcreation::JoinWarnings(warnings));
            if (!restored) { out.failure = Failure::Backend; out.error = QString::fromStdString(util::ErrorString(restored).translated); return out; }
            out.copied = true; out.recovery.push_back(QDir(m_facts.wallet_directory).filePath(m_target));
            // A public implementation may return a handle even when loading
            // was disabled. Preserve that actual result without a second load.
            if (*restored) {
                auto wallet = std::move(*restored);
                out.primary = walletidentity::Canonical(QString::fromStdString(wallet->getWalletName()), m_facts.wallet_directory);
                out.names[0] = out.primary; out.primary_index = 0; out.key_scheme = Metadata(*wallet); out.count = 1;
                if (m_observation) out.observations[0] = m_observation->resolve(out.primary, [directory = m_facts.wallet_directory](interfaces::Wallet& value) { return walletidentity::Canonical(QString::fromStdString(value.getWalletName()), directory); });
                { std::lock_guard lock{m_mutex}; m_backends[0]->wallet = std::move(wallet); }
                return out;
            }
        }
        if (m_facts.format == Format::Berkeley && !m_migrated) {
            auto passphrase = m_secret ? secretinput::Materialize(m_secret) : SecureString{}; m_secret.reset();
            const auto clear = qScopeGuard([&] { QmlUtil::ClearSecureString(passphrase); });
            out.effect_started = true;
            if (!out.recovery.contains(m_facts.wallet_directory)) out.recovery.push_back(m_facts.wallet_directory);
            auto migrated = m_services.node->walletLoader().migrateWallet(m_target.toStdString(), passphrase, false);
            if (!migrated) { const auto& error = util::ErrorString(migrated); out.failure = Reason(error.original); out.error = QString::fromStdString(error.translated); return out; }
            out.migrated = true;
            if (!migrated->backup_path.empty()) out.recovery.push_back(QString::fromStdString(fs::PathToString(migrated->backup_path)));
            if (migrated->watchonly_wallet_name) out.auxiliary.push_back(walletidentity::Canonical(QString::fromStdString(*migrated->watchonly_wallet_name), m_facts.wallet_directory));
            if (migrated->solvables_wallet_name) out.auxiliary.push_back(walletidentity::Canonical(QString::fromStdString(*migrated->solvables_wallet_name), m_facts.wallet_directory));
            if (migrated->wallet) {
                auto wallet = std::move(migrated->wallet);
                out.primary = walletidentity::Canonical(QString::fromStdString(wallet->getWalletName()), m_facts.wallet_directory); out.names[0] = out.primary;
                out.primary_index = 0; out.key_scheme = Metadata(*wallet); out.count = 1;
                if (m_observation) out.observations[0] = m_observation->resolve(out.primary, [directory = m_facts.wallet_directory](interfaces::Wallet& value) { return walletidentity::Canonical(QString::fromStdString(value.getWalletName()), directory); });
                { std::lock_guard lock{m_mutex}; m_backends[0]->wallet = std::move(wallet); }
            } else {
                for (const auto& [id, format] : m_services.node->walletLoader().listWalletDir()) {
                    if (walletidentity::Canonical(QString::fromStdString(id), m_facts.wallet_directory) == m_target && format == "sqlite") { loadResult(out, m_target, 0); if (out.count) out.primary_index = 0; break; }
                }
            }
        } else { loadResult(out, m_target, 0); if (out.count) out.primary_index = 0; }
        for (const auto& auxiliary : out.auxiliary) if (out.count < 3) loadResult(out, auxiliary, out.count);
    } catch (const execution::NotStarted&) {
        out.failure = Failure::Backend; out.error = Text("The wallet operation was cancelled before its next effect.");
    } catch (const std::exception& error) {
        out.failure = SettingsException(error) ? Failure::Settings : Failure::Exception;
        out.error = QString::fromUtf8(error.what()); out.uncertain = out.effect_started;
    }
    return out;
}
void Owner::performed()
{
    const auto outcome = m_effect->result(); if (!outcome) return;
    if (!outcome->value) { complete(Phase::Uncertain, Failure::Exception, Text("Core did not report whether the wallet action finished.")); return; }
    { std::lock_guard lock{m_mutex};
        auto current = *outcome->value;
        m_history.push_back(current);
        if (m_reload_only && !m_actual.error.isEmpty()) Append(current.warnings, Text("Opening the wallet did not finish earlier. The copied wallet is now open; no backup was copied again.") + QLatin1Char('\n') + m_actual.error);
        if (!m_actual.warnings.isEmpty()) { auto warnings = m_actual.warnings; Append(warnings, current.warnings); current.warnings = std::move(warnings); }
        for (const auto& path : m_actual.recovery) if (!current.recovery.contains(path)) current.recovery.push_back(path);
        m_actual = std::move(current); m_copied = m_actual.copied; m_migrated = m_actual.migrated;
    }
    if (m_actual.failure == Failure::CredentialsRequired || m_actual.failure == Failure::Credentials) { for (auto& backend : m_backends) backend.reset(); prompt(Phase::AwaitingPassphrase, m_actual.error, m_actual.failure); return; }
    if (m_actual.count) { publishReady(); return; }
    if (m_actual.failure != Failure::None) {
        Phase phase{Phase::Failed};
        if (m_actual.uncertain) phase = Phase::Uncertain;
        else if (m_copied || m_actual.effect_started) phase = Phase::Partial;
        complete(phase, m_actual.failure, m_actual.error);
        return;
    }
    complete(Phase::Succeeded);
}
void Owner::publishReady()
{
    if (m_detached.load() || (m_services.shutdown && m_services.shutdown->load())) { complete(Phase::Partial, Failure::Backend, Text("The wallet files were kept after their view closed.")); return; }
    { std::lock_guard lock{m_mutex}; m_frame.publication_requested = true; m_awaited = true; }
    update(Phase::Publishing);
}
size_t Owner::publicationCount() const { std::lock_guard lock{m_mutex}; return m_actual.count; }
QString Owner::publicationName(size_t index) const { std::lock_guard lock{m_mutex}; return index < m_actual.count ? m_actual.names[index] : QString{}; }
bool Owner::publicationPrimary(size_t index) const { std::lock_guard lock{m_mutex}; return index == m_actual.primary_index; }
walletopening::NativeEvents Owner::publicationObservation(size_t index) const { std::lock_guard lock{m_mutex}; return index < m_actual.count ? m_actual.observations[index] : walletopening::NativeEvents{}; }
walletopening::NativeEvents Owner::observationForPublication() const { return publicationObservation(0); }
walletpublication::TakeWallet Owner::publicationWallet(size_t index) const
{
    std::lock_guard lock{m_mutex};
    if (index >= m_actual.count || !m_backends[index] || !m_backends[index]->canPublish()) return {};
    return [self = shared_from_this(), backend = m_backends[index]](auto& handler) {
        std::lock_guard lock{self->m_mutex};
        return backend->take(handler);
    };
}
std::shared_ptr<walletpublication::Release> Owner::publicationRelease(size_t index) const
{
    std::lock_guard lock{m_mutex}; return index < m_actual.count && m_backends[index] ? m_backends[index]->release : nullptr;
}
walletpublication::TakeWallet Owner::walletForPublication() const { return publicationWallet(0); }
void Owner::publicationConsumed(walletpublication::Availability value, QString error) { publicationsConsumed({value}, std::move(error)); }
void Owner::publicationsConsumed(std::vector<walletpublication::Availability> values, QString error)
{
    const auto self = shared_from_this();
    if (!m_services.post(execution::Lane::Utility, [self, values = std::move(values), error = std::move(error)] {
            if (!self->m_awaited) return;
            self->m_awaited = false;
            bool all_available{true};
            { std::lock_guard lock{self->m_mutex};
                for (size_t index = 0; index < self->m_actual.count; ++index) {
                    const auto availability = index < values.size() ? values[index] : walletpublication::Availability::Unavailable;
                    all_available &= availability == walletpublication::Availability::Available;
                    const bool unloaded = self->m_actual.observations[index].unloaded && self->m_actual.observations[index].unloaded->load();
                    if (availability != walletpublication::Availability::Unavailable || unloaded) self->m_backends[index].reset();
                }
                for (size_t index = self->m_actual.count; index < 3; ++index) self->m_backends[index].reset();
            }
            // Each exact facade now owns its own backend-cell token. The
            // original aggregate guards only the handoff before this receipt;
            // releasing it here prevents an open auxiliary pinning its sibling.
            if (self->m_release) self->m_release->release();
            if (all_available && self->m_actual.failure == Failure::None) {
                self->complete(Phase::Succeeded);
                return;
            }
            const auto failure = self->m_actual.failure == Failure::None ? Failure::Backend : self->m_actual.failure;
            QString message = self->m_actual.error;
            if (message.isEmpty()) message = error;
            if (message.isEmpty()) message = Text("The wallet files exist, but they cannot be shown.");
            self->complete(Phase::Partial, failure, std::move(message));
        })) complete(Phase::Partial, Failure::Backend, Text("The wallet result could not be shown."));
}
void Owner::complete(Phase phase, Failure reason, QString error)
{
    if (phase == Phase::Cancelled && (m_copied || m_migrated || m_actual.effect_started)) {
        phase = Phase::Partial; reason = m_actual.failure == Failure::None ? Failure::Backend : m_actual.failure;
        error = Text("The remaining operation was cancelled. Existing wallet files and recovery backups were kept.");
    }
    bool no_native_wallet{false};
    {
        std::lock_guard lock{m_mutex};
        no_native_wallet = std::none_of(m_backends.begin(), m_backends.end(), [](const auto& backend) { return backend && (backend->get() || backend->notification); });
        const auto prepared = FailureFrame(reason, error, m_target, m_copied || m_migrated || m_actual.effect_started);
        m_frame.phase = phase; ++m_frame.revision; m_frame.publication_requested = false; m_frame.prompt_id.clear();
        m_frame.error = prepared.error; m_frame.error_title = prepared.error_title; m_frame.error_description = prepared.error_description;
        m_frame.target_id = m_actual.primary_index < m_actual.count ? m_actual.names[m_actual.primary_index] : m_target; m_frame.key_scheme = m_actual.key_scheme;
        m_frame.outcome_known = phase != Phase::Uncertain;
        m_frame.result.result_id = QString::number(m_request.id) + QStringLiteral(":result:") + QString::number(m_frame.revision); m_frame.result.wallet_id = m_frame.target_id;
        m_frame.result.auxiliary_wallet_ids = m_actual.auxiliary; m_frame.result.recovery_paths = m_actual.recovery;
        if ((m_copied || m_migrated || m_actual.effect_started) && !m_facts.target_path.isEmpty() && !m_frame.result.recovery_paths.contains(m_facts.target_path)) m_frame.result.recovery_paths.push_back(m_facts.target_path);
        if (m_migrated) {
            m_frame.result.effect = WalletOperationResult::WalletMigrated;
        } else if (m_copied) {
            m_frame.result.effect = phase == Phase::Succeeded ? WalletOperationResult::WalletRestored : WalletOperationResult::BackupCopied;
        } else {
            m_frame.result.effect = m_actual.effect_started ? WalletOperationResult::PossibleEffect : WalletOperationResult::NoEffect;
        }
        m_frame.result.outcome_known = m_frame.outcome_known;
        if (!m_actual.warnings.isEmpty()) m_frame.result.warnings = {m_actual.warnings.left(1024)};
        m_frame.result.warnings_truncated = m_actual.warnings.size() > 1024;
        if (!m_actual.count) for (auto& backend : m_backends) backend.reset();
    }
    m_secret.reset();
    // Empty cells cannot retain a native wallet or a publication retry. Their
    // reserved Cleanup tasks can finish later without holding the controller's
    // aggregate shutdown fence. Nonempty, unconsumed cells remain owned.
    if (m_release && (!m_backend_retained || no_native_wallet)) m_release->release();
    if (m_detached.load()) m_services.admission->presentationConsumed(m_request.id);
    notify(); m_keep_alive.reset();
}
bool Owner::reconcileNeedsTicket() const
{
    std::lock_guard lock{m_mutex};
    // This exact public error precedes adoption. Generic/late exceptions are
    // not proof that reopening the name is safe; preserve their recovery facts.
    return m_actual.count == 0 && m_copied && m_actual.failure == Failure::Pruned && !m_actual.target_stamp.path.isEmpty();
}
bool Owner::reconcileAvailable() const
{
    std::lock_guard lock{m_mutex};
    if (m_reconciled) return false;
    if (m_actual.count == 0 && m_copied && m_actual.failure == Failure::Pruned && !m_actual.target_stamp.path.isEmpty()) return true;
    for (size_t i = 0; i < m_actual.count; ++i) if (m_backends[i] && m_backends[i]->canPublish() && !(m_actual.observations[i].unloaded && m_actual.observations[i].unloaded->load())) return true;
    return false;
}
void Owner::reconcileWithPorts(std::shared_ptr<walletpublication::Release> release, std::shared_ptr<walletpublication::NativeObservation> observation)
{
    if (!reconcileNeedsTicket()) { reconcilePublication(); return; }
    if (m_reconciled) { if (release) release->release(); return; }
    m_reconciled = true; m_reload_only = true; m_release = std::move(release); m_observation = std::move(observation); m_backend_retained = false;
    m_keep_alive = shared_from_this(); inspect();
}
void Owner::reconcilePublication() { if (m_reconciled) return; m_reconciled = true; m_keep_alive = shared_from_this(); publishReady(); }
void Owner::cancel()
{
    const auto phase = snapshot().phase;
    if (phase != Phase::Requested && phase != Phase::Validating && phase != Phase::AwaitingConfirmation && phase != Phase::AwaitingPassphrase) return;
    m_cancelled = true;
    const auto self = shared_from_this();
    const auto admitted = m_services.post(execution::Lane::Utility, [self] { const auto current = self->snapshot().phase; if (current == Phase::AwaitingConfirmation || current == Phase::AwaitingPassphrase) self->complete(Phase::Cancelled); });
    // A rejected transport cannot leave a prompt's self-retention cycle alive.
    // Cancellation is already proven by the pre-effect atomic guard; this
    // bounded fallback performs no Core/I/O and retires all payloads on Native.
    if (!admitted && (phase == Phase::AwaitingConfirmation || phase == Phase::AwaitingPassphrase)) complete(Phase::Cancelled);
}
void Owner::detach()
{
    m_detached = true; const auto phase = snapshot().phase;
    if (phase == Phase::Requested || phase == Phase::Validating || phase == Phase::AwaitingConfirmation || phase == Phase::AwaitingPassphrase) cancel();
    if (phase == Phase::Publishing) { const auto self = shared_from_this(); (void)m_services.post(execution::Lane::Utility, [self] { self->complete(Phase::Partial, Failure::Backend, Text("The window closed; wallet files and results were kept.")); }); }
}
} // namespace walletrestoration
