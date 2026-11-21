// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/models/requestsharingmodel.h>
#include <qml/guipresentation.h>

#include <QQmlEngine>
#include <QUuid>

#include <limits>

namespace {
using Scope = std::shared_ptr<execution::Scheduler>;
bool Terminal(requestshare::Phase phase) { return phase >= requestshare::Phase::Completed; }
bool Current(const requestshare::Source& source) { try { return source.current && source.current(); } catch (...) { return false; } }
}
struct ShareOperationModel::Cell {
    std::mutex mutex;
    requestshare::Phase phase{requestshare::Phase::Checking};
    requestshare::Error error{requestshare::Error::None};
    ShareResult result;
    std::shared_ptr<requestshare::Grant> grant;
    std::atomic_bool cancelled{false};
    std::function<void()> notify;
    void finish(requestshare::Delivery outcome, requestshare::Error failure, bool performed = false)
    {
        {
            std::lock_guard lock{mutex};
            if (result.present) return;
            result.present = true;
            result.admitted = grant && grant->admitted();
            result.performed = performed;
            result.outcomeKnown = outcome != requestshare::Delivery::Unknown;
            error = failure;
            if (outcome == requestshare::Delivery::Unknown) {
                phase = requestshare::Phase::OutcomeUnknown;
            } else if (performed) {
                phase = requestshare::Phase::Completed;
            } else if (failure == requestshare::Error::Cancelled) {
                phase = requestshare::Phase::Cancelled;
            } else {
                phase = requestshare::Phase::Failed;
            }
        }
        notify();
    }
};
struct ShareOperationModel::Work : std::enable_shared_from_this<Work> {
    requestshare::Kind kind;
    int64_t id;
    requestshare::Services services;
    std::shared_ptr<requestshare::View> view;
    uint64_t token;
    uint64_t input_revision;
    std::shared_ptr<Cell> cell;
    std::optional<requestshare::Source> expected;
    bool stopped()
    {
        { std::lock_guard lock{cell->mutex}; if (cell->result.present) return true; }
        if (!cell->cancelled.load()) return false;
        cell->finish(requestshare::Delivery::Failed, requestshare::Error::Cancelled); return true;
    }
    void fail(requestshare::Error error) { cell->finish(requestshare::Delivery::Failed, error); }
    void prepared(requestshare::Source source, QImage image, Scope original)
    {
        if (stopped()) return;
        const auto grant = std::make_shared<requestshare::Grant>(source, view, token, input_revision);
        {
            std::lock_guard lock{cell->mutex};
            cell->grant = grant;
            cell->phase = requestshare::Phase::Authorizing;
            cell->result.requestId = QString::number(source.request_id);
            cell->result.payloadRevision = QString::number(source.record_revision);
        }
        cell->notify();
        // This handoff owns only bounded values and cached permission. In
        // particular it does not retain Work/read ports or an action scope.
        const auto result = cell;
        const auto effect = services.deliver;
        const auto action = kind;
        const auto post = services.execution.post;
        original.reset();
        post([result, grant, action, payload = std::move(source.payload), image = std::move(image), effect] {
            { std::lock_guard lock{result->mutex}; if (result->result.present) return; }
            if (result->cancelled.load()) grant->revoke();
            if (!grant->admit()) {
                result->finish(requestshare::Delivery::Failed,
                    result->cancelled.load() ? requestshare::Error::Cancelled : requestshare::Error::Changed);
                return;
            }
            { std::lock_guard lock{result->mutex}; result->phase = requestshare::Phase::Delivering; result->result.admitted = true; }
            result->notify();
            try {
                const auto outcome = effect(action, payload, image);
                result->finish(outcome, outcome == requestshare::Delivery::Accepted ? requestshare::Error::None
                    : outcome == requestshare::Delivery::Unknown ? requestshare::Error::Unknown : requestshare::Error::Native,
                    outcome == requestshare::Delivery::Accepted);
            } catch (...) {
                result->finish(requestshare::Delivery::Unknown, requestshare::Error::Unknown);
            }
        });
    }
    void begin(Scope scope)
    {
        if (stopped()) return;
        if (!view->active(token, input_revision)) { fail(requestshare::Error::Inactive); return; }
        if (expected && (expected->error != requestshare::Error::None || !expected->current || !expected->current())) {
            fail(expected->error == requestshare::Error::None ? requestshare::Error::Changed : expected->error); return;
        }
        if (!services.read || !services.deliver || (kind == requestshare::Kind::Qr && !services.render)) { fail(requestshare::Error::Rejected); return; }
        const auto self = shared_from_this();
        const auto admission = services.read(id, [self, scope](requestshare::Source source) mutable {
            if (self->stopped()) return;
            if (source.error != requestshare::Error::None) { self->fail(source.error); return; }
            if (!source.opening || source.request_id != self->id || source.record_revision == 0) { self->fail(requestshare::Error::Changed); return; }
            if (self->expected && (self->expected->opening != source.opening || self->expected->request_id != source.request_id ||
                self->expected->record_revision != source.record_revision || self->expected->payload != source.payload)) { self->fail(requestshare::Error::Changed); return; }
            if (source.payload.isEmpty() || !source.current) { self->fail(requestshare::Error::Missing); return; }
            if (source.payload.size() > 16'384) { self->fail(requestshare::Error::Oversize); return; }
            if (self->kind == requestshare::Kind::Uri) { self->prepared(std::move(source), {}, std::move(scope)); return; }
            { std::lock_guard lock{self->cell->mutex}; self->cell->phase = requestshare::Phase::Preparing; }
            self->cell->notify();
            const auto payload = source.payload;
            const auto admission = execution::Submit(*scope,
                [render = self->services.render, payload] { return render(payload); },
                [self, scope, source = std::move(source)](QImage image) mutable {
                    if (image.isNull() || image.width() <= 1 || image.width() != image.height() || image.width() > 736) {
                        self->fail(requestshare::Error::QrEncoding); return;
                    }
                    self->prepared(std::move(source), std::move(image), std::move(scope));
                }, [self](std::exception_ptr) { self->fail(requestshare::Error::QrEncoding); },
                {execution::Lane::Domain});
            if (!admission) self->fail(requestshare::Error::Rejected);
        });
        if (!admission) fail(requestshare::Error::Rejected);
    }
};
ShareOperationModel::ShareOperationModel(requestshare::Kind kind, int64_t request_id, requestshare::Services services,
    std::shared_ptr<requestshare::View> view, std::optional<requestshare::Source> expected, QObject* parent)
    : QObject(parent), m_id(QUuid::createUuid().toString(QUuid::WithoutBraces)), m_cell(std::make_shared<Cell>())
{
    services.execution.post = presentation::ReliablePost(std::move(services.execution.post));
    m_cell->result.operationId = m_id;
    const QPointer<ShareOperationModel> observer{this};
    m_cell->notify = [post = services.execution.post, observer] { post([observer] { if (observer) Q_EMIT observer->changed(); }); };
    const auto work = std::make_shared<Work>();
    work->kind = kind; work->id = request_id; work->services = std::move(services); work->view = std::move(view);
    work->token = work->view->token.load(); work->cell = m_cell;
    work->input_revision = work->view->input_epoch ? work->view->input_epoch->value.load() : 0;
    work->expected = std::move(expected);
    const auto scope = work->services.execution.scheduler ? work->services.execution.scheduler->openActionScope() : nullptr;
    if (!scope) { work->fail(requestshare::Error::Rejected); return; }
    const auto admission = execution::Submit(*scope, [] { return true; },
        [work, scope](bool) { work->begin(scope); }, [work](std::exception_ptr) { work->fail(requestshare::Error::Rejected); }, {execution::Lane::Domain});
    if (!admission) work->fail(requestshare::Error::Rejected);
}
ShareOperationModel::~ShareOperationModel() { cancel(); }
ShareOperationModel::Phase ShareOperationModel::phase() const { std::lock_guard lock{m_cell->mutex}; return static_cast<Phase>(m_cell->phase); }
bool ShareOperationModel::busy() const { return phase() < Completed; }
bool ShareOperationModel::outcomeKnown() const { std::lock_guard lock{m_cell->mutex}; return m_cell->result.outcomeKnown; }
bool ShareOperationModel::canCancel() const { return busy(); }
SharingError::Code ShareOperationModel::error() const { std::lock_guard lock{m_cell->mutex}; return m_cell->error; }
QString ShareOperationModel::errorText() const { return requestshare::ErrorText(error()); }
ShareResult ShareOperationModel::result() const { std::lock_guard lock{m_cell->mutex}; return m_cell->result; }
void ShareOperationModel::cancel()
{
    {
        std::lock_guard lock{m_cell->mutex};
        if (Terminal(m_cell->phase) || m_cell->cancelled.exchange(true)) return;
        if (m_cell->grant) m_cell->grant->revoke();
        m_cell->phase = requestshare::Phase::Cancelling;
    }
    Q_EMIT changed();
}

struct RequestSharingModel::Cache {
    requestshare::Source source;
    QImage image;
    requestshare::Error qr_error{requestshare::Error::None};
};
RequestSharingModel::RequestSharingModel(int64_t id, requestshare::Services services, std::shared_ptr<std::atomic_bool> input, QObject* parent, std::shared_ptr<SourceEpoch> input_epoch)
    : QObject(parent), m_request_id(id), m_services(std::move(services)), m_view(std::make_shared<requestshare::View>())
{
    if (parent) m_gui_engine = qmlEngine(parent);
    m_services.execution.post = presentation::ReliablePost(std::move(m_services.execution.post));
    m_view->input = std::move(input);
    m_view->input_epoch = std::move(input_epoch);
}
RequestSharingModel::~RequestSharingModel() { m_view->close(); }
QString RequestSharingModel::payloadRevision() const { return m_cache ? QString::number(m_cache->source.record_revision) : QString{}; }
RequestSharingModel::SharingInterest RequestSharingModel::phase() const { return static_cast<SharingInterest>(m_view->interest.load()); }
bool RequestSharingModel::active() const { return phase() == Active; }
SharingError::Code RequestSharingModel::error() const
{
    if (phase() == Closed) return requestshare::Error::Closed;
    if (!m_view->active(m_view->token.load())) return requestshare::Error::Inactive;
    if (!m_cache) return requestshare::Error::Rejected;
    if (m_cache->source.error != requestshare::Error::None) return m_cache->source.error;
    if (!Current(m_cache->source)) return requestshare::Error::Changed;
    return m_cache->qr_error;
}
bool RequestSharingModel::canCopyUri() const
{
    return m_cache && m_view->active(m_view->token.load()) && m_cache->source.error == requestshare::Error::None &&
        !m_cache->source.payload.isEmpty() && Current(m_cache->source);
}
bool RequestSharingModel::canCopyQr() const { return canCopyUri() && m_cache->qr_error == requestshare::Error::None && !m_cache->image.isNull(); }
QString RequestSharingModel::disabledReason() const { return requestshare::ErrorText(error()); }
QString RequestSharingModel::paymentUri() const { return m_cache ? m_cache->source.payload : QString{}; }
QImage RequestSharingModel::image() const { return canCopyQr() ? m_cache->image : QImage{}; }
void RequestSharingModel::setActive(bool enabled)
{
    const auto old = phase();
    if (!m_view->setActive(enabled) || old == phase()) return;
    if (enabled) refresh();
    Q_EMIT changed();
}
void RequestSharingModel::refresh()
{
    if (phase() == Closed || !active()) return;
    if (m_refresh_pending) { m_refresh_again = true; return; }
    if (m_refresh == std::numeric_limits<uint64_t>::max()) { retire(); return; }
    ++m_refresh; m_refresh_pending = true;
    const auto token = m_view->token.load();
    const QPointer<RequestSharingModel> observer{this};
    const auto post = m_services.execution.post;
    const auto finish = [observer, post, token](std::shared_ptr<const Cache> result) {
        post([observer, result = std::move(result), token] { if (observer) observer->publish(result, token); });
    };
    const auto read = m_services.read;
    const auto render = m_services.render;
    const auto scheduler = m_services.execution.scheduler;
    if (!read || !scheduler) {
        auto cache = std::make_shared<Cache>(); cache->source.error = requestshare::Error::Rejected; finish(cache); return;
    }
    // An observation owns no original action rights. Its work and render
    // complete on Domain; the GUI callback receives bounded values only.
    const auto admission = read(m_request_id, [finish, scheduler, render, id = m_request_id](requestshare::Source source) {
        auto cache = std::make_shared<Cache>(); cache->source = std::move(source);
        if (cache->source.error != requestshare::Error::None) { finish(cache); return; }
        if (!cache->source.opening || cache->source.request_id != id || cache->source.record_revision == 0) { cache->source.error = requestshare::Error::Changed; finish(cache); return; }
        if (cache->source.payload.isEmpty()) { cache->source.error = requestshare::Error::Missing; finish(cache); return; }
        if (cache->source.payload.size() > 16'384) { cache->source.error = requestshare::Error::Oversize; finish(cache); return; }
        const auto admission = execution::Submit(*scheduler, [render, payload = cache->source.payload] { return render ? render(payload) : QImage{}; },
            [cache, finish](QImage image) {
                if (image.isNull() || image.width() <= 1 || image.width() != image.height() || image.width() > 736) cache->qr_error = requestshare::Error::QrEncoding;
                else cache->image = std::move(image);
                finish(cache);
            }, [cache, finish](std::exception_ptr) { cache->qr_error = requestshare::Error::QrEncoding; finish(cache); }, {execution::Lane::Domain});
        if (!admission) { cache->qr_error = requestshare::Error::Rejected; finish(cache); }
    });
    if (!admission) { auto cache = std::make_shared<Cache>(); cache->source.error = requestshare::Error::Rejected; finish(cache); }
}
void RequestSharingModel::publish(std::shared_ptr<const Cache> cache, uint64_t token)
{
    m_refresh_pending = false;
    if (phase() == Closed) return;
    if (m_view->token.load() == token) m_cache = std::move(cache);
    const bool again = std::exchange(m_refresh_again, false) || token != m_view->token.load();
    const QPointer<RequestSharingModel> guard{this};
    Q_EMIT changed();
    if (guard && again && active()) refresh();
}
ShareOperationModel* RequestSharingModel::copy(requestshare::Kind kind)
{
    requestshare::Source expected;
    if (m_cache) expected = m_cache->source; else expected.error = requestshare::Error::Rejected;
    const auto operation = new ShareOperationModel(kind, m_request_id, m_services, m_view, std::move(expected));
    QQmlEngine::setObjectOwnership(operation, QQmlEngine::JavaScriptOwnership);
    m_last = operation;
    // Retain one current script projection even when a QML action ignores its
    // return value. No QObject parent ties an externally retained operation to
    // this view's lifetime; replacing/retiring the cache releases only our ref.
    if (auto* engine = m_gui_engine ? m_gui_engine.data() : qmlEngine(this)) m_last_script = engine->newQObject(operation);
    else m_last_script = {};
    const QPointer<ShareOperationModel> original{operation};
    Q_EMIT changed();
    return original;
}
ShareOperationModel* RequestSharingModel::copyUri() { return copy(requestshare::Kind::Uri); }
ShareOperationModel* RequestSharingModel::copyQr() { return copy(requestshare::Kind::Qr); }
RequestImageExportOperation* RequestSharingModel::beginQrExport()
{
    requestshare::Source expected;
    if (m_cache) expected = m_cache->source; else expected.error = requestshare::Error::Rejected;
    const auto operation = new RequestImageExportOperation(m_request_id, m_services, m_view, std::move(expected));
    QQmlEngine::setObjectOwnership(operation, QQmlEngine::JavaScriptOwnership);
    return operation;
}
void RequestSharingModel::retire()
{
    if (phase() == Closed) return;
    m_view->close(); m_cache.reset(); m_last_script = {};
    Q_EMIT changed();
}
