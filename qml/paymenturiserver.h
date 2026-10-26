// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_PAYMENTURISERVER_H
#define BITCOIN_QML_PAYMENTURISERVER_H

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>

#include <util/fs.h>

#include <chrono>
#include <functional>

class QLocalServer;
class QLocalSocket;
class QTimer;

// "BCAURI" | version u8 | count u16 | (length u32 | UTF-8) x count, big endian.
// The receiver answers with one Reply byte.
namespace PaymentUriIpc {

constexpr quint8 PROTOCOL_VERSION{1};
// MAX_ARG_STRLEN on Linux.
constexpr qsizetype MAX_URI_BYTES{128 * 1024};
constexpr int MAX_URIS{64};
// ARG_MAX on Linux.
constexpr qsizetype MAX_REQUEST_BYTES{2 * 1024 * 1024};
constexpr int MAX_CONNECTIONS{4};

enum class Reply : quint8 {
    Queued = 0,
    Malformed = 1,
    Busy = 2,
    ShuttingDown = 3,
};

enum class DecodeStatus {
    Incomplete,
    Complete,
    Invalid,
};

enum class SendStatus {
    NoReceiver,
    Queued,
    Failed,
};

struct SendResult {
    SendStatus status{SendStatus::Failed};
    QString error;
};

QString ServerName(const QString& network_data_dir);

QByteArray EncodeRequest(const QStringList& uris);

DecodeStatus ScanRequest(const QByteArray& buffer, qsizetype& size);

DecodeStatus DecodeRequest(const QByteArray& buffer, QStringList& uris);

// Failed after connecting means delivery is uncertain: do not resend.
SendResult SendRequests(const QString& server_name, const QStringList& uris,
                        std::chrono::milliseconds connect_timeout,
                        std::chrono::milliseconds reply_timeout);

// Keeps the lock for baseInitialize(): releasing it would let a concurrent
// instance take it and this one would fail with its URIs undelivered.
SendResult LockDataDirOrHandOver(const fs::path& network_data_dir, const QStringList& uris,
                                 std::chrono::milliseconds owner_wait,
                                 std::chrono::milliseconds connect_timeout,
                                 std::chrono::milliseconds reply_timeout);

} // namespace PaymentUriIpc

class PaymentUriServer : public QObject
{
    Q_OBJECT

public:
    explicit PaymentUriServer(QObject* parent = nullptr);
    ~PaymentUriServer() override;

    // Call only while holding the data directory lock: it replaces a stale socket.
    bool listen(const QString& server_name, QString& error);

    void setReadTimeout(std::chrono::milliseconds timeout) { m_read_timeout = timeout; }

    using RequestSink = std::function<bool(const QStringList&)>;
    void setRequestSink(RequestSink sink) { m_sink = std::move(sink); }

    void stopAccepting() { m_accepting = false; }

Q_SIGNALS:
    void requestsReceived(const QStringList& uris);

private:
    struct Connection {
        QByteArray buffer;
        QTimer* timer{nullptr};
    };

    void acceptConnections();
    void readFrom(QLocalSocket* socket);
    void finish(QLocalSocket* socket, PaymentUriIpc::Reply reply);
    void reject(QLocalSocket* socket, PaymentUriIpc::Reply reply);
    void drop(QLocalSocket* socket);

    QLocalServer* m_server{nullptr};
    QHash<QLocalSocket*, Connection> m_connections;
    std::chrono::milliseconds m_read_timeout{std::chrono::seconds{5}};
    RequestSink m_sink;
    bool m_accepting{true};
};

#endif // BITCOIN_QML_PAYMENTURISERVER_H
