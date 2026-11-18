// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/models/walletcreationmodel.h>
#include <QUuid>
#include <utility>
void WalletCreationText::edit(const QString& text)
{
    if (text.size() > m_maximum) { m_transport_rejected = true; Q_EMIT changed(); return; }
    const bool was_rejected = std::exchange(m_transport_rejected, false);
    if (m_text == text && !was_rejected) return;
    m_text = text;
    const QPointer<WalletCreationText> guard{this};
    Q_EMIT edited(); if (guard) Q_EMIT changed();
}
void WalletCreationText::setError(QString error) { if (m_error == error) return; m_error = std::move(error); Q_EMIT changed(); }
WalletCreationModel::WalletCreationModel(QObject* parent)
    : QObject(parent), m_editor_id(QUuid::createUuid().toString(QUuid::WithoutBraces))
{
    connect(&m_name, &WalletCreationText::edited, this, &WalletCreationModel::edited);
    connect(&m_key, &WalletCreationText::edited, this, &WalletCreationModel::edited);
}
WalletCreationModel::WalletCreationModel(Services services, QObject* parent)
    : WalletCreationModel(parent)
{
    setServices(std::move(services));
}
void WalletCreationModel::setServices(Services services)
{
    if (m_check || m_closed || !services.operation.native.node) return;
    m_services = std::move(services);
    m_check = std::make_unique<WalletProvisioningCheck>(*m_services.operation.native.node, m_services.operation.native.gui_post, this,
        m_services.operation.native.initialized, m_services.operation.native.open_names, m_services.operation.native.shutdown, true,
        WalletProvisioningCheck::Queues{m_services.operation.native.core, m_services.operation.native.outcomes});
    connect(m_check.get(), &WalletProvisioningCheck::changed, this, &WalletCreationModel::validationChanged);
    const QPointer<WalletCreationModel> guard{this};
    edited(); if (guard) Q_EMIT changed();
}
WalletCreationModel::~WalletCreationModel() { discard(); }
WalletCreationModel::FactoryPhase WalletCreationModel::factoryPhase() const
{
    if (m_closed) return FactoryCancelled;
    const auto& native = m_services.operation.native;
    if (!native.node || !native.value_cleanup || !native.core || !native.outcomes) return FactoryRejected;
    return m_factory_phase;
}
WalletOperationError WalletCreationModel::factoryError() const
{
    return factoryPhase() == FactoryRejected ? WalletOperationError{QStringLiteral("Unavailable"), tr("Wallet creation is unavailable."), {}, {}} : WalletOperationError{};
}
QString WalletCreationModel::selectedSignerId() const
{
    if (!m_signer.isEmpty() || m_kind != ExternalSigner || !m_services.signer) return m_signer;
    const auto snapshot = m_services.signer();
    return snapshot.id.isEmpty() ? snapshot.name : snapshot.id;
}
bool WalletCreationModel::canCreate() const
{
    const auto admission = m_services.operation.native.admission;
    if (factoryPhase() != FactoryReady || !admission || admission->creationPending() || admission->legacyBusy() || !m_check || !m_check->ready() || !m_check->nameError().isEmpty() || !m_name.error().isEmpty()) return false;
    if (m_kind == WatchOnly && (!m_check->xpubValid() || !m_key.error().isEmpty())) return false;
    if (m_kind == ExternalSigner) {
        const auto signer = m_services.signer ? m_services.signer() : walletcreation::SignerSnapshot{};
        if (signer.pending || !signer.configured || signer.count != 1 || !signer.error.isEmpty() || (!m_signer.isEmpty() && m_signer != (signer.id.isEmpty() ? signer.name : signer.id))) return false;
    }
    return true;
}
void WalletCreationModel::edited()
{
    if (m_closed) return;
    ++m_revision; m_command_error = {};
    if (m_check) { m_check->setName(m_name.text()); m_check->setXpub(m_key.text()); }
    Q_EMIT changed();
}
void WalletCreationModel::validationChanged()
{
    if (m_closed || !m_check) return;
    if (m_factory_phase == FactoryRequested && !m_check->pending() && m_services.operation.native.initialized && m_services.operation.native.initialized->load()) m_factory_phase = m_check->error().isEmpty() ? FactoryReady : FactoryRejected;
    const QPointer<WalletCreationModel> guard{this};
    QString name_error;
    if (!m_check->pending()) name_error = m_check->error().isEmpty() ? m_check->nameError() : m_check->error();
    m_name.setError(std::move(name_error));
    if (!guard) return;
    m_key.setError(m_check->ready() && !m_key.text().isEmpty() && !m_check->xpubValid() ? tr("Invalid extended public key.") : QString{});
    if (guard) Q_EMIT changed();
}
void WalletCreationModel::chooseKind(Kind kind) { if (m_closed || kind < Software || kind > ExternalSigner || m_kind == kind) return; m_kind = kind; ++m_revision; Q_EMIT changed(); }
void WalletCreationModel::chooseSigner(const QString& id) { if (m_closed || m_signer == id) return; m_signer = id; ++m_revision; Q_EMIT changed(); }
void WalletCreationModel::setEncryptRequested(bool encrypt) { if (m_closed || m_encrypt == encrypt) return; m_encrypt = encrypt; ++m_revision; Q_EMIT changed(); }
WalletOperationModel* WalletCreationModel::create(const QString& passphrase, const QString& confirmation)
{
    walletcreation::Request request;
    if (m_closed) request.transport_error = {QStringLiteral("FactoryClosed"), tr("This creation editor has closed."), {}, {}};
    request.editor_revision = m_revision; request.kind = static_cast<walletcreation::Kind>(m_kind);
    request.submitted_name = m_name.text(); request.extended_public_key = m_key.text(); request.signer_id = selectedSignerId();
    request.encrypt = m_encrypt;
    request.signer = m_services.signer ? m_services.signer() : walletcreation::SignerSnapshot{};
    if (m_name.transportRejected() || m_key.transportRejected() || request.submitted_name.size() > 32768 || request.extended_public_key.size() > 4096 || request.signer_id.size() > 4096) request.transport_error = {QStringLiteral("TransportBudget"), tr("This wallet input is too long. No wallet was created."), {}, {}};
    if (m_kind == Software && m_encrypt && request.transport_error.empty()) {
        auto captured_passphrase = m_services.operation.native.capture(passphrase);
        auto captured_confirmation = m_services.operation.native.capture(confirmation);
        if (!captured_passphrase || !captured_confirmation) {
            request.transport_error = {QStringLiteral("InputUnavailable"), tr("The password could not be used. No wallet was created."), QStringLiteral("credentials"), {}};
        } else {
            request.passphrase = std::move(captured_passphrase.input);
            request.confirmation = std::move(captured_confirmation.input);
        }
    }
    auto* operation = WalletOperationModel::Make(m_services.operation, std::move(request));
    if (!operation->acceptedForDelivery()) {
        m_command_error = operation->error();
        const QPointer<WalletCreationModel> guard{this};
        Q_EMIT commandRejected(operation->operationId(), m_command_error); if (guard) Q_EMIT changed();
        return operation;
    }
    m_command_error = {};
    m_operation = operation;
    const QPointer<WalletCreationModel> guard{this}; const QPointer<WalletOperationModel> original{operation};
    Q_EMIT operationCreated(operation);
    if (original) operation->start();
    if (guard) Q_EMIT changed();
    return original;
}
void WalletCreationModel::refreshCapabilities() { if (!m_closed) validationChanged(); }
void WalletCreationModel::discard() { if (m_closed) return; m_closed = true; if (m_check) m_check->close(); m_services.operation.native.open_names.reset(); m_services.operation.publisher.reset(); Q_EMIT changed(); }

