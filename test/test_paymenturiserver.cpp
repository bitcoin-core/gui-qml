// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QtTest/QtTest>

#include <qml/paymenturiserver.h>

#include <QDir>
#include <QLocalServer>
#include <QLocalSocket>
#include <QTemporaryDir>
#include <QtEndian>

#include <atomic>
#include <memory>
#include <optional>
#include <thread>
#include <vector>

#ifdef Q_OS_UNIX
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>

#include <util/fs_helpers.h>
#endif

using namespace std::chrono_literals;

namespace {

const QStringList SAMPLE_URIS{
    QStringLiteral("bitcoin:bcrt1qexample?amount=0.5&label=caf%C3%A9"),
    QStringLiteral("BITCOIN:bcrt1qother?message=a=b"),
};

QByteArray Header(quint8 version, quint16 count)
{
    QByteArray out{"BCAURI"};
    out.append(static_cast<char>(version));
    char bytes[2];
    qToBigEndian(count, bytes);
    out.append(bytes, 2);
    return out;
}

QByteArray Entry(const QByteArray& payload, std::optional<quint32> declared_length = std::nullopt)
{
    char bytes[4];
    qToBigEndian(declared_length.value_or(static_cast<quint32>(payload.size())), bytes);
    return QByteArray(bytes, 4) + payload;
}

// The sender blocks, so the receiver needs this thread's event loop.
PaymentUriIpc::SendResult SendFromThread(const QString& name, const QStringList& uris,
                                         std::chrono::milliseconds reply_timeout = 2s)
{
    PaymentUriIpc::SendResult result;
    std::atomic<bool> done{false};
    std::thread sender([&] {
        result = PaymentUriIpc::SendRequests(name, uris, 1s, reply_timeout);
        done = true;
    });
    while (!done) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
    sender.join();
    return result;
}

} // namespace

class PaymentUriServerTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init();

    void requestRoundTrips();
    void everyTruncationIsIncomplete();
    void malformedRequestsAreInvalid();
    void truncatedUtf8IsInvalid();
    void requestOverTheTotalLimitIsInvalidFromItsLengths();
    void encodeRefusesBatchesOutsideTheLimits();
    void serverNameIdentifiesTheDataDirectory();

    void sendWithoutReceiverReportsNoReceiver();
    void sendDeliversTheBatchAndWaitsForTheReply();
    void requestArrivingInPiecesIsAssembled();
    void malformedRequestIsRejectedWithoutQueueing();
    void stalledClientIsDropped();
    void requestsAreRefusedOnceShuttingDown();
    void sinkRefusalIsReportedAsBusy();
    void connectionsBeyondTheLimitAreBusy();
    void receiverClosingWithoutReplyIsAFailure();
    void listenRefusesALiveReceiver();
#ifdef Q_OS_UNIX
    void listenReplacesAStaleSocket();
    void socketIsPrivateToTheUser();
    void lockIsKeptWhenNoInstanceIsListening();
#endif

private:
    QString uniqueName();

    QTemporaryDir m_dir;
    int m_counter{0};
};

void PaymentUriServerTests::init()
{
    QVERIFY(m_dir.isValid());
}

QString PaymentUriServerTests::uniqueName()
{
    const QString path{m_dir.filePath(QString::number(++m_counter))};
    QDir().mkpath(path);
    return PaymentUriIpc::ServerName(path);
}

void PaymentUriServerTests::requestRoundTrips()
{
    const QByteArray request{PaymentUriIpc::EncodeRequest(SAMPLE_URIS)};
    QVERIFY(!request.isEmpty());

    QStringList decoded;
    QCOMPARE(PaymentUriIpc::DecodeRequest(request, decoded), PaymentUriIpc::DecodeStatus::Complete);
    QCOMPARE(decoded, SAMPLE_URIS);
}

void PaymentUriServerTests::everyTruncationIsIncomplete()
{
    const QByteArray request{PaymentUriIpc::EncodeRequest(SAMPLE_URIS)};
    for (qsizetype size = 0; size < request.size(); ++size) {
        QStringList decoded;
        QCOMPARE(PaymentUriIpc::DecodeRequest(request.left(size), decoded), PaymentUriIpc::DecodeStatus::Incomplete);
        QVERIFY(decoded.isEmpty());
    }
}

void PaymentUriServerTests::malformedRequestsAreInvalid()
{
    const QByteArray uri{"bitcoin:bcrt1qexample"};
    const QList<QByteArray> cases{
        QByteArray{"GET / HTTP/1.1\r\n"},
        Header(2, 1) + Entry(uri),
        Header(1, 0),
        Header(1, PaymentUriIpc::MAX_URIS + 1),
        Header(1, 1) + Entry({}),
        Header(1, 1) + Entry(uri, PaymentUriIpc::MAX_URI_BYTES + 1),
        Header(1, 1) + Entry(QByteArray{"bitcoin:\xff\xfe"}),
        Header(1, 1) + Entry(QByteArray{"file:///etc/passwd"}),
        Header(1, 1) + Entry(uri) + QByteArray{"x"},
    };
    for (const QByteArray& request : cases) {
        QStringList decoded;
        QCOMPARE(PaymentUriIpc::DecodeRequest(request, decoded), PaymentUriIpc::DecodeStatus::Invalid);
        QVERIFY(decoded.isEmpty());
    }
}

void PaymentUriServerTests::truncatedUtf8IsInvalid()
{
    QStringList decoded;
    const QByteArray request{Header(1, 1) + Entry(QByteArray{"bitcoin:abc?label=caf\xc3"})};
    QCOMPARE(PaymentUriIpc::DecodeRequest(request, decoded), PaymentUriIpc::DecodeStatus::Invalid);
    QVERIFY(decoded.isEmpty());
}

void PaymentUriServerTests::requestOverTheTotalLimitIsInvalidFromItsLengths()
{
    const QByteArray uri{QByteArray{"bitcoin:"} + QByteArray(PaymentUriIpc::MAX_URI_BYTES - 8, 'a')};
    const int fitting{int(PaymentUriIpc::MAX_REQUEST_BYTES / (4 + uri.size()))};
    QByteArray request{Header(1, PaymentUriIpc::MAX_URIS)};
    for (int i = 0; i < fitting; ++i) request += Entry(uri);

    qsizetype size{0};
    QCOMPARE(PaymentUriIpc::ScanRequest(request, size), PaymentUriIpc::DecodeStatus::Incomplete);
    request += Entry({}, quint32(uri.size())).left(4);
    QCOMPARE(PaymentUriIpc::ScanRequest(request, size), PaymentUriIpc::DecodeStatus::Invalid);
}

void PaymentUriServerTests::encodeRefusesBatchesOutsideTheLimits()
{
    QVERIFY(PaymentUriIpc::EncodeRequest({}).isEmpty());

    QStringList too_many;
    for (int i = 0; i <= PaymentUriIpc::MAX_URIS; ++i) too_many.append(QStringLiteral("bitcoin:a"));
    QVERIFY(PaymentUriIpc::EncodeRequest(too_many).isEmpty());
    too_many.removeLast();
    QVERIFY(!PaymentUriIpc::EncodeRequest(too_many).isEmpty());

    const QString prefix{QStringLiteral("bitcoin:")};
    const QString longest{prefix + QString(PaymentUriIpc::MAX_URI_BYTES - prefix.size(), QLatin1Char('a'))};
    QVERIFY(!PaymentUriIpc::EncodeRequest({longest}).isEmpty());
    QVERIFY(PaymentUriIpc::EncodeRequest({longest + QLatin1Char('a')}).isEmpty());

    QStringList fitting;
    while (PaymentUriIpc::EncodeRequest(fitting + QStringList{longest}).size() > 0) fitting.append(longest);
    QVERIFY(!fitting.isEmpty());
    const QByteArray largest{PaymentUriIpc::EncodeRequest(fitting)};
    QVERIFY(largest.size() <= PaymentUriIpc::MAX_REQUEST_BYTES);
    QStringList decoded;
    QCOMPARE(PaymentUriIpc::DecodeRequest(largest, decoded), PaymentUriIpc::DecodeStatus::Complete);
    QVERIFY(PaymentUriIpc::EncodeRequest(fitting + QStringList{longest}).isEmpty());
}

void PaymentUriServerTests::serverNameIdentifiesTheDataDirectory()
{
    const QString a{m_dir.filePath(QStringLiteral("a"))};
    const QString b{m_dir.filePath(QStringLiteral("b"))};
    QDir().mkpath(a);
    QDir().mkpath(b);

    const QString name{PaymentUriIpc::ServerName(a)};
    QVERIFY(name.startsWith(QStringLiteral("BitcoinCoreApp-")));
    QCOMPARE(PaymentUriIpc::ServerName(a + QStringLiteral("/../a/")), name);
    QVERIFY(PaymentUriIpc::ServerName(b) != name);
}

void PaymentUriServerTests::sendWithoutReceiverReportsNoReceiver()
{
    const PaymentUriIpc::SendResult result{PaymentUriIpc::SendRequests(uniqueName(), SAMPLE_URIS, 1s, 1s)};
    QCOMPARE(result.status, PaymentUriIpc::SendStatus::NoReceiver);
}

void PaymentUriServerTests::sendDeliversTheBatchAndWaitsForTheReply()
{
    const QString name{uniqueName()};
    PaymentUriServer server;
    QString error;
    QVERIFY2(server.listen(name, error), qPrintable(error));
    QSignalSpy received(&server, &PaymentUriServer::requestsReceived);

    const PaymentUriIpc::SendResult result{SendFromThread(name, SAMPLE_URIS)};
    QCOMPARE(result.status, PaymentUriIpc::SendStatus::Queued);
    QCOMPARE(received.count(), 1);
    QCOMPARE(received.at(0).at(0).toStringList(), SAMPLE_URIS);

    QCOMPARE(SendFromThread(name, {SAMPLE_URIS.first()}).status, PaymentUriIpc::SendStatus::Queued);
    QCOMPARE(received.count(), 2);
}

void PaymentUriServerTests::requestArrivingInPiecesIsAssembled()
{
    const QString name{uniqueName()};
    PaymentUriServer server;
    QString error;
    QVERIFY2(server.listen(name, error), qPrintable(error));
    QSignalSpy received(&server, &PaymentUriServer::requestsReceived);

    QLocalSocket client;
    client.connectToServer(name);
    QVERIFY(client.waitForConnected(1000));
    const QByteArray request{PaymentUriIpc::EncodeRequest(SAMPLE_URIS)};
    for (const char byte : request) {
        client.write(&byte, 1);
        client.flush();
        QCoreApplication::processEvents();
    }
    QTRY_COMPARE(received.count(), 1);
    QCOMPARE(received.at(0).at(0).toStringList(), SAMPLE_URIS);
    QTRY_VERIFY(client.bytesAvailable() > 0 || client.waitForReadyRead(10));
    QCOMPARE(client.read(1), QByteArray(1, static_cast<char>(PaymentUriIpc::Reply::Queued)));
}

void PaymentUriServerTests::malformedRequestIsRejectedWithoutQueueing()
{
    const QString name{uniqueName()};
    PaymentUriServer server;
    QString error;
    QVERIFY2(server.listen(name, error), qPrintable(error));
    QSignalSpy received(&server, &PaymentUriServer::requestsReceived);

    QLocalSocket client;
    client.connectToServer(name);
    QVERIFY(client.waitForConnected(1000));
    client.write(Header(1, 1) + Entry(QByteArray{"file:///etc/passwd"}));
    client.flush();
    QTRY_VERIFY(client.bytesAvailable() > 0 || client.waitForReadyRead(10));
    QCOMPARE(client.read(1), QByteArray(1, static_cast<char>(PaymentUriIpc::Reply::Malformed)));
    QCOMPARE(received.count(), 0);
}

void PaymentUriServerTests::stalledClientIsDropped()
{
    const QString name{uniqueName()};
    PaymentUriServer server;
    server.setReadTimeout(200ms);
    QString error;
    QVERIFY2(server.listen(name, error), qPrintable(error));
    QSignalSpy received(&server, &PaymentUriServer::requestsReceived);

    QLocalSocket client;
    client.connectToServer(name);
    QVERIFY(client.waitForConnected(1000));
    client.write(PaymentUriIpc::EncodeRequest(SAMPLE_URIS).left(12));
    client.flush();
    QTRY_COMPARE_WITH_TIMEOUT(client.state(), QLocalSocket::UnconnectedState, 5000);
    QCOMPARE(client.bytesAvailable(), 0);
    QCOMPARE(received.count(), 0);
}

void PaymentUriServerTests::requestsAreRefusedOnceShuttingDown()
{
    const QString name{uniqueName()};
    PaymentUriServer server;
    QString error;
    QVERIFY2(server.listen(name, error), qPrintable(error));
    QSignalSpy received(&server, &PaymentUriServer::requestsReceived);

    QLocalSocket client;
    client.connectToServer(name);
    QVERIFY(client.waitForConnected(1000));
    const QByteArray request{PaymentUriIpc::EncodeRequest(SAMPLE_URIS)};
    client.write(request.left(10));
    client.flush();
    QTest::qWait(50);
    server.stopAccepting();
    client.write(request.mid(10));
    client.flush();
    QTRY_VERIFY(client.bytesAvailable() > 0 || client.waitForReadyRead(10));
    QCOMPARE(client.read(1), QByteArray(1, static_cast<char>(PaymentUriIpc::Reply::ShuttingDown)));

    const PaymentUriIpc::SendResult result{SendFromThread(name, SAMPLE_URIS)};
    QCOMPARE(result.status, PaymentUriIpc::SendStatus::Failed);
    QVERIFY(result.error.contains(QStringLiteral("shutting down")));
    QCOMPARE(received.count(), 0);
}

void PaymentUriServerTests::sinkRefusalIsReportedAsBusy()
{
    const QString name{uniqueName()};
    PaymentUriServer server;
    QString error;
    QVERIFY2(server.listen(name, error), qPrintable(error));
    QStringList sunk;
    bool accept{false};
    server.setRequestSink([&](const QStringList& uris) {
        if (accept) sunk += uris;
        return accept;
    });
    QSignalSpy received(&server, &PaymentUriServer::requestsReceived);

    const PaymentUriIpc::SendResult refused{SendFromThread(name, SAMPLE_URIS)};
    QCOMPARE(refused.status, PaymentUriIpc::SendStatus::Failed);
    QVERIFY(refused.error.contains(QStringLiteral("too many pending")));
    QCOMPARE(received.count(), 0);

    accept = true;
    QCOMPARE(SendFromThread(name, SAMPLE_URIS).status, PaymentUriIpc::SendStatus::Queued);
    QCOMPARE(sunk, SAMPLE_URIS);
    QCOMPARE(received.count(), 1);
}

void PaymentUriServerTests::connectionsBeyondTheLimitAreBusy()
{
    const QString name{uniqueName()};
    PaymentUriServer server;
    QString error;
    QVERIFY2(server.listen(name, error), qPrintable(error));

    std::vector<std::unique_ptr<QLocalSocket>> idle;
    for (int i = 0; i < PaymentUriIpc::MAX_CONNECTIONS; ++i) {
        idle.push_back(std::make_unique<QLocalSocket>());
        idle.back()->connectToServer(name);
        QVERIFY(idle.back()->waitForConnected(1000));
    }
    QLocalSocket extra;
    extra.connectToServer(name);
    QVERIFY(extra.waitForConnected(1000));
    QTRY_VERIFY(extra.bytesAvailable() > 0 || extra.waitForReadyRead(10));
    QCOMPARE(extra.read(1), QByteArray(1, static_cast<char>(PaymentUriIpc::Reply::Busy)));

    idle.front()->abort();
    idle.erase(idle.begin());
    QCOMPARE(SendFromThread(name, SAMPLE_URIS).status, PaymentUriIpc::SendStatus::Queued);
}

void PaymentUriServerTests::receiverClosingWithoutReplyIsAFailure()
{
    const QString name{uniqueName()};
    QLocalServer receiver;
    QVERIFY(receiver.listen(name));
    QObject::connect(&receiver, &QLocalServer::newConnection, [&receiver] {
        receiver.nextPendingConnection()->abort();
    });

    const PaymentUriIpc::SendResult result{SendFromThread(name, SAMPLE_URIS)};
    QCOMPARE(result.status, PaymentUriIpc::SendStatus::Failed);
    QVERIFY(!result.error.isEmpty());
}

void PaymentUriServerTests::listenRefusesALiveReceiver()
{
    const QString name{uniqueName()};
    PaymentUriServer owner;
    QString error;
    QVERIFY2(owner.listen(name, error), qPrintable(error));
    QSignalSpy received(&owner, &PaymentUriServer::requestsReceived);

    bool listened{true};
    std::atomic<bool> done{false};
    std::thread probe([&] {
        QString intruder_error;
        PaymentUriServer intruder;
        listened = intruder.listen(name, intruder_error);
        done = true;
    });
    while (!done) QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    probe.join();
    QVERIFY(!listened);

    QCOMPARE(SendFromThread(name, SAMPLE_URIS).status, PaymentUriIpc::SendStatus::Queued);
    QCOMPARE(received.count(), 1);
}

#ifdef Q_OS_UNIX
void PaymentUriServerTests::listenReplacesAStaleSocket()
{
    const QString name{uniqueName()};
    const QByteArray path{QDir(QDir::tempPath()).filePath(name).toUtf8()};

    const int fd{::socket(AF_UNIX, SOCK_STREAM, 0)};
    QVERIFY(fd >= 0);
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    QVERIFY(size_t(path.size()) < sizeof(addr.sun_path));
    memcpy(addr.sun_path, path.constData(), path.size());
    QCOMPARE(::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)), 0);
    ::close(fd);
    QVERIFY(QFileInfo::exists(QString::fromUtf8(path)));
    QCOMPARE(PaymentUriIpc::SendRequests(name, SAMPLE_URIS, 1s, 1s).status, PaymentUriIpc::SendStatus::NoReceiver);

    PaymentUriServer server;
    QString error;
    QVERIFY2(server.listen(name, error), qPrintable(error));
    QCOMPARE(SendFromThread(name, SAMPLE_URIS).status, PaymentUriIpc::SendStatus::Queued);
}

void PaymentUriServerTests::socketIsPrivateToTheUser()
{
    const QString name{uniqueName()};
    PaymentUriServer server;
    QString error;
    QVERIFY2(server.listen(name, error), qPrintable(error));

    struct stat info{};
    const QByteArray path{QDir(QDir::tempPath()).filePath(name).toUtf8()};
    QCOMPARE(::stat(path.constData(), &info), 0);
    QCOMPARE(info.st_mode & (S_IRWXG | S_IRWXO), mode_t{0});
}

void PaymentUriServerTests::lockIsKeptWhenNoInstanceIsListening()
{
    const QString dir{m_dir.filePath(QStringLiteral("lock"))};
    QVERIFY(QDir().mkpath(dir));
    const fs::path path{fs::PathFromString(dir.toStdString())};

    const PaymentUriIpc::SendResult result{PaymentUriIpc::LockDataDirOrHandOver(path, SAMPLE_URIS, 0ms, 1s, 1s)};
    QCOMPARE(result.status, PaymentUriIpc::SendStatus::NoReceiver);

    // Record locks are per process, so only a child can see this one.
    const std::string lock_file{fs::PathToString(path / ".lock")};
    const pid_t child{::fork()};
    QVERIFY(child >= 0);
    if (child == 0) {
        const int fd{::open(lock_file.c_str(), O_RDWR)};
        struct flock lock{};
        lock.l_type = F_WRLCK;
        lock.l_whence = SEEK_SET;
        _exit(fd >= 0 && ::fcntl(fd, F_SETLK, &lock) == -1 ? 0 : 1);
    }
    int status{0};
    QCOMPARE(::waitpid(child, &status, 0), child);
    UnlockDirectory(path, ".lock");
    QVERIFY(WIFEXITED(status));
    QCOMPARE(WEXITSTATUS(status), 0);
}
#endif

#ifdef BITCOINQML_NO_TEST_MAIN
#include <test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(PaymentUriServerTests)
#else
QTEST_MAIN(PaymentUriServerTests)
#endif
#include "test_paymenturiserver.moc"
