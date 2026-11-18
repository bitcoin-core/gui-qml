// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/desktopintenttransport.h>
#include <qml/retainedvalue.h>
#include <util/fs.h>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <QMetaObject>
#include <QStringList>
#include <QThread>
#include <QTimer>
#include <QUuid>
#include <QVariant>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <exception>
#include <limits>
#include <map>
#include <mutex>
#include <system_error>
#include <utility>
#ifdef Q_OS_WIN
#include <windows.h>
#include <aclapi.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace desktopintent::transport {
namespace {
constexpr qsizetype MAX_FRAME{128 * 1024};
constexpr size_t MAX_CLIENTS{4}, MAX_REPLIES{64};
StartResult Rejected(const QString& error) { return {StartStatus::Rejected, error, {}}; }
StartResult Unknown() { return {StartStatus::Unknown, QStringLiteral("Delivery to the running GUI could not be confirmed. Check that GUI before trying again."), {}}; }
std::vector<QString> ProducerKeys(const std::vector<Input>& inputs)
{
    std::vector<QString> result; result.reserve(inputs.size());
    for (const auto& input : inputs) result.push_back(input.producer_key);
    return result;
}
QString NormalizeProfile(const QString& directory)
{
    const auto input = fs::PathFromString(directory.toStdString());
    if (!input.is_absolute()) return {};
    std::error_code error;
    const fs::path normalized = fs::weakly_canonical(input, error);
    if (error) return {};
    if (fs::exists(normalized) && !fs::is_directory(normalized)) return {};
    return QString::fromStdString(fs::PathToString(normalized));
}
QJsonObject ReceiptJson(const Receipt& receipt, const QString& key)
{
    return {{"key", key}, {"delivery", QString::number(receipt.delivery)}, {"inspection", QString::number(receipt.inspection)},
        {"disposition", static_cast<int>(receipt.disposition)}};
}
std::optional<uint64_t> Identity(const QJsonValue& value)
{
    if (!value.isString()) return {};
    const auto text = value.toString(); bool valid{false}; const auto result = text.toULongLong(&valid);
    return valid && QString::number(result) == text ? std::optional<uint64_t>{result} : std::nullopt;
}
std::optional<Receipt> DecodeReceipt(const QJsonValue& value)
{
    if (!value.isObject()) return {};
    const auto object = value.toObject(); const auto delivery = Identity(object.value("delivery")), inspection = Identity(object.value("inspection"));
    const auto disposition = object.value("disposition").toInt(-1);
    if (!delivery || !*delivery || !inspection || disposition < 0 || object.value("disposition").toDouble(-1) != disposition || disposition > static_cast<int>(Disposition::DroppedOnStop) ||
        (disposition == static_cast<int>(Disposition::Accepted) && !*inspection)) return {};
    return Receipt{*delivery, *inspection, static_cast<Disposition>(disposition)};
}
QByteArray Frame(QJsonObject object)
{
    object.insert("version", 1);
    return QJsonDocument{object}.toJson(QJsonDocument::Compact) + '\n';
}
bool Version(const QJsonObject& object) { return object.value("version").isDouble() && object.value("version").toDouble() == 1; }
std::optional<std::vector<Receipt>> Known(const std::optional<QJsonObject>& response, const std::vector<Input>& inputs)
{
    if (!response || (response->value("status").toString() != "accepted" && response->value("status").toString() != "known")) return {};
    const auto receipts = response->value("receipts").toArray();
    if (receipts.size() != static_cast<qsizetype>(inputs.size())) return {};
    std::vector<Receipt> result;
    for (qsizetype index{0}; index < receipts.size(); ++index) {
        if (receipts[index].toObject().value("key").toString() != inputs[static_cast<size_t>(index)].producer_key) return {};
        const auto receipt = DecodeReceipt(receipts[index]); if (!receipt) return {}; result.push_back(*receipt);
    }
    return result;
}
bool SameUser(QLocalSocket& socket)
{
#ifdef Q_OS_WIN
    PSID owner{nullptr}; PSECURITY_DESCRIPTOR descriptor{nullptr};
    if (GetSecurityInfo(reinterpret_cast<HANDLE>(socket.socketDescriptor()), SE_KERNEL_OBJECT, OWNER_SECURITY_INFORMATION,
                        &owner, nullptr, nullptr, nullptr, &descriptor) != ERROR_SUCCESS) return false;
    HANDLE token{nullptr}; bool same{false};
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        DWORD size{0}; GetTokenInformation(token, TokenUser, nullptr, 0, &size);
        if (size && size <= 64 * 1024) {
            std::vector<unsigned char> bytes(size);
            if (GetTokenInformation(token, TokenUser, bytes.data(), size, &size))
                same = owner && IsValidSid(owner) && EqualSid(owner, reinterpret_cast<TOKEN_USER*>(bytes.data())->User.Sid);
        }
        CloseHandle(token);
    }
    LocalFree(descriptor); return same;
#else
    (void)socket;
    // The checked real directory is mode0700 and both directory/socket are
    // owned by this uid. Other users cannot replace or connect to this path.
    return true;
#endif
}
int Remaining(const Limits& limits, Limits::Time start, std::chrono::milliseconds budget, int cap)
{
    const auto now = limits.now();
    if (now < start) return 0;
    const auto deadline = start > Limits::Time::max() - budget ? Limits::Time::max() : start + budget;
    if (now >= deadline) return 0;
    return static_cast<int>(std::min<int64_t>(std::chrono::ceil<std::chrono::milliseconds>(deadline - now).count(), cap));
}
std::optional<QJsonObject> Exchange(const QString& endpoint, const QByteArray& frame, const Limits& limits, Limits::Time start,
                                  std::chrono::milliseconds reply_budget, const std::atomic_bool& stopping)
{
    const auto left = [&](int cap) { return Remaining(limits, start, limits.startup, cap); };
    if (stopping || frame.size() > MAX_FRAME || left(1) == 0) return {};
    QLocalSocket socket; socket.setReadBufferSize(MAX_FRAME + 1); socket.connectToServer(endpoint);
    if (!socket.waitForConnected(left(static_cast<int>(limits.connect.count()))) || !SameUser(socket)) return {};
    if (socket.write(frame) != frame.size()) return {};
    while (socket.bytesToWrite() && !stopping && left(1)) {
        if (socket.state() != QLocalSocket::ConnectedState) return {};
        (void)socket.waitForBytesWritten(left(static_cast<int>(limits.slice.count())));
    }
    const auto reply_start = limits.now();
    while (!socket.canReadLine() && socket.bytesAvailable() <= MAX_FRAME && !stopping && left(1)) {
        const auto wait = std::min(left(static_cast<int>(limits.slice.count())), Remaining(limits, reply_start, reply_budget, static_cast<int>(limits.slice.count())));
        if (wait <= 0 || socket.state() != QLocalSocket::ConnectedState) break;
        (void)socket.waitForReadyRead(wait);
    }
    if (stopping || !left(1) || !Remaining(limits, reply_start, reply_budget, 1) || !socket.canReadLine() || socket.bytesAvailable() > MAX_FRAME) return {};
    QJsonParseError error; const auto document = QJsonDocument::fromJson(socket.readLine(MAX_FRAME + 1), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject() || !Version(document.object())) return {};
    return document.object();
}
StartResult Forward(const QString& endpoint, std::vector<Input> inputs, const Limits& limits, const std::atomic_bool& stopping)
{
    if (inputs.empty()) return Rejected(QStringLiteral("Another GUI is already using this data directory."));
    for (const auto& input : inputs)
        if (QString::fromUtf8(input.producer_key.toUtf8()) != input.producer_key || QString::fromUtf8(input.value.toUtf8()) != input.value)
            return Rejected(QStringLiteral("The payment requests could not be encoded without changing their values."));
    // Local FileOpen keys can contain a reused PID. One Native-generated
    // service nonce namespaces the immutable batch, and both ACK and query use
    // this same mapping. Hashing keeps even maximal original keys bounded.
    const auto nonce = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto producer_keys = ProducerKeys(inputs);
    for (auto& input : inputs)
        input.producer_key = nonce + QLatin1Char(':') + QString::fromLatin1(QCryptographicHash::hash(input.producer_key.toUtf8(), QCryptographicHash::Sha256).toHex());
    const auto start = limits.now(); QJsonArray items, keys;
    for (const auto& input : inputs) {
        items.append(QJsonObject{{"key", input.producer_key}, {"kind", input.kind == Kind::File ? "file" : "text"}, {"value", input.value}});
        keys.append(input.producer_key);
    }
    const auto admission_frame = Frame({{"command", "admit"}, {"items", items}});
    if (admission_frame.size() > MAX_FRAME) return Rejected(QStringLiteral("The payment requests are too large for delivery."));
    auto response = Exchange(endpoint, admission_frame, limits, start, limits.acknowledge, stopping);
    if (response && response->value("status").toString() == "rejected")
        return Rejected(QStringLiteral("The running GUI could not accept these requests."));
    if (auto known = Known(response, inputs)) return {StartStatus::Forwarded, {}, std::move(*known), producer_keys};
    // Sending is attempted only once. Missing, malformed or evicted receipts
    // authorize only an original-key query, including across peer restart.
    response = Exchange(endpoint, Frame({{"command", "query"}, {"keys", keys}}), limits, start, limits.query, stopping);
    if (auto known = Known(response, inputs)) return {StartStatus::Forwarded, {}, std::move(*known), producer_keys};
    return Unknown();
}
struct Discovery {
    StartResult result;
    QString endpoint;
    bool missing{false};
    Discovery(StartResult value, QString target = {}, bool absent = false) : result(std::move(value)), endpoint(std::move(target)), missing(absent) {}
};
} // namespace

struct Service::Flags {
    std::atomic_bool admission_closed{false}, stopping{false}, started{false}, retired{false};
    std::mutex mutex;
    execution::TaskId retired_task;
    std::function<void()> completed_stop;
    bool probed{false}, probe_pending{false}, probe_terminal{false}; // Protected by mutex.
};
struct Service::State {
    struct Native;
    std::shared_ptr<execution::Scheduler> scope;
    std::shared_ptr<Owner> owner;
    Limits limits;
    execution::ResourceKey key;
    const std::shared_ptr<Flags> flags{std::make_shared<Flags>()};
    std::shared_ptr<execution::ResourceLease> native_lifetime;
    std::mutex mutex;
    Native* native{nullptr};
};

struct Service::State::Native final : QObject {
    struct Query { QJsonArray results; QStringList keys; size_t remaining{0}; bool unknown{false}; };
    struct Reply { std::optional<desktopintent::Admission> admission; std::optional<Receipt> receipt; };
    struct Cell {
        std::mutex mutex;
        QObject* receiver{nullptr}; std::function<void()> notify;
        std::optional<Reply> value;
        void complete(Reply reply) {
            std::lock_guard lock{mutex};
            if (!receiver || value) return;
            value = std::move(reply);
            // Native retirement clears receiver under the same lock before
            // destroying it. Queued functors are owned by that QObject.
            (void)QMetaObject::invokeMethod(receiver, notify, Qt::QueuedConnection);
        }
    };
    struct Pending { uint64_t connection; std::shared_ptr<Cell> cell; std::shared_ptr<Query> query{}; qsizetype index{0}; QStringList keys{}; };
    const std::shared_ptr<State> state;
    QLocalServer server;
    std::unique_ptr<QLockFile> instance_lock;
    struct Client { QLocalSocket* socket; Limits::Time admitted; };
    std::map<uint64_t, Client> clients;
    std::map<uint64_t, Pending> replies;
    uint64_t sequence{0};

    explicit Native(std::shared_ptr<State> value) : state(std::move(value)) {
        server.setSocketOptions(QLocalServer::UserAccessOption); server.setMaxPendingConnections(static_cast<int>(MAX_CLIENTS));
        connect(&server, &QLocalServer::newConnection, this, [this] { accept(); });
    }
    void clearReply(uint64_t id) {
        const auto found = replies.find(id); if (found == replies.end()) return;
        { std::lock_guard lock{found->second.cell->mutex}; found->second.cell->receiver = nullptr; found->second.cell->notify = {}; }
        replies.erase(found);
    }
    void disconnected(uint64_t id) {
        const auto found = clients.find(id); if (found == clients.end()) return;
        auto* socket = found->second.socket; clients.erase(found); socket->deleteLater();
        std::vector<uint64_t> expired;
        for (const auto& [key, pending] : replies) if (pending.connection == id) expired.push_back(key);
        for (const auto key : expired) clearReply(key);
    }
    void send(uint64_t id, QJsonObject reply) {
        const auto found = clients.find(id); if (found == clients.end()) return;
        auto* socket = found->second.socket;
        if (socket->state() != QLocalSocket::ConnectedState) return;
        (void)socket->write(Frame(std::move(reply))); socket->disconnectFromServer();
    }
    std::shared_ptr<Cell> reserve(uint64_t connection, std::shared_ptr<Query> query = {}, qsizetype index = 0, QStringList keys = {}) {
        if (replies.size() == MAX_REPLIES || sequence == std::numeric_limits<uint64_t>::max()) return {};
        const auto id = ++sequence; auto cell = std::make_shared<Cell>();
        cell->receiver = this; cell->notify = [this, id] { consume(id); };
        replies.emplace(id, Pending{connection, cell, std::move(query), index, std::move(keys)}); return cell;
    }
    void consume(uint64_t id) {
        const auto found = replies.find(id); if (found == replies.end()) return;
        auto pending = found->second; std::optional<Reply> reply;
        { std::lock_guard lock{pending.cell->mutex}; reply = std::move(pending.cell->value); }
        if (!reply) return;
        clearReply(id);
        if (pending.query) {
            pending.query->results[pending.index] = reply->receipt ? QJsonValue{ReceiptJson(*reply->receipt, pending.query->keys[pending.index])} : QJsonValue{QJsonValue::Null};
            pending.query->unknown |= !reply->receipt;
            if (--pending.query->remaining == 0)
                send(pending.connection, {{"status", pending.query->unknown ? "unknown" : "known"}, {"receipts", pending.query->results}});
        } else if (reply->admission) {
            QJsonArray receipts;
            for (size_t index{0}; index < reply->admission->receipts.size(); ++index)
                receipts.append(ReceiptJson(reply->admission->receipts[index], pending.keys[static_cast<qsizetype>(index)]));
            send(pending.connection, {{"status", *reply->admission ? "accepted" : "rejected"}, {"receipts", receipts}});
        }
    }
    void receive(uint64_t id) {
        const auto found = clients.find(id); if (found == clients.end()) return;
        auto* socket = found->second.socket;
        if (socket->property("handled").toBool()) return;
        if (state->flags->admission_closed || !Remaining(state->limits, found->second.admitted, state->limits.client, 1) || socket->bytesAvailable() > MAX_FRAME) { socket->abort(); return; }
        if (!socket->canReadLine()) return;
        socket->setProperty("handled", true);
        QJsonParseError error; const auto document = QJsonDocument::fromJson(socket->readLine(MAX_FRAME + 1), &error);
        if (error.error != QJsonParseError::NoError || !document.isObject()) { socket->abort(); return; }
        const auto object = document.object();
        if (!Version(object)) { socket->abort(); return; }
        const auto command = object.value("command").toString();
        if (command == "admit") {
            const auto items = object.value("items").toArray(); std::vector<Input> inputs;
            if (items.empty() || items.size() > static_cast<qsizetype>(Queue::MAX_PENDING)) { socket->abort(); return; }
            for (const auto item : items) {
                const auto entry = item.toObject(); const auto kind = entry.value("kind").toString();
                if (!entry.value("key").isString() || !entry.value("value").isString() || (kind != "text" && kind != "file")) { socket->abort(); return; }
                inputs.push_back({entry.value("key").toString(), kind == "file" ? Kind::File : Kind::Text, entry.value("value").toString(), Origin::Forwarded});
            }
            QStringList keys; for (const auto& input : inputs) keys.push_back(input.producer_key);
            const auto cell = reserve(id, {}, 0, std::move(keys)); if (!cell) { send(id, {{"status", "rejected"}}); return; }
            const auto admitted = state->owner->admit(std::move(inputs), [cell](auto result) { cell->complete({std::move(result), {}}); });
            if (!admitted) cell->complete({desktopintent::Admission{Error::Closed, {}}, {}});
        } else if (command == "query") {
            const auto keys = object.value("keys").toArray();
            if (keys.empty() || keys.size() > static_cast<qsizetype>(Queue::MAX_PENDING) || replies.size() + static_cast<size_t>(keys.size()) > MAX_REPLIES) { socket->abort(); return; }
            for (const auto key : keys) if (!key.isString() || key.toString().isEmpty() || key.toString().size() > Queue::MAX_KEY_CHARS) { socket->abort(); return; }
            auto query = std::make_shared<Query>(); query->remaining = static_cast<size_t>(keys.size());
            for (qsizetype index{0}; index < keys.size(); ++index) { query->results.append(QJsonValue{QJsonValue::Null}); query->keys.push_back(keys[index].toString()); }
            for (qsizetype index{0}; index < keys.size(); ++index) {
                const auto cell = reserve(id, query, index); if (!cell) { socket->abort(); return; }
                const auto admitted = state->owner->lookup(keys[index].toString(), [cell](auto result) { cell->complete({{}, std::move(result)}); });
                if (!admitted) cell->complete({{}, {}});
            }
        } else socket->abort();
    }
    void accept() {
        while (auto* socket = server.nextPendingConnection()) {
            socket->setParent(this);
            if (state->flags->admission_closed || clients.size() == MAX_CLIENTS || sequence == std::numeric_limits<uint64_t>::max()) { socket->deleteLater(); continue; }
            const auto id = ++sequence; clients.emplace(id, Client{socket, state->limits.now()}); socket->setReadBufferSize(MAX_FRAME + 1);
            connect(socket, &QLocalSocket::disconnected, this, [this, id] { disconnected(id); });
            connect(socket, &QLocalSocket::readyRead, this, [this, id] { ingress(id); });
            auto* deadline = new QTimer{socket}; deadline->setInterval(static_cast<int>(state->limits.slice.count()));
            connect(deadline, &QTimer::timeout, this, [this, id] {
                const auto found = clients.find(id);
                if (found == clients.end()) return;
                try { if (!Remaining(state->limits, found->second.admitted, state->limits.client, 1)) found->second.socket->abort(); }
                catch (...) { found->second.socket->abort(); }
            });
            deadline->start(); ingress(id);
        }
    }
    void ingress(uint64_t id) {
        try { receive(id); }
        catch (...) {
            // Any already admitted queue mutation keeps its original receipt.
            // An unacknowledged caller can query; no exception becomes replay.
            const auto found = clients.find(id); if (found != clients.end()) found->second.socket->abort();
        }
    }
    void retire() {
        server.close();
        while (!replies.empty()) clearReply(replies.begin()->first);
        const auto sockets = std::exchange(clients, {});
        for (const auto& [id, client] : sockets) { client.socket->abort(); delete client.socket; }
        instance_lock.reset();
        std::shared_ptr<execution::ResourceLease> lifetime;
        { std::lock_guard lock{state->mutex}; state->native = nullptr; lifetime = std::move(state->native_lifetime); }
        delete this;
        lifetime.reset(); // Only now may the original Native event loop drain.
    }
    Discovery initialize(const QString& directory, const QString& network, bool existing_only = false) {
        const auto canonical = NormalizeProfile(directory);
        if (canonical.isEmpty()) return Rejected(QStringLiteral("Could not resolve the payment-request data directory."));
        QString private_directory;
#ifndef Q_OS_WIN
        private_directory = QDir{QStringLiteral("/tmp")}.filePath(QStringLiteral("qml-intent-%1").arg(getuid()));
        const auto encoded = QFile::encodeName(private_directory);
        if (!existing_only) (void)mkdir(encoded.constData(), 0700);
        struct stat info{};
        if (lstat(encoded.constData(), &info)) {
            if (existing_only && errno == ENOENT) return {StartResult{}, {}, true};
            return Rejected(QStringLiteral("The private payment-delivery directory is unavailable or unsafe."));
        }
        if (!S_ISDIR(info.st_mode) || info.st_uid != getuid() || (info.st_mode & 0077))
            return Rejected(QStringLiteral("The private payment-delivery directory is unavailable or unsafe."));
        private_directory = QFileInfo{private_directory}.canonicalFilePath();
#else
        private_directory = QDir::tempPath();
#endif
        const QString identity = canonical + QLatin1Char('\n') + network;
        const auto digest = QCryptographicHash::hash(identity.toUtf8(), QCryptographicHash::Sha256).toHex().first(24);
#ifndef Q_OS_WIN
        const auto endpoint = QDir{private_directory}.filePath(QString::fromLatin1(digest));
        if (QFile::encodeName(endpoint).size() > 100) return Rejected(QStringLiteral("The payment-delivery path is too long."));
#else
        const QString endpoint = QStringLiteral("qml-intent-") + QString::fromLatin1(digest);
#endif
        if (existing_only) {
            // Discovery grants no listener/lock authority and mutates no local
            // queue. A known absent peer leaves normal final-profile startup
            // available; only a connected same-user peer permits forwarding.
#ifndef Q_OS_WIN
            const auto path = QFile::encodeName(endpoint); struct stat peer{};
            if (lstat(path.constData(), &peer)) {
                if (errno == ENOENT) return {StartResult{}, {}, true};
                return Rejected(QStringLiteral("The payment-delivery endpoint could not be inspected."));
            }
            if (!S_ISSOCK(peer.st_mode) || peer.st_uid != getuid())
                return Rejected(QStringLiteral("The running GUI's payment-delivery endpoint is unsafe."));
#endif
            if (state->flags->admission_closed) return Rejected(QStringLiteral("Payment delivery is closing."));
            if (NormalizeProfile(directory) != canonical) return Rejected(QStringLiteral("The payment-request data directory changed during lookup."));
            const auto began = state->limits.now();
            const auto remaining = Remaining(state->limits, began, state->limits.startup, static_cast<int>(state->limits.connect.count()));
            if (!remaining) return Rejected(QStringLiteral("The payment-delivery lookup expired."));
            QLocalSocket probe; probe.connectToServer(endpoint);
            const bool connected = probe.waitForConnected(remaining);
            if (!connected) {
                if (probe.error() == QLocalSocket::ServerNotFoundError || probe.error() == QLocalSocket::ConnectionRefusedError)
                    return {StartResult{}, {}, true};
                return Rejected(QStringLiteral("The existing payment-delivery endpoint could not be contacted."));
            }
            if (state->flags->admission_closed || !Remaining(state->limits, began, state->limits.startup, 1))
                return Rejected(QStringLiteral("The payment-delivery lookup expired or is closing."));
            if (!SameUser(probe)) return Rejected(QStringLiteral("The running GUI's payment-delivery endpoint is unsafe."));
            return {StartResult{}, endpoint};
        }
        instance_lock = std::make_unique<QLockFile>(QDir{private_directory}.filePath(QString::fromLatin1(digest) + QStringLiteral(".lock")));
        instance_lock->setStaleLockTime(0);
        if (instance_lock->tryLock(0)) {
            QLocalSocket probe; probe.connectToServer(endpoint); const bool live = probe.waitForConnected(200);
            bool reclaimable = !live && (probe.error() == QLocalSocket::ServerNotFoundError || probe.error() == QLocalSocket::ConnectionRefusedError);
#ifndef Q_OS_WIN
            const auto path = QFile::encodeName(endpoint); struct stat socket_info{};
            if (lstat(path.constData(), &socket_info) == 0) {
                reclaimable &= S_ISSOCK(socket_info.st_mode) && socket_info.st_uid == getuid();
                if (reclaimable) reclaimable = QLocalServer::removeServer(endpoint);
            }
#endif
            if (NormalizeProfile(directory) != canonical) return Rejected(QStringLiteral("The payment-request data directory changed during startup."));
            if (reclaimable && !state->flags->admission_closed && server.listen(endpoint)) return StartResult{StartStatus::Primary, {}, {}};
        }
        instance_lock.reset();
        if (NormalizeProfile(directory) != canonical) return Rejected(QStringLiteral("The payment-request data directory changed during startup."));
#ifndef Q_OS_WIN
        const auto path = QFile::encodeName(endpoint); struct stat peer{};
        if (lstat(path.constData(), &peer) || !S_ISSOCK(peer.st_mode) || peer.st_uid != getuid())
            return Rejected(QStringLiteral("The running GUI's payment-delivery endpoint is unavailable or unsafe."));
#endif
        if (state->flags->admission_closed) return Rejected(QStringLiteral("Payment delivery is closing."));
        return {StartResult{}, endpoint};
    }
};

Service::Service(std::shared_ptr<State> state) : m_state(state), m_flags(state->flags) {}
Service::~Service() { stop(); }
std::shared_ptr<Service> Service::Create(std::shared_ptr<execution::Scheduler> scheduler, std::shared_ptr<Owner> owner, Limits limits)
{
    const auto maximum = std::chrono::seconds{30};
    if (!scheduler || !owner || !limits.now || limits.startup <= std::chrono::milliseconds::zero() || limits.startup > maximum ||
        limits.connect <= std::chrono::milliseconds::zero() || limits.connect > limits.startup ||
        limits.acknowledge <= std::chrono::milliseconds::zero() || limits.acknowledge > limits.startup ||
        limits.query <= std::chrono::milliseconds::zero() || limits.query > limits.startup ||
        limits.client <= std::chrono::milliseconds::zero() || limits.client > maximum ||
        limits.slice <= std::chrono::milliseconds::zero() || limits.slice > std::chrono::milliseconds{100}) return {};
    auto scope = scheduler->openActionScope(); if (!scope) return {};
    auto state = execution::RetainValues<State>(scope, [] { return std::make_unique<State>(); }); if (!state) return {};
    state->scope = scope; state->owner = std::move(owner); state->limits = std::move(limits); state->key = scope->allocateKey();
    const auto retired = execution::Submit(*scope, [] {}, [state] {
        std::function<void()> done;
        { std::lock_guard lock{state->mutex}; state->owner.reset(); }
        { std::lock_guard lock{state->flags->mutex}; state->flags->retired = true; done = std::move(state->flags->completed_stop); }
        if (done) done();
    }, {}, {execution::Lane::Cleanup, state->key}, true);
    auto borrowed = retired ? scope->borrow(state->key) : execution::BorrowAdmission{{}, retired.rejection};
    if (!borrowed) { if (retired) scope->activateCleanup(retired.id); return {}; }
    state->flags->retired_task = retired.id;
    state->native_lifetime = std::make_shared<execution::ResourceLease>(std::move(*borrowed.lease));
    return std::shared_ptr<Service>{new Service(std::move(state))};
}
execution::Admission Service::start(QString directory, QString network, std::vector<Input> inputs, std::function<void(StartResult)> completed)
{
    return begin(std::move(directory), std::move(network), std::move(inputs), false, std::move(completed), {});
}
execution::Admission Service::probeExisting(QString directory, QString network, const std::vector<Input>& inputs, std::function<void(ProbeResult)> completed)
{
    if (inputs.size() > Queue::MAX_PENDING) return {{}, execution::Rejection::Invalid};
    const auto callback = std::move(completed);
    const auto transfer = [callback](StartResult result) {
        ProbeStatus status{ProbeStatus::Rejected};
        switch (result.status) {
        case StartStatus::Forwarded: status = ProbeStatus::Forwarded; break;
        case StartStatus::Rejected: break;
        case StartStatus::Unknown: status = ProbeStatus::Unknown; break;
        case StartStatus::Primary: result = Rejected(QStringLiteral("An existing-instance lookup cannot become primary.")); break;
        }
        if (callback) callback({status, std::move(result)});
    };
    const auto missing = [callback] { if (callback) callback({ProbeStatus::Missing, {}}); };
    return begin(std::move(directory), std::move(network), inputs, true, transfer, missing);
}
execution::Admission Service::begin(QString directory, QString network, std::vector<Input> inputs, bool existing_only,
                                    std::function<void(StartResult)> completed, std::function<void()> missing)
{
    const auto state = m_state.load();
    if (!state) return {{}, execution::Rejection::Closed};
    { std::lock_guard lock{m_flags->mutex};
      if (m_flags->admission_closed || m_flags->started || m_flags->probe_pending || m_flags->probe_terminal || (existing_only && m_flags->probed))
          return {{}, execution::Rejection::Closed};
      if (existing_only) { m_flags->probed = true; m_flags->probe_pending = true; }
      else m_flags->started = true;
    }
    const auto reject = [flags = m_flags, existing_only](execution::Rejection reason) {
        if (existing_only) { std::lock_guard lock{flags->mutex}; flags->probe_pending = false; flags->probe_terminal = true; }
        return execution::Admission{{}, reason};
    };
    if (directory.size() > 4096 || QString::fromUtf8(directory.toUtf8()) != directory || network.isEmpty() || network.size() > 128 || directory.contains(QChar{u'\0'}) || network.contains(QChar{u'\0'}) || network.contains(QChar{u'\n'}) || inputs.size() > Queue::MAX_PENDING)
        return reject(execution::Rejection::Invalid);
    for (const auto& input : inputs) {
        if (input.producer_key.size() > Queue::MAX_KEY_CHARS || input.value.size() > Queue::MAX_INPUT_CHARS) return reject(execution::Rejection::Capacity);
    }
    auto held = state->scope->borrow(state->key); if (!held) return reject(held.rejection);
    auto lifetime = std::make_shared<execution::ResourceLease>(std::move(*held.lease));
    std::shared_ptr<Owner> owner;
    { std::lock_guard lock{state->mutex}; owner = state->owner; }
    const auto payload = execution::RetainValues<std::vector<Input>>(state->scope, [&] { return std::make_unique<std::vector<Input>>(std::move(inputs)); });
    if (!payload) return reject(execution::Rejection::Closed);
    const auto claimed = std::make_shared<std::atomic_bool>(false);
    const auto finish = [done = std::move(completed), claimed, flags = m_flags, existing_only](StartResult result) {
        if (claimed->exchange(true)) return;
        if (existing_only) { std::lock_guard lock{flags->mutex}; flags->probe_pending = false; flags->probe_terminal = true; }
        if (done) done(std::move(result));
    };
    const auto submitted = execution::Submit(*state->scope, [state, directory = std::move(directory), network = std::move(network), payload, lifetime, existing_only] {
        if (state->flags->admission_closed || (QCoreApplication::instance() && QThread::currentThread() == QCoreApplication::instance()->thread()))
            return Discovery{Rejected(QStringLiteral("Payment delivery could not start on its background worker."))};
        auto native = std::make_unique<State::Native>(state);
        auto discovery = native->initialize(directory, network, existing_only);
        if (discovery.result.status == StartStatus::Primary) {
            std::lock_guard lock{state->mutex};
            if (!state->flags->stopping) state->native = native.release();
            else discovery.result = Rejected(QStringLiteral("Payment delivery is closing."));
        }
        return discovery;
    }, [state, owner, payload, finish, lifetime, claimed, missing = std::move(missing)](Discovery discovery) {
        if (discovery.missing) {
            if (state->flags->admission_closed) { finish(Rejected(QStringLiteral("Payment delivery is closing."))); return; }
            if (!claimed->exchange(true)) {
                { std::lock_guard lock{state->flags->mutex}; state->flags->probe_pending = false; }
                if (missing) missing();
            }
            return;
        }
        if (!discovery.endpoint.isEmpty()) {
            if (state->flags->admission_closed) { finish(Rejected(QStringLiteral("Payment delivery is closing."))); return; }
            try {
                const auto admitted = owner->prepareForwarding(*payload, [state, endpoint = std::move(discovery.endpoint), finish, lifetime](ForwardingSnapshot snapshot) {
                    if (snapshot.error != Error::None || state->flags->admission_closed) {
                        finish(Rejected(QStringLiteral("The GUI could not transfer its pending requests."))); return;
                    }
                    const auto forward_payload = execution::RetainValues<std::vector<Input>>(state->scope, [&] { return std::make_unique<std::vector<Input>>(std::move(snapshot.inputs)); });
                    if (!forward_payload) { finish(Rejected(QStringLiteral("The GUI could not retain its pending requests."))); return; }
                    const auto forwarded = execution::Submit(*state->scope, [state, endpoint, forward_payload, lifetime] {
                        if (state->flags->stopping) return Rejected(QStringLiteral("Payment delivery is closing."));
                        // Once a frame may have left this process, an exception
                        // is an unknown outcome, never permission to replay.
                        try { return Forward(endpoint, *forward_payload, state->limits, state->flags->stopping); }
                        catch (...) { return Unknown(); }
                    }, [finish, lifetime, forward_payload](StartResult result) {
                        if (result.producer_keys.empty()) result.producer_keys = ProducerKeys(*forward_payload);
                        finish(std::move(result));
                    }, [finish, lifetime, forward_payload](std::exception_ptr) {
                        auto result = Unknown(); result.producer_keys = ProducerKeys(*forward_payload);
                        finish(std::move(result));
                    },
                        {execution::Lane::Core, state->key});
                    if (!forwarded) finish(Rejected(QStringLiteral("The GUI could not start payment delivery.")));
                });
                if (!admitted) finish(Rejected(QStringLiteral("The GUI could not retain its pending requests.")));
            } catch (...) { finish(Rejected(QStringLiteral("The GUI could not prepare payment delivery."))); }
            return;
        }
        auto result = std::move(discovery.result);
        if (result.status != StartStatus::Primary || payload->empty()) { finish(std::move(result)); return; }
        if (state->flags->admission_closed) { finish(Rejected(QStringLiteral("Payment delivery is closing."))); return; }
        try {
            const auto admitted = owner->admit(*payload, [finish, payload, result = std::move(result)](auto admission) mutable {
                if (!admission) result = Rejected(QStringLiteral("The GUI could not accept these requests."));
                else { result.receipts = std::move(admission.receipts); result.producer_keys = ProducerKeys(*payload); }
                finish(std::move(result));
            });
            if (!admitted) finish(Rejected(QStringLiteral("The GUI could not accept these requests.")));
        } catch (...) { finish(Unknown()); }
    }, [finish](std::exception_ptr) { finish(Rejected(QStringLiteral("Payment delivery could not start."))); },
        {execution::Lane::Core, state->key});
    return submitted ? submitted : reject(submitted.rejection);
}
void Service::close() { m_flags->admission_closed = true; }
execution::Admission Service::stop(std::function<void()> completed)
{
    close();
    { std::lock_guard lock{m_flags->mutex};
      if (completed) {
          if (m_flags->retired) return {{}, execution::Rejection::Closed};
          if (m_flags->completed_stop) return {{}, execution::Rejection::Capacity};
          m_flags->completed_stop = std::move(completed);
      }
    }
    const auto state = m_state.exchange({});
    if (state && !m_flags->stopping.exchange(true)) {
        std::shared_ptr<Owner> owner;
        { std::lock_guard lock{state->mutex};
          owner = state->owner;
          if (state->native) {
              auto* native = state->native;
              const bool posted = QMetaObject::invokeMethod(native, [native] { native->retire(); }, Qt::QueuedConnection);
              if (!posted) std::terminate();
          } else state->native_lifetime.reset();
        }
        if (owner) owner->stop();
        state->scope->activateCleanup(m_flags->retired_task);
    }
    return {m_flags->retired_task, execution::Rejection::None};
}
bool Service::retired() const { return m_flags->retired; }
} // namespace desktopintent::transport
