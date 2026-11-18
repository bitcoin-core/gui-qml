// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/paymentimport.h>
#include <qml/models/bitcoinuri.h>
#include <qml/psbtwork.h>
#include <qml/retainedvalue.h>

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QUrl>
#include <key_io.h>
#include <psbt.h>

#include <atomic>
#include <algorithm>
#include <limits>
#include <map>
#include <mutex>
#include <stdexcept>
#include <span>
#include <utility>

namespace paymentimport {
namespace {
constexpr qsizetype MAX_TEXT{1024 * 1024}, MAX_FILE{4 * 1024 * 1024};
constexpr auto RETRY{std::chrono::milliseconds{100}};
Inspection Invalid(const QString& error) { Inspection result; result.error = error.left(4096); return result; }
QString PaymentIdentity(const senddraft::PaymentRequest& payment)
{
    const auto destination = DecodeDestination(payment.address.toStdString());
    const auto address = IsValidDestination(destination) ? QString::fromStdString(EncodeDestination(destination)) : payment.address;
    QByteArray bytes;
    for (const auto& field : {address, payment.amount ? QString::number(*payment.amount) : QStringLiteral("absent"), payment.label, payment.message}) {
        const auto encoded = field.toUtf8(); bytes += QByteArray::number(encoded.size()) + ':' + encoded;
    }
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}
}

Inspection ReadSource(const Input& input)
{
    auto kind = input.kind;
    QString value = input.value;
    if (kind == Input::Kind::Drop) {
        if (input.url_count < 0 || input.url_count > 1) return Invalid(QStringLiteral("Drop one payment request or PSBT at a time."));
        if (input.url_count == 1) value = input.url.toString();
        kind = input.url_count == 1 && input.url.isLocalFile() ? Input::Kind::File : Input::Kind::Text;
    } else if (kind == Input::Kind::File && !input.url.isEmpty()) {
        value = input.url.toString();
    }
    QByteArray bytes;
    QString identity_path;
    if (kind == Input::Kind::File) {
        const auto path = psbtwork::LocalFilePath(value);
        if (path.isEmpty() || path.size() > 4096) return Invalid(QStringLiteral("The import path is invalid."));
        if (!QFileInfo(path).isFile()) return Invalid(QStringLiteral("The import source must be a regular local file."));
        QFile file{path};
        if (!file.open(QIODevice::ReadOnly)) return Invalid(QStringLiteral("The import source could not be opened."));
        identity_path = QFileInfo(file).canonicalFilePath();
        if (identity_path.isEmpty()) return Invalid(QStringLiteral("The import file path could not be resolved."));
        bytes = file.read(MAX_FILE + 1);
        if (file.error() != QFileDevice::NoError || bytes.size() > MAX_FILE || !file.atEnd())
            return Invalid(QStringLiteral("The import source is too large or could not be read."));
    } else {
        if (value.size() > MAX_TEXT) return Invalid(QStringLiteral("The import source is too large."));
        bytes = value.toUtf8();
    }
    Inspection result;
    result.field = input.field;
    result.source_identity = QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()) + QLatin1Char(':') +
        QString::fromLatin1(QCryptographicHash::hash(identity_path.toUtf8(), QCryptographicHash::Sha256).toHex());
    const auto text = QString::fromUtf8(bytes).trimmed();
    if (text.startsWith(QStringLiteral("bitcoin:"), Qt::CaseInsensitive)) {
        const auto payment = BitcoinUri::Parse(text);
        if (!payment.success) return Invalid(payment.error);
        if (payment.address.size() > 1024 || payment.label.size() > senddraft::Limits::note ||
            payment.message.size() > senddraft::Limits::message) return Invalid(QStringLiteral("The payment request metadata is too large."));
        result.kind = Kind::Payment;
        result.payment.address = payment.address;
        result.payment.label = payment.label;
        result.payment.message = payment.message;
        if (payment.has_amount) result.payment.amount = payment.amount_sats;
        result.payment_identity = PaymentIdentity(result.payment);
        // Public URI fields never manufacture a user-authored private note.
        return result;
    }
    const auto raw = DecodeRawPSBT(std::as_bytes(std::span{bytes.constData(), static_cast<size_t>(bytes.size())}));
    auto document = raw ? std::optional<PartiallySignedTransaction>{*raw} : std::nullopt;
    if (!document) {
        const auto decoded = DecodeBase64PSBT(text.toStdString());
        if (decoded) document = *decoded;
    }
    if (!document) return Invalid(QStringLiteral("The source is not a supported payment request or PSBT."));
    const auto transaction = document->GetUnsignedTx();
    if (!transaction || transaction->vin.empty() || transaction->vout.empty() ||
        document->inputs.size() != transaction->vin.size() || document->outputs.size() != transaction->vout.size())
        return Invalid(QStringLiteral("The PSBT is malformed."));
    auto parsed = std::make_shared<psbtwork::Snapshot>();
    parsed->document = std::make_shared<const PartiallySignedTransaction>(std::move(*document));
    result.kind = Kind::Psbt;
    result.psbt = std::move(parsed);
    return result;
}

Inspection AnalyzeSource(const Inspection& source, const psbtfee::Lookup& lookup)
{
    if (source.kind != Kind::Psbt || !source.psbt || !source.psbt->document) return Invalid(QStringLiteral("No PSBT is available for inspection."));
    psbtwork::Request request;
    request.kind = psbtwork::Kind::Inspect;
    request.document = source.psbt->document;
    psbtwork::Context context; context.lookup = lookup;
    const auto analyzed = psbtwork::Execute(nullptr, std::move(request), context);
    if (!analyzed.snapshot) return Invalid(analyzed.error.isEmpty() ? QStringLiteral("The PSBT could not be inspected.") : analyzed.error);
    auto result = source;
    result.psbt = analyzed.snapshot;
    return result;
}

struct Owner::Reply {
    std::optional<Target> target;
    std::optional<ChildReply> child;
    std::exception_ptr error;
};
struct Owner::Delivery {
    struct Cell {
        std::mutex mutex;
        bool ready{false};
        Reply reply;
    };
    std::shared_ptr<Cell> cell;
    std::shared_ptr<execution::Scheduler> scope;
    execution::TaskId cleanup;
    ReplyKind kind;
    std::optional<Child> child;
    uint64_t identity{0}, source{0}, target{0};
    void complete(Reply value) const
    {
        {
            std::lock_guard lock{cell->mutex};
            if (!cell->ready) {
                cell->reply = std::move(value);
                cell->ready = true;
            }
        }
        scope->activateCleanup(cleanup);
    }
};
struct Owner::State {
    Flow flow;
    std::shared_ptr<const Input> input;
    std::shared_ptr<const Inspection> inspection;
    std::shared_ptr<const TargetPort> target;
    std::shared_ptr<const ChildReply> result;
    std::map<uint64_t, std::shared_ptr<Delivery>> deliveries;
    uint64_t sequence{0}, target_sequence{0}, delivery_sequence{0}, query_delivery{0};
    size_t native_jobs{0};
    QString error;
    bool closing{false}, retired{false}, wake_pending{false}, use_requested{false};
    std::chrono::steady_clock::time_point retry_at{};
};
struct Owner::Close {
    std::mutex mutex;
    std::shared_ptr<Owner> owner;
};

Owner::Owner(uint64_t identity, Services services, std::shared_ptr<execution::Scheduler> scope,
             Notify notify, Retired retired, std::shared_ptr<State> state)
    : m_identity(identity), m_services(std::move(services)), m_scope(std::move(scope)),
      m_key(m_scope->allocateKey()), m_notify(std::move(notify)), m_retired(std::move(retired)), m_state(std::move(state)),
      m_close(std::make_shared<Close>()) {}

std::shared_ptr<Owner> Owner::Create(uint64_t identity, Services services, Notify notify, Retired retired)
{
    if (!identity || !services.scheduler) return {};
    auto scope = services.scheduler->openActionScope();
    if (!scope) return {};
    auto state = execution::RetainValues<State>(scope, [] { return std::make_unique<State>(); });
    if (!state) return {};
    if (!services.read) services.read = ReadSource;
    if (!services.analyze) services.analyze = [](const Inspection& source) { return AnalyzeSource(source); };
    if (!services.now) services.now = std::chrono::steady_clock::now;
    auto owner = std::shared_ptr<Owner>{new Owner(identity, std::move(services), scope, std::move(notify), std::move(retired), std::move(state))};
    const auto cell = owner->m_close;
    // Retire on Domain through an already reserved Cleanup relay. The slot
    // holds no Owner until close(), so dropping an unused actor cannot cycle.
    const auto close = execution::Submit(*scope, [] {}, [cell] {
        std::shared_ptr<Owner> original;
        { std::lock_guard lock{cell->mutex}; original = std::move(cell->owner); }
        if (original) original->closeOnDomain();
    }, {}, {execution::Lane::Cleanup, scope->allocateKey()}, true);
    if (!close) return {};
    owner->m_close_task = close.id;
    return owner;
}

execution::SubmitResult Owner::submit(std::function<void()> decision)
{
    return execution::Submit(*m_scope, [self = shared_from_this(), decision = std::move(decision)] {
        decision(); self->publish();
    }, [] {}, {}, {execution::Lane::Domain, m_key});
}

void Owner::publish()
{
    auto& state = *m_state;
    Snapshot snapshot;
    snapshot.sequence = ++state.sequence; snapshot.owner = m_identity; snapshot.source = state.flow.source();
    snapshot.phase = state.flow.phase(); snapshot.kind = state.flow.kind(); snapshot.error_code = state.flow.error();
    snapshot.error = state.error; snapshot.target = state.flow.target(); snapshot.prompt = state.flow.prompt();
    snapshot.result = state.flow.result(); snapshot.inspection = state.inspection; snapshot.child_result = state.result;
    snapshot.pending = state.flow.pending() || state.native_jobs || !state.deliveries.empty();
    if (snapshot.phase == Phase::Closed && snapshot.pending) snapshot.phase = Phase::Closing;
    snapshot.can_apply = state.flow.canApply(); snapshot.can_review = state.flow.canReview();
    snapshot.can_export = state.flow.canExport(); snapshot.can_reconcile = state.flow.canReconcile();
    try { if (m_notify) m_notify(std::move(snapshot)); } catch (...) {}
    if (state.flow.phase() == Phase::Closed && !state.native_jobs && state.deliveries.empty() && !state.retired) {
        state.retired = true;
        if (m_retired) m_retired(m_identity);
    }
}

execution::SubmitResult Owner::inspect(Input input)
{
    if (input.value.size() > MAX_TEXT) return {{}, execution::Rejection::Capacity};
    auto retained = execution::RetainValues<Input>(m_scope, [&] { return std::make_unique<Input>(std::move(input)); });
    if (!retained) return {{}, execution::Rejection::Closed};
    return submit([self = shared_from_this(), retained] {
        auto& state = *self->m_state;
        const auto source = state.flow.inspect();
        if (!source || state.closing || state.native_jobs) return;
        state.input = retained; state.inspection.reset(); state.result.reset(); state.error.clear();
        self->cancelObsolete(); self->read<false>(*source);
    });
}

execution::SubmitResult Owner::retry()
{
    return submit([self = shared_from_this()] {
        auto& state = *self->m_state;
        if (state.flow.phase() != Phase::Failed || state.closing || !state.input || state.native_jobs) return;
        const auto source = state.flow.inspect();
        if (!source) return;
        state.use_requested = false; state.inspection.reset(); state.result.reset(); state.error.clear();
        self->cancelObsolete(); self->read<false>(*source);
    });
}

template <bool Analysis>
void Owner::consumed(uint64_t source, std::shared_ptr<const Inspection> output, std::exception_ptr error)
{
    auto& state = *m_state;
    if (source != state.flow.source()) return;
    if (error || output->kind == Kind::Unknown || !output->error.isEmpty()) {
        state.error = error ? QStringLiteral("The import source could not be inspected.") : output->error;
        state.flow.inspectionFailed(source);
    } else {
        state.inspection = std::move(output);
        if constexpr (Analysis) state.flow.analyzed(source);
        else state.flow.parsed(source, state.inspection->kind);
        if constexpr (!Analysis) {
            if (!state.closing && state.inspection->kind == Kind::Psbt) read<true>(source);
            else if (!state.closing) observe();
        } else if (!state.closing) observe();
    }
}

template <bool Analysis>
void Owner::read(uint64_t source)
{
    auto output = execution::RetainValues<Inspection>(m_scope, [] { return std::make_unique<Inspection>(); });
    if (!output) { m_state->flow.inspectionFailed(source); m_state->error = QStringLiteral("Import result is unavailable."); return; }
    const auto input = m_state->input;
    const auto original = m_state->inspection;
    ++m_state->native_jobs;
    const auto completed = [self = shared_from_this(), output, source](std::exception_ptr error) {
        --self->m_state->native_jobs;
        self->consumed<Analysis>(source, output, error);
        self->cancelObsolete(); self->publish();
    };
    const auto submitted = execution::Submit(*m_scope, [output, input, original, services = m_services, presentation_allowed = m_presentation_allowed] {
        *output = Analysis ? services.analyze(*original) : services.read(*input);
        output->presentation_allowed = presentation_allowed;
    }, [completed] { completed({}); }, completed, {execution::Lane::Core});
    if (!submitted) completed(std::make_exception_ptr(execution::NotStarted{execution::NotStarted::Cause::Transport}));
}

std::shared_ptr<Owner::Delivery> Owner::reserve(ReplyKind kind, std::optional<Child> child)
{
    auto& state = *m_state;
    if (state.delivery_sequence == std::numeric_limits<uint64_t>::max()) return {};
    const auto identity = ++state.delivery_sequence;
    const auto source = state.flow.source();
    const auto target = state.target_sequence;
    auto cell = std::make_shared<Delivery::Cell>();
    auto delivery = std::make_shared<Delivery>();
    delivery->cell = cell;
    delivery->scope = m_scope;
    delivery->kind = kind;
    delivery->child = child;
    delivery->identity = identity;
    delivery->source = source;
    delivery->target = target;
    const auto cleanup_reservation = execution::Submit(*m_scope, [cell] {
        std::lock_guard lock{cell->mutex};
        return std::move(cell->reply);
    }, [self = shared_from_this(), identity, kind, source, target, child](Reply reply) {
        self->m_state->deliveries.erase(identity);
        self->finish(std::move(reply), kind, source, target, child, identity);
    }, {}, {execution::Lane::Cleanup, m_scope->allocateKey()}, true);
    if (!cleanup_reservation) return {};
    delivery->cleanup = cleanup_reservation.id;
    try {
        state.deliveries.emplace(identity, delivery);
    } catch (...) {
        delivery->complete(Reply{{}, {}, std::current_exception()});
        return {};
    }
    return delivery;
}

execution::SubmitResult Owner::chooseTarget(std::shared_ptr<const TargetPort> target)
{
    return submit([self = shared_from_this(), target = std::move(target)] {
        auto& state = *self->m_state;
        const auto facts = target ? std::optional<Target>{{target->opening, target->draft, 0, target->recipient, false, false, false}} : std::nullopt;
        if (!state.flow.chooseTarget(facts)) return;
        state.target = target; ++state.target_sequence;
        self->cancelObsolete(); self->observe();
    });
}

void Owner::observe()
{
    auto& state = *m_state;
    if (state.closing || !state.target || !state.target->observe || !state.inspection) return;
    const auto delivery = reserve(ReplyKind::Observe);
    if (!delivery) { state.error = QStringLiteral("Target observation is unavailable."); return; }
    const auto done = [delivery](Target target) { delivery->complete(Reply{std::move(target), {}, {}}); };
    const auto failed = [delivery](std::exception_ptr error) { delivery->complete(Reply{{}, {}, error}); };
    try { if (!state.target->observe(state.inspection, done, failed)) failed(std::make_exception_ptr(execution::NotStarted{execution::NotStarted::Cause::Transport})); }
    catch (...) { failed(std::current_exception()); }
}

execution::SubmitResult Owner::use()
{
    return submit([self = shared_from_this()] { self->m_state->use_requested = true; self->useReady(); });
}
void Owner::useReady()
{
    auto& state = *m_state;
    if (!state.use_requested || state.closing || state.flow.phase() != Phase::Inspecting ||
        !state.flow.target() || !state.flow.target()->available || !state.flow.target()->compatible) return;
    state.use_requested = false;
    const auto child = state.flow.kind() == Kind::Payment ? state.flow.apply() : state.flow.openReview();
    if (child) dispatch(*child);
}
execution::SubmitResult Owner::apply() { return submit([self = shared_from_this()] { if (const auto child = self->m_state->flow.apply()) self->dispatch(*child); }); }
execution::SubmitResult Owner::replace(uint64_t prompt) { return submit([self = shared_from_this(), prompt] { if (const auto child = self->m_state->flow.replace(prompt)) self->dispatch(*child); }); }
execution::SubmitResult Owner::keepDraft(uint64_t prompt) { return submit([self = shared_from_this(), prompt] { self->m_state->flow.keepDraft(prompt); }); }
execution::SubmitResult Owner::openReview() { return submit([self = shared_from_this()] { if (const auto child = self->m_state->flow.openReview()) self->dispatch(*child); }); }

void Owner::dispatch(Child child)
{
    auto& state = *m_state;
    const auto delivery = reserve(ReplyKind::Dispatch, child);
    if (!delivery || !state.target || !state.target->dispatch) {
        state.flow.finished({child, Outcome::Rejected, 0, child.target.revision}); state.flow.acknowledged(child);
        state.error = QStringLiteral("The import could not start. Try again."); return;
    }
    state.retry_at = m_services.now() + RETRY;
    const auto done = [delivery](ChildReply reply) { delivery->complete(Reply{{}, std::move(reply), {}}); };
    const auto failed = [delivery](std::exception_ptr error) { delivery->complete(Reply{{}, {}, error}); };
    try {
        const auto submitted = state.target->dispatch({m_identity, child.id}, child, state.inspection, done, failed);
        if (!submitted) failed(std::make_exception_ptr(execution::NotStarted{execution::NotStarted::Cause::Transport}));
    } catch (...) { failed(std::current_exception()); }
    wake();
}

void Owner::finish(Reply reply, ReplyKind kind, uint64_t source, uint64_t target, std::optional<Child> child, uint64_t delivery)
{
    auto& state = *m_state;
    const bool established = reply.child && reply.child->receipt.outcome != Outcome::Unknown;
    if (kind == ReplyKind::Query && delivery != state.query_delivery && !established) { cancelObsolete(); publish(); return; }
    if (kind == ReplyKind::Observe) {
        if (!state.closing && source == state.flow.source() && target == state.target_sequence) {
            if (reply.target) {
                if (state.flow.chooseTarget(reply.target) && state.flow.error() != Error::Source) state.error.clear();
            }
            else state.error = QStringLiteral("The draft could not be checked.");
        }
    } else if (child && state.flow.child() && *child == *state.flow.child() &&
               (!state.flow.result() || state.flow.result()->outcome == Outcome::Unknown)) {
        if (reply.child && reply.child->receipt.child == *child) {
            state.flow.finished(reply.child->receipt, reply.child->current_target);
            if (reply.child->receipt.outcome == Outcome::Rejected)
                state.error = reply.child->review && !reply.child->review->message.isEmpty()
                    ? reply.child->review->message.left(4096) : QStringLiteral("The draft did not accept this import.");
            else if (reply.child->receipt.outcome == Outcome::Stale)
                state.error = QStringLiteral("The draft changed. Review the replacement again.");
            else if (reply.child->receipt.outcome != Outcome::Unknown) state.error.clear();
            state.result = std::make_shared<const ChildReply>(std::move(*reply.child));
        } else {
            const bool not_started{execution::NotStartedCause(reply.error)};
            if (kind == ReplyKind::Dispatch && not_started) {
                state.flow.finished({*child, Outcome::Rejected, 0, child->target.revision});
                state.flow.acknowledged(*child);
                state.error = QStringLiteral("The import could not start. Try again.");
            } else {
                state.flow.acknowledgmentLost(*child);
                if (kind == ReplyKind::Query) state.flow.queryUnavailable(*child);
                state.error = QStringLiteral("The import result is not available yet. Check its status before trying again.");
            }
        }
    }
    useReady(); account(); cancelObsolete(); publish();
}

void Owner::account()
{
    auto& state = *m_state;
    if (!state.flow.child() || !state.flow.result() || state.flow.result()->outcome == Outcome::Unknown) return;
    const auto child = *state.flow.child();
    try {
        if (state.target && state.target->acknowledge && state.target->acknowledge({m_identity, child.id}, child)) {
            state.flow.acknowledged(child);
        } else state.flow.acknowledgmentLost(child);
    } catch (...) { state.flow.acknowledgmentLost(child); }
    if (state.flow.child()) { state.retry_at = m_services.now() + RETRY; wake(); }
}

void Owner::query()
{
    auto& state = *m_state;
    if (!state.flow.child()) return;
    if (state.flow.result() && state.flow.result()->outcome != Outcome::Unknown) { account(); return; }
    const auto child = *state.flow.child();
    if (!state.flow.beginQuery()) return;
    const auto delivery = reserve(ReplyKind::Query, child);
    if (!delivery || !state.target || !state.target->query) {
        state.flow.queryUnavailable(child); state.retry_at = m_services.now() + RETRY; wake(); return;
    }
    state.query_delivery = delivery->identity;
    const auto done = [delivery](ChildReply reply) { delivery->complete(Reply{{}, std::move(reply), {}}); };
    const auto failed = [delivery](std::exception_ptr error) { delivery->complete(Reply{{}, {}, error}); };
    try { if (!state.target->query({m_identity, child.id}, child, done, failed)) failed(std::make_exception_ptr(execution::NotStarted{execution::NotStarted::Cause::Transport})); }
    catch (...) { failed(std::current_exception()); }
}

void Owner::cancelObsolete()
{
    const auto& state = *m_state;
    std::vector<std::shared_ptr<Delivery>> obsolete;
    for (const auto& [identity, delivery] : state.deliveries) {
        if ((delivery->kind == ReplyKind::Observe && (state.closing || delivery->source != state.flow.source() || delivery->target != state.target_sequence)) ||
            (delivery->child && (!state.flow.child() || *delivery->child != *state.flow.child() ||
             (state.flow.result() && state.flow.result()->outcome != Outcome::Unknown))))
            obsolete.push_back(delivery);
    }
    for (const auto& delivery : obsolete) delivery->complete(Reply{{}, {}, std::make_exception_ptr(std::runtime_error{"Retired import delivery"})});
}

void Owner::wake()
{
    if (m_state->wake_pending || !m_services.wake || !m_state->flow.child()) return;
    m_state->wake_pending = true;
    try { m_services.wake(RETRY, [self = shared_from_this()] { self->tick(); }); }
    catch (...) { m_state->wake_pending = false; m_state->flow.acknowledgmentLost(*m_state->flow.child()); }
}
void Owner::poll()
{
    auto& state = *m_state;
    state.wake_pending = false;
    if (state.flow.child() && m_services.now() >= state.retry_at) {
        std::vector<std::shared_ptr<Delivery>> expired;
        for (const auto& [identity, delivery] : state.deliveries)
            if (delivery->kind == ReplyKind::Query && delivery->child == state.flow.child()) expired.push_back(delivery);
        for (const auto& delivery : expired) delivery->complete(Reply{{}, {}, std::make_exception_ptr(std::runtime_error{"Import outcome query expired"})});
        state.flow.acknowledgmentLost(*state.flow.child());
        query();
        state.retry_at = m_services.now() + RETRY;
    }
    wake(); cancelObsolete();
}
execution::SubmitResult Owner::tick() { return submit([self = shared_from_this()] { self->poll(); }); }
execution::SubmitResult Owner::reconcile() { return submit([self = shared_from_this()] { self->query(); }); }
execution::SubmitResult Owner::close()
{
    m_presentation_allowed->store(false);
    if (!m_close_requested.exchange(true)) {
        { std::lock_guard lock{m_close->mutex}; m_close->owner = shared_from_this(); }
        if (!m_scope->activateCleanup(m_close_task)) std::terminate();
    }
    return {m_close_task, execution::Rejection::None};
}
void Owner::closeOnDomain()
{
    auto& state = *m_state;
    state.closing = true;
    if (state.result && state.result->presentation_allowed) state.result->presentation_allowed->store(false);
    if (state.flow.child() && state.target && state.target->retire) {
        try { state.target->retire({m_identity, state.flow.child()->id}, *state.flow.child()); }
        catch (...) { /* An admitted child still belongs to its original query. */ }
    }
    state.flow.close();
    if (state.flow.child()) { state.flow.acknowledgmentLost(*state.flow.child()); query(); }
    cancelObsolete(); publish();
}
} // namespace paymentimport
