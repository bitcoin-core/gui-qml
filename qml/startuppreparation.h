// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_QML_STARTUPPREPARATION_H
#define BITCOIN_QML_STARTUPPREPARATION_H

#include <qml/desktopintenttransport.h>
#include <qml/onboarding_settings.h>
#include <qml/startuppolicy.h>
#include <qml/snapshotquery.h>

#include <functional>
#include <memory>
#include <optional>

namespace execution { class Executor; }

namespace startup::preparation {
namespace transport = desktopintent::transport;
struct Profile {
    QString directory, network;
    bool operator==(const Profile&) const = default;
};
enum class Disposition { Ready, Forwarded, Failed, Uncertain };
// Full saved-profile context belongs on Domain/Native, never a GUI projection.
// A caller retains Native backing for an input rejected as duplicate/closed.
struct Context { std::optional<QmlOnboardingSettings::ProfileResult> saved_profile; };
struct Result {
    Disposition disposition{Disposition::Failed};
    std::optional<Profile> profile;
    std::optional<transport::StartResult> transport;
    std::optional<startup::PreparationOutcome> native;
    bool native_started{false}, closing{false}, endpoint_retired{false};
    // Known cancellation before Native preparation entered, independent of a
    // later close accompanying a real failure or an uncertain Native outcome.
    bool cancelled{false};
    QString error;
    std::optional<QmlOnboardingSettings::ProfileResult> saved_profile;
};
using CompleteTransport = std::function<void(transport::StartResult)>;
using Done = std::function<void()>;
// Full results contain native saved-profile recovery ownership. Both
// snapshots are allocated before effects and destroyed on a worker. Release
// them before global drain. GUI bridges copy bounded scalar facts.
using CompleteResult = std::function<void(std::shared_ptr<const Result>)>;
struct Ports {
    std::function<Profile()> read_profile; // Native, read-only.
    std::function<execution::SubmitResult(Profile, CompleteTransport)> start_transport; // Utility.
    std::function<startup::PreparationOutcome(Profile)> prepare_native; // Native, Primary only.
    std::function<execution::SubmitResult(Done)> retire_transport; // Utility; done means physical retirement.
    std::function<bool()> transport_retired; // Worker-safe observation.
};
enum class Phase { Idle, Reading, StartingTransport, Preparing, Retiring, RetirementBlocked, Completed };
/** One admitted startup intent, with no synchronous Core/Domain waits.
 * Start and close arbitrate through one bounded ingress inbox, including when
 * onboarding and application shutdown use different threads. Utility consumes
 * accepted input, including transport replies received from other threads.
 * Phase is safe to observe elsewhere. The application retains its native port
 * context on an unadmitted construction/start path.
 * Rejected start has no callback. An admitted start owns one terminal callback,
 * independent of facade lifetime; duplicates cannot replay effects. Ports must
 * release or invoke admitted callbacks, including on stop.
 *
 * Non-Ready completion waits for physical endpoint retirement. Ready transfers
 * the endpoint to the application: dropping this facade does not stop it. Call
 * close explicitly to stop; a known Ready outcome remains Ready. A rejected
 * stop requires explicit retryRetirement while this recovery owner is retained.
 */
class Coordinator final {
public:
    Coordinator(execution::Executor& executor, Ports ports);
    ~Coordinator();
    Coordinator(const Coordinator&) = delete;
    Coordinator& operator=(const Coordinator&) = delete;
    execution::SubmitResult start(Context context, CompleteResult completed);
    void close();
    execution::SubmitResult retryRetirement();
    // Register before releasing a shared-executor coordinator. The receipt
    // follows work, result aliases and native port cleanup.
    void whenDrained(Done done);
    std::shared_ptr<const Result> result() const;
    Phase phase() const;
private:
    struct State;
    struct Publication;
    execution::SubmitResult postClose(bool retry);
    std::shared_ptr<Publication> m_publication;
    std::shared_ptr<State> m_owner;
};
} // namespace startup::preparation
#endif // BITCOIN_QML_STARTUPPREPARATION_H
