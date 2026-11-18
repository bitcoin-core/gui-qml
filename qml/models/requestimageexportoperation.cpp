// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/models/requestimageexportoperation.h>
#include <qml/guipresentation.h>

#include <QPointer>
#include <QUuid>

namespace {
using Scope = std::shared_ptr<execution::Scheduler>;
RequestImageExportError::Code FileError(backup::Error error)
{
    switch (error) {
    case backup::Error::None: return RequestImageExportError::None;
    case backup::Error::InvalidPath: return RequestImageExportError::InvalidPath;
    case backup::Error::Inspection: return RequestImageExportError::Inspection;
    case backup::Error::UnsupportedTarget: return RequestImageExportError::UnsupportedTarget;
    case backup::Error::WalletFile: return RequestImageExportError::WalletFile;
    case backup::Error::Staging: case backup::Error::CoreBackup: return RequestImageExportError::Encoding;
    case backup::Error::Changed: return RequestImageExportError::Changed;
    case backup::Error::Install: return RequestImageExportError::Install;
    case backup::Error::Unknown: return RequestImageExportError::Unknown;
    case backup::Error::Transport: return RequestImageExportError::Rejected;
    }
    return RequestImageExportError::Unknown;
}
}
struct RequestImageExportOperation::Cell {
    std::mutex mutex;
    Phase phase{Checking};
    RequestImageExportError::Code error{RequestImageExportError::None};
    requestshare::Error source_error{requestshare::Error::None};
    RequestImageExportResult result;
    backup::Proposal target;
    std::shared_ptr<requestshare::Grant> grant;
    std::atomic_bool cancelled{false};
    bool pending{true};
    std::function<void()> notify;
};
struct RequestImageExportOperation::Work : std::enable_shared_from_this<Work> {
    const int64_t id;
    requestshare::Services services;
    const std::shared_ptr<requestshare::View> view;
    const uint64_t view_token;
    const uint64_t input_revision;
    const std::shared_ptr<Cell> cell;
    backup::Snapshot policy;
    QImage image;
    std::optional<requestshare::Source> expected;
    Work(int64_t request_id, requestshare::Services ports, std::shared_ptr<requestshare::View> interest, std::shared_ptr<Cell> result)
        : id(request_id), services(std::move(ports)), view(std::move(interest)), view_token(view->token.load()),
          input_revision(view->input_epoch ? view->input_epoch->value.load() : 0), cell(std::move(result)) {}
    void notice() { cell->notify(); }
    void finish(RequestImageExportError::Code error, requestshare::Error source_error = requestshare::Error::None,
                bool written = false, bool unknown = false)
    {
        {
            std::lock_guard lock{cell->mutex};
            if (cell->result.present) return;
            const bool admitted = cell->grant && cell->grant->admitted();
            cell->pending = false;
            cell->error = error; cell->source_error = source_error;
            cell->result.present = true; cell->result.admitted = admitted;
            cell->result.written = written; cell->result.outcomeKnown = !unknown;
            cell->phase = unknown ? OutcomeUnknown : written ? Completed : error == RequestImageExportError::Cancelled ? Cancelled : Failed;
        }
        notice();
    }
    bool stopped()
    {
        { std::lock_guard lock{cell->mutex}; if (cell->result.present) return true; }
        if (!cell->cancelled.load()) return false;
        finish(RequestImageExportError::Cancelled); return true;
    }
    template<typename Effect, typename Done>
    bool step(Scope scope, execution::Lane lane, Effect effect, Done done, bool mutation = false)
    {
        if (!scope) { finish(RequestImageExportError::Rejected); return false; }
        const auto self = shared_from_this();
        const auto submitted = execution::Submit(*scope, std::move(effect),
            [self, scope, done = std::move(done)](auto result) mutable { done(std::move(result), std::move(scope)); },
            [self, scope, mutation](std::exception_ptr failure) {
                // A thrown native writer can have installed a file. Only an
                // adapter's known receipt may classify it as not written.
                try { std::rethrow_exception(failure); }
                catch (const execution::NotStarted&) { self->finish(RequestImageExportError::Rejected); return; }
                catch (...) { }
                self->finish(mutation ? RequestImageExportError::Unknown : RequestImageExportError::Rejected, requestshare::Error::None, false, mutation);
            }, {lane});
        if (!submitted) { finish(RequestImageExportError::Rejected); return false; }
        return true;
    }
    void begin(Scope scope)
    {
        if (stopped()) return;
        if (!view->active(view_token, input_revision)) { finish(RequestImageExportError::Source, requestshare::Error::Inactive); return; }
        if (expected && (expected->error != requestshare::Error::None || !expected->current || !expected->current())) {
            finish(RequestImageExportError::Source, expected->error == requestshare::Error::None ? requestshare::Error::Changed : expected->error); return;
        }
        const auto self = shared_from_this();
        if (!services.read || !services.files || !services.files->inspect || !services.files->write || !services.render) { finish(RequestImageExportError::Rejected); return; }
        const auto submitted = services.read(id, [self, scope](requestshare::Source source) mutable {
            if (self->stopped()) return;
            if (!source.opening || source.request_id != self->id || source.record_revision == 0) {
                self->finish(RequestImageExportError::Source, requestshare::Error::Changed); return;
            }
            if (self->expected && (self->expected->opening != source.opening || self->expected->request_id != source.request_id ||
                self->expected->record_revision != source.record_revision || self->expected->payload != source.payload)) {
                self->finish(RequestImageExportError::Source, requestshare::Error::Changed); return;
            }
            if (source.error != requestshare::Error::None || source.payload.isEmpty() || !source.current) {
                self->finish(RequestImageExportError::Source, source.error == requestshare::Error::None ? requestshare::Error::Missing : source.error); return;
            }
            if (source.payload.size() > 16'384) { self->finish(RequestImageExportError::Source, requestshare::Error::Oversize); return; }
            const auto grant = std::make_shared<requestshare::Grant>(source, self->view, self->view_token, self->input_revision);
            {
                std::lock_guard lock{self->cell->mutex};
                self->cell->grant = grant;
                self->cell->phase = Preparing;
                self->cell->result.requestId = QString::number(source.request_id);
                self->cell->result.payloadRevision = QString::number(source.record_revision);
            }
            self->notice();
            self->step(std::move(scope), execution::Lane::Domain,
                [render = self->services.render, payload = std::move(source.payload)] { return render(payload); },
                [self](QImage image, Scope original) {
                    if (self->stopped()) return;
                    if (image.isNull() || image.width() <= 1 || image.width() != image.height() || image.width() > 736) {
                        self->finish(RequestImageExportError::Source, requestshare::Error::QrEncoding); return;
                    }
                    self->image = std::move(image);
                    // No effect/capability waits for a chooser or GUI turn.
                    original.reset();
                    { std::lock_guard lock{self->cell->mutex}; self->cell->phase = Choosing; self->cell->pending = false; }
                    self->notice();
                });
        });
        if (!submitted) finish(RequestImageExportError::Rejected);
    }
    void inspect(QUrl destination, Scope scope)
    {
        if (stopped()) return;
        policy = backup::Advance(policy, backup::Event::Start).snapshot;
        const auto self = shared_from_this();
        step(std::move(scope), execution::Lane::Core,
            [inspect = services.files->inspect, destination = std::move(destination)] { return inspect(destination); },
            [self](backup::Proposal proposal, Scope original) {
                if (self->stopped()) return;
                const auto decision = backup::Advance(self->policy, backup::Event::Inspected, proposal);
                self->policy = decision.snapshot;
                if (self->policy.phase == backup::Phase::Failed) { self->finish(FileError(self->policy.error)); return; }
                {
                    std::lock_guard lock{self->cell->mutex};
                    self->cell->target = proposal;
                    self->cell->result.finalPath = QString::fromUtf8(proposal.path.data(), static_cast<qsizetype>(proposal.path.size()));
                }
                if (self->policy.phase == backup::Phase::AwaitingConfirmation) {
                    original.reset();
                    { std::lock_guard lock{self->cell->mutex}; self->cell->phase = Confirming; self->cell->pending = false; }
                    self->notice();
                } else self->write(std::move(original));
            });
    }
    void write(Scope scope)
    {
        if (stopped()) return;
        const auto decision = backup::Advance(policy, backup::Event::AdmitWrite);
        if (!decision.accepted) { finish(RequestImageExportError::Rejected); return; }
        policy = decision.snapshot;
        std::shared_ptr<requestshare::Grant> grant;
        { std::lock_guard lock{cell->mutex}; cell->phase = Writing; grant = cell->grant; }
        notice();
        const auto self = shared_from_this();
        step(std::move(scope), execution::Lane::Core,
            [writer = services.files->write, target = policy.target, approved = policy.destination_approved, pixels = image, grant] {
                return writer(target, approved, pixels, grant);
            }, [self, grant](backup::WriteResult result, Scope) {
                self->policy = backup::Advance(self->policy, backup::Event::Written, {}, result).snapshot;
                if (result.error == backup::Error::None) {
                    if (!grant || !grant->admitted()) { self->finish(RequestImageExportError::Unknown, requestshare::Error::None, false, true); return; }
                    self->finish(RequestImageExportError::None, requestshare::Error::None, true); return;
                }
                if (self->cell->cancelled.load() && (!grant || !grant->admitted())) { self->finish(RequestImageExportError::Cancelled); return; }
                if (grant && !grant->admitted() && !grant->current() && result.error == backup::Error::Changed) {
                    self->finish(RequestImageExportError::Source, requestshare::Error::Changed); return;
                }
                self->finish(FileError(result.error), requestshare::Error::None, false, result.error == backup::Error::Unknown);
            }, true);
    }
};

RequestImageExportOperation::RequestImageExportOperation(int64_t id, requestshare::Services services, std::shared_ptr<requestshare::View> view,
    std::optional<requestshare::Source> expected, QObject* parent)
    : FileSaveOperation(parent), m_id(QUuid::createUuid().toString(QUuid::WithoutBraces)), m_cell(std::make_shared<Cell>()),
      m_work(std::make_shared<Work>(id, std::move(services), std::move(view), m_cell))
{
    m_work->expected = std::move(expected);
    m_cell->result.operationId = m_id;
    const QPointer<RequestImageExportOperation> observer{this};
    const auto post = presentation::ReliablePost(m_work->services.execution.post);
    m_cell->notify = [observer, post] { post([observer] { if (observer) Q_EMIT observer->changed(); }); };
    auto scope = m_work->services.execution.scheduler ? m_work->services.execution.scheduler->openActionScope() : nullptr;
    const auto work = m_work;
    if (scope) work->step(scope, execution::Lane::Domain, [] { return true; }, [work](bool, Scope original) { work->begin(std::move(original)); });
    else work->finish(RequestImageExportError::Rejected);
}
RequestImageExportOperation::~RequestImageExportOperation() { cancel(); }
RequestImageExportOperation::Phase RequestImageExportOperation::phase() const { std::lock_guard lock{m_cell->mutex}; return m_cell->phase; }
bool RequestImageExportOperation::replacing() const { std::lock_guard lock{m_cell->mutex}; return m_cell->target.exists; }
QString RequestImageExportOperation::finalPath() const { std::lock_guard lock{m_cell->mutex}; return m_cell->result.finalPath; }
RequestImageExportError::Code RequestImageExportOperation::errorCode() const { std::lock_guard lock{m_cell->mutex}; return m_cell->error; }
RequestImageExportResult RequestImageExportOperation::result() const { std::lock_guard lock{m_cell->mutex}; return m_cell->result; }
QString RequestImageExportOperation::error() const
{
    RequestImageExportError::Code error;
    requestshare::Error source;
    { std::lock_guard lock{m_cell->mutex}; error = m_cell->error; source = m_cell->source_error; }
    switch (error) {
    case RequestImageExportError::None: return {};
    case RequestImageExportError::Source: return requestshare::ErrorText(source);
    case RequestImageExportError::Rejected: return tr("The image could not be saved. Try again.");
    case RequestImageExportError::InvalidPath: return tr("Choose a valid local PNG destination.");
    case RequestImageExportError::Inspection: return tr("The image destination could not be inspected.");
    case RequestImageExportError::UnsupportedTarget: return tr("Choose a regular file as the image destination.");
    case RequestImageExportError::WalletFile: return tr("Choose a file that is not a wallet.");
    case RequestImageExportError::Encoding: return tr("The QR image could not be written in that directory.");
    case RequestImageExportError::Changed: return tr("The destination changed. Choose the image location again.");
    case RequestImageExportError::Install: return tr("The image could not be installed. Check the destination before retrying.");
    case RequestImageExportError::Unknown: return requestshare::ErrorText(requestshare::Error::Unknown);
    case RequestImageExportError::Cancelled: return tr("The image export was cancelled.");
    case RequestImageExportError::Chooser: return tr("The file chooser could not be opened.");
    }
    return {};
}
bool RequestImageExportOperation::choose(const QUrl& destination)
{
    if (destination.toString().size() > 32'768) { Q_EMIT commandRejected(QStringLiteral("choose")); return false; }
    {
        std::lock_guard lock{m_cell->mutex};
        if (m_cell->phase != Choosing || m_cell->pending) return false;
        m_cell->pending = true; m_cell->phase = Inspecting;
    }
    auto scope = m_work->services.execution.scheduler ? m_work->services.execution.scheduler->openActionScope() : nullptr;
    const auto work = m_work;
    const bool admitted = work->step(scope, execution::Lane::Domain, [destination] { return destination; },
        [work](QUrl target, Scope original) { work->inspect(std::move(target), std::move(original)); });
    Q_EMIT changed(); return admitted;
}
bool RequestImageExportOperation::confirm()
{
    {
        std::lock_guard lock{m_cell->mutex};
        if (m_cell->phase != Confirming || m_cell->pending) return false;
        m_cell->pending = true; m_cell->phase = Writing;
    }
    auto scope = m_work->services.execution.scheduler ? m_work->services.execution.scheduler->openActionScope() : nullptr;
    const auto work = m_work;
    const bool admitted = work->step(scope, execution::Lane::Domain, [] { return true; },
        [work](bool, Scope original) {
            const auto decision = backup::Advance(work->policy, backup::Event::Approve);
            if (!decision.accepted) { work->finish(RequestImageExportError::Rejected); return; }
            work->policy = decision.snapshot; work->write(std::move(original));
        });
    Q_EMIT changed(); return admitted;
}
bool RequestImageExportOperation::cancel()
{
    bool waiting{false};
    {
        std::lock_guard lock{m_cell->mutex};
        if (m_cell->result.present || m_cell->cancelled.exchange(true)) return false;
        if (m_cell->grant) m_cell->grant->revoke();
        waiting = m_cell->pending;
        m_cell->phase = Cancelling;
    }
    if (!waiting) m_work->finish(RequestImageExportError::Cancelled);
    else Q_EMIT changed();
    return true;
}
void RequestImageExportOperation::chooserFailed()
{
    if (phase() != Choosing) return;
    m_work->finish(RequestImageExportError::Chooser);
}
