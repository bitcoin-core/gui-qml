// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/models/peercommandmodel.h>
#include <qml/guipresentation.h>
#include <QPointer>
#include <QQmlEngine>
#include <QUuid>
#include <limits>

namespace {
execution::SnapshotServices Bound(execution::SnapshotServices services)
{
    if (!services.post) services.post = presentation::QueuedPost();
    return services;
}
}
PeerCommandModel::PeerCommandModel(interfaces::Node& node, execution::SnapshotServices services, std::function<bool()> ready, QObject* parent)
    : QObject(parent), m_services(Bound(std::move(services))), m_ready(std::move(ready))
{
    const QPointer<PeerCommandModel> observer{this};
    m_store = std::make_shared<peercommands::Store>(node, m_services, [post = m_services.post, observer] {
        post([observer] { if (observer) observer->refresh(); });
    });
}
PeerCommandModel::~PeerCommandModel() { m_store->close(); }
peercommands::Request PeerCommandModel::request(PeerConnection target, peercommands::Kind kind, int64_t duration, bool exact)
{
    if (m_closed || m_ticket == std::numeric_limits<quint64>::max()) return {};
    return {QUuid::createUuid().toString(QUuid::WithoutBraces), ++m_ticket, std::move(target), kind, duration, exact};
}
PeerCommandOperation* PeerCommandModel::prepare(const PeerConnection& target, peercommands::Kind kind, int64_t duration)
{
    auto original = request(target, kind, duration, true);
    if (!original.ticket) return nullptr;
    const QPointer<PeerCommandModel> observer{this};
    auto* operation = new PeerCommandOperation(m_store, std::move(original), m_services,
        [observer] { return observer && observer->canStart(); }, [observer] { if (observer) observer->admitted(); },
        [observer] { return observer && !observer->m_closed; });
    QQmlEngine::setObjectOwnership(operation, QQmlEngine::JavaScriptOwnership);
    return operation;
}
QMetaObject::Connection PeerCommandModel::observe(QObject* receiver, std::function<void()> changed)
{
    return connect(this, &PeerCommandModel::changed, receiver, std::move(changed));
}
void PeerCommandModel::admitted() { m_pending = true; if (!m_closed) Q_EMIT changed(); }
void PeerCommandModel::closeAdmission()
{
    if (m_closed) return;
    m_closed = true; m_store->close(); Q_EMIT changed();
}
void PeerCommandModel::refresh()
{
    const auto snapshot = m_store->snapshot();
    m_pending = snapshot.pending;
    if (snapshot.latest) m_result = PeerCommandResult::Project(*snapshot.latest);
    const auto completed = snapshot.mutation_revision > m_mutation_revision ? snapshot.mutation : std::nullopt;
    m_mutation_revision = snapshot.mutation_revision;
    if (m_closed) return;
    const QPointer<PeerCommandModel> guard{this};
    Q_EMIT changed();
    // A reentrant successor cannot erase the original terminal notification.
    if (guard && completed) Q_EMIT finished(completed->request.ticket, static_cast<PeerCommandOperation::Outcome>(completed->outcome), completed->error);
}
quint64 PeerCommandModel::legacy(peercommands::Request original)
{
    if (!canStart() || !original.ticket) return 0;
    const QPointer<PeerCommandModel> guard{this};
    const auto store = m_store;
    auto operation = std::make_shared<peercommands::Operation>(original);
    const auto admission = store->submit(operation);
    if (!admission) return 0;
    if (guard) guard->admitted();
    return original.ticket;
}
quint64 PeerCommandModel::requestDisconnectPeer(qint64 id)
{
    if (id < 0) return 0;
    PeerConnection target; target.node_id = id;
    return legacy(request(std::move(target), peercommands::Kind::Disconnect, 0, false));
}
quint64 PeerCommandModel::requestBanPeer(const QString& raw_address, int64_t duration)
{
    if (raw_address.isEmpty() || raw_address.size() > 512 || duration <= 0) return 0;
    PeerConnection target; target.raw_address = raw_address; target.display_address = raw_address;
    return legacy(request(std::move(target), peercommands::Kind::Ban, duration, false));
}
