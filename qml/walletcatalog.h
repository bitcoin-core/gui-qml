// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_WALLETCATALOG_H
#define BITCOIN_QML_WALLETCATALOG_H

#include <qml/execution.h>

#include <QString>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace walletcatalog {
enum class LoadState { Closed, Open, Loading, LoadError };
enum class Phase { Idle, Scheduled, Reading, ReadingWithPendingWork, Applying, Current, Failed, Stopping, Stopped };
enum class Availability { Uninitialized, Available, Disabled, Unavailable };
enum Field : unsigned { DisplayName = 1, Load = 2, Section = 4, Error = 8, Information = 16, BalanceText = 32, OpenWallet = 64, Operation = 128 };

struct Input {
    enum class Kind { Directory, Load, Unit, Info, Names, Availability, OpenWallet, Operation } kind;
    explicit Input(Kind value) : kind(value) {}
    QString name, error, operation;
    LoadState load{LoadState::Closed};
    walletcatalog::Availability availability{walletcatalog::Availability::Available};
    qint64 balance{0};
    int scheme{-1}, unit{0};
    bool known{false};
    quint64 revision{0}, directory_revision{0};
};
struct Row {
    QString name, format, display_name, balance_text, section, error, operation;
    bool from_wallet_dir{false}, balance_known{false};
    qint64 balance{0};
    int key_scheme{-1};
    LoadState load_state{LoadState::Closed};
    quint64 facade_revision{0};
};
struct Snapshot {
    std::vector<Row> rows;
    std::vector<std::pair<int, unsigned>> notices;
    // A diff is valid only against this exact GUI publication. A skipped
    // intermediate publication requires a reset, never an incomplete diff.
    std::weak_ptr<const Snapshot> base;
    quint64 revision{0}, directory_epoch{0}, directory_revision{0};
    bool loaded{false}, structure_changed{false};
    QString error;
};
struct Observation {
    Phase phase{Phase::Idle};
    Availability availability{Availability::Available};
    bool fresh{false};
    QString error;
    quint64 revision{0};
};
struct Update {
    Observation observation;
    std::shared_ptr<const Snapshot> snapshot;
};

/** Catalog discovery and ordered registry observations have separate producers.
 * Only directory intents are replaceable. Core reads and snapshot construction
 * run off GUI; Domain owns completion and the single publication mailbox.
 * Notify merely posts a presentation wakeup and never dereferences a QObject.
 */
class Reader final : public std::enable_shared_from_this<Reader>
{
public:
    using Directory = std::vector<std::pair<std::string, std::string>>;
    using Read = std::function<Directory()>;
    using Notify = std::function<void()>;
    Reader(std::shared_ptr<execution::Scheduler> scheduler, Read read, Notify notify);
    void request(Input input);
    Observation observation() const;
    Update take();
    void published(std::shared_ptr<const Snapshot> snapshot);
    void close();

private:
    struct Data;
    struct Control;
    struct Attempt;
    void submit(std::shared_ptr<Data> data);
    void rejected(const std::shared_ptr<Attempt>& attempt);
    std::shared_ptr<const Snapshot> read(const std::shared_ptr<Data>& data, const std::shared_ptr<Attempt>& attempt);
    void complete(const std::shared_ptr<Data>& data, const std::shared_ptr<Attempt>& attempt,
                  std::shared_ptr<const Snapshot> snapshot, bool failed);
    void notify();
    const std::shared_ptr<execution::Scheduler> m_scheduler;
    const execution::ResourceKey m_key;
    const Read m_read;
    const Notify m_notify;
    const std::shared_ptr<Control> m_control;
    std::shared_ptr<Data> m_data;
};
} // namespace walletcatalog

#endif // BITCOIN_QML_WALLETCATALOG_H
