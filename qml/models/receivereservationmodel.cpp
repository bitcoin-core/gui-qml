// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/models/receivereservationmodel.h>
#include <qml/guipresentation.h>

ReceiveAuthorizationPrompt::ReceiveAuthorizationPrompt(ReceiveReservationModel* owner, QString token, QObject* parent)
    : QObject(parent), m_owner(owner), m_token(std::move(token))
{
    connect(owner, &ReceiveReservationModel::changed, this, &ReceiveAuthorizationPrompt::changed);
}
bool ReceiveAuthorizationPrompt::active() const { return m_active && m_owner && m_owner->ownsPrompt(m_token); }
QString ReceiveAuthorizationPrompt::errorText() const { return active() && m_owner ? m_owner->error().message : QString{}; }
void ReceiveAuthorizationPrompt::answer(const QString& passphrase) { if (active()) m_owner->authorize(m_token, passphrase); }
void ReceiveAuthorizationPrompt::cancel() { if (active()) m_owner->cancel(); }
void ReceiveAuthorizationPrompt::invalidate() { if (std::exchange(m_active, false)) Q_EMIT changed(); }

ReceiveReservationModel::ReceiveReservationModel(uint64_t view, Ports ports, QObject* parent)
    : QObject(parent), m_view(view), m_ports(std::move(ports))
{
    m_ports.post = presentation::ReliablePost(std::move(m_ports.post));
}
bool ReceiveReservationModel::refuse(const QString& command, TextEditError error)
{
    m_error = error; const QPointer<ReceiveReservationModel> guard{this};
    Q_EMIT changed(); if (guard) Q_EMIT commandRejected(command, error); return false;
}
void ReceiveReservationModel::revokePrompt()
{
    if (!m_prompt) return;
    const QPointer<ReceiveAuthorizationPrompt> old{std::exchange(m_prompt, nullptr)};
    old->invalidate(); if (old) old->deleteLater();
}
bool ReceiveReservationModel::start(walletrequests::Command command, bool cancellation, std::optional<QString> secret)
{
    if (m_phase == Closed || m_sequence == UINT64_MAX || m_revision == UINT64_MAX || !m_ports.submit)
        return refuse(QStringLiteral("address"), {TextEditError::Closed, tr("This receive view is closed.")});
    const auto sequence = ++m_sequence;
    command.view = m_view; command.view_revision = ++m_revision;
    struct Binding { std::shared_ptr<const walletrequests::AddressIntent> intent; };
    const auto binding = std::make_shared<Binding>();
    const QPointer<ReceiveReservationModel> observer{this};
    const auto complete = [observer, post = m_ports.post, binding, sequence, cancellation](const walletrequests::Result& result) {
        // Domain owns the result before bounded GUI projection. No GUI callback
        // is needed to finish the command or release an opening Action.
        // Large legacy records stay in the native operation cell and retire on
        // Domain. A reservation projection receives bounded value facts only.
        auto facts = result; facts.record.reset(); facts.current_label.reset();
        if (facts.address && (facts.address->address.size() > 128 || facts.address->address_type.size() > 16 || facts.address->label.size() > 16'384)) {
            facts.address.reset(); facts.error = walletrequests::Error::Invalid;
        }
        if (facts.paid_address.size() > 128) { facts.paid_address.clear(); facts.error = walletrequests::Error::Invalid; }
        post([observer, binding, sequence, result = std::move(facts), cancellation] {
            if (observer) observer->complete(sequence, binding->intent, result, cancellation);
        });
    };
    const auto admission = secret ? (m_ports.authorize ? m_ports.authorize(std::move(command), *secret, complete)
            : Admission{{}, {}, execution::Rejection::Invalid}) : m_ports.submit(std::move(command), complete);
    if (!observer) return false;
    if (!admission) {
        m_phase = Failed;
        return refuse(QStringLiteral("address"), {TextEditError::Refused, tr("The address action could not start. Try again.")});
    }
    m_operation_owner = admission.owner; binding->intent = admission.intent;
    if (!cancellation) m_intent = binding->intent;
    m_phase = cancellation ? Cancelling : Generating; m_error = {};
    Q_EMIT changed(); return true;
}
bool ReceiveReservationModel::ensure(bool next, const QString& type, bool explicit_type, bool remember_type)
{
    if (type.size() > 16) return refuse(QStringLiteral("address"), {TextEditError::Invalid, tr("This receiving address type is invalid.")});
    if (busy()) return refuse(QStringLiteral("address"), {TextEditError::Refused, tr("The address action is still in progress.")});
    const QPointer<ReceiveReservationModel> guard{this};
    revokePrompt(); if (!guard) return false;
    walletrequests::RevokeAddressIntent(m_intent);
    m_next = next; m_explicit_type = explicit_type; m_remember_type = remember_type; m_requested_type = type;
    walletrequests::Command command; command.kind = walletrequests::Kind::ReserveAddress;
    command.draft.address = m_address; command.draft.address_type = type;
    command.next = next; command.explicit_type = explicit_type;
    command.remember_type = remember_type;
    // The previous display may remain visible, but it is never armed while a
    // new type is being generated or after that generation fails.
    return start(std::move(command));
}
bool ReceiveReservationModel::select(const QString& address)
{
    if (address.isEmpty() || address.size() > 128) return refuse(QStringLiteral("selectAddress"), {TextEditError::Invalid, tr("This receiving address is invalid.")});
    if (busy()) return refuse(QStringLiteral("selectAddress"), {TextEditError::Refused, tr("The address action is still in progress.")});
    const QPointer<ReceiveReservationModel> guard{this}; revokePrompt(); if (!guard) return false;
    walletrequests::RevokeAddressIntent(m_intent);
    walletrequests::Command command; command.kind = walletrequests::Kind::SelectAddress; command.draft.address = address;
    return start(std::move(command));
}
void ReceiveReservationModel::complete(uint64_t sequence, const std::shared_ptr<const walletrequests::AddressIntent>& intent,
    walletrequests::Result result, bool cancellation)
{
    if (!cancellation) { m_last = result; }
    if (m_phase == Closed || sequence != m_sequence) { if (!cancellation) Q_EMIT changed(); return; }
    m_operation_owner.reset();
    const QPointer<ReceiveReservationModel> guard{this};
    if (cancellation) {
        m_intent.reset(); m_phase = result.error == walletrequests::Error::None ? Cancelled : Failed;
        m_error = result.error == walletrequests::Error::None ? TextEditError{} :
            TextEditError{TextEditError::Persistence, walletrequests::ErrorText(result.error)};
        Q_EMIT changed(); return;
    }
    m_intent = intent;
    if (result.error == walletrequests::Error::NeedsUnlock || result.error == walletrequests::Error::LockedAttemptFailed || result.error == walletrequests::Error::IncorrectPassphrase) {
        m_phase = Authorizing;
        m_error = result.error != walletrequests::Error::NeedsUnlock
            ? TextEditError{TextEditError::Persistence, walletrequests::ErrorText(result.error)} : TextEditError{};
        const auto token = reservationId();
        if (!m_prompt || m_prompt->token() != token) {
            revokePrompt(); if (!guard) return;
            m_prompt = new ReceiveAuthorizationPrompt(this, token, this);
        }
        Q_EMIT m_prompt->changed(); if (guard) Q_EMIT changed(); return;
    }
    revokePrompt(); if (!guard) return;
    m_used = !result.paid_address.isEmpty();
    if (result.error == walletrequests::Error::None && result.request_loaded) {
        m_phase = Idle; m_error = {};
        const auto request_id = selectedRequestId(); Q_EMIT requestSelected(request_id); if (!guard) return;
    } else if ((result.error == walletrequests::Error::None || result.error == walletrequests::Error::PreferencePersistence) && result.address && result.address->persisted && !m_used) {
        m_address = result.address->address; m_type = result.address->address_type;
        m_phase = Ready; m_error = result.error == walletrequests::Error::None ? TextEditError{} :
            TextEditError{TextEditError::Persistence, walletrequests::ErrorText(result.error)};
    } else {
        m_phase = Failed;
        m_error = {result.error == walletrequests::Error::Changed ? TextEditError::Changed : TextEditError::Persistence,
            walletrequests::ErrorText(result.error)};
    }
    Q_EMIT changed();
}
bool ReceiveReservationModel::ownsPrompt(const QString& token) const
{
    return m_phase != Closed && m_phase != Cancelled && m_phase != Cancelling && m_intent && reservationId() == token &&
        (m_phase == Authorizing || m_phase == Generating);
}
void ReceiveReservationModel::authorize(const QString& token, const QString& passphrase)
{
    if (!ownsPrompt(token) || busy()) {
        refuse(QStringLiteral("authorize"), {TextEditError::Changed, tr("This password question no longer owns an address operation.")}); return;
    }
    walletrequests::Command command; command.kind = walletrequests::Kind::ResumeAddress; command.resume = m_intent;
    start(std::move(command), false, passphrase);
}
void ReceiveReservationModel::retry()
{
    if (busy()) { refuse(QStringLiteral("retry"), {TextEditError::Refused, tr("The address action is still in progress.")}); return; }
    if (m_phase == Closed) return;
    if ((m_last.error == walletrequests::Error::ReservationPersistence || m_last.error == walletrequests::Error::PreferencePersistence) && m_intent) {
        walletrequests::Command command; command.kind = walletrequests::Kind::ResumeAddress; command.resume = m_intent;
        start(std::move(command));
    } else ensure(m_next, m_requested_type, m_explicit_type, m_remember_type);
}
void ReceiveReservationModel::cancel()
{
    if (m_phase == Closed || m_phase == Cancelling || m_phase == Cancelled) return;
    const QPointer<ReceiveReservationModel> guard{this}; revokePrompt(); if (!guard) return;
    walletrequests::RevokeAddressIntent(m_intent);
    m_error = {}; m_next = m_explicit_type = m_remember_type = false; m_requested_type.clear();
    walletrequests::Command command; command.kind = walletrequests::Kind::ForgetAddress;
    start(std::move(command), true);
}
void ReceiveReservationModel::observe(bool used, bool source_current)
{
    if (m_phase == Closed) return;
    const bool changed = m_used != (m_used || used) || m_source_current != source_current;
    m_source_current = source_current;
    m_used = m_used || used;
    if (m_used) {
        walletrequests::RevokeAddressIntent(m_intent);
        if (m_phase == Ready || m_phase == Authorizing) m_phase = Idle;
        const QPointer<ReceiveReservationModel> guard{this}; revokePrompt(); if (!guard) return;
    }
    if (changed) Q_EMIT this->changed();
}
void ReceiveReservationModel::retire()
{
    if (m_phase == Closed) return;
    m_phase = Closed; walletrequests::RevokeAddressIntent(m_intent); m_operation_owner.reset();
    const QPointer<ReceiveReservationModel> guard{this}; revokePrompt();
    if (guard) Q_EMIT changed();
}
