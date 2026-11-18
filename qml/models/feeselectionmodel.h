// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_QML_MODELS_FEESELECTIONMODEL_H
#define BITCOIN_QML_MODELS_FEESELECTIONMODEL_H
#include <qml/guipresentation.h>
#include <qml/models/feeestimatevalues.h>
#include <qml/snapshotquery.h>
#include <QObject>
#include <policy/policy.h>
#include <memory>
#include <optional>
namespace feepolicy {
std::optional<CAmount> ParseRate(const QString& text, QChar decimal = QLatin1Char{'.'});
QString FormatRate(CAmount rate);
struct Snapshot {
    quint64 generation{0}, intent{0}, observations{0};
    unsigned int target{6};
    int preset{1}, target_index{3};
    bool manual_mode{false}, manual_rate{false};
    QString text, error;
    std::optional<CAmount> rate;
};
}
class FeeRateEntry : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString text READ text NOTIFY changed)
    Q_PROPERTY(bool valid READ valid NOTIFY changed)
    Q_PROPERTY(bool pending READ pending NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
public:
    explicit FeeRateEntry(QObject* parent = nullptr) : QObject(parent) {}
    QString text() const { return m_text; }
    bool valid() const { return !m_pending && m_valid; }
    bool pending() const { return m_pending; }
    QString error() const { return m_error; }
    Q_INVOKABLE void edit(const QString& text) { Q_EMIT edited(text); }
Q_SIGNALS:
    void edited(const QString& text);
    void changed();
private:
    friend class FeeSelectionModel;
    QString m_text, m_error;
    bool m_valid{false}, m_pending{false};
};
class FeeSelectionModel : public QObject {
    Q_OBJECT
public:
    enum Mode { Automatic, Manual }; Q_ENUM(Mode)
    enum EstimateState { Idle, Scheduled, Reading, ReadingWithPendingWork, Applying, Current, Failed, Stopping, Stopped }; Q_ENUM(EstimateState)
    enum Provenance { Unknown, Estimated, Fallback, Required, Static, Explicit }; Q_ENUM(Provenance)
    enum Error { NoError, Unavailable, Rejected, Backend, Closed, Invalid }; Q_ENUM(Error)
    Q_PROPERTY(Mode mode READ mode NOTIFY policyChanged)
    Q_PROPERTY(int targetBlocks READ targetBlocks NOTIFY policyChanged)
    Q_PROPERTY(QString revision READ revision NOTIFY intentChanged)
    Q_PROPERTY(int presetIndex READ presetIndex NOTIFY policyChanged)
    Q_PROPERTY(int targetIndex READ targetIndex NOTIFY policyChanged)
    Q_PROPERTY(FeeRateEntry* customRate READ customRate CONSTANT)
    Q_PROPERTY(bool policyPending READ policyPending NOTIFY policyChanged)
    Q_PROPERTY(bool customDefaultPending READ customDefaultPending NOTIFY policyChanged)
    Q_PROPERTY(EstimateState estimateState READ estimateState NOTIFY estimateChanged)
    Q_PROPERTY(bool estimateFresh READ estimateFresh NOTIFY estimateChanged)
    Q_PROPERTY(bool estimatePending READ estimatePending NOTIFY estimateChanged)
    Q_PROPERTY(qint64 estimatedFeeSatoshi READ estimatedFeeSatoshi NOTIFY estimateChanged)
    Q_PROPERTY(QString estimatedRate READ estimatedRate NOTIFY estimateChanged)
    Q_PROPERTY(bool hasPreviousEstimate READ hasPreviousEstimate NOTIFY estimateChanged)
    Q_PROPERTY(QString estimateDraftRevision READ estimateDraftRevision NOTIFY estimateChanged)
    Q_PROPERTY(QString estimatePolicyRevision READ estimatePolicyRevision NOTIFY estimateChanged)
    Q_PROPERTY(Provenance provenance READ provenance NOTIFY estimateChanged)
    Q_PROPERTY(Error error READ error NOTIFY estimateChanged)
    Q_PROPERTY(QString errorText READ errorText NOTIFY estimateChanged)
    Q_PROPERTY(bool hasEstimatorTarget READ hasEstimatorTarget NOTIFY estimateChanged)
public:
    explicit FeeSelectionModel(QObject* parent = nullptr);
    ~FeeSelectionModel() override;
    void setServices(presentation::Post post, execution::SnapshotQuery<feepolicy::Snapshot>::Post submit);
    void observe(CAmount minimum, QHash<unsigned int, CAmount> rates);
    void close();
    void finishStop();
    void retryPublications();
    Mode mode() const { return m_manual_mode ? Manual : Automatic; }
    int targetBlocks() const { return static_cast<int>(m_target); }
    QString revision() const { return QString::number(m_intent); }
    int presetIndex() const;
    int targetIndex() const;
    FeeRateEntry* customRate() const { return m_rate; }
    bool policyPending() const { return m_pending; }
    bool customDefaultPending() const { return m_pending && m_manual_mode && !m_manual_rate; }
    std::optional<CAmount> ratePerKvB() const;
    quint64 generation() const { return m_generation; }
    quint64 policyRevision() const { return m_intent; }
    quint64 observationRevision() const { return m_observations; }
    Q_INVOKABLE void choosePreset(int target);
    Q_INVOKABLE void chooseCustom();
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void chooseCustomTarget(int target);
    Q_INVOKABLE void chooseTargetIndex(int index);
    void captureMode(bool manual);
    void captureTarget(unsigned int target);
    void editRate(const QString& text);
    EstimateState estimateState() const { return m_estimate_state; }
    bool estimateFresh() const { return m_fresh; }
    bool estimatePending() const;
    qint64 estimatedFeeSatoshi() const;
    QString estimatedRate() const;
    bool hasPreviousEstimate() const { return !m_fresh && estimatedFeeSatoshi() >= 0; }
    QString estimateDraftRevision() const { return QString::number(m_estimate_dependencies.draft); }
    QString estimatePolicyRevision() const { return QString::number(m_estimate_dependencies.policy); }
    Provenance provenance() const;
    Error error() const { return m_error; }
    QString errorText() const { return m_error_text; }
    bool hasEstimatorTarget() const;
    std::optional<SendFeePreview> selectedPreview() const;
    std::optional<SendFeePreview> preview(unsigned int target) const;
    void invalidateEstimate(EstimateState state = Idle);
    void publishEstimate(feeestimate::Phase phase, FeeReadResult result, bool fresh, feeestimate::Dependencies dependencies = {}, bool retain_previous = false);
Q_SIGNALS:
    void refreshRequested();
    void intentChanged();
    void policyChanged();
    void estimateChanged();
    void commandRejected(const QString& error);
private:
    struct Input;
    void request(bool intent);
    void publishPolicy();
    void reject(const QString& error);
    std::shared_ptr<Input> m_input;
    std::shared_ptr<execution::SnapshotQuery<feepolicy::Snapshot>> m_query;
    std::optional<feepolicy::Snapshot> m_snapshot;
    FeeRateEntry* m_rate;
    unsigned int m_target{6};
    bool m_manual_mode{false}, m_manual_rate{false}, m_pending{true}, m_closed{false};
    quint64 m_generation{0}, m_intent{0}, m_observations{0};
    CAmount m_minimum{DEFAULT_MIN_RELAY_TX_FEE};
    QHash<unsigned int, CAmount> m_rates;
    QString m_manual_text;
    EstimateState m_estimate_state{Idle};
    FeeReadResult m_estimates;
    feeestimate::Dependencies m_estimate_dependencies;
    std::optional<SendFeePreview> retainedPreview() const;
    bool m_fresh{false};
    Error m_error{NoError};
    QString m_error_text;
};
#endif // BITCOIN_QML_MODELS_FEESELECTIONMODEL_H
