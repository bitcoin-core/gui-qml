// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/psbtqmlmodel.h>
#include <qml/backendexecutor.h>
#include <qml/util.h>
#include <qml/ownedwork.h>
#include <qml/guipresentation.h>
#include <qml/psbtdocument.h>
#include <qml/retainedvalue.h>
#include <limits>
#include <QClipboard>
#include <QGuiApplication>
#include <QPointer>
#include <QThread>
#include <algorithm>

struct PsbtQmlModel::SummaryRequestState {
    mutable std::mutex mutex;
    std::shared_ptr<const psbtwork::SummarySource> source;
    quint64 generation{0}, request{0};
    int offset{0}, count{128};

    psbtwork::SummaryPage read() const
    {
        std::shared_ptr<const psbtwork::SummarySource> captured_source;
        quint64 captured_generation, captured_request;
        int captured_offset, captured_count;
        {
            std::lock_guard lock{mutex};
            captured_source = source;
            captured_generation = generation;
            captured_request = request;
            captured_offset = offset;
            captured_count = count;
        }
        try {
            return psbtwork::ProjectSummaryPage(std::move(captured_source), captured_offset,
                captured_count, captured_generation, captured_request);
        } catch (...) {
            psbtwork::SummaryPage page;
            page.generation = captured_generation;
            page.request = captured_request;
            page.offset = captured_offset;
            page.error = QObject::tr("The PSBT summary page could not be prepared.");
            return page;
        }
    }
};

QString PsbtQmlModel::LocalFilePath(const QString& path) { return psbtwork::LocalFilePath(path); }
QString PsbtQmlModel::LoadPsbtFromFile(const QString& path, PartiallySignedTransaction& document) { return psbtwork::LoadFile(path, document); }
QByteArray PsbtQmlModel::SerializePsbtRaw(const PartiallySignedTransaction& document) { return psbtwork::SerializeRaw(document); }
QString PsbtQmlModel::SavePsbtToFile(const PartiallySignedTransaction& document, const QString& path) { return psbtwork::SaveFile(document, path); }
QString PsbtQmlModel::PsbtErrorText(common::PSBTError error) { return psbtwork::ErrorText(error); }
bool PsbtQmlModel::IsMultisigPsbtInput(const PartiallySignedTransaction& document, size_t index) { return psbtwork::Multisig(document, index).has_value(); }

PsbtQmlModel::PsbtQmlModel(interfaces::Wallet* wallet, interfaces::Node* node, QObject* parent)
    : QObject(parent), m_node(node), m_wallet_expected(wallet != nullptr) {}

PsbtQmlModel::~PsbtQmlModel()
{
    closeAdmission();
    releaseSnapshot();
    // The reserved Cleanup owner still retains each structural result when its
    // GUI alias is released; activation happens only after the GUI reference.
    m_active.reset();
    m_active_lease.reset();
    // BackendExecutor owns its asynchronous native drain after GUI teardown.
    // Never enter a nested GUI loop or wait for Core/device work here.
    if (m_owned_executor) m_owned_executor->shutdown();
}

void PsbtQmlModel::configureSession(std::shared_ptr<walletopening::Session> opening, psbtfee::Services services)
{
    Q_ASSERT(!m_active && !m_snapshot);
    m_opening = std::move(opening);
    if (m_opening && !services.scheduler) services.scheduler = m_opening->scheduler();
    setFeeServices(std::move(services));
}
void PsbtQmlModel::setFeeServices(psbtfee::Services services)
{
    // In-flight work retains its original services and Native lifetime.
    retireDocumentOwner();
    m_services = std::move(services);
    m_services.post=presentation::ReliablePost(std::move(m_services.post));
    if (!m_services.scheduler && m_opening) m_services.scheduler = m_opening->scheduler();
}
void PsbtQmlModel::ensureServices()
{
    if (!m_services.scheduler) {
        m_owned_executor = std::make_unique<BackendExecutor>(execution::Options{});
        m_services.scheduler = m_owned_executor->scheduler();
    }
    if (!m_services.post) {
        m_bridge = presentation::MakeGuiObject();
        m_services.post = [bridge = m_bridge](std::function<void()> turn) {
            QMetaObject::invokeMethod(bridge.get(), std::move(turn), Qt::QueuedConnection);
        };
    }
    if (!m_document_owner) {
        m_document_owner = execution::RetainValues<psbtdocument::Owner>(m_services.scheduler,
            [generation = m_generation] { return std::make_unique<psbtdocument::Owner>(generation); });
    }
}
void PsbtQmlModel::setNode(interfaces::Node* node)
{
    if (m_node == node) return;
    m_node = node;
    m_services.policy = {}; // A replacement Node needs a newly composed read port.
    retireDocumentOwner();
    ++m_generation; // Late results from the old public Node context cannot publish.
    // Keep the immutable summary, but replace the query whose notifier owns
    // the retired generation before admitting another page request.
    retireSummaryQuery();
    if (!busy() && m_snapshot && !m_closed) refreshState();
}
void PsbtQmlModel::closeAdmission()
{
    m_closed = true;
    m_phase = Phase::Closed;
    retireDocumentOwner();
    // Closing GUI delivery cannot hold the service drain hostage. Native work
    // retains its own operation and bounded scalar receipt through its outcome.
    releaseSnapshot();
    releaseQuarantine();
    m_active.reset();
    m_active_lease.reset();
}
void PsbtQmlModel::detachWallet()
{
    closeAdmission();
    ++m_generation;
    Q_EMIT changed();
}
void PsbtQmlModel::retireSummaryQuery()
{
    ++m_summary_epoch;
    if (m_summary_query) m_summary_query->close();
    m_summary_query.reset();
    m_summary_request_state.reset();
    m_summary_pending = false;
}
void PsbtQmlModel::releaseSnapshot()
{
    retireSummaryQuery();
    m_snapshot.reset();
    m_summary_window.clear();
    m_summary_offset = 0;
    m_summary_size = 128;
    m_summary_request_id = 0;
    m_summary_error.clear();
    m_snapshot_lease.reset();
}
void PsbtQmlModel::releaseQuarantine()
{
    m_quarantined_document.reset();
    m_quarantine_lease.reset();
    m_quarantined_operation = m_quarantined_generation = 0;
}
void PsbtQmlModel::retireDocumentOwner()
{
    if (m_document_owner) m_document_owner->withdraw();
    m_document_owner.reset();
}
void PsbtQmlModel::clear()
{
    retireDocumentOwner();
    releaseQuarantine();
    ++m_generation;
    releaseSnapshot();
    m_matched_txid.clear();
    m_error.clear(); m_status.clear();
    if (!busy()) m_phase = m_closed ? Phase::Closed : Phase::Idle;
    Q_EMIT changed();
}
void PsbtQmlModel::setError(const QString& error)
{
    retireDocumentOwner();
    releaseQuarantine();
    ++m_generation;
    releaseSnapshot();
    m_matched_txid.clear();
    m_status.clear();
    m_error = error;
    m_phase = m_closed ? Phase::Closed : Phase::Failed;
    Q_EMIT changed();
}
void PsbtQmlModel::setMatchedTxid(const QString& txid) { m_matched_txid = txid; Q_EMIT changed(); }
QString PsbtQmlModel::error() const {
    if (m_closed && m_receipt) { std::lock_guard lock{m_receipt->mutex}; if (m_receipt->ready) return m_receipt->error; }
    return m_error;
}
bool PsbtQmlModel::submitted() const { if (!m_submission_receipt) return m_submitted; std::lock_guard lock{m_submission_receipt->mutex}; return m_submission_receipt->ready && m_submission_receipt->submitted; }
bool PsbtQmlModel::saved() const { if (!m_save_receipt) return m_saved; std::lock_guard lock{m_save_receipt->mutex}; return m_save_receipt->ready && m_save_receipt->saved; }
QString PsbtQmlModel::savedPath() const { if (!m_save_receipt) return m_saved_path; std::lock_guard lock{m_save_receipt->mutex}; return m_save_receipt->ready && m_save_receipt->saved ? m_save_receipt->path : m_saved_path; }
QString PsbtQmlModel::submittedTxid() const { if (!m_submission_receipt) return m_submitted_txid; std::lock_guard lock{m_submission_receipt->mutex}; return m_submission_receipt->ready ? m_submission_receipt->txid : m_submitted_txid; }
void PsbtQmlModel::prepareSummaryPaging()
{
    m_summary_window = m_snapshot ? m_snapshot->initial_summary_page.rows : QStringList{};
    m_summary_offset = m_snapshot ? m_snapshot->initial_summary_page.offset : 0;
    m_summary_size = 128;
    m_summary_request_id = 0;
    m_summary_pending = false;
    m_summary_error = m_snapshot ? m_snapshot->initial_summary_page.error : QString{};
}

void PsbtQmlModel::ensureSummaryQuery()
{
    if (m_summary_query || !m_snapshot || !m_snapshot->summary_source || !m_services.scheduler) return;

    const auto state = execution::RetainValues<SummaryRequestState>(m_services.scheduler, [] {
        return std::make_unique<SummaryRequestState>();
    });
    if (!state) {
        m_summary_error = tr("PSBT summary paging is unavailable.");
        return;
    }
    state->source = m_snapshot->summary_source;
    state->generation = m_generation;
    m_summary_request_state = state;

    const auto generation = m_generation;
    const auto epoch = ++m_summary_epoch;
    const QPointer<PsbtQmlModel> observer{this};
    m_summary_query = std::make_shared<execution::SnapshotQuery<psbtwork::SummaryPage>>(
        m_services.scheduler, [state] { return state->read(); },
        [post = m_services.post, observer, generation, epoch] {
            post([observer, generation, epoch] {
                if (observer) observer->publishSummaryPage(generation, epoch);
            });
        }, execution::Lane::Domain);
}
void PsbtQmlModel::requestSummaryWindow(int offset, int count)
{
    if (offset < 0 || count < 1 || !m_snapshot || m_closed) return;
    ensureSummaryQuery();
    m_summary_offset = offset;
    m_summary_size = std::min(count, 128);
    m_summary_window.clear();
    m_summary_error.clear();
    m_summary_pending = true;
    const auto request = ++m_summary_request_id;
    if (!m_summary_query || !m_summary_request_state) {
        m_summary_pending = false;
        m_summary_error = tr("PSBT summary paging is unavailable.");
        Q_EMIT changed();
        return;
    }
    {
        std::lock_guard lock{m_summary_request_state->mutex};
        m_summary_request_state->generation = m_generation;
        m_summary_request_state->request = request;
        m_summary_request_state->offset = offset;
        m_summary_request_state->count = m_summary_size;
    }
    m_summary_query->request();
    Q_EMIT changed();
}

void PsbtQmlModel::publishSummaryPage(quint64 generation, quint64 epoch)
{
    if (m_closed || generation != m_generation || epoch != m_summary_epoch || !m_summary_query) return;
    auto result = m_summary_query->take();
    if (!result) return;
    if (result->error || !result->snapshot) {
        m_summary_window.clear();
        m_summary_pending = false;
        m_summary_error = tr("The PSBT summary page could not be prepared.");
        Q_EMIT changed();
        return;
    }
    const auto& page = *result->snapshot;
    if (page.generation != generation || page.request != m_summary_request_id) return;
    m_summary_window = page.rows;
    m_summary_offset = page.offset;
    m_summary_error = page.error;
    m_summary_pending = false;
    Q_EMIT changed();
}
quint64 PsbtQmlModel::requestLoadFromFile(const QString& path)
{
    if (busy()) {
        if (m_active_kind != psbtwork::Kind::Load && m_active_kind != psbtwork::Kind::Inspect) return 0;
        // A replaceable read may finish on its own original inputs. Its result
        // is retired on Native/Domain and cannot publish into the new document.
        m_active.reset(); m_active_lease.reset();
    }
    retireDocumentOwner();
    releaseQuarantine();
    ++m_generation;
    releaseSnapshot();
    psbtwork::Request request; request.kind = psbtwork::Kind::Load; request.path = path;
    return start(std::move(request));
}
quint64 PsbtQmlModel::requestSaveToFile(const QString& path)
{
    psbtwork::Request request; request.kind = psbtwork::Kind::Save; request.path = path;
    return start(std::move(request));
}
quint64 PsbtQmlModel::requestSign() { psbtwork::Request request; request.kind = psbtwork::Kind::Sign; return start(std::move(request)); }
QString PsbtQmlModel::unverifiedResultToken() const
{
    if (m_closed || !m_quarantined_document || m_quarantined_generation != m_generation ||
        (m_opening && !m_opening->available())) return {};
    return QString::number(m_quarantined_operation) + QLatin1Char(':') + QString::number(m_quarantined_generation);
}
quint64 PsbtQmlModel::requestSaveUnverifiedResult(const QString& token, const QString& path)
{
    if (token.isEmpty() || token != unverifiedResultToken()) return 0;
    psbtwork::Request request;
    request.kind = psbtwork::Kind::Save;
    request.path = path;
    request.document = m_quarantined_document;
    return start(std::move(request));
}
quint64 PsbtQmlModel::requestSignWithPassphrase(const QString& passphrase)
{
    if(busy()||m_closed||!canSign())return 0;
    ensureServices();
    if (!m_document_owner) {
        m_phase = Phase::Failed; m_error = tr("PSBT result is unavailable."); Q_EMIT changed(); return 0;
    }
    const auto captured=secretinput::Capture(m_services.scheduler,passphrase);
    if(!captured){m_phase=Phase::Failed;m_error=tr("The signing password could not be admitted for background execution.");Q_EMIT changed();return 0;}
    psbtwork::Request request;request.kind=psbtwork::Kind::Sign;request.captured_passphrase=captured.input;
    return start(std::move(request));
}
quint64 PsbtQmlModel::requestBroadcast() { psbtwork::Request request; request.kind = psbtwork::Kind::Broadcast; return start(std::move(request)); }
quint64 PsbtQmlModel::requestCopyToClipboard() { psbtwork::Request request; request.kind = psbtwork::Kind::Clipboard; return start(std::move(request)); }
void PsbtQmlModel::refreshState(const QString& status)
{
    if (!m_snapshot || busy() || m_closed) return;
    psbtwork::Request request; request.status = status;
    start(std::move(request));
}

quint64 PsbtQmlModel::start(psbtwork::Request request)
{
    if (busy() || m_closed || (m_opening && !m_opening->available())) return 0;
    if (request.kind == psbtwork::Kind::Sign && !canSign()) return 0;
    if (request.kind == psbtwork::Kind::Broadcast && !canBroadcast()) return 0;
    if (request.kind != psbtwork::Kind::Load && !loaded()) return 0;
    const auto ticket = ++m_operation_id;
    if (m_wallet_expected && !m_opening) {
        m_phase = Phase::Failed; m_error = tr("The wallet session is unavailable."); Q_EMIT changed(); return ticket;
    }
    const QPointer<PsbtQmlModel> observer{this};
    ensureServices();
    if (!observer || m_closed) return ticket;
    if (!m_document_owner) {
        m_phase = Phase::Failed; m_error = tr("PSBT result is unavailable."); Q_EMIT changed(); return ticket;
    }
    const auto generation = m_generation;
    // Reserve Native destruction before capturing document/assessment aliases.
    // Even a rejected admission or a reentrant close must release them off GUI.
    auto input = execution::RetainValues<psbtwork::Request>(m_services.scheduler,
        [] { return std::make_unique<psbtwork::Request>(); });
    if (!observer || m_closed || generation != m_generation) return ticket;
    if (!input) { m_phase = Phase::Failed; m_error = tr("PSBT input is unavailable."); Q_EMIT changed(); return ticket; }
    *input = std::move(request);
    if (m_snapshot) {
        if (!input->document) input->document = m_snapshot->document;
        input->assessment = m_snapshot->assessment;
        input->proof = m_snapshot->proof;
        input->approved_fee = m_snapshot->proof ? m_snapshot->proof->amount : std::nullopt;
    }
    auto receipt = std::make_shared<Receipt>();
    auto operation_ref = std::make_shared<std::weak_ptr<Operation>>();
    auto operation = std::make_shared<Operation>(m_services.scheduler, [post = m_services.post, observer, ticket, generation, receipt, operation_ref] {
        if (const auto operation = operation_ref->lock()) if (const auto result = operation->result()) {
            if (result->value) receipt->record(*result->value);
            else {
                psbtwork::Result failed;
                failed.code = psbtwork::Error::Uncertain;
                failed.error = tr("The PSBT action ended without a confirmed outcome. Reconcile before retrying.");
                receipt->record(failed);
            }
        }
        post([observer, ticket, generation] { if (observer) observer->publish(ticket, generation); });
    });
    *operation_ref = operation;
    m_receipt = receipt;
    if (input->kind == psbtwork::Kind::Broadcast) m_submission_receipt = receipt;
    if (input->kind == psbtwork::Kind::Save) m_save_receipt = receipt;
    {
        // Every action result retains Native destruction, including an accepted
        // raw transaction or a large clipboard value. Reserve before dispatch.
        auto owner = std::make_shared<std::shared_ptr<Operation>>(operation);
        const auto key = m_services.scheduler->allocateKey();
        const auto cleanup = execution::Submit(*m_services.scheduler, [owner] { owner->reset(); }, [] {}, {},
            {execution::Lane::Cleanup, key, execution::WorkClass::Action}, true);
        if (!cleanup) { m_phase = Phase::Failed; m_error = tr("PSBT result is unavailable."); Q_EMIT changed(); return ticket; }
        auto held = m_services.scheduler->borrow(key);
        m_services.scheduler->activateCleanup(cleanup.id);
        if (!held) { m_phase = Phase::Failed; m_error = tr("PSBT result is unavailable."); Q_EMIT changed(); return ticket; }
        m_active_lease.emplace(std::move(*held.lease));
    }
    m_active = operation; m_active_generation = generation; m_active_kind = input->kind;
    psbtwork::Context context{m_services.lookup ? m_services.lookup : psbtfee::NodeLookup(m_node), psbtwork::NodeBroadcast(m_node)};
    context.policy = m_services.policy;
    const auto consume = [owner = m_document_owner, ticket, kind = input->kind](auto done, auto failed) {
        // Opening/Submit deliver Native outcomes on Domain. Analysis ownership
        // is resolved there before OwnedCommand posts its GUI notification.
        return [owner, ticket, kind, done = std::move(done), failed = std::move(failed)](psbtwork::Result result) mutable {
            try { done(owner->consume(ticket, kind, std::move(result))); }
            catch (...) { failed(std::current_exception()); }
        };
    };
    execution::Admission admission;
    if (m_opening) {
        const bool observation = input->kind == psbtwork::Kind::Load || input->kind == psbtwork::Kind::Inspect;
        const auto action = observation ? nullptr : m_opening->startAction();
        if (observation) admission = operation->startWith([opening = m_opening, input, context,consume](auto done, auto failed) {
            auto analyzed = consume(std::move(done), failed);
            return opening->read([input, context](interfaces::Wallet& wallet) {
                return psbtwork::Execute(&wallet, std::move(*input), context);
            }, std::move(analyzed), std::move(failed),execution::WorkClass::Observation);
        });
        else if (action) admission = operation->startWith([opening = m_opening, action, input, context,consume](auto done, auto failed) {
            auto analyzed = consume(std::move(done), failed);
            return opening->perform(action, [input, context](interfaces::Wallet& wallet) {
                return psbtwork::Execute(&wallet, std::move(*input), context);
            }, std::move(analyzed), std::move(failed));
        });
        else admission = {{}, execution::Rejection::Closed};
    } else admission = operation->startWith([scheduler = m_services.scheduler, input, context, consume](auto done, auto failed) {
        auto analyzed = consume(std::move(done), failed);
        return execution::Submit(*scheduler, [input, context] { return psbtwork::Execute(nullptr, std::move(*input), context); },
            std::move(analyzed), std::move(failed), {execution::Lane::Core});
    });
    if (!admission) {
        m_active.reset(); m_active_lease.reset();
        m_phase = Phase::Failed; m_error = tr("The PSBT action could not be queued."); Q_EMIT changed(); return ticket;
    }
    m_error.clear(); m_phase = Phase::Pending;
    Q_EMIT changed();
    return ticket;
}

void PsbtQmlModel::retryPublication()
{
    if (m_active) publish(m_operation_id, m_active_generation);
}
void PsbtQmlModel::publish(quint64 ticket, quint64 generation)
{
    if (!m_active || ticket != m_operation_id) return;
    auto outcome = m_active->result();
    if (!outcome) return;
    QString clipboard;
    if (outcome->error || !outcome->value) {
        const bool not_started{execution::NotStartedCause(outcome->error)};
        m_phase = m_closed ? Phase::Closed : not_started ? Phase::Failed : Phase::Uncertain;
        m_error = not_started ? tr("The PSBT action did not start.") : tr("The PSBT action ended without a confirmed outcome. Reconcile before retrying.");
        if (!not_started && m_active_kind == psbtwork::Kind::Broadcast) m_broadcast_blocked = true;
        if (!not_started && m_active_kind == psbtwork::Kind::Sign) m_signing_blocked = true;
    } else {
        const auto& result = *outcome->value;
        if (m_active_kind == psbtwork::Kind::Broadcast && generation == m_generation) m_broadcast_blocked = true;
        if (result.submitted) m_submitted = true;
        if (result.saved) m_saved = true;
        if (result.submitted || result.code == psbtwork::Error::Uncertain) {
            m_submitted_txid = result.txid;
            if (m_active_kind == psbtwork::Kind::Broadcast && generation == m_generation) m_broadcast_blocked = true;
        }
        if ((result.code == psbtwork::Error::Uncertain || result.code == psbtwork::Error::Relock) && m_active_kind == psbtwork::Kind::Sign) m_signing_blocked = true;
        if (result.saved) m_saved_path = result.path;
        if (!m_closed && generation == m_generation) {
            if (result.quarantined_document) {
                releaseQuarantine();
                m_quarantined_document = result.quarantined_document;
                m_quarantined_operation = ticket;
                m_quarantined_generation = generation;
                if (m_active_lease) { m_quarantine_lease.emplace(std::move(*m_active_lease)); m_active_lease.reset(); }
                m_signing_blocked = true;
                m_broadcast_blocked = true;
            }
            if (result.snapshot) {
                releaseSnapshot();
                m_snapshot = result.snapshot;
                if (m_active_lease) { m_snapshot_lease.emplace(std::move(*m_active_lease)); m_active_lease.reset(); }
                prepareSummaryPaging();
                m_status = result.snapshot->status; m_error = result.snapshot->error;
                if (m_active_kind == psbtwork::Kind::Load) { m_broadcast_blocked = false; m_signing_blocked = false; }
            }
            if (!result.error.isEmpty()) { m_error = result.error; m_status = result.error; }
            if (result.submitted) m_status = tr("Transaction accepted by the local node. Transaction ID: %1").arg(result.txid);
            else if (result.saved) m_status = tr("PSBT saved.");
            else if (m_active_kind == psbtwork::Kind::Clipboard && result.code == psbtwork::Error::None) {
                clipboard = result.clipboard;
                m_status = tr("PSBT copied to clipboard.");
            }
            m_phase = result.code == psbtwork::Error::None ? Phase::Ready
                : result.code == psbtwork::Error::NeedsUnlock || result.code == psbtwork::Error::IncorrectPassphrase ? Phase::NeedsUnlock
                : result.code == psbtwork::Error::Uncertain || result.code == psbtwork::Error::Relock ? Phase::Uncertain
                : result.quarantined_document ? Phase::Unverified : Phase::Failed;
        } else m_phase = m_closed ? Phase::Closed : Phase::Stale;
    }
    // Release GUI aliases before activating native structural teardown.
    outcome.reset(); m_active.reset(); m_active_lease.reset();
    const QPointer<PsbtQmlModel> observer{this};
    if (!clipboard.isEmpty()) QGuiApplication::clipboard()->setText(clipboard);
    if (!observer) return;
    Q_EMIT changed();
    if (observer) Q_EMIT finished(ticket, generation);
}
