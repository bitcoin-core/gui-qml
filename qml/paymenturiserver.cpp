// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/paymenturiserver.h>

#include <util/fs_helpers.h>

#include <QCryptographicHash>
#include <QDeadlineTimer>
#include <QDir>
#include <QFileInfo>
#include <QLocalServer>
#include <QLocalSocket>
#include <QStringDecoder>
#include <QThread>
#include <QTimer>
#include <QtEndian>

#include <algorithm>

namespace PaymentUriIpc {
namespace {

const QByteArray MAGIC{"BCAURI"};
const QLatin1String URI_PREFIX{"bitcoin:"};
constexpr qsizetype HEADER_SIZE{6 + 1 + 2};
constexpr qsizetype LENGTH_SIZE{4};

template <typename T>
void AppendBigEndian(QByteArray& out, T value)
{
    char bytes[sizeof(T)];
    qToBigEndian(value, bytes);
    out.append(bytes, sizeof(T));
}

template <typename T>
T ReadBigEndian(const QByteArray& buffer, qsizetype pos)
{
    return qFromBigEndian<T>(buffer.constData() + pos);
}

int RemainingMs(const QDeadlineTimer& deadline)
{
    return static_cast<int>(std::max<qint64>(0, deadline.remainingTime()));
}

} // namespace

QString ServerName(const QString& network_data_dir)
{
    QString path{QFileInfo(network_data_dir).canonicalFilePath()};
    if (path.isEmpty()) {
        path = QDir::cleanPath(QFileInfo(network_data_dir).absoluteFilePath());
    }
#ifdef Q_OS_WIN
    path = path.toLower();
#endif
    const QByteArray digest{QCryptographicHash::hash(path.toUtf8(), QCryptographicHash::Sha256)};
    // Not "BitcoinQt", which bitcoin-qt uses for the same data directory.
    return QStringLiteral("BitcoinCoreApp-") + QString::fromLatin1(digest.left(8).toHex());
}

QByteArray EncodeRequest(const QStringList& uris)
{
    if (uris.isEmpty() || uris.size() > MAX_URIS) return {};

    QByteArray out{MAGIC};
    out.append(static_cast<char>(PROTOCOL_VERSION));
    AppendBigEndian<quint16>(out, static_cast<quint16>(uris.size()));
    for (const QString& uri : uris) {
        const QByteArray bytes{uri.toUtf8()};
        if (bytes.isEmpty() || bytes.size() > MAX_URI_BYTES) return {};
        if (out.size() + LENGTH_SIZE + bytes.size() > MAX_REQUEST_BYTES) return {};
        AppendBigEndian<quint32>(out, static_cast<quint32>(bytes.size()));
        out.append(bytes);
    }
    return out;
}

DecodeStatus ScanRequest(const QByteArray& buffer, qsizetype& size)
{
    if (!MAGIC.startsWith(buffer.left(MAGIC.size()))) return DecodeStatus::Invalid;
    if (buffer.size() < HEADER_SIZE) return DecodeStatus::Incomplete;
    if (static_cast<quint8>(buffer.at(MAGIC.size())) != PROTOCOL_VERSION) return DecodeStatus::Invalid;

    const quint16 count{ReadBigEndian<quint16>(buffer, MAGIC.size() + 1)};
    if (count == 0 || count > MAX_URIS) return DecodeStatus::Invalid;

    qsizetype pos{HEADER_SIZE};
    for (quint16 i = 0; i < count; ++i) {
        if (buffer.size() < pos + LENGTH_SIZE) return DecodeStatus::Incomplete;
        const quint32 length{ReadBigEndian<quint32>(buffer, pos)};
        if (length == 0 || length > MAX_URI_BYTES) return DecodeStatus::Invalid;
        pos += LENGTH_SIZE + qsizetype(length);
        if (pos > MAX_REQUEST_BYTES) return DecodeStatus::Invalid;
        if (buffer.size() < pos) return DecodeStatus::Incomplete;
    }
    size = pos;
    return DecodeStatus::Complete;
}

DecodeStatus DecodeRequest(const QByteArray& buffer, QStringList& uris)
{
    uris.clear();
    qsizetype size{0};
    const DecodeStatus scanned{ScanRequest(buffer, size)};
    if (scanned != DecodeStatus::Complete) return scanned;
    if (size != buffer.size()) return DecodeStatus::Invalid;

    QStringList decoded;
    qsizetype pos{HEADER_SIZE};
    while (pos < size) {
        const qsizetype length{ReadBigEndian<quint32>(buffer, pos)};
        pos += LENGTH_SIZE;
        // Stateless, so a truncated sequence is an error.
        QStringDecoder decoder{QStringDecoder::Utf8, QStringDecoder::Flag::Stateless};
        const QString uri{decoder.decode(QByteArrayView{buffer.constData() + pos, length})};
        if (decoder.hasError() || !uri.startsWith(URI_PREFIX, Qt::CaseInsensitive)) {
            return DecodeStatus::Invalid;
        }
        decoded.append(uri);
        pos += length;
    }

    uris = decoded;
    return DecodeStatus::Complete;
}

SendResult SendRequests(const QString& server_name, const QStringList& uris,
                        std::chrono::milliseconds connect_timeout,
                        std::chrono::milliseconds reply_timeout)
{
    const QByteArray request{EncodeRequest(uris)};
    if (request.isEmpty()) {
        return {SendStatus::Failed, QObject::tr("The payment request is too large to hand to the running instance.")};
    }

    QLocalSocket socket;
    socket.connectToServer(server_name);
    if (!socket.waitForConnected(static_cast<int>(connect_timeout.count()))) {
        switch (socket.error()) {
        case QLocalSocket::ServerNotFoundError:
        case QLocalSocket::ConnectionRefusedError:
            return {SendStatus::NoReceiver, {}};
        default:
            return {SendStatus::Failed, socket.errorString()};
        }
    }

    const auto unconfirmed = [&socket] {
        return SendResult{SendStatus::Failed,
                          QObject::tr("The running instance did not confirm the payment request: %1").arg(socket.errorString())};
    };

    const QDeadlineTimer deadline{reply_timeout};
    if (socket.write(request) != request.size()) return unconfirmed();
    while (socket.bytesToWrite() > 0) {
        if (!socket.waitForBytesWritten(RemainingMs(deadline))) return unconfirmed();
    }
    while (socket.bytesAvailable() < 1) {
        if (!socket.waitForReadyRead(RemainingMs(deadline))) return unconfirmed();
    }

    char reply{0};
    socket.getChar(&reply);
    socket.disconnectFromServer();
    switch (static_cast<Reply>(reply)) {
    case Reply::Queued:
        return {SendStatus::Queued, {}};
    case Reply::Busy:
        return {SendStatus::Failed, QObject::tr("The running instance has too many pending payment requests.")};
    case Reply::ShuttingDown:
        return {SendStatus::Failed, QObject::tr("The running instance is shutting down.")};
    case Reply::Malformed:
        break;
    }
    return {SendStatus::Failed, QObject::tr("The running instance rejected the payment request.")};
}

SendResult LockDataDirOrHandOver(const fs::path& network_data_dir, const QStringList& uris,
                                 std::chrono::milliseconds owner_wait,
                                 std::chrono::milliseconds connect_timeout,
                                 std::chrono::milliseconds reply_timeout)
{
    const QString server_name{ServerName(QString::fromStdString(fs::PathToString(network_data_dir)))};
    const QDeadlineTimer deadline{owner_wait};
    while (true) {
        const SendResult result{SendRequests(server_name, uris, connect_timeout, reply_timeout)};
        if (result.status != SendStatus::NoReceiver) return result;
        if (util::LockDirectory(network_data_dir, ".lock") != util::LockResult::ErrorLock) return result;
        if (deadline.hasExpired()) return result;
        QThread::msleep(100);
    }
}

} // namespace PaymentUriIpc

PaymentUriServer::PaymentUriServer(QObject* parent)
    : QObject(parent),
      m_server(new QLocalServer(this))
{
    connect(m_server, &QLocalServer::newConnection, this, &PaymentUriServer::acceptConnections);
}

PaymentUriServer::~PaymentUriServer() = default;

bool PaymentUriServer::listen(const QString& server_name, QString& error)
{
    // With an access option Qt renames over the path, replacing a live socket.
    {
        QLocalSocket probe;
        probe.connectToServer(server_name);
        if (probe.waitForConnected(1000)) {
            probe.abort();
            error = tr("Another process is already receiving payment requests for this data directory.");
            return false;
        }
    }

    m_server->setSocketOptions(QLocalServer::UserAccessOption);
    if (!m_server->listen(server_name)) {
        if (m_server->serverError() != QAbstractSocket::AddressInUseError) {
            error = m_server->errorString();
            return false;
        }
        QLocalServer::removeServer(server_name);
        if (!m_server->listen(server_name)) {
            error = m_server->errorString();
            return false;
        }
    }
    error.clear();
    return true;
}

void PaymentUriServer::acceptConnections()
{
    while (QLocalSocket* socket = m_server->nextPendingConnection()) {
        if (m_connections.size() >= PaymentUriIpc::MAX_CONNECTIONS) {
            reject(socket, PaymentUriIpc::Reply::Busy);
            continue;
        }
        socket->setReadBufferSize(PaymentUriIpc::MAX_REQUEST_BYTES + 1);
        auto* timer = new QTimer(socket);
        timer->setSingleShot(true);
        connect(timer, &QTimer::timeout, this, [this, socket] { drop(socket); });
        timer->start(m_read_timeout);
        m_connections.insert(socket, Connection{{}, timer});

        connect(socket, &QLocalSocket::readyRead, this, [this, socket] { readFrom(socket); });
        connect(socket, &QLocalSocket::disconnected, this, [this, socket] { drop(socket); });
        readFrom(socket);
    }
}

void PaymentUriServer::readFrom(QLocalSocket* socket)
{
    const auto it = m_connections.find(socket);
    if (it == m_connections.end()) return;

    it->buffer.append(socket->read(PaymentUriIpc::MAX_REQUEST_BYTES + 1 - it->buffer.size()));

    qsizetype size{0};
    switch (PaymentUriIpc::ScanRequest(it->buffer, size)) {
    case PaymentUriIpc::DecodeStatus::Incomplete:
        return;
    case PaymentUriIpc::DecodeStatus::Invalid:
        finish(socket, PaymentUriIpc::Reply::Malformed);
        return;
    case PaymentUriIpc::DecodeStatus::Complete:
        break;
    }

    QStringList uris;
    if (PaymentUriIpc::DecodeRequest(it->buffer, uris) != PaymentUriIpc::DecodeStatus::Complete) {
        finish(socket, PaymentUriIpc::Reply::Malformed);
        return;
    }
    if (!m_accepting) {
        finish(socket, PaymentUriIpc::Reply::ShuttingDown);
        return;
    }
    if (m_sink && !m_sink(uris)) {
        finish(socket, PaymentUriIpc::Reply::Busy);
        return;
    }
    Q_EMIT requestsReceived(uris);
    finish(socket, PaymentUriIpc::Reply::Queued);
}

void PaymentUriServer::finish(QLocalSocket* socket, PaymentUriIpc::Reply reply)
{
    const auto it = m_connections.find(socket);
    if (it == m_connections.end()) return;
    it->timer->stop();
    m_connections.erase(it);
    reject(socket, reply);
}

void PaymentUriServer::reject(QLocalSocket* socket, PaymentUriIpc::Reply reply)
{
    socket->disconnect(this);
    socket->putChar(static_cast<char>(reply));
    connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
    socket->disconnectFromServer();
    if (socket->state() == QLocalSocket::UnconnectedState) {
        socket->deleteLater();
    }
}

void PaymentUriServer::drop(QLocalSocket* socket)
{
    const auto it = m_connections.find(socket);
    if (it != m_connections.end()) {
        it->timer->stop();
        m_connections.erase(it);
    }
    socket->disconnect(this);
    socket->abort();
    socket->deleteLater();
}
