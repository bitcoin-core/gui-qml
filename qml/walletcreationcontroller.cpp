// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/walletcreationcontroller.h>

#include <qml/models/walletqmlmodel.h>
#include <qml/secretinput.h>
#include <qml/walletregistry.h>

#include <QQmlEngine>
#include <QScopeGuard>

WalletCreationController::WalletCreationController(Host host, QObject* parent)
    : QObject(parent), m_host(std::move(host))
{
}

bool WalletCreationController::busy() const
{
    return m_host.admission && m_host.admission->creationPending();
}

bool WalletCreationController::canCreateExternalSignerWallet() const
{
    return !m_closed && m_signers && !m_signers->pending() && m_signers->fresh() && !m_signers->uniqueSignerId().isEmpty() &&
        m_signers->error().code == SignerDiscoveryError::None;
}

WalletCreationModel::Services WalletCreationController::creationServices()
{
    WalletCreationModel::Services services;
    services.operation.native = {m_host.node, {m_host.scheduler, m_host.post}, m_host.admission, m_host.registry->nameRegistry(), m_host.names_ready, m_host.shutdown_gate};
    services.operation.publisher = m_host.registry; services.operation.projection_parent = this;
    const QPointer<WalletCreationController> controller{this};
    services.signer = [controller] {
        if (!controller || !controller->m_signers) return walletcreation::SignerSnapshot{};
        const auto* signers = controller->m_signers;
        return walletcreation::SignerSnapshot{signers->pending(), !signers->effectivePath().isEmpty(), signers->deviceCount(),
            signers->uniqueSignerName(), signers->error().message, signers->uniqueSignerId(), signers->effectiveConfigurationId(), signers->versionGate(), signers->admissionVersion()};
    };
    return services;
}

WalletOperationModel::Services WalletCreationController::operationServices() { return creationServices().operation; }

WalletCreationModel* WalletCreationController::newCreation()
{
    if (m_closed || !m_host.registry->retained()) return nullptr;
    auto editor = std::make_unique<WalletCreationModel>();
    editor->setCreation(this);
    return editor->ready() || editor->pending() ? editor.release() : nullptr;
}

// The form's setter lives with the controller that adopts it, so the model
// does not depend on the controller.
void WalletCreationModel::setCreation(WalletCreationController* creation)
{
    // A form creates through one controller for its whole life.
    if (creation == this->creation() || m_creation || m_check) return;
    m_creation = creation; m_creation_alive = creation;
    const QPointer<WalletCreationModel> guard{this};
    Q_EMIT creationChanged();
    if (guard && creation) creation->adopt(this);
}

void WalletCreationController::adopt(WalletCreationModel* editor)
{
    // A form without services reports that creation is unavailable.
    if (!editor || m_closed || !m_host.registry->retained()) return;
    const QPointer<WalletCreationController> owner{this};
    editor->setServices(creationServices());
    if (!owner) return;
    const QPointer<WalletCreationModel> original{editor};
    m_host.registry->registerForm(editor, [original] { if (original) original->discard(); });
    connect(editor, &QObject::destroyed, this, [this, editor] { if (m_host.registry) m_host.registry->forgetForm(editor); });
    connect(this, &WalletCreationController::capabilitiesChanged, editor, &WalletCreationModel::refreshCapabilities);
    connect(this, &WalletCreationController::busyChanged, editor, &WalletCreationModel::refreshCapabilities);
    connect(this, &WalletCreationController::signersChanged, editor, &WalletCreationModel::refreshCapabilities);
    connect(editor, &WalletCreationModel::operationCreated, this, &WalletCreationController::track);
}

WalletProvisioningCheck* WalletCreationController::createProvisioningCheck()
{
    if (m_closed || !m_host.registry->retirement()->retained) return nullptr;
    auto* form = new WalletProvisioningCheck(*m_host.node, {m_host.scheduler, m_host.post}, nullptr, m_host.names_ready, m_host.registry->nameRegistry(), m_host.shutdown_gate);
    QQmlEngine::setObjectOwnership(form, QQmlEngine::JavaScriptOwnership);
    const QPointer<WalletProvisioningCheck> original{form};
    m_host.registry->registerForm(form, [original] { if (original) original->close(); });
    connect(form, &QObject::destroyed, this, [this, form] { if (m_host.registry) m_host.registry->forgetForm(form); });
    connect(this, &WalletCreationController::capabilitiesChanged, form, &WalletProvisioningCheck::refresh);
    return form;
}

void WalletCreationController::track(WalletOperationModel* operation)
{
    if (!operation || !operation->acceptedForDelivery() || m_closed) return;
    m_operation = operation;
    m_busy = true;
    const QPointer<WalletOperationModel> original{operation};
    m_host.registry->registerForm(operation, [original] { if (original) original->close(); });
    connect(operation, &QObject::destroyed, this, [this, operation] { if (m_host.registry) m_host.registry->forgetForm(operation); });
    connect(operation, &WalletOperationModel::activated, this, [this](WalletOperationModel* active) {
        if (m_operation == active && m_busy) return;
        m_operation = active; m_busy = true;
        const QPointer<WalletCreationController> guard{this};
        m_host.context_changed();
        if (guard && !m_closed) Q_EMIT busyChanged();
    });
    connect(operation, &WalletOperationModel::changed, this, [this, original] {
        if (!original || m_closed || m_operation != original) return;
        const QPointer<WalletCreationController> guard{this};
        m_host.context_changed();
        if (!guard || m_closed || !original || m_operation != original) return;
        Q_EMIT operationChanged(); if (!guard || !original) return;
        const bool running = busy();
        if (std::exchange(m_busy, running) != running) Q_EMIT busyChanged();
    });
    const QPointer<WalletCreationController> guard{this};
    m_host.context_changed();
    if (!guard || m_closed) return;
    Q_EMIT operationChanged(); if (guard) Q_EMIT busyChanged();
}

void WalletCreationController::showRefused(WalletOperationModel* operation)
{
    m_operation = operation;
    Q_EMIT operationChanged();
}

WalletOperationModel* WalletCreationController::create(WalletCreationModel::Kind kind, const QString& name, const QString& key, const QString& passphrase)
{
    if (m_closed || m_admitting) return nullptr;
    // A second creation while one runs is ignored; an unavailable wallet list
    // or signer is a refused operation that says why.
    const bool ready = m_host.ready();
    if (ready && !m_host.accepting()) return nullptr;
    const QPointer<WalletCreationController> guard{this};
    m_admitting = true;
    const auto release = qScopeGuard([guard] { if (guard) guard->m_admitting = false; });
    walletcreation::Request request;
    request.kind = static_cast<walletcreation::Kind>(kind); request.submitted_name = name; request.extended_public_key = key;
    request.encrypt = !passphrase.isEmpty(); request.legacy_normalize = true;
    auto services = creationServices(); request.signer = services.signer();
    if (!ready) request.transport_error = {QStringLiteral("Unavailable"), m_host.unavailable(), {}, {}};
    else if (kind == WalletCreationModel::ExternalSigner && !canCreateExternalSignerWallet()) {
        const auto message = m_signers && m_signers->pending() ? tr("Wait for the external signer check to finish.")
            : m_signers && !m_signers->error().message.isEmpty() ? m_signers->error().message
            : tr("Connect an external signer and try again.");
        request.transport_error = {QStringLiteral("SignerUnavailable"), message, {}, {}};
    }
    if (request.transport_error.empty() && (name.size() > 32768 || key.size() > 4096)) request.transport_error = {QStringLiteral("TransportBudget"), tr("This wallet input is too long. No wallet was created."), {}, {}};
    if (kind == WalletCreationModel::Software && request.encrypt && request.transport_error.empty()) {
        auto captured = secretinput::Capture(m_host.scheduler, passphrase);
        if (!captured) request.transport_error = {QStringLiteral("InputUnavailable"), tr("The password could not be used. No wallet was created."), QStringLiteral("credentials"), {}};
        else request.passphrase = request.confirmation = std::move(captured.input);
    }
    auto* operation = WalletOperationModel::Make(services.operation, std::move(request));
    if (!guard) { operation->deleteLater(); return nullptr; }
    if (!operation->acceptedForDelivery()) {
        showRefused(operation);
    } else {
        track(operation);
        if (guard) operation->start();
    }
    return operation;
}

WalletOperationModel* WalletCreationController::startRestoration(walletrestoration::Request request)
{
    const QPointer<WalletCreationController> guard{this};
    if (!m_host.ready() || m_closed) request.transport_error = {QStringLiteral("Unavailable"), m_host.unavailable(), {}, {}};
    // A refusal while the wallet list is busy belongs to that list; it does
    // not replace what it shows.
    const bool previous_busy = m_host.loading();
    const auto operation = QPointer<WalletOperationModel>{WalletOperationModel::Make(creationServices().operation, std::move(request))};
    if (!guard) { if (operation) operation->deleteLater(); return nullptr; }
    if (!operation->acceptedForDelivery()) {
        if (!previous_busy) {
            showRefused(operation);
        }
        return operation;
    }
    track(operation); if (guard && operation) operation->start();
    return operation;
}

WalletOperationModel* WalletCreationController::restore(const QUrl& source)
{
    walletrestoration::Request request; request.kind = walletrestoration::Kind::Restore; request.source = source.toString(QUrl::FullyEncoded);
    return startRestoration(std::move(request));
}

WalletOperationModel* WalletCreationController::restorePath(const QString& path)
{
    walletrestoration::Request request; request.kind = walletrestoration::Kind::Restore; request.source = path;
    return startRestoration(std::move(request));
}

WalletOperationModel* WalletCreationController::migrate(const QString& wallet_id)
{
    walletrestoration::Request request; request.kind = walletrestoration::Kind::Migrate; request.source = wallet_id;
    const QPointer<WalletCreationController> guard{this}; auto* operation = startRestoration(std::move(request));
    if (guard && operation && operation->acceptedForDelivery()) Q_EMIT migrationOffered(operation);
    return operation;
}

WalletOperationModel* WalletCreationController::migrateConfirmed(const QString& path, const QString& passphrase)
{
    walletrestoration::Request request; request.kind = walletrestoration::Kind::Migrate; request.source = path; request.confirmed = true;
    if (path.size() > 32768) request.transport_error = {QStringLiteral("TransportBudget"), tr("This wallet input is too long. Nothing was changed."), {}, {}};
    if (!passphrase.isEmpty() && request.transport_error.empty()) {
        auto input = secretinput::Capture(m_host.scheduler, passphrase);
        if (!input) request.transport_error = {QStringLiteral("InputUnavailable"), tr("The password could not be used. The wallet was not updated."), {}, {}};
        else request.passphrase = std::move(input.input);
    }
    return startRestoration(std::move(request));
}

void WalletCreationController::clear()
{
    if (m_operation && m_operation->busy()) return;
    if (!m_operation) return;
    m_operation.clear();
    Q_EMIT operationChanged();
}


void WalletCreationController::setSignerServices(execution::SnapshotServices services)
{
    Q_ASSERT(!m_signers);
    m_signer_services = std::move(services);
}

void WalletCreationController::setSignerPorts(signerdiscovery::Ports ports)
{
    Q_ASSERT(!m_signers);
    m_signer_ports = std::move(ports);
}

void WalletCreationController::refreshSigners()
{
    if (m_closed) return;
    if (!m_signers) {
        SignerDiscoveryModel::Services services;
        services.discovery = m_signer_services; // Default owns a separate bounded process lane.
        services.configuration = {m_host.scheduler, m_host.post};
        services.retire = m_host.post;
        services.ports = m_signer_ports.configuration ? m_signer_ports : signerdiscovery::PersistentPorts(*m_host.node);
        m_signers = new SignerDiscoveryModel(std::move(services), this);
        connect(m_signers, &SignerDiscoveryModel::changed, this, [this] { if (!m_closed) Q_EMIT signersChanged(); });
        connect(m_signers, &SignerDiscoveryModel::shutdownFinished, this, &WalletCreationController::signersDrained);
    }
    m_signers->refresh();
}

void WalletCreationController::close()
{
    m_closed = true;
    // The active operation's projection is closed synchronously; the registry
    // retires every other retained form in its ordinary shutdown batches.
    if (m_operation) m_operation->close();
    if (m_signers) m_signers->close();
}
