// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_QML_SIGNERDISCOVERY_H
#define BITCOIN_QML_SIGNERDISCOVERY_H
#include <qml/execution.h>
#include <QString>
#include <functional>
#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>
class ArgsManager;
namespace interfaces { class Node; }
namespace signerdiscovery {
enum class Phase { Idle, Scheduled, Reading, ReadingWithPendingWork, Applying, Current, Failed, Stopping, Stopped };
enum class Error { None, Unconfigured, InvalidPath, Unavailable, Backend, InvalidResponse, Rejected, Timeout, ConfigurationChanged, Closed, Persistence };
struct Configuration {
    QString command, id;
    bool editable{true};
    QString source;
    friend bool operator==(const Configuration&, const Configuration&) = default;
};
struct Device {
    // Core/HWI's eight-hex master fingerprint is a key-origin hint. Same-seed
    // replacement devices share it; it is neither a unique physical-device ID
    // nor a cryptographic wallet ID. This entry scopes the hint by config.
    QString id, name, fingerprint;
    bool available{true};
    friend bool operator==(const Device&, const Device&) = default;
};
struct Result {
    Configuration configuration;
    std::vector<Device> devices;
    Error error{Error::None};
    QString message;
    bool persisted{false}; // True is known successful persistence, never an inference from an exception.
    bool configuration_known{true};
};
struct Ports {
    std::function<Configuration()> configuration;
    std::function<std::vector<Device>(const Configuration&)> enumerate;
    std::function<Result(const QString&)> save;
    bool available{true};
};
// Public Node/RPC/configuration adapter. No wallet implementation access.
Ports PublicPorts(interfaces::Node& node, ArgsManager& args);
Ports PersistentPorts(interfaces::Node& node);
/** Translate in the signer discovery context. */
QString Text(const char* text);
Result Failure(Error error, QString message, Configuration config = {});
QString NormalizeCommand(const QString& command);
QString PathError(const QString& command);
Result Discover(const Ports& ports);
struct Publication {
    quint64 revision{0}, source_revision{0};
    Phase phase{Phase::Idle};
    Result result;
    bool fresh{false}, invocation_active{false};
    bool configuration_observed{false};
};
/** One owned process call and one replaceable refresh intent. A logical
 * deadline never cancels the public synchronous call or releases its captures.
 * Every decision/shape check runs on Domain; GUI only takes publications. */
class Reader final : public std::enable_shared_from_this<Reader> {
public:
    Reader(std::shared_ptr<execution::Scheduler> scheduler, std::function<Result()> read, std::function<void()> notify, std::function<void()> drained = {});
    execution::Admission request(quint64 revision);
    void expire(quint64 revision);
    void close();
    std::optional<Publication> take();
private:
    execution::Admission wake();
    void consume();
    void complete(quint64 revision, Result result);
    void publish(Publication value);
    void rejected();
    const std::shared_ptr<execution::Scheduler> m_scheduler;
    const std::function<Result()> m_read;
    const std::function<void()> m_notify;
    const execution::ResourceKey m_key;
    struct Drain { std::function<void()> notify; ~Drain(); };
    std::shared_ptr<Drain> m_drain;
    std::atomic_bool m_active_flag{false};
    std::mutex m_mutex;
    quint64 m_desired{0}, m_expired{0};
    bool m_wake{false}, m_closed{false}, m_notified{false};
    std::optional<Publication> m_latest;
    // Serial Domain state only.
    std::optional<quint64> m_active;
    quint64 m_completed{0}, m_source{0};
    Result m_previous;
};
} // namespace signerdiscovery
#endif // BITCOIN_QML_SIGNERDISCOVERY_H
