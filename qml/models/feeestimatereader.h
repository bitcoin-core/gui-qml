// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_QML_MODELS_FEEESTIMATEREADER_H
#define BITCOIN_QML_MODELS_FEEESTIMATEREADER_H
#include <qml/execution.h>
#include <qml/models/feeestimatevalues.h>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
namespace feeestimate {
struct Publication {
    quint64 revision{0};
    Dependencies dependencies;
    Phase phase{Phase::Idle};
    FeeReadResult result;
    bool fresh{false};
    Dependencies observed_dependencies{};
    quint64 source_revision{0};
};
/** One bounded replaceable estimate lane. Core effects own immutable input;
 * only serial Domain callbacks decide phases, freshness and follow-up work. */
class Reader : public std::enable_shared_from_this<Reader> {
public:
    using Read = std::function<FeeReadResult(const FeeReadInput&)>;
    Reader(std::shared_ptr<execution::Scheduler> scheduler, Read read, std::function<void()> notify, std::function<void()> drained = {});
    execution::Admission request(quint64 revision, FeeReadInput input);
    void close();
    void invalidate(quint64 revision, bool scheduled = false);
    std::optional<Publication> take();
private:
    struct Drain { std::function<void()> notify; ~Drain(); };
    std::shared_ptr<Drain> m_drain;
    struct Intent { quint64 revision; FeeReadInput input; };
    execution::Admission queueWake();
    void consume();
    void complete(Intent intent, FeeReadResult result);
    void publish(Publication value);
    void rejected(QString message);
    const std::shared_ptr<execution::Scheduler> m_scheduler;
    const Read m_read;
    const std::function<void()> m_notify;
    const execution::ResourceKey m_key;
    std::mutex m_mutex;
    std::optional<Intent> m_desired;
    std::optional<Publication> m_latest;
    bool m_wake{false}, m_notified{false}, m_closed{false};
    // Domain only, never read through a QObject or from the GUI.
    std::optional<Intent> m_active;
    // Preserve a successful result with the dependencies that produced it.
    std::optional<FeeReadResult> m_retained_result;
    Dependencies m_retained_dependencies;
    quint64 m_retained_revision{0};
    quint64 m_completed{0};
};
}
#endif // BITCOIN_QML_MODELS_FEEESTIMATEREADER_H
