// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_QML_PROFILEINSPECTIONPROCESS_H
#define BITCOIN_QML_PROFILEINSPECTIONPROCESS_H

#include <qml/profileinspection.h>

namespace profileinspection::process {
enum class Phase { Starting, Running, Exited, FailedToStart };
enum class Channel { Output, Error };
struct Status {
    Phase phase{Phase::Starting};
    bool normal_exit{false};
    std::optional<int> exit_status;
    QString error;
};

/** Internal Native driver seam. One instance is one original child; it is never
 * replaced or restarted. read() returns at most max_bytes. wait() advances only
 * this child. reap() retains it until physical exit/start failure and returns
 * terminal facts. Killing an OS child has no universal completion deadline. */
class Process {
public:
    virtual ~Process() = default;
    virtual void start(const QString& executable, const QStringList& arguments) = 0;
    virtual Status status() const = 0;
    virtual QByteArray read(Channel channel, qsizetype max_bytes) = 0;
    virtual void wait(std::chrono::milliseconds duration) = 0;
    virtual void kill() noexcept = 0;
    virtual Status reap() noexcept = 0;
};
using Clock = std::function<std::chrono::milliseconds()>;
Result Run(const Request& request, const Cancellation& cancelled, const QString& executable,
           Limits limits, Process& child, const Clock& clock);
} // namespace profileinspection::process
#endif // BITCOIN_QML_PROFILEINSPECTIONPROCESS_H
