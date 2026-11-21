// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/models/paymentimportoperation.h>
#include <qml/models/walletqmlmodel.h>
#include <QPointer>
PaymentImportOperation::~PaymentImportOperation() { cancel(); }
PaymentImportOperation::Outcome PaymentImportOperation::outcome() const
{
    if (!m_snapshot.result) return Outcome::NoOutcome;
    switch (m_snapshot.result->outcome) {
    case paymentimport::Outcome::Applied: return Outcome::AppliedOutcome;
    case paymentimport::Outcome::ReviewOpened: return Outcome::ReviewOpenedOutcome;
    case paymentimport::Outcome::Rejected: return Outcome::RejectedOutcome;
    case paymentimport::Outcome::Stale: return Outcome::StaleOutcome;
    case paymentimport::Outcome::Unknown: return Outcome::UnknownOutcome;
    }
    return Outcome::NoOutcome;
}
void PaymentImportOperation::project(paymentimport::Snapshot snapshot)
{
    if (snapshot.owner != m_identity || snapshot.sequence <= m_snapshot.sequence) return;
    if (m_closed || snapshot.phase == paymentimport::Phase::Closed) {
        snapshot.inspection.reset(); snapshot.child_result.reset();
    }
    if (snapshot.phase == paymentimport::Phase::Closed) m_owner.reset();
    m_snapshot = std::move(snapshot);
    Q_EMIT changed();
}
void PaymentImportOperation::chooseTarget(WalletQmlModel* wallet, SendRecipient* recipient)
{
    if (m_closed || !m_owner) return;
    const QPointer<PaymentImportOperation> self{this};
    const auto actor = m_owner;
    const auto target = wallet ? wallet->importTarget(recipient) : nullptr;
    if (self && !self->m_closed && self->m_owner == actor) self->command([target](auto& original) { return original.chooseTarget(target); });
}
void PaymentImportOperation::command(const std::function<execution::Admission(paymentimport::Owner&)>& submit)
{
    if (m_closed || !m_owner) return;
    const QPointer<PaymentImportOperation> self{this}; const auto owner = m_owner;
    const auto admitted = submit(*owner);
    if (!self || self->m_closed || self->m_owner != owner) return;
    const auto error = admitted ? QString{} : tr("The import command could not be queued. Try again.");
    if (m_transport_error != error) { m_transport_error = error; Q_EMIT changed(); }
}
void PaymentImportOperation::retry() { command([](auto& owner) { return owner.retry(); }); }
void PaymentImportOperation::use() { command([](auto& owner) { return owner.use(); }); }
void PaymentImportOperation::apply() { command([](auto& owner) { return owner.apply(); }); }
void PaymentImportOperation::openReview() { command([](auto& owner) { return owner.openReview(); }); }
void PaymentImportOperation::replace(quint64 prompt) { command([prompt](auto& owner) { return owner.replace(prompt); }); }
void PaymentImportOperation::keepDraft(quint64 prompt) { command([prompt](auto& owner) { return owner.keepDraft(prompt); }); }
void PaymentImportOperation::reconcile() { command([](auto& owner) { return owner.reconcile(); }); }
bool PaymentImportOperation::requestFile(const QUrl& file)
{
    if (m_closed || !m_owner || m_input_started) return false;
    const QPointer<PaymentImportOperation> self{this}; const auto owner = m_owner;
    m_input_started = true;
    paymentimport::Input input; input.kind = paymentimport::Input::Kind::File; input.url = file;
    const auto admitted = owner->inspect(std::move(input));
    if (!self || self->m_closed || self->m_owner != owner) return false;
    if (!admitted) {
        self->m_input_started = false; self->m_transport_error = tr("The import source could not be queued. Try again.");
    } else {
        self->m_snapshot.phase = paymentimport::Phase::Reading; self->m_snapshot.pending = true;
        self->m_transport_error.clear();
    }
    Q_EMIT self->changed();
    return bool(admitted) && self && !self->m_closed && self->m_owner == owner;
}
void PaymentImportOperation::close()
{
    if (std::exchange(m_closed, true)) return;
    if (const auto owner = std::exchange(m_owner, {})) owner->close();
    m_snapshot.inspection.reset(); m_snapshot.child_result.reset();
}
void PaymentImportOperation::cancel()
{
    if (m_closed) return;
    close();
    Q_EMIT changed();
}

QString PaymentImportOperation::exportToken() const
{
    return canExport() && m_snapshot.inspection && m_snapshot.inspection->psbt && m_snapshot.inspection->psbt->document
        ? QString::number(m_identity) + QLatin1Char(':') + QString::number(m_snapshot.source) : QString{};
}
bool PaymentImportOperation::requestSaveOriginal(const QString& token, const QString& path)
{
    if (token.isEmpty() || token != exportToken() || (m_export && m_export->pending())) return false;
    const auto document = std::shared_ptr<const PartiallySignedTransaction>{m_snapshot.inspection, m_snapshot.inspection->psbt->document.get()};
    const QPointer<PaymentImportOperation> self{this};
    auto* next = new FileExportOperationModel(path, document, m_export_services);
    if (!self || self->m_closed || token != self->exportToken()) { next->deleteLater(); return false; }
    next->setParent(this);
    if (m_export) m_export->deleteLater();
    m_export = next;
    const auto admitted = next->start();
    if (self && self->m_export == next) Q_EMIT self->exportChanged();
    return admitted;
}
