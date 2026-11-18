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
// Full results contain Native-only saved-profile recovery ownership. Both
// snapshots are reserved before effects; their final destruction runs Cleanup,
// even if the last alias is released on GUI. Keep these aliases on Domain/Native
// and release them before global drain. GUI bridges copy bounded scalar facts.
using CompleteResult = std::function<void(std::shared_ptr<const Result>)>;
struct Ports {
    std::function<Profile()> read_profile; // Native, read-only.
    std::function<execution::Admission(Profile, CompleteTransport)> start_transport; // Domain.
    std::function<startup::PreparationOutcome(Profile)> prepare_native; // Native, Primary only.
    std::function<execution::Admission(Done)> retire_transport; // Domain; done means physical retirement.
    std::function<bool()> transport_retired; // Domain-safe observation.
};
enum class Phase { Idle, Reading, StartingTransport, Preparing, Retiring, RetirementBlocked, Completed };
/** One admitted startup intent, with no synchronous Core/Domain waits.
 * Start and close arbitrate through one bounded ingress inbox, including when
 * onboarding and application shutdown use different threads. Domain consumes
 * accepted input; result aliases remain for Domain/Native, while phase is safe
 * to observe elsewhere. The application retains its Native port context on an
 * unadmitted construction/start path. Port completions run on Domain (inline is permitted).
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
    Coordinator(execution::SnapshotServices services, Ports ports);
    ~Coordinator();
    Coordinator(const Coordinator&) = delete;
    Coordinator& operator=(const Coordinator&) = delete;
    execution::Admission start(Context context, CompleteResult completed);
    void close();
    execution::Admission retryRetirement();
    std::shared_ptr<const Result> result() const;
    Phase phase() const;
private:
    struct State;
    struct Publication;
    execution::Admission postClose(bool retry);
    const execution::SnapshotServices m_services;
    std::shared_ptr<Publication> m_publication;
    std::shared_ptr<State> m_owner;
};
} // namespace startup::preparation
#endif // BITCOIN_QML_STARTUPPREPARATION_H
