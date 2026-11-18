// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/models/signerdiscoverymodel.h>
#include <qml/backendexecutor.h>
#include <qml/guipresentation.h>
#include <QRegularExpression>
#include <QTimer>
#include <utility>
namespace {
SignerDiscoveryError Error(const signerdiscovery::Result& result) { return {static_cast<SignerDiscoveryError::Code>(result.error), result.message}; }
}
SignerDiscoveryModel::ConfigurationDrain::~ConfigurationDrain() { if (notify) notify(); }
SignerDiscoveryModel::SignerDiscoveryModel(Services services, QObject* parent)
    : QAbstractListModel(parent), m_services(std::move(services)), m_bridge(presentation::MakeGuiObject())
{
    if (!m_services.discovery.scheduler) { m_executor = std::make_shared<BackendExecutor>(execution::Options{}); m_services.discovery.scheduler = m_executor->scheduler(); }
    if (!m_services.configuration.scheduler) { m_config_executor = std::make_shared<BackendExecutor>(execution::Options{}); m_services.configuration.scheduler = m_config_executor->scheduler(); }
    if (!m_services.discovery.post) m_services.discovery.post = [bridge = m_bridge](auto work) { QMetaObject::invokeMethod(bridge.get(), std::move(work), Qt::QueuedConnection); };
    if (!m_services.configuration.post) m_services.configuration.post = m_services.discovery.post;
    m_services.discovery.post = presentation::ReliablePost(std::move(m_services.discovery.post));
    m_services.configuration.post = presentation::ReliablePost(std::move(m_services.configuration.post));
    if (!m_services.retire) m_services.retire = presentation::QueuedPost();
    m_services.retire = presentation::ReliablePost(std::move(m_services.retire));
    if (!m_services.arm) {
        auto* timer = new QTimer(this); timer->setSingleShot(true);
        m_services.arm = [timer](auto duration, auto callback) { timer->disconnect(); QObject::connect(timer, &QTimer::timeout, timer, std::move(callback)); timer->start(int(duration.count())); };
        m_services.stop = [timer] { timer->stop(); timer->disconnect(); };
    }
    const QPointer<SignerDiscoveryModel> observer{this};
    m_configuration_drain = std::make_shared<ConfigurationDrain>([observer, post = m_services.retire] {
        post([observer] { if (observer) { observer->m_configuration_drained = true; observer->finishDrain(); } });
    });
    m_configuration_reader = std::make_shared<signerdiscovery::Reader>(m_services.configuration.scheduler,
        [ports = m_services.ports, drain = m_configuration_drain]() -> signerdiscovery::Result {
            if (!ports.configuration) return {{}, {}, signerdiscovery::Error::Unavailable, tr("The effective signer configuration is unavailable.")};
            return {ports.configuration(), {}, signerdiscovery::Error::None, {}};
        }, [observer, drain = m_configuration_drain, post = m_services.configuration.post] { post([observer] { if (observer) observer->publishConfiguration(); }); });
    m_reader = std::make_shared<signerdiscovery::Reader>(m_services.discovery.scheduler, [ports = m_services.ports] { return signerdiscovery::Discover(ports); },
        [observer, post = m_services.discovery.post] { post([observer] { if (observer) observer->publish(); }); },
        [observer, post = m_services.retire] { post([observer] { if (observer) { observer->m_discovery_drained = true; observer->finishDrain(); } }); });
}
SignerDiscoveryModel::~SignerDiscoveryModel() { closeAdmission(); }
int SignerDiscoveryModel::rowCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : int(m_devices.size()); }
QVariant SignerDiscoveryModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.parent().isValid() || index.row() < 0 || index.row() >= rowCount()) return {};
    const auto& device = m_devices[size_t(index.row())];
    switch (role) {
    case SignerIdRole: return device.id;
    case NameRole: return device.name;
    case FingerprintRole: return device.fingerprint;
    case AvailableRole: return m_fresh && device.available;
    default: return {};
    }
}
QHash<int, QByteArray> SignerDiscoveryModel::roleNames() const { return {{SignerIdRole, "signerId"}, {NameRole, "name"}, {FingerprintRole, "fingerprint"}, {AvailableRole, "available"}}; }
void SignerDiscoveryModel::editPath(const QString& text)
{
    if (m_closed) return;
    ++m_edit_revision; m_edited_by_user = true;
    // Retain one invalid sentinel instead of accepting a truncated prefix.
    m_edited = text.left(32769);
    m_edit_error = text.size() > 32768 ? SignerDiscoveryError{SignerDiscoveryError::InvalidPath, tr("The signer command is too long.")} : SignerDiscoveryError{};
    const QPointer<SignerDiscoveryModel> guard{this};
    Q_EMIT editedChanged();
    if (guard && !m_closed) Q_EMIT changed();
}
void SignerDiscoveryModel::reject(QString command, SignerDiscoveryError error)
{
    m_edit_error = error;
    const QPointer<SignerDiscoveryModel> guard{this};
    Q_EMIT commandRejected(command, error);
    if (guard && !m_closed) Q_EMIT editedChanged();
}
void SignerDiscoveryModel::discardChanges()
{
    if (m_closed) return;
    ++m_edit_revision; m_edited = m_effective; m_edited_by_user = false; m_edit_error = {};
    const QPointer<SignerDiscoveryModel> guard{this};
    Q_EMIT editedChanged();
    if (guard && !m_closed) Q_EMIT changed();
}
bool SignerDiscoveryModel::apply()
{
    if (!canApply() || m_edited.size() > 32768 || !m_services.ports.save) {
        reject(QStringLiteral("apply"), {m_closed ? SignerDiscoveryError::Closed : m_edited.size() > 32768 ? SignerDiscoveryError::InvalidPath : SignerDiscoveryError::Unavailable, tr("The signer configuration cannot be applied now.")}); return false;
    }
    m_saving = true; m_fresh = false; m_save_edit_revision = m_edit_revision;
    m_last_save_persisted = false;
    m_version_gate->store(++m_gate_revision);
    ++m_configuration_revision;
    const QPointer<SignerDiscoveryModel> observer{this};
    const auto drain = m_configuration_drain;
    m_save = std::make_shared<execution::OwnedCommand<signerdiscovery::Result>>(m_services.configuration.scheduler,
        [observer, drain, post = m_services.configuration.post] { post([observer] { if (observer) observer->saved(); }); });
    const auto command = m_edited;
    const auto admitted = m_save->start([save = m_services.ports.save, command, drain] { return save(command); }, {execution::Lane::Core, {}, execution::WorkClass::Action});
    if (!admitted) { m_saving = false; m_fresh = m_read_fresh; m_save.reset(); reject(QStringLiteral("apply"), {SignerDiscoveryError::Rejected, tr("The signer configuration could not be queued. No setting was changed.")}); }
    if (observer && !m_closed) Q_EMIT changed();
    return bool(admitted);
}
void SignerDiscoveryModel::applyConfiguration(const signerdiscovery::Configuration& config)
{
    if (m_config_id != config.id) { m_read_fresh = false; m_fresh = false; }
    m_effective = config.command; m_config_id = config.id; m_editable = config.editable; m_source = config.source;
    m_configuration_known = !config.id.isEmpty();
    if (!m_edited_by_user) { m_edited = m_effective; Q_EMIT editedChanged(); }
}
void SignerDiscoveryModel::saved()
{
    if (!m_save || m_closed) return;
    const auto result = m_save->result(); if (!result) return;
    m_saving = false;
    const QPointer<SignerDiscoveryModel> guard{this};
    if (result->value) {
        m_last_save_persisted = result->value->persisted;
        if (result->value->configuration_known) { applyConfiguration(result->value->configuration); if (!guard || m_closed) return; }
        m_edit_error = Error(*result->value);
        if (result->value->error == signerdiscovery::Error::None) {
            if (m_edit_revision == m_save_edit_revision) { m_edited = m_effective; m_edited_by_user = false; }
            m_save.reset();
            Q_EMIT editedChanged(); if (!guard || m_closed) return;
            scheduleRefresh();
        } else {
            if (result->value->persisted) { m_save_unknown = true; m_unknown_revision = m_revision; m_read_fresh = false; }
            m_fresh = m_read_fresh; m_save.reset(); Q_EMIT editedChanged();
        }
    } else {
        m_save.reset();
        m_save_unknown = true;
        m_unknown_revision = m_revision;
        m_read_fresh = false;
        reject(QStringLiteral("apply"), {SignerDiscoveryError::OutcomeUnknown, tr("The signer setting outcome is unknown. Refresh the effective configuration before retrying.")});
    }
    if (guard && !m_closed) Q_EMIT changed();
}
void SignerDiscoveryModel::refresh()
{
    if (m_closed || m_saving) { reject(QStringLiteral("refresh"), {m_closed ? SignerDiscoveryError::Closed : SignerDiscoveryError::Unavailable, tr("The signer check is unavailable while settings are being saved.")}); return; }
    scheduleRefresh();
}
void SignerDiscoveryModel::scheduleRefresh()
{
    const auto revision = ++m_revision;
    m_version_gate->store(++m_gate_revision);
    m_configuration_reader->request(++m_configuration_revision);
    m_pending = true; m_fresh = false; m_read_fresh = false; m_phase = Scheduled; m_error = {};
    const auto reader = m_reader;
    reader->request(revision);
    m_services.arm(std::chrono::seconds{30}, [reader, revision] { reader->expire(revision); });
    Q_EMIT changed();
}
void SignerDiscoveryModel::publishConfiguration()
{
    if (m_closed) return;
    const auto next = m_configuration_reader->take(); if (!next || next->revision != m_configuration_revision) return;
    const QPointer<SignerDiscoveryModel> guard{this};
    if (next->configuration_observed && next->result.error == signerdiscovery::Error::None && !m_saving) {
        applyConfiguration(next->result.configuration); if (!guard || m_closed) return;
    }
    Q_EMIT changed();
}
void SignerDiscoveryModel::publish()
{
    if (m_closed) return;
    const auto next = m_reader->take(); if (!next || next->revision != m_revision) return;
    m_phase = static_cast<ReaderPhase>(next->phase); m_invocation = next->invocation_active;
    m_pending = next->phase == signerdiscovery::Phase::Reading || next->phase == signerdiscovery::Phase::ReadingWithPendingWork || next->phase == signerdiscovery::Phase::Scheduled;
    const QPointer<SignerDiscoveryModel> guard{this};
    if (next->configuration_observed && next->result.configuration_known) {
        applyConfiguration(next->result.configuration); if (!guard || m_closed) return;
        if (!next->result.configuration.id.isEmpty() && next->revision > m_unknown_revision && m_save_unknown) {
            m_save_unknown = false; m_edit_error = {};
            Q_EMIT editedChanged(); if (!guard || m_closed) return;
        }
    }
    m_read_fresh = next->fresh;
    m_fresh = m_read_fresh && !m_saving && !m_save_unknown;
    m_error = Error(next->result);
    if (m_fresh && next->result.devices.empty()) m_error = {SignerDiscoveryError::NoDevices, tr("No external signer is currently detected.")};
    else if (m_fresh && next->result.devices.size() > 1) m_error = {SignerDiscoveryError::MultipleDevices, tr("More than one external signer was found. Connect only one device.")};
    beginResetModel(); if (!guard) return;
    if (next->fresh || !next->result.devices.empty()) m_devices = next->result.devices;
    endResetModel(); if (!guard || m_closed) return;
    m_suggested = uniqueSignerName().trimmed(); m_suggested.replace(QRegularExpression{QStringLiteral("[^A-Za-z0-9_-]+")}, QStringLiteral("_"));
    if (m_suggested.isEmpty()) m_suggested = QStringLiteral("external_signer");
    if (!m_pending && m_services.stop) m_services.stop();
    Q_EMIT changed();
}
void SignerDiscoveryModel::closeAdmission()
{
    if (std::exchange(m_closed, true)) return;
    m_version_gate->store(0);
    m_phase = Stopping; m_pending = false; m_saving = false; m_fresh = false;
    if (m_services.stop) m_services.stop();
    m_reader->close();
    m_configuration_reader->close(); m_configuration_reader.reset();
    m_save.reset();
    m_configuration_drain.reset();
    if (m_executor) m_executor->shutdown();
    if (m_config_executor) m_config_executor->shutdown();
    Q_EMIT changed();
}
void SignerDiscoveryModel::finishDrain()
{
    if (!m_closed || m_drained || !m_discovery_drained || !m_configuration_drained) return;
    m_drained = true; m_phase = Stopped; m_invocation = false;
    const QPointer<SignerDiscoveryModel> guard{this};
    Q_EMIT changed(); if (guard) Q_EMIT shutdownFinished();
}
