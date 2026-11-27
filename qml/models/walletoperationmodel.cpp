// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/models/walletoperationmodel.h>
#include <qml/models/walletmodel.h>
#include <QScopeGuard>
#include <QQmlEngine>
#include <utility>
#include <limits>
#include <algorithm>

namespace {
// A read-only terminal projection over the legacy Open writer's exact receipt.
// It does not admit a second command, retain a backend handle, or call Core.
class OpenNoticeService final : public walletcreation::OperationService {
public:
    OpenNoticeService(walletcreation::Frame value, QString text, std::shared_ptr<const void> lifetime) : frame(std::move(value)), warnings(std::move(text)), owner(std::move(lifetime)) {}
    void start() override {} void cancel() override {} void detach() override {}
    walletcreation::Frame snapshot() const override { return frame; }
    QString errorText() const override { return {}; }
    QString warningText() const override { return warnings; }
    walletopening::NativeEvents observationForPublication() const override { return {}; }
    walletpublication::TakeWallet walletForPublication() const override { return {}; }
    void publicationConsumed(walletpublication::Availability, QString) override {}
    void reconcilePublication() override {}
private:
    const walletcreation::Frame frame;
    const QString warnings;
    const std::shared_ptr<const void> owner;
};
} // namespace
WalletOperationModel::WalletOperationModel(Services services) : QObject(nullptr), m_services(std::move(services)), m_kind(Open) {}
WalletOperationModel* WalletOperationModel::OpenNotice(Services services, quint64 id, QString target, QString warnings, std::shared_ptr<const void> lifetime, WalletModel* original)
{
    const QPointer<QObject> parent = services.projection_parent;
    const QPointer<WalletModel> exact{original};
    auto* model = new WalletOperationModel(std::move(services));
    model->m_request.id = id; model->m_frame.target_id = std::move(target); model->m_frame.phase = walletcreation::Phase::Succeeded;
    model->m_frame.outcome_known = true; model->m_frame.result.result_id = QString::number(id) + QStringLiteral(":opened");
    model->m_frame.result.wallet_id = model->m_frame.target_id; model->m_frame.result.effect = WalletOperationResult::WalletOpened;
    model->m_frame.result.warnings = {warnings.left(1024)}; model->m_frame.result.warnings_truncated = warnings.size() > 1024;
    model->m_owner = model->m_services.native.retain<OpenNoticeService>(
        [frame = model->m_frame, warnings = std::move(warnings), lifetime = std::move(lifetime)]() mutable { return std::make_unique<OpenNoticeService>(std::move(frame), std::move(warnings), std::move(lifetime)); });
    if (model->m_owner) model->m_warnings.bind(model->m_owner);
    else {
        model->m_frame.error = {QStringLiteral("WarningDetailsUnavailable"), tr("Full warning details could not be shown. Read debug.log before closing the wallet."), {}, {}};
        model->m_frame.error_title = tr("Wallet opened with warnings"); model->m_frame.error_description = model->m_frame.error.message;
    }
    model->m_result_wallet = exact; model->m_finished = true;
    if (exact) { model->m_available = connect(exact, &WalletModel::availableChanged, model, &WalletOperationModel::changed); model->m_destroyed = connect(exact, &QObject::destroyed, model, &WalletOperationModel::changed); }
    if (parent) model->setParent(parent);
    QQmlEngine::setObjectOwnership(model, QQmlEngine::CppOwnership);
    return model;
}

WalletOperationModel* WalletOperationModel::OpenFailure(Services services, quint64 id, QString target, const QString& error)
{
    const QPointer<QObject> parent = services.projection_parent;
    auto* model = new WalletOperationModel(std::move(services));
    model->m_request.id = id; model->m_frame.target_id = std::move(target); model->m_frame.phase = walletcreation::Phase::Failed;
    model->m_frame.outcome_known = true; model->m_frame.result.effect = WalletOperationResult::NoEffect;
    model->m_frame.error = {QStringLiteral("OpenFailed"), error.left(1024), {}, {}};
    model->m_frame.error_title = tr("Failed to open wallet"); model->m_frame.error_description = model->m_frame.error.message;
    // The bounded message is the whole report: no Native value is retained.
    model->m_finished = true;
    if (parent) model->setParent(parent);
    QQmlEngine::setObjectOwnership(model, QQmlEngine::CppOwnership);
    return model;
}

WalletOperationText::WalletOperationText(bool warning, QObject* parent) : QAbstractListModel(parent), m_warning(warning) {}
QString WalletOperationText::text() const { return m_owner ? m_warning ? m_owner->warningText() : m_owner->errorText() : QString{}; }
int WalletOperationText::rowCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : int((text().size() + 1023) / 1024); }
QVariant WalletOperationText::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount() || (role != Qt::DisplayRole && role != Qt::UserRole && role != Qt::UserRole + 1)) return {};
    return text().mid(index.row() * 1024, 1024);
}
QHash<int, QByteArray> WalletOperationText::roleNames() const { return {{Qt::UserRole + 1, "content"}}; }
void WalletOperationText::bind(std::shared_ptr<walletcreation::OperationService> owner) { m_owner = std::move(owner); }
void WalletOperationText::refresh() { const QPointer<WalletOperationText> guard{this}; beginResetModel(); if (guard) endResetModel(); }
WalletOperationModel* WalletOperationModel::Make(Services services, walletcreation::Request request)
{
    if (!request.id) request.id = services.native.admission->allocate();
    if (!request.transport_error.empty()) { request.submitted_name = request.submitted_name.left(1024); request.extended_public_key.clear(); }
    return new WalletOperationModel(std::move(services), std::move(request));
}
WalletOperationModel* WalletOperationModel::Make(Services services, walletrestoration::Request request)
{
    if (!request.id) request.id = services.native.admission->allocate();
    return new WalletOperationModel(std::move(services), std::move(request));
}
WalletOperationModel::WalletOperationModel(Services services, walletrestoration::Request request)
    : QObject(nullptr), m_services(std::move(services)), m_kind(request.kind == walletrestoration::Kind::Restore ? Restore : Migrate), m_source(request.source.left(32768))
{
    m_request.id = request.id; m_request.submitted_name = m_source; m_request.encrypt = false;
    const QPointer<QObject> parent{m_services.projection_parent}; const bool needs_parent = parent != nullptr;
    QQmlEngine::setObjectOwnership(this, QQmlEngine::JavaScriptOwnership);
    const auto rejected = [this, parent](WalletOperationError error) {
        m_accepted = false; m_frame.phase = walletcreation::Phase::Rejected; m_frame.error = std::move(error); m_frame.error_title = tr("Wallet operation could not start"); m_frame.error_description = m_frame.error.message; m_frame.outcome_known = true;
        m_frame.result.result_id = operationId() + QStringLiteral(":rejected"); m_frame.result.outcome_known = true;
        m_services.native.admission->presentationConsumed(m_request.id);
        if (m_ticket) { m_ticket->backend_release->release(); m_services.publisher->finishCreation(m_ticket); m_ticket.reset(); }
        m_services.native.open_names.reset(); m_services.publisher.reset();
        if (parent) { setParent(parent); QQmlEngine::setObjectOwnership(this, QQmlEngine::CppOwnership); }
    };
    if (!request.transport_error.empty()) { rejected(request.transport_error); return; }
    if (!request.id) { rejected({QStringLiteral("IdentityExhausted"), tr("Restart the app before another wallet action."), {}, {}}); return; }
    if (request.source.size() > 32768) { rejected({QStringLiteral("TransportBudget"), tr("This wallet input is too long. Nothing was changed."), {}, {}}); return; }
    const bool needs_admission = request.kind == walletrestoration::Kind::Restore || request.confirmed;
    m_accepted = !needs_admission || m_services.native.admission->request(request.id);
    if (!m_accepted) { rejected({QStringLiteral("Busy"), tr("Wait for the current wallet operation to finish."), {}, {}}); return; }
    if (needs_admission) m_ticket = m_services.publisher ? m_services.publisher->reserveCreation(operationId()) : nullptr;
    if ((needs_admission && !m_ticket) || (needs_parent && !parent)) { rejected({QStringLiteral("PublicationUnavailable"), tr("The wallet action could not start. Nothing was changed."), {}, {}}); return; }
    const QPointer<WalletOperationModel> guard{this};
    m_owner = walletrestoration::Service::Make(m_services.native, std::move(request), [guard, post = m_services.native.gui_post] { post([guard] { if (guard) guard->publish(); }); },
        m_ticket ? m_ticket->backend_release : nullptr, m_ticket ? m_ticket->observation : nullptr);
    if (!m_owner) { rejected({QStringLiteral("TransportRejected"), tr("The wallet action could not start. Nothing was changed."), {}, {}}); return; }
    if (parent) { setParent(parent); QQmlEngine::setObjectOwnership(this, QQmlEngine::CppOwnership); }
    m_errors.bind(m_owner); m_warnings.bind(m_owner);
}
WalletOperationModel::WalletOperationModel(Services services, walletcreation::Request request)
    : QObject(nullptr), m_services(std::move(services)), m_request(std::move(request))
{
    auto native_request = m_request;
    m_request.passphrase.reset(); m_request.confirmation.reset();
    const QPointer<QObject> projection_parent{m_services.projection_parent};
    const bool needs_parent = projection_parent != nullptr;
    QQmlEngine::setObjectOwnership(this, QQmlEngine::JavaScriptOwnership);
    const auto rejected_projection = [this, projection_parent] {
        if (m_ticket) {
            m_ticket->backend_release->release();
            if (m_services.publisher) m_services.publisher->finishCreation(m_ticket);
            m_ticket.reset();
        }
        // A refused request owns no Native resource or reconciliation right.
        // Keep its small diagnostic handle, not a lease on the live registry.
        m_services.native.open_names.reset(); m_services.publisher.reset();
        m_frame.outcome_known = true; m_frame.result.outcome_known = true;
        m_frame.result.result_id = operationId() + QStringLiteral(":rejected");
        m_frame.target_id.clear();
        if (projection_parent) { setParent(projection_parent); QQmlEngine::setObjectOwnership(this, QQmlEngine::CppOwnership); }
    };
    m_frame.target_id = m_request.submitted_name;
    m_accepted = m_request.transport_error.empty() && m_request.id && m_services.native.admission->request(m_request.id);
    if (!m_accepted) {
        m_frame.phase = walletcreation::Phase::Rejected;
        if (!m_request.transport_error.empty()) {
            m_frame.error = m_request.transport_error;
        } else if (!m_request.id) {
            m_frame.error = {QStringLiteral("IdentityExhausted"), tr("Restart the app before creating another wallet."), {}, {}};
        } else {
            m_frame.error = {QStringLiteral("Busy"), tr("Wait for the current wallet operation to finish."), {}, {}};
        }
        rejected_projection(); return;
    }
    if (m_services.publisher) m_ticket = m_services.publisher->reserveCreation(operationId());
    if (!m_ticket || (needs_parent && !projection_parent)) {
        m_accepted = false; m_services.native.admission->presentationConsumed(m_request.id);
        m_frame.phase = walletcreation::Phase::Rejected;
        m_frame.error = {QStringLiteral("PublicationUnavailable"), tr("Wallet creation could not start. No wallet was created."), {}, {}};
        rejected_projection(); return;
    }
    const QPointer<WalletOperationModel> guard{this};
    m_owner = walletcreation::Service::Make(m_services.native, std::move(native_request),
        [guard, post = m_services.native.gui_post] { post([guard] { if (guard) guard->publish(); }); }, m_ticket->backend_release, m_ticket->observation);
    if (!m_owner) {
        m_accepted = false; m_services.native.admission->presentationConsumed(m_request.id);
        m_frame.phase = walletcreation::Phase::Rejected;
        m_frame.error = {QStringLiteral("TransportRejected"), tr("Wallet creation could not start. No wallet was created."), {}, {}};
    }
    if (!m_owner) { rejected_projection(); return; }
    if (projection_parent) { setParent(projection_parent); QQmlEngine::setObjectOwnership(this, QQmlEngine::CppOwnership); }
    else if (needs_parent) { if (m_owner) m_owner->detach(); m_accepted = false; m_services.native.admission->presentationConsumed(m_request.id); }
    m_errors.bind(m_owner); m_warnings.bind(m_owner);
}
WalletOperationModel::~WalletOperationModel()
{
    if (m_owner) m_owner->detach();
    if (m_ticket && m_services.publisher) m_services.publisher->finishCreation(m_ticket);
}
void WalletOperationModel::start()
{
    if (!m_accepted || !m_owner || m_closed) return;
    const QPointer<WalletOperationModel> guard{this}; const auto owner = m_owner;
    Q_EMIT activated(this);
    if (guard && !m_closed) owner->start(); else owner->detach();
}
void WalletOperationModel::close()
{
    if (std::exchange(m_closed, true)) return;
    if (m_owner) {
        m_frame = m_owner->snapshot();
        m_owner->detach();
        if (walletcreation::Busy(m_frame.phase)) {
            m_frame.phase = walletcreation::Phase::Uncertain; m_frame.outcome_known = false;
            m_frame.error = {QStringLiteral("ProjectionClosed"), tr("The window closed before the wallet action finished. Check the wallet directory before trying again."), {}, {}};
        }
    }
    m_errors.bind({}); m_warnings.bind({}); m_owner.reset();
    m_ticket.reset(); m_services.publisher.reset(); m_services.native.open_names.reset();
}
WalletModel* WalletOperationModel::resultWallet() const { return m_result_wallet; }
QQmlListProperty<WalletModel> WalletOperationModel::resultWallets()
{
    return {this, this, nullptr, [](QQmlListProperty<WalletModel>* value) -> qsizetype { const auto* model = static_cast<WalletOperationModel*>(value->data); return qsizetype(model->m_result_wallets.size()) + (model->m_result_wallet ? 1 : 0); },
        [](QQmlListProperty<WalletModel>* value, qsizetype index) -> WalletModel* { const auto* model = static_cast<WalletOperationModel*>(value->data); if (model->m_result_wallet) { if (!index) return model->m_result_wallet; --index; } return index >= 0 && size_t(index) < model->m_result_wallets.size() ? model->m_result_wallets[size_t(index)].data() : nullptr; }, nullptr};
}
bool WalletOperationModel::canOpenResult() const
{
    return !m_closed && (phase() == Succeeded || phase() == Partial) && m_result_wallet && m_result_wallet->available() && m_services.publisher && m_services.publisher->containsExact(m_result_wallet);
}
bool WalletOperationModel::canReconcile() const
{
    return !m_closed && phase() == Partial && !m_reconciled && m_ticket && m_owner && m_owner->reconcileAvailable()
        && !m_ticket->observation->failed() && !m_services.native.admission->creationPending() && !m_services.native.admission->legacyBusy();
}
QString WalletOperationModel::errorText() const { return m_owner ? m_owner->errorText() : m_frame.error.message; }
QString WalletOperationModel::warningText() const { return m_owner ? m_owner->warningText() : QString{}; }
void WalletOperationModel::publish()
{
    if (m_closed || !m_owner || m_publishing) return;
    const auto frame = m_owner->snapshot();
    if (frame.revision <= m_frame.revision) return;
    const QPointer<WalletOperationModel> guard{this};
    m_publishing = true;
    const auto restore = qScopeGuard([guard] { if (guard) guard->m_publishing = false; });
    m_frame = frame; m_command_pending = false;
    if (frame.publication_requested && frame.phase == walletcreation::Phase::Publishing && !m_publication_in_flight) {
        m_publication_in_flight = true;
        std::vector<walletpublication::Availability> values;
        QString error;
        const auto count = m_owner->publicationCount();
        for (size_t index = 0; index < count && index < 3; ++index) {
            auto published = m_services.publisher ? m_services.publisher->publishWithLifetime(m_ticket, m_owner->publicationWallet(index), m_owner->publicationName(index), m_owner->publicationObservation(index), m_owner->publicationRelease(index)) : walletpublication::Result{};
            if (!guard) return;
            if (m_owner->publicationPrimary(index)) {
                if (m_result_wallet && published.wallet && m_result_wallet != published.wallet) published = {{}, walletpublication::Availability::Unavailable, tr("Finish with this wallet result before opening another wallet.")};
                else if (!m_result_wallet && published.wallet) {
                    m_result_wallet = published.wallet;
                    m_available = connect(published.wallet, &WalletModel::availableChanged, this, &WalletOperationModel::changed);
                    m_destroyed = connect(published.wallet, &QObject::destroyed, this, &WalletOperationModel::changed);
                }
            } else if (published.wallet) {
                if (std::find(m_result_wallets.begin(), m_result_wallets.end(), published.wallet) == m_result_wallets.end()) {
                    m_result_wallets.push_back(published.wallet);
                    connect(published.wallet, &WalletModel::availableChanged, this, &WalletOperationModel::changed);
                    connect(published.wallet, &QObject::destroyed, this, &WalletOperationModel::changed);
                }
            }
            values.push_back(published.availability);
            if (!published.error.isEmpty()) { if (!error.isEmpty()) error += QLatin1Char('\n'); error += published.error; }
        }
        m_owner->publicationsConsumed(std::move(values), std::move(error));
    }
    if (m_frame.rejected_command != m_last_rejected && !m_frame.rejected_command.isEmpty()) {
        m_last_rejected = m_frame.rejected_command; Q_EMIT commandRejected(m_last_rejected, m_frame.command_error); if (!guard) return;
    }
    if (phase() == AwaitingConfirmation || phase() == AwaitingPassphrase) {
        m_errors.refresh(); if (!guard) return;
        if (m_ticket && m_services.publisher) m_services.publisher->finishCreation(m_ticket);
        if (!guard) return;
        m_ticket.reset(); m_publication_in_flight = false;
        m_services.native.admission->presentationConsumed(m_request.id);
    }
    if ((phase() == AwaitingConfirmation || phase() == AwaitingPassphrase) && (!m_prompt || m_prompt->promptId() != m_frame.prompt_id)) {
        m_prompt = new WalletOperationPrompt(this, m_frame.prompt_id, phase() == AwaitingPassphrase ? WalletOperationPrompt::Passphrase : WalletOperationPrompt::Confirmation);
        if (phase() == AwaitingPassphrase) m_passphrase_prompt = m_prompt; else m_confirmation_prompt = m_prompt;
        m_prompts.push_back(m_prompt); Q_EMIT promptsChanged(); if (!guard) return;
    }
    if (m_prompt) m_prompt->refresh();
    if (!guard) return;
    if (terminal()) {
        m_errors.refresh(); if (!guard) return;
        m_warnings.refresh(); if (!guard) return;
        if (!m_finished) { m_finished = true; Q_EMIT finished(operationId(), m_frame.result.result_id); if (!guard) return; }
        if (m_ticket && m_services.publisher) m_services.publisher->finishCreation(m_ticket);
        if (!guard) return;
        m_services.native.admission->presentationConsumed(m_request.id);
        m_publishing = false; // Idle may admit a successor after terminal delivery.
    }
    Q_EMIT changed();
}
WalletCreationReceipt WalletOperationModel::reject(const char* command, const QString& message)
{
    WalletCreationReceipt receipt{operationId() + QLatin1Char(':') + QString::fromLatin1(command), false,
        {QStringLiteral("UnsupportedTransition"), message, {}, {}}};
    Q_EMIT commandRejected(receipt.command_id, receipt.error); return receipt;
}
WalletCreationReceipt WalletOperationModel::cancel()
{
    if (m_closed || !canCancel()) return reject("cancel", tr("This wallet change has already started and cannot be cancelled."));
    m_owner->cancel(); return {operationId() + QStringLiteral(":cancel"), true, {}};
}
WalletCreationReceipt WalletOperationModel::retry()
{
    return reject("retry", tr("Edit the creation form and submit a new operation. A created wallet must not be created again."));
}
WalletCreationReceipt WalletOperationModel::reconcile()
{
    if (!canReconcile() || !m_services.native.admission->request(m_request.id)) return reject("reconcile", tr("This result cannot be checked now."));
    const QPointer<WalletOperationModel> guard{this};
    const auto owner = m_owner; const auto services = m_services; const auto id = m_request.id; const QString command = operationId() + QStringLiteral(":reconcile");
    const auto previous_ticket = m_ticket; const bool new_ticket = owner->reconcileNeedsTicket();
    if (new_ticket) {
        auto ticket = services.publisher ? services.publisher->reserveCreation(QString::number(id)) : nullptr;
        if (!guard || m_closed) {
            services.native.admission->presentationConsumed(id);
            if (ticket) { ticket->backend_release->release(); services.publisher->finishCreation(ticket); }
            return {command, false, {QStringLiteral("Closed"), tr("The operation view closed."), {}, {}}};
        }
        if (!ticket) { services.native.admission->presentationConsumed(id); return reject("reconcile", tr("The wallet could not be shown. Try again.")); }
        m_ticket = std::move(ticket);
    }
    const auto ticket = m_ticket;
    const bool previous_finished = m_finished;
    m_command_pending = true; m_finished = false; m_publication_in_flight = false;
    Q_EMIT activated(this);
    if (!guard || m_closed) {
        services.native.admission->presentationConsumed(id);
        if (new_ticket && ticket) { ticket->backend_release->release(); services.publisher->finishCreation(ticket); }
        return {command, false, {QStringLiteral("Closed"), tr("The operation view closed."), {}, {}}};
    }
    const auto admitted = services.native.post(execution::Lane::Utility, [owner, ticket] { owner->reconcileWithPorts(ticket->backend_release, ticket->observation); });
    if (!admitted) {
        m_command_pending = false; m_finished = previous_finished; m_ticket = previous_ticket;
        services.native.admission->presentationConsumed(id);
        if (new_ticket && ticket) { ticket->backend_release->release(); services.publisher->finishCreation(ticket); }
        if (guard && !m_closed) Q_EMIT changed();
        if (guard && !m_closed) return reject("reconcile", tr("The status check could not start. Try again."));
        return {command, false, {QStringLiteral("TransportRejected"), tr("The status check could not start. Try again."), {}, {}}};
    }
    m_reconciled = true; m_acknowledged = false;
    return {command, true, {}};
}
WalletCreationReceipt WalletOperationModel::acknowledge(const QString& result_id)
{
    if (!canAcknowledge() || result_id != m_frame.result.result_id) return reject("acknowledge", tr("This answer does not belong to the current result."));
    m_acknowledged = true; Q_EMIT changed(); return {operationId() + QStringLiteral(":acknowledge"), true, {}};
}

WalletCreationReceipt WalletOperationModel::answerPrompt(const QString& prompt_id, const QString& secret, bool passphrase)
{
    if (m_closed || !m_owner) return reject("answer", tr("This wallet operation is closed."));
    const QPointer<WalletOperationModel> guard{this};
    const auto owner = m_owner; const auto services = m_services; const auto id = m_request.id;
    const auto frame = owner->snapshot();
    if (frame.prompt_id != prompt_id || (frame.phase != walletcreation::Phase::AwaitingConfirmation && frame.phase != walletcreation::Phase::AwaitingPassphrase)) return reject("answer", tr("This answer is for a question that is no longer open."));
    if (passphrase != (frame.phase == walletcreation::Phase::AwaitingPassphrase) || (passphrase && secret.isEmpty())) return reject("answer", tr("Use the response requested by this wallet prompt."));
    if (m_command_revision == std::numeric_limits<quint64>::max()) return reject("answer", tr("Restart the app to keep using this wallet."));
    const QString command = operationId() + QStringLiteral(":answer:") + QString::number(++m_command_revision);
    if (!services.native.admission->request(id)) return reject("answer", tr("Wait for the current wallet operation to finish."));
    const auto closed = [&] { return WalletCreationReceipt{command, false, {QStringLiteral("Closed"), tr("The window closed before the answer was sent."), {}, {}}}; };
    secretinput::Captured captured;
    if (passphrase) {
        auto input = services.native.capture(secret);
        if (!guard || m_closed) { services.native.admission->presentationConsumed(id); return closed(); }
        if (!input) { services.native.admission->presentationConsumed(id); return reject("answer", tr("The password could not be used. Nothing was changed.")); }
        captured = std::move(input.input);
    }
    auto ticket = services.publisher ? services.publisher->reserveCreation(QString::number(id)) : nullptr;
    if (!guard || m_closed) {
        services.native.admission->presentationConsumed(id);
        if (ticket) { ticket->backend_release->release(); services.publisher->finishCreation(ticket); }
        return closed();
    }
    if (!ticket) { services.native.admission->presentationConsumed(id); return reject("answer", tr("The wallet action could not start. Nothing was changed.")); }
    m_ticket = ticket; m_publication_in_flight = false; m_command_pending = true;
    Q_EMIT activated(this);
    if (!guard || m_closed) { ticket->backend_release->release(); services.native.admission->presentationConsumed(id); services.publisher->finishCreation(ticket); return closed(); }
    const auto admitted = services.native.post(execution::Lane::Utility, [owner, command, prompt_id, ticket, captured = std::move(captured)]() mutable {
        owner->answer(command, prompt_id, ticket->backend_release, ticket->observation, std::move(captured));
    });
    if (!admitted) {
        services.native.admission->presentationConsumed(id); ticket->backend_release->release();
        if (guard && !m_closed) { m_ticket.reset(); m_command_pending = false; }
        services.publisher->finishCreation(ticket);
        if (guard && !m_closed) Q_EMIT changed();
        return guard && !m_closed ? reject("answer", tr("Your answer could not be sent to the wallet. The question is still open.")) : WalletCreationReceipt{command, false, {QStringLiteral("TransportRejected"), tr("Your answer could not be sent to the wallet."), {}, {}}};
    }
    return {command, true, {}};
}
WalletOperationPrompt::WalletOperationPrompt(WalletOperationModel* operation, QString id, Kind kind) : QObject(operation), m_operation(operation), m_id(std::move(id)), m_kind(kind)
{
    connect(operation, &WalletOperationModel::commandRejected, this, [this](const QString&, const WalletOperationError& error) { m_submission_error = error; m_submitting = false; Q_EMIT changed(); });
}
QString WalletOperationPrompt::promptId() const { return m_id; }
QString WalletOperationPrompt::operationId() const { return m_operation->operationId(); }
WalletOperationPrompt::Kind WalletOperationPrompt::kind() const { return m_kind; }
QString WalletOperationPrompt::title() const { return kind() == Passphrase ? tr("Enter wallet password") : tr("Wallet update required"); }
QString WalletOperationPrompt::message() const
{
    const auto name = m_operation->targetWalletId().isEmpty() ? tr("Default wallet") : m_operation->targetWalletId().left(1024);
    auto message = kind() == Passphrase ? tr("Enter the password for %1. It is used only for this action.").arg(name) : tr("Update the legacy wallet %1 to the current format. Core will preserve a recovery backup.").arg(name);
    if (m_operation->result().effect != WalletOperationResult::NoEffect) message += QLatin1Char('\n') + tr("Wallet files already exist. Cancelling stops further work and keeps those files and their recovery backups.");
    return message;
}
bool WalletOperationPrompt::busy() const { return m_submitting || m_operation->busy(); }
WalletOperationError WalletOperationPrompt::error() const { return m_submission_error.empty() ? m_operation->error() : m_submission_error; }
const QObject* WalletOperationModel::presentationPrompt() const
{
    return presentationHasPrompt() && m_prompt && m_prompt->promptId() == m_frame.prompt_id ? m_prompt : nullptr;
}
WalletCreationReceipt WalletOperationPrompt::answer(const QString& prompt, Response response)
{
    if (prompt != m_id) return m_operation->reject("answer", tr("This answer does not belong to this immutable prompt."));
    if (response == Decline) return dismiss(prompt);
    const QPointer<WalletOperationPrompt> guard{this}; const auto receipt = m_operation->answerPrompt(prompt, {}, false);
    if (guard && receipt.accepted) { m_submission_error = {}; m_submitting = true; Q_EMIT changed(); } return receipt;
}
WalletCreationReceipt WalletOperationPrompt::submitSecret(const QString& prompt, const QString& secret)
{
    if (prompt != m_id) return m_operation->reject("answer", tr("This answer does not belong to this immutable prompt."));
    const QPointer<WalletOperationPrompt> guard{this}; const auto receipt = m_operation->answerPrompt(prompt, secret, true);
    if (guard && receipt.accepted) { m_submission_error = {}; m_submitting = true; Q_EMIT changed(); } return receipt;
}
WalletCreationReceipt WalletOperationPrompt::dismiss(const QString& prompt)
{
    if (prompt != promptId() || m_operation->m_frame.prompt_id != m_id) return m_operation->reject("dismiss", tr("This dismissal does not belong to the active wallet prompt."));
    return m_operation->cancel();
}
void WalletOperationPrompt::refresh()
{
    m_submitting = false;

    Q_EMIT changed();
}

QQmlListProperty<WalletOperationPrompt> WalletOperationModel::prompts()
{
    return {this, this, [](QQmlListProperty<WalletOperationPrompt>* list) -> qsizetype { return qsizetype(static_cast<WalletOperationModel*>(list->data)->m_prompts.size()); },
        [](QQmlListProperty<WalletOperationPrompt>* list, qsizetype index) -> WalletOperationPrompt* { const auto& values = static_cast<WalletOperationModel*>(list->data)->m_prompts; return index >= 0 && size_t(index) < values.size() ? values[size_t(index)] : nullptr; }};
}
