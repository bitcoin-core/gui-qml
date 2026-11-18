// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_QML_MODELS_STARTUPDIAGNOSTICSMODEL_H
#define BITCOIN_QML_MODELS_STARTUPDIAGNOSTICSMODEL_H
#include <qml/guipresentation.h>
#include <qml/presentationservices.h>
#include <qml/snapshotquery.h>
#include <QAbstractListModel>
#include <QStringList>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <list>
#include <mutex>

namespace startupdiagnostics {
int FinalExitStatus(int original, bool diagnostic_loss);
struct TextBudget { static constexpr uint64_t MAX_CHARS{4 * 1024 * 1024}; std::atomic<uint64_t> used{0}; };
std::shared_ptr<const QString> RetainText(const std::shared_ptr<TextBudget>& budget, QString message,
    const PresentationServices::RetireValue& retire);
// App-owned immutable batches. Intake appends a single linked node; normalization,
// deduplication, concatenation and destruction belong to the Native owner.
struct Collection {
    struct Batch {
        bool warning{false};
        QStringList messages;
        std::shared_ptr<const Collection> previous;
    };
    mutable std::mutex mutex;
    std::list<Batch> batches;
    bool append(QString message, bool warning);
    bool append(QStringList messages, bool warning);
    bool adopt(std::shared_ptr<const Collection> previous);
    void seal() const;
    static constexpr size_t MAX_BATCH_MESSAGES{64}, MAX_BATCHES{4096}, MAX_MESSAGES{65536}, MAX_CHARS{4 * 1024 * 1024};
    std::atomic<size_t> owned_chars{0}, owned_messages{0}, owned_batches{0};
    mutable std::atomic_bool sealed{false};
};
std::shared_ptr<Collection> Create(const PresentationServices::RetireValue& retire);
}

class StartupDiagnosticsModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString summary READ summary NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(bool hasWarnings READ hasWarnings NOTIFY changed)
    Q_PROPERTY(int count READ rowCount NOTIFY changed)
public:
    StartupDiagnosticsModel(presentation::Post post, PresentationServices& diagnostics, QObject* parent = nullptr);
    ~StartupDiagnosticsModel() override;
    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    bool busy() const { return m_busy; }
    QString summary() const;
    QString error() const { return m_error; }
    bool hasWarnings() const;
    bool append(QString message, bool warning);
    bool append(QStringList messages, bool warning);
    bool adopt(std::shared_ptr<const startupdiagnostics::Collection> previous);
    // Seal the input before dispatch. No GUI callback scans or copies the list.
    bool project(bool success, QString failure = {}, QString warning_heading = {}, QString error_prefix = {}, bool normalize = true);
    bool resetInput();
    void retryPublication();
    void close();
Q_SIGNALS:
    void changed();
    void finished();
private:
    void retire(bool notify);
    void resetRows();
    void schedulePresentation();
    void deliverPresentation();
    void flushChanges();
    struct Prepared { QString summary, intake_error; QStringList rows; bool warnings{false}; };
    void publish();
    presentation::Post m_post;
    execution::SnapshotQuery<Prepared>::Post m_observation_post;
    PresentationServices::RetireValue m_retire_value;
    std::shared_ptr<startupdiagnostics::Collection> m_input, m_authoritative_input;
    std::shared_ptr<const Prepared> m_prepared;
    std::shared_ptr<execution::OwnedCommand<std::shared_ptr<Prepared>>> m_operation;
    bool m_closed{false}, m_busy{false}, m_publishing{false}, m_deferred_changed{false};
    bool m_rows_reset_pending{false}, m_publication_pending{false}, m_presentation_scheduled{false};
    quint64 m_generation{0};
    QString m_error, m_intake_error;
};
#endif // BITCOIN_QML_MODELS_STARTUPDIAGNOSTICSMODEL_H
