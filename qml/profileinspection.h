// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_QML_PROFILEINSPECTION_H
#define BITCOIN_QML_PROFILEINSPECTION_H

#include <util/chaintype.h>
#include <QByteArray>
#include <QString>
#include <QStringList>
#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <optional>

namespace profileinspection {
struct Request {
    QString directory;
    ChainType network{ChainType::MAIN};
    // Presence matters: an explicitly empty challenge is not the default.
    std::optional<QString> signet_challenge;
    friend bool operator==(const Request&, const Request&) = default;
};
enum class Outcome { Succeeded, Rejected, Failed, Cancelled, TimedOut };
struct Result {
    Outcome outcome{Outcome::Failed};
    bool has_wallet_data{false};
    QString error;
    bool started{false}, exited{false};
    std::optional<int> exit_status;
};
struct Limits {
    std::chrono::milliseconds deadline{5000};
    qsizetype stdout_bytes{4096}, stderr_bytes{4096};
};
using Cancellation = std::shared_ptr<std::atomic_bool>;
using Port = std::function<Result(const Request&, const Cancellation&)>;
// Deterministic protocol values. The tag identifies these request bytes; it is
// neither a wallet identity nor authority to publish into a different owner.
QString Validate(const Request& request);
QString Tag(const Request& request);
QStringList Arguments(const Request& request);
Result Decode(const Request& request, const QByteArray& output);
// Native-only synchronous effect. Its original scheduler task owns the child
// through physical exit/reaping, independently of a GUI receiver's lifetime.
Result Run(const Request& request, const Cancellation& cancelled = {}, const QString& executable = {}, Limits limits = {});
}
#endif // BITCOIN_QML_PROFILEINSPECTION_H
