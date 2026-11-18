// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_QML_ONBOARDINGFLOW_H
#define BITCOIN_QML_ONBOARDINGFLOW_H

#include <qml/onboarding_settings.h>
#include <qml/startuppreparation.h>

#include <functional>
#include <memory>
#include <optional>

namespace execution { class Executor; }

namespace onboarding {
enum class Phase {Drafting, Checking, Ready, Persisting, PreparingNode, Completed, Failed, Closing, Closed, Reconciling, Forwarded};
struct Checks {
    uint64_t revision{0};
    bool preview{false}, storage{false}, effective{false};
    QString error;
};
struct Attempt {
    uint64_t id{0}, revision{0};
    QmlOnboardingSettings::ApplyRequest request;
};
/** Domain-only decision owner. Observations never authorize a different
 * revision and a known partial write never returns to the mutation route. */
class Flow {
public:
    void edit(uint64_t revision);
    bool checked(Checks checks);
    std::optional<Attempt> finish(uint64_t revision, QmlOnboardingSettings::ApplyRequest request);
    bool persisted(uint64_t attempt, QmlOnboardingSettings::ProfileResult result);
    bool beginQuery();
    bool reconciled(uint64_t attempt, QmlOnboardingSettings::ProfileResult result);
    bool beginCompletion();
    bool beginPreparation();
    void prepared(bool success, bool unknown, QString error);
    void prepared(startup::preparation::Disposition disposition, QString error);
    void close();
    void queryUnavailable(QString error);
    Phase phase() const { return m_phase; }
    uint64_t revision() const { return m_revision; }
    const std::optional<Attempt>& attempt() const { return m_attempt; }
    const QmlOnboardingSettings::ProfileResult& result() const { return m_result; }
    const QString& error() const { return m_error; }
    bool pending() const { return m_pending || m_query_pending; }
    bool preparationAdmitted() const { return m_preparation_admitted; }
    bool preparationUnknown() const { return m_preparation_unknown; }
    bool canFinish() const { return m_phase == Phase::Ready && !pending(); }
private:
    bool account(uint64_t attempt, QmlOnboardingSettings::ProfileResult result);
    Phase m_phase{Phase::Drafting};
    uint64_t m_revision{0}, m_next_attempt{0};
    bool m_pending{false}, m_query_pending{false}, m_preparation_admitted{false}, m_closing{false}, m_preparation_unknown{false};
    std::optional<Attempt> m_attempt;
    QmlOnboardingSettings::ProfileResult m_result;
    QString m_error;
};

struct Projection {
    Phase phase{Phase::Drafting};
    uint64_t revision{0}, attempt{0};
    bool pending{false}, profile_written{false}, profile_complete{false}, preparation_complete{false};
    bool preparation_finished{false}, preparation_unknown{false}, profile_unknown{false};
    QString error;
    std::optional<startup::preparation::Disposition> preparation_disposition{};
};
struct Ports {
    std::function<QmlOnboardingSettings::ProfileResult(const QmlOnboardingSettings::ApplyRequest&)> write;
    std::function<QmlOnboardingSettings::ProfileResult(const QmlOnboardingSettings::ProfileResult&)> query, complete;
};
struct AsyncPreparation {
    using Complete = startup::preparation::CompleteResult;
    std::function<execution::SubmitResult(startup::preparation::Context, Complete)> start;
    // Bounded command only. The original admitted completion still owns its
    // receipt and physical retirement after this cancellation request.
    std::function<void()> close;
};
/** Slice-specific asynchronous owner. GUI supplies immutable input and narrow
 * commands. Utility owns decisions and Core runs the effects. */
class Coordinator {
public:
    using Publish = std::function<void(Projection)>;
    Coordinator(execution::Executor& executor, Publish publish, std::function<void()> drained = {});
    ~Coordinator();
    bool check(Checks checks, std::function<QString()> validate);
    bool finish(uint64_t revision, QmlOnboardingSettings::ApplyRequest request, Ports ports);
    bool finishAndPrepare(uint64_t revision, QmlOnboardingSettings::ApplyRequest request, Ports ports,
        std::function<std::pair<bool,QString>(const QmlOnboardingSettings::ApplyRequest&, bool)> effect);
    bool recover(bool complete);
    bool prepare(std::function<std::pair<bool,QString>(const QmlOnboardingSettings::ApplyRequest&, bool)> effect);
    bool finishAndPrepareAsync(uint64_t revision, QmlOnboardingSettings::ApplyRequest request, Ports ports, AsyncPreparation preparation);
    bool prepareAsync(AsyncPreparation preparation);
    void close();
private:
    struct State;
    std::weak_ptr<State> m_state;
    std::shared_ptr<State> m_owner;
};
} // namespace onboarding
#endif // BITCOIN_QML_ONBOARDINGFLOW_H
