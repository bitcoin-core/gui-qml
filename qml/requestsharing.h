// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_QML_REQUESTSHARING_H
#define BITCOIN_QML_REQUESTSHARING_H
#include <qml/snapshotquery.h>
#include <qml/walletopeningstate.h>
#include <qml/sourceepoch.h>
#include <QImage>
#include <QObject>
#include <QString>
#include <atomic>
#include <mutex>
#include <optional>

namespace requestimageexport { struct Services; }

class SharingError {
    Q_GADGET
public:
    enum class Code { None, Missing, Changed, Used, Closed, Inactive, Rejected, Oversize, QrEncoding, Native, Unknown, Cancelled };
    Q_ENUM(Code)
};
namespace requestshare {
enum class Kind { Uri, Qr };
enum class Phase { Checking, Preparing, Authorizing, Delivering, Cancelling, Completed, Failed, OutcomeUnknown, Cancelled };
using Error = SharingError::Code;
enum class Delivery { Accepted, Failed, Unknown };
struct Source {
    walletopening::Opening opening;
    int64_t request_id{0};
    uint64_t record_revision{0}, revision{0}, receipt_epoch{0}, native_revision{0};
    QString payload;
    Error error{Error::None};
    // Bounded cached authorization only; never calls Core or performs I/O.
    std::function<bool()> current;
};
struct View {
    enum class Interest { Inactive, Active, Closed };
    std::atomic<Interest> interest{Interest::Inactive};
    std::atomic<uint64_t> token{0};
    std::shared_ptr<std::atomic_bool> input;
    std::shared_ptr<SourceEpoch> input_epoch;
    bool active(uint64_t expected, std::optional<uint64_t> input_revision = {}) const;
    bool setActive(bool active);
    void close();
};
/** One source/presentation capability. Native admission linearizes before its
 * effect. Revocation before that point prevents delivery; afterward the actual
 * effect owns its result and cannot be reported as undone. */
class Grant {
public:
    Grant(Source source, std::shared_ptr<View> view, uint64_t token, uint64_t input_revision = 0);
    bool admit();
    bool current() const;
    void revoke();
    bool admitted() const;
    const Source source;
private:
    enum class State { Open, Admitted, Revoked };
    const std::shared_ptr<View> m_view;
    const uint64_t m_token;
    const uint64_t m_input_revision;
    mutable std::mutex m_mutex;
    State m_state{State::Open};
};
struct Result {
    QString operation_id;
    Source source;
    Kind kind{Kind::Uri};
    Delivery outcome{Delivery::Failed};
    Error error{Error::None};
    bool admitted{false}, performed{false};
};
struct Services {
    execution::SnapshotServices execution;
    std::function<execution::Admission(int64_t, std::function<void(Source)>)> read;
    std::function<QImage(const QString&)> render;
    // Necessary native handoff, on GUI only. Its result describes this local
    // adapter effect, not durable clipboard ownership by another OS process.
    std::function<Delivery(Kind, const QString&, const QImage&)> deliver;
    std::shared_ptr<const requestimageexport::Services> files;
};
QString ErrorText(Error error);
QImage RenderQr(const QString& payload);
} // namespace requestshare

#endif // BITCOIN_QML_REQUESTSHARING_H
