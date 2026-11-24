// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/walletcreation.h>
#include <qml/executor.h>
#include <qml/walletidentity.h>
#include <qml/walletnames.h>
#include <qml/util.h>
#include <interfaces/node.h>
#include <interfaces/wallet.h>
#include <key_io.h>
#include <script/descriptor.h>
#include <util/result.h>
#include <util/translation.h>
#include <wallet/imports.h>
#include <wallet/walletutil.h>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <algorithm>
#include <cassert>
#include <limits>
#include <stdexcept>
#include <utility>

namespace walletcreation {
namespace {
QString Text(const char* text) { return QCoreApplication::translate("WalletCreationModel", text); }
WalletOperationError Error(const char* code, QString message, const char* field = "", QString recovery = {})
{ return {QString::fromLatin1(code), std::move(message), QString::fromLatin1(field), std::move(recovery)}; }
}

execution::SubmitResult Services::post(execution::Lane lane, std::shared_ptr<execution::Task> task) const
{
    assert(lane == execution::Lane::Core || lane == execution::Lane::Utility);
    if (lane == execution::Lane::Core) {
        auto delivery = std::make_shared<execution::ValueCleanupQueue>(outcomes);
        return delivery->postWithOutcome(*core, std::move(task));
    }
    const bool accepted = outcomes->post([task = std::move(task)] {
        task->execute();
        task->deliver();
    });
    return {{}, accepted ? execution::Rejection::None : execution::Rejection::Transport};
}
QString JoinWarnings(const std::vector<bilingual_str>& warnings)
{
    QStringList lines;
    for (const auto& warning : warnings) if (!warning.translated.empty()) lines.push_back(QString::fromStdString(warning.translated));
    return lines.join(QLatin1Char('\n'));
}
bool Busy(Phase phase) { return phase == Phase::Requested || phase == Phase::Validating || phase == Phase::Creating || phase == Phase::SettingUp || phase == Phase::Publishing; }
bool Terminal(Phase phase) { return !Busy(phase) && phase != Phase::AwaitingConfirmation && phase != Phase::AwaitingPassphrase; }
quint64 Admission::allocate()
{
    auto id = m_sequence.load();
    while (id != std::numeric_limits<quint64>::max()) if (m_sequence.compare_exchange_weak(id, id + 1)) return id + 1;
    return 0;
}
bool Admission::request(quint64 id)
{
    if (!id || m_legacy.load()) return false;
    quint64 empty{0}; return m_presentation.compare_exchange_strong(empty, id);
}
bool Admission::reserveName(quint64 id, const QString& name)
{
    std::lock_guard lock{m_mutex};
    if (m_legacy.load() || m_presentation.load() != id) return false;
    for (const auto& [operation, pending] : m_names) if (operation != id && pending.compare(name, Qt::CaseInsensitive) == 0) return false;
    m_names.emplace(id, name); return true;
}
void Admission::releaseName(quint64 id) { std::lock_guard lock{m_mutex}; m_names.erase(id); }
void Admission::presentationConsumed(quint64 id) { m_presentation.compare_exchange_strong(id, 0); }
Facts ReadFacts(interfaces::Node& node, const Request& request, const std::shared_ptr<WalletNameRegistry>& names)
{
    Facts facts; facts.wallet_directory = QString::fromStdString(node.walletLoader().getWalletDir());
    facts.signer_current = !request.signer.version || (request.signer.admitted_version != 0 && request.signer.version->load() == request.signer.admitted_version);
    QSettings settings;
    for (const auto& [name, format] : node.walletLoader().listWalletDir()) {
        (void)format;
        const auto id = walletidentity::Canonical(QString::fromStdString(name), facts.wallet_directory);
        facts.existing.emplace_back(id, walletidentity::ReadAlias(settings, id));
    }
    std::optional<QString> previous;
    while (names) {
        QString id;
        { std::lock_guard lock{names->mutex}; const auto next = previous ? names->names.upper_bound(*previous) : names->names.begin(); if (next == names->names.end()) break; id = *next; }
        previous = id;
        facts.existing.emplace_back(id, walletidentity::ReadAlias(settings, id));
    }
    if (settings.status() != QSettings::NoError) throw std::runtime_error("Wallet aliases could not be read");
    if (request.kind == Kind::Software && request.encrypt) {
        auto passphrase = secretinput::Materialize(request.passphrase), confirmation = secretinput::Materialize(request.confirmation);
        facts.credentials_present = !passphrase.empty(); facts.credentials_match = passphrase == confirmation;
        QmlUtil::ClearSecureString(passphrase); QmlUtil::ClearSecureString(confirmation);
    }
    return facts;
}
Decision Validate(const Request& request, const Facts& facts)
{
    Decision result; result.name = request.legacy_normalize ? request.submitted_name.trimmed() : request.submitted_name;
    if (const auto error = walletidentity::CreationError(result.name); !error.isEmpty()) { result.error = Error("InvalidName", error, "name"); return result; }
    const auto candidate = result.name.normalized(QString::NormalizationForm_C);
    for (const auto& [id, alias] : facts.existing) {
        if (id.normalized(QString::NormalizationForm_C).compare(candidate, Qt::CaseInsensitive) == 0 || (!alias.isEmpty() && alias.normalized(QString::NormalizationForm_C).compare(candidate, Qt::CaseInsensitive) == 0)) {
            result.error = Error("NameConflict", Text("A wallet with this name already exists"), "name"); return result;
        }
    }
    if (request.kind == Kind::Software && request.encrypt && (!facts.credentials_present || !facts.credentials_match)) {
        result.error = Error("CredentialsMismatch", Text("Enter matching nonempty passwords."), "credentials"); return result;
    }
    if (request.kind == Kind::WatchOnly && (request.extended_public_key.size() > 4096 || !DecodeExtPubKey(request.extended_public_key.trimmed().toStdString()).pubkey.IsValid())) {
        result.error = Error("InvalidPublicKey", Text("Invalid extended public key."), "extendedPublicKey"); return result;
    }
    if (request.kind == Kind::ExternalSigner) {
        const auto& signer = request.signer; QString error;
        if (!facts.signer_current) error = Text("The signer configuration changed. Check the connected signer again.");
        else if (signer.pending) error = Text("Wait for the external signer check to finish.");
        else if (!signer.configured) error = Text("Set an external signer path in Wallet settings first.");
        else if (!signer.error.isEmpty()) error = signer.error;
        else if (signer.count == 0) error = Text("Connect an external signer and try again.");
        else if (signer.count != 1) error = Text("More than one external signer was found. Connect only one device.");
        else if (!request.signer_id.isEmpty() && request.signer_id != (signer.id.isEmpty() ? signer.name : signer.id)) error = Text("The selected signer changed. Choose the connected signer again.");
        if (!error.isEmpty()) { result.error = Error("SignerUnavailable", error, "selectedSignerId"); return result; }
    }
    result.wallet_directory = facts.wallet_directory;
    if (!facts.wallet_directory.isEmpty()) result.recovery_path = QDir(facts.wallet_directory).filePath(result.name);
    result.flags = wallet::WALLET_FLAG_DESCRIPTORS;
    if (request.kind != Kind::Software) result.flags |= wallet::WALLET_FLAG_DISABLE_PRIVATE_KEYS;
    if (request.kind == Kind::WatchOnly) result.flags |= wallet::WALLET_FLAG_BLANK_WALLET;
    if (request.kind == Kind::ExternalSigner) result.flags |= wallet::WALLET_FLAG_EXTERNAL_SIGNER;
    return result;
}
NativeResult Create(interfaces::Node& node, const Request& request, const Decision& decision,
                    std::unique_ptr<interfaces::Wallet>& created_wallet)
{
    NativeResult result; result.name = decision.name; result.recovery_path = decision.recovery_path; SecureString passphrase;
    try {
        if (request.kind == Kind::ExternalSigner && request.signer.version && (request.signer.admitted_version == 0 || request.signer.version->load() != request.signer.admitted_version)) {
            result.error = Text("The signer configuration changed. No wallet was created."); return result;
        }
        if (request.kind == Kind::Software && request.encrypt) passphrase = secretinput::Materialize(request.passphrase);
        std::vector<bilingual_str> warnings;
        result.effect_started = true;
        auto wallet = node.walletLoader().createWallet(decision.name.toStdString(), passphrase, decision.flags, warnings);
        QmlUtil::ClearSecureString(passphrase); result.warnings = JoinWarnings(warnings);
        if (!wallet) {
            result.error = QString::fromStdString(util::ErrorString(wallet).translated);
            // A reported failure is a known call outcome, not proof that no
            // storage artifact exists. Inspect only the public directory/path.
            if (!result.recovery_path.isEmpty()) {
                result.created = QFileInfo::exists(result.recovery_path);
                result.incomplete = result.created;
            }
            return result;
        }
        result.created = true; created_wallet = std::move(*wallet);
        try {
            result.name = walletidentity::Canonical(QString::fromStdString(created_wallet->getWalletName()), decision.wallet_directory);
            if (!decision.wallet_directory.isEmpty()) result.recovery_path = QDir(decision.wallet_directory).filePath(result.name);
            result.key_scheme = request.kind == Kind::ExternalSigner ? Text("External signer") : request.kind == Kind::WatchOnly ? Text("Watch-only") : Text("Software wallet");
        } catch (...) { result.incomplete = true; result.error = Text("The wallet was created, but its metadata could not be read. Check the wallet directory before retrying."); }
    } catch (...) {
        QmlUtil::ClearSecureString(passphrase); result.uncertain = result.effect_started;
        result.error = result.effect_started ? Text("The wallet may not have been created. Check the wallet directory before trying again.") : Text("The password could not be used. No wallet was created.");
    }
    if (result.error.isEmpty() && !result.created) result.error = Text("Wallet creation failed.");
    return result;
}
QString Setup(interfaces::Wallet& wallet, const Request& request)
{
    if (request.kind != Kind::WatchOnly) return {};
    const auto key = request.extended_public_key.trimmed().toStdString();
    std::vector<wallet::ImportDescriptorRequest> descriptors;
    for (bool internal : {false, true}) {
        const auto descriptor = "wpkh(" + key + (internal ? "/1/*)" : "/0/*)");
        const auto checksum = GetDescriptorChecksum(descriptor);
        if (checksum.empty()) return Text("Failed to import descriptors into watch-only wallet.");
        wallet::ImportDescriptorRequest value; value.descriptor = descriptor + "#" + checksum;
        value.active = true; value.internal = internal; value.range = std::pair<int64_t, int64_t>{0, 0}; value.next_index = 0;
        descriptors.push_back(std::move(value));
    }
    const auto results = wallet.importDescriptors(descriptors);
    return results.size() == descriptors.size() && std::none_of(results.begin(), results.end(), [](const auto& result) { return result.has_error(); })
        ? QString{} : Text("Failed to import descriptors into watch-only wallet.");
}
Backend::~Backend()
{
    std::visit([](auto& source) { source.reset(); }, wallet);
    notification.reset();
    if (release) release->release();
}

interfaces::Wallet* Backend::get() const
{
    return std::visit([](const auto& source) { return source.get(); }, wallet);
}

walletopening::Source Backend::take(std::unique_ptr<interfaces::Handler>& handler)
{
    if (notification) return notification->take(handler);
    if (get()) transferred = true;
    return std::exchange(wallet, {});
}
Service::Service(Services services, Request request, Notify notify, std::shared_ptr<walletpublication::Release> release, std::shared_ptr<walletpublication::NativeObservation> observation)
    : m_services(std::move(services)), m_request(std::move(request)), m_notify(std::move(notify)), m_release(std::move(release)), m_observation(std::move(observation)) { m_frame.target_id = m_request.submitted_name; }
Service::~Service()
{
    if (!m_backend_retained && m_release) m_release->release();
    if (!m_started || m_detached.load()) { m_services.admission->releaseName(m_request.id); m_services.admission->presentationConsumed(m_request.id); }
}
std::shared_ptr<Service> Service::Make(Services services, Request request, Notify notify, std::shared_ptr<walletpublication::Release> release, std::shared_ptr<walletpublication::NativeObservation> observation)
{
    return services.retain<Service>([&] {
        return std::unique_ptr<Service>{new Service(std::move(services), std::move(request), std::move(notify),
            std::move(release), std::move(observation))};
    });
}
Frame Service::snapshot() const { std::lock_guard lock{m_mutex}; return m_frame; }
QString Service::errorText() const { std::lock_guard lock{m_mutex}; return m_actual.error.isEmpty() ? m_frame.error.message : m_actual.error; }
walletopening::NativeEvents Service::observationForPublication() const { std::lock_guard lock{m_mutex}; return m_actual.observation; }
QString Service::warningText() const { std::lock_guard lock{m_mutex}; return m_actual.warnings; }
walletpublication::TakeWallet Service::walletForPublication() const
{
    std::lock_guard lock{m_mutex};
    if (!m_backend || !m_backend->canPublish()) return {};
    return [self = shared_from_this(), backend = m_backend](auto& handler) {
        std::lock_guard lock{self->m_mutex};
        return backend->take(handler);
    };
}
void Service::notify() { if (!m_detached.load()) try { m_notify(); } catch (...) {} }
void Service::update(Phase phase) { { std::lock_guard lock{m_mutex}; m_frame.phase = phase; ++m_frame.revision; } notify(); }
void Service::start()
{
    if (std::exchange(m_started, true)) return;
    m_keep_alive = shared_from_this(); update(Phase::Validating);
    const auto weak = weak_from_this();
    m_facts = std::make_shared<execution::OwnedCommand<Facts>>([weak] { if (const auto owner = weak.lock()) owner->factsReady(); });
    const auto admitted = m_services.start(*m_facts, [self = shared_from_this()] {
        if ((self->m_services.shutdown && self->m_services.shutdown->load()) || self->m_cancelled.load()) throw execution::NotStarted{execution::NotStarted::Cause::Cancelled};
        return ReadFacts(*self->m_services.node, self->m_request, self->m_services.open_names);
    });
    if (!admitted) complete(Phase::Rejected, Error("TransportRejected", Text("Wallet creation could not start. No wallet was created.")));
}
void Service::factsReady()
{
    const auto result = m_facts->result(); if (!result) return;
    if (m_cancelled.load() || (m_services.shutdown && m_services.shutdown->load())) { complete(Phase::Cancelled); return; }
    if (!result->value) { complete(Phase::Failed, Error("ValidationUnavailable", Text("Wallet details could not be checked. No wallet was created."))); return; }
    if (!m_services.initialized || !m_services.initialized->load()) { complete(Phase::Failed, Error("Unavailable", Text("Wallets are still loading. Try again in a moment."))); return; }
    m_decision = Validate(m_request, *result->value);
    { std::lock_guard lock{m_mutex}; m_frame.target_id = m_decision.name; }
    if (!m_decision.error.empty()) { complete(Phase::Failed, m_decision.error); return; }
    if (!m_services.admission->reserveName(m_request.id, m_decision.name)) { complete(Phase::Rejected, Error("Busy", Text("Wait for the current wallet operation to finish."))); return; }
    preparePublication(m_release);
}
void Service::preparePublication(std::shared_ptr<walletpublication::Release> release)
{
    const auto self = shared_from_this();
    const auto admitted = m_services.post(execution::Lane::Utility, [self, release = std::move(release)] {
        if (!release) { self->complete(Phase::Rejected, Error("PublicationUnavailable", Text("Wallet creation could not start. No wallet was created."))); return; }
        if (self->m_cancelled.load() || self->m_detached.load() || (self->m_services.shutdown && self->m_services.shutdown->load())) { release->release(); self->complete(Phase::Cancelled); return; }
        auto retained_backend = self->m_services.retain<Backend>([release] { auto value = std::make_unique<Backend>(); value->release = release; return value; });
        if (!retained_backend) { release->release(); self->complete(Phase::Rejected, Error("TransportRejected", Text("Wallet creation could not start. No wallet was created."))); return; }
        { std::lock_guard lock{self->m_mutex}; self->m_frame.publication_requested = false; }
        self->update(Phase::Creating);
        const auto weak = self->weak_from_this();
        self->m_create = std::make_shared<execution::OwnedCommand<NativeResult>>([weak] { if (const auto owner = weak.lock()) owner->created(); });
        const auto backend = retained_backend;
        { std::lock_guard lock{self->m_mutex}; self->m_backend = std::move(retained_backend); self->m_backend_retained = true; }
        const auto admitted = self->m_services.start(*self->m_create, [self, backend] {
            if (self->m_cancelled.load()) throw execution::NotStarted{execution::NotStarted::Cause::Cancelled};
            std::unique_ptr<interfaces::Wallet> wallet;
            auto result = Create(*self->m_services.node, self->m_request, self->m_decision, wallet);
            backend->wallet = std::move(wallet);
            if (result.created && self->m_observation) {
                try { result.observation = self->m_observation->resolve(result.name, [directory = self->m_decision.wallet_directory](interfaces::Wallet& wallet) { return walletidentity::Canonical(QString::fromStdString(wallet.getWalletName()), directory); }); }
                catch (...) { self->m_observation->fail(); }
                if (self->m_observation->failed()) { result.incomplete = true; result.error = Text("Wallet storage was created, but its lifecycle observation is unavailable. Check the wallet directory before retrying."); }
            }
            return result;
        });
        if (!admitted) self->complete(Phase::Rejected, Error("TransportRejected", Text("Wallet creation could not start. No wallet was created.")));
    });
    if (!admitted) complete(Phase::Rejected, Error("TransportRejected", Text("Wallet creation could not start. No wallet was created.")));
}
void Service::created()
{
    const auto result = m_create->result(); if (!result) return;
    if (!result->value) {
        bool cancelled{false}; try { if (result->error) std::rethrow_exception(result->error); } catch (const execution::NotStarted&) { cancelled = true; } catch (...) {}
        complete(cancelled ? Phase::Cancelled : Phase::Uncertain, cancelled ? WalletOperationError{} : Error("OutcomeUnknown", Text("The wallet may not have been created. Check the wallet directory before trying again."))); return;
    }
    { std::lock_guard lock{m_mutex}; m_actual = *result->value; m_native_created = m_actual.created; }
    if (!m_native_created || m_actual.uncertain || m_actual.incomplete) {
        complete(m_actual.uncertain ? Phase::Uncertain : m_actual.created ? Phase::Partial : Phase::Failed, Error("CoreCreateFailed", m_actual.error.left(1024), "", Text("Check the wallet directory before retrying."))); return;
    }
    if (m_request.kind != Kind::WatchOnly) { readyToPublish(); return; }
    update(Phase::SettingUp);
    const auto weak = weak_from_this();
    m_setup = std::make_shared<execution::OwnedCommand<QString>>([weak] { if (const auto owner = weak.lock()) owner->setupReady(); });
    std::shared_ptr<Backend> backend;
    { std::lock_guard lock{m_mutex}; backend = m_backend; }
    const auto admitted = m_services.start(*m_setup, [self = shared_from_this(), backend] {
        QString error;
        try { error = Setup(*backend->get(), self->m_request); }
        catch (...) { error = Text("The wallet file was created, but its keys may not have been set up. Check the wallet directory before trying again."); }
        if (!error.isEmpty()) try { backend->get()->remove(); } catch (...) { error += QLatin1Char('\n') + Text("The incomplete wallet could not be unloaded."); }
        return error;
    });
    if (!admitted) { { std::lock_guard lock{m_mutex}; m_actual.incomplete = true; } complete(Phase::Partial, Error("SetupNotQueued", Text("The wallet file was created, but its setup could not start."), "", Text("Keep the created wallet file; do not repeat creation."))); }
}
void Service::setupReady()
{
    const auto result = m_setup->result(); if (!result) return;
    const auto error = result->value ? *result->value : Text("The wallet file was created, but its setup may not have finished.");
    if (!error.isEmpty()) { { std::lock_guard lock{m_mutex}; m_actual.error = error; m_actual.incomplete = true; } complete(Phase::Partial, Error("SetupIncomplete", error.left(1024), "", Text("Keep the created wallet file; do not repeat creation."))); return; }
    readyToPublish();
}
void Service::readyToPublish()
{
    if (m_detached.load() || (m_services.shutdown && m_services.shutdown->load())) { complete(Phase::Partial, Error("PresentationClosed", Text("Wallet storage was created after the view closed."))); return; }
    { std::lock_guard lock{m_mutex}; m_publication_awaited = true; m_frame.publication_requested = true; }
    update(Phase::Publishing);
}
void Service::publicationConsumed(walletpublication::Availability availability, QString error)
{
    const auto self = shared_from_this();
    const auto admitted = m_services.post(execution::Lane::Utility, [self, availability, error = std::move(error)] {
        if (!self->m_publication_awaited) return;
        self->m_publication_awaited = false;
        {
            std::lock_guard lock{self->m_mutex};
            // A failed GUI fence may retain an Unavailable diagnostic even
            // after this exact Native opening was publicly unloaded. Its
            // backend is consumed, not a supported adoption-retry resource.
            const bool original_unloaded = self->m_actual.observation.unloaded && self->m_actual.observation.unloaded->load();
            if (availability != walletpublication::Availability::Unavailable || original_unloaded) self->m_backend.reset();
        }
        if (availability == walletpublication::Availability::Available) self->complete(Phase::Succeeded);
        else self->complete(Phase::Partial, Error("PublicationUnavailable", error.isEmpty() ? Text("Wallet storage was created, but its view is unavailable.") : error, "", Text("Keep the created wallet file. Check this result or open that wallet later.")));
    });
    if (!admitted) complete(Phase::Partial, Error("PublicationReceiptUnavailable", Text("The wallet was created, but its result could not be shown.")));
}
void Service::complete(Phase phase, WalletOperationError error)
{
    {
        std::lock_guard lock{m_mutex};
        m_frame.phase = phase; ++m_frame.revision; m_frame.publication_requested = false;
        m_frame.error = std::move(error); m_frame.outcome_known = phase != Phase::Uncertain;
        if (!m_actual.name.isEmpty()) m_frame.target_id = m_actual.name;
        m_frame.key_scheme = m_actual.key_scheme;
        m_frame.result.result_id = QString::number(m_request.id) + QStringLiteral(":result");
        m_frame.result.wallet_id = m_frame.target_id;
        if (phase == Phase::Uncertain) {
            m_frame.result.effect = WalletOperationResult::PossibleEffect;
        } else if (m_actual.created) {
            if (phase == Phase::Succeeded) m_frame.result.effect = WalletOperationResult::WalletCreated;
            else if (m_actual.incomplete) m_frame.result.effect = WalletOperationResult::SetupIncomplete;
            else m_frame.result.effect = WalletOperationResult::PublicationUnavailable;
        } else {
            m_frame.result.effect = m_actual.effect_started ? WalletOperationResult::PossibleEffect : WalletOperationResult::NoEffect;
        }
        m_frame.result.outcome_known = m_frame.outcome_known;
        if (!m_actual.recovery_path.isEmpty()) m_frame.result.recovery_paths = {m_actual.recovery_path};
        if (!m_actual.warnings.isEmpty()) m_frame.result.warnings = {m_actual.warnings.left(1024)};
        m_frame.result.warnings_truncated = m_actual.warnings.size() > 1024;
        if (!m_actual.created || m_actual.incomplete) m_backend.reset();
    }
    if (!m_backend_retained && m_release) m_release->release();
    m_services.admission->releaseName(m_request.id);
    if (m_detached.load()) m_services.admission->presentationConsumed(m_request.id);
    notify(); m_keep_alive.reset();
}
void Service::cancel() { m_cancelled = true; }
void Service::detach()
{
    m_detached = true;
    const auto frame = snapshot();
    if (frame.phase == Phase::Requested || frame.phase == Phase::Validating) m_cancelled = true;
    if (frame.phase == Phase::Publishing || frame.publication_requested) {
        const auto self = shared_from_this();
        (void)m_services.post(execution::Lane::Utility, [self] { if (self->m_native_created) self->complete(Phase::Partial, Error("PresentationClosed", Text("Wallet storage was created after the view closed."))); else self->complete(Phase::Cancelled); });
    }
}
void Service::reconcilePublication()
{
    { std::lock_guard lock{m_mutex}; if (m_reconciled || !m_backend || !m_backend->canPublish()) return; m_reconciled = true; } m_keep_alive = shared_from_this(); readyToPublish();
}
} // namespace walletcreation
