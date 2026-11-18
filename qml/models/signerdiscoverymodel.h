// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_QML_MODELS_SIGNERDISCOVERYMODEL_H
#define BITCOIN_QML_MODELS_SIGNERDISCOVERYMODEL_H
#include <qml/signerdiscovery.h>
#include <qml/snapshotquery.h>
#include <QAbstractListModel>
#include <QPointer>
#include <chrono>
class BackendExecutor;
struct SignerDiscoveryError {
    Q_GADGET
    Q_PROPERTY(Code code MEMBER code)
    Q_PROPERTY(QString message MEMBER message)
public:
    enum Code { None, Unconfigured, InvalidPath, Unavailable, Backend, InvalidResponse, Rejected, Timeout, ConfigurationChanged, Closed, Persistence, NoDevices, MultipleDevices, OutcomeUnknown };
    Q_ENUM(Code)
    Code code{None};
    QString message;
};
Q_DECLARE_METATYPE(SignerDiscoveryError)
class SignerDiscoveryModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(ReaderPhase phase READ phase NOTIFY changed)
    Q_PROPERTY(bool fresh READ fresh NOTIFY changed)
    Q_PROPERTY(bool pending READ pending NOTIFY changed)
    Q_PROPERTY(bool invocationActive READ invocationActive NOTIFY changed)
    Q_PROPERTY(bool canRefresh READ canRefresh NOTIFY changed)
    Q_PROPERTY(bool canApply READ canApply NOTIFY changed)
    Q_PROPERTY(bool editable READ editable NOTIFY changed)
    Q_PROPERTY(bool configurationKnown READ configurationKnown NOTIFY changed)
    Q_PROPERTY(bool saving READ saving NOTIFY changed)
    Q_PROPERTY(bool lastSavePersisted READ lastSavePersisted NOTIFY changed)
    Q_PROPERTY(QString editedPath READ editedPath NOTIFY editedChanged)
    Q_PROPERTY(QString effectivePath READ effectivePath NOTIFY changed)
    Q_PROPERTY(QString effectiveConfigurationId READ effectiveConfigurationId NOTIFY changed)
    Q_PROPERTY(QString configurationSource READ configurationSource NOTIFY changed)
    Q_PROPERTY(QString uniqueSignerId READ uniqueSignerId NOTIFY changed)
    Q_PROPERTY(QString uniqueSignerName READ uniqueSignerName NOTIFY changed)
    Q_PROPERTY(QString suggestedWalletName READ suggestedWalletName NOTIFY changed)
    Q_PROPERTY(int deviceCount READ deviceCount NOTIFY changed)
    Q_PROPERTY(SignerDiscoveryError error READ error NOTIFY changed)
    Q_PROPERTY(SignerDiscoveryError editError READ editError NOTIFY editedChanged)
public:
    enum ReaderPhase { Idle, Scheduled, Reading, ReadingWithPendingWork, Applying, Current, Failed, Stopping, Stopped };
    Q_ENUM(ReaderPhase)
    enum Role { SignerIdRole = Qt::UserRole + 1, NameRole, FingerprintRole, AvailableRole };
    Q_ENUM(Role)
    struct Services {
        execution::SnapshotServices discovery;
        execution::SnapshotServices configuration;
        signerdiscovery::Ports ports;
        // Deadline transport only; the callback enqueues a Domain decision.
        std::function<void(std::chrono::milliseconds, std::function<void()>)> arm;
        std::function<void()> stop;
        // Physical retirement is independent of held/lost view publication.
        std::function<void(std::function<void()>)> retire;
    };
    explicit SignerDiscoveryModel(Services services, QObject* parent = nullptr);
    ~SignerDiscoveryModel() override;
    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    ReaderPhase phase() const { return m_phase; }
    bool fresh() const { return m_fresh; }
    bool pending() const { return m_pending || m_saving; }
    bool invocationActive() const { return m_invocation; }
    bool canRefresh() const { return !m_closed && !m_saving && !m_invocation; }
    bool canApply() const { return !m_closed && !m_saving && !m_save_unknown && m_configuration_known && editable() && m_edited != m_effective; }
    bool editable() const { return m_editable && m_services.ports.available; }
    bool configurationKnown() const { return m_configuration_known; }
    bool saving() const { return m_saving; }
    bool lastSavePersisted() const { return m_last_save_persisted; }
    QString editedPath() const { return m_edited; }
    QString effectivePath() const { return m_effective; }
    QString effectiveConfigurationId() const { return m_config_id; }
    QString configurationSource() const { return m_source; }
    QString uniqueSignerId() const { return m_fresh && m_devices.size() == 1 ? m_devices.front().id : QString{}; }
    QString uniqueSignerName() const { return m_fresh && m_devices.size() == 1 ? m_devices.front().name : QString{}; }
    QString suggestedWalletName() const { return m_suggested; }
    int deviceCount() const { return int(m_devices.size()); }
    SignerDiscoveryError error() const { return m_error; }
    SignerDiscoveryError editError() const { return m_edit_error; }
    Q_INVOKABLE void editPath(const QString& text);
    Q_INVOKABLE void discardChanges();
    Q_INVOKABLE bool apply();
    Q_INVOKABLE void refresh();
    void closeAdmission();
    bool drained() const { return m_drained; }
    std::shared_ptr<const std::atomic<quint64>> versionGate() const { return m_version_gate; }
    quint64 admissionVersion() const { return m_version_gate->load(); }
Q_SIGNALS:
    void changed();
    void editedChanged();
    void commandRejected(const QString& command_id, const SignerDiscoveryError& error);
    void shutdownFinished();
private:
    void publish();
    void publishConfiguration();
    void saved();
    void reject(QString command, SignerDiscoveryError error);
    void applyConfiguration(const signerdiscovery::Configuration& config);
    void finishDrain();
    void scheduleRefresh();
    Services m_services;
    std::shared_ptr<BackendExecutor> m_executor, m_config_executor;
    std::shared_ptr<QObject> m_bridge;
    std::shared_ptr<signerdiscovery::Reader> m_reader;
    std::shared_ptr<signerdiscovery::Reader> m_configuration_reader;
    std::shared_ptr<execution::OwnedCommand<signerdiscovery::Result>> m_save;
    struct ConfigurationDrain { std::function<void()> notify; ~ConfigurationDrain(); };
    std::shared_ptr<ConfigurationDrain> m_configuration_drain;
    std::vector<signerdiscovery::Device> m_devices;
    ReaderPhase m_phase{Idle};
    quint64 m_revision{0}, m_edit_revision{0}, m_save_edit_revision{0}, m_gate_revision{0}, m_unknown_revision{0}, m_configuration_revision{0};
    QString m_edited, m_effective, m_config_id, m_source, m_suggested{QStringLiteral("external_signer")};
    SignerDiscoveryError m_error, m_edit_error;
    bool m_fresh{false}, m_pending{false}, m_invocation{false}, m_editable{true}, m_saving{false}, m_closed{false}, m_drained{false}, m_edited_by_user{false};
    bool m_discovery_drained{false}, m_configuration_drained{false};
    bool m_save_unknown{false};
    bool m_read_fresh{false};
    bool m_last_save_persisted{false};
    bool m_configuration_known{false};
    const std::shared_ptr<std::atomic<quint64>> m_version_gate{std::make_shared<std::atomic<quint64>>(0)};
};
#endif // BITCOIN_QML_MODELS_SIGNERDISCOVERYMODEL_H
