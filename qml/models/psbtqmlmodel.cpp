// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/psbtqmlmodel.h>
#include <qml/asyncjoin.h>
#include <qml/executor.h>
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

namespace {
template <typename T, typename Factory>
std::shared_ptr<T> RetainValue(const psbtfee::Services& services, const std::shared_ptr<walletopening::Session>& opening, Factory make)
{
    if (opening) return walletopening::RetainValues<T>(opening, std::move(make));
    if (!services.value_cleanup) return {};
    return execution::RetainValues<T>(services.value_cleanup, std::move(make));
}

execution::SubmitResult PostTask(execution::SerialQueue& queue, std::shared_ptr<execution::Task> task,
                                 std::shared_ptr<execution::SerialQueue> outcomes = {})
{
    const bool accepted = queue.post([task = std::move(task), outcomes = std::move(outcomes)]() mutable {
        task->execute();
        const auto deliver = [task] { task->deliver(); };
        if (!outcomes || !outcomes->post(deliver)) deliver();
    });
    return {{}, accepted ? execution::Rejection::None : execution::Rejection::Transport};
}

psbtwork::Result ReadForInspection(psbtwork::Request& request)
{
    psbtwork::Result result;
    result.path = request.path;
    try {
        PartiallySignedTransaction document{CMutableTransaction{}, 2};
        result.error = psbtwork::LoadFile(request.path, document);
        if (result.error.isEmpty()) {
            request.document = std::make_shared<const PartiallySignedTransaction>(std::move(document));
            request.kind = psbtwork::Kind::Inspect;
            request.proof.reset();
        } else {
            result.code = psbtwork::Error::Invalid;
        }
    } catch (...) {
        result.code = psbtwork::Error::Core;
        result.error = QObject::tr("The PSBT could not be processed.");
    }
    return result;
}
} // namespace

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
QString PsbtQmlModel::PsbtErrorText(common::PSBTError error) { return psbtwork::ErrorText(error); }
bool PsbtQmlModel::IsMultisigPsbtInput(const PartiallySignedTransaction& document, size_t index) { return psbtwork::Multisig(document, index).has_value(); }

struct PsbtQmlModel::OwnedQueues {
    std::shared_ptr<execution::Executor> executor{std::make_shared<execution::Executor>(execution::Executor::Config{})};
    std::shared_ptr<execution::SerialQueue> core{std::make_shared<execution::SerialQueue>(executor->makeQueue(execution::Lane::Core))};
    std::shared_ptr<execution::SerialQueue> utility{std::make_shared<execution::SerialQueue>(executor->makeQueue(execution::Lane::Utility))};
    std::shared_ptr<execution::ValueCleanupQueue> values{std::make_shared<execution::ValueCleanupQueue>(
        std::make_shared<execution::SerialQueue>(executor->makeQueue(execution::Lane::Utility)))};

    void retire()
    {
        auto stopped = JoinThreadAfterDrain([executor = std::move(executor)] { executor->stopAndJoin(); },
            QCoreApplication::instance(), [] {});
        // Accepted file reads can still schedule their Core inspection.
        values->close([core = core, utility = utility, stopped = std::move(stopped)]() mutable {
            core->close();
            core->whenDrained([utility, stopped = std::move(stopped)]() mutable {
                utility->close();
                utility->whenDrained(std::move(stopped));
            });
        });
    }
};

PsbtQmlModel::PsbtQmlModel(bool wallet_expected, interfaces::Node* node, QObject* parent)
    : QObject(parent), m_node(node), m_wallet_expected(wallet_expected) {}

PsbtQmlModel::~PsbtQmlModel()
{
    close();
    if (m_owned_queues) m_owned_queues->retire();
}

void PsbtQmlModel::configureSession(std::shared_ptr<walletopening::Session> opening, psbtfee::Services services)
{
    Q_ASSERT(!m_active && !m_snapshot);
    m_opening = std::move(opening);
    setFeeServices(std::move(services));
}
void PsbtQmlModel::setFeeServices(psbtfee::Services services)
{
    // In-flight work retains its original services and Native lifetime.
    retireDocumentOwner();
    m_services = std::move(services);
    m_services.post=presentation::ReliablePost(std::move(m_services.post));
}
void PsbtQmlModel::ensureServices()
{
    if (!m_opening && (!m_services.core || !m_services.utility || !m_services.value_cleanup)) {
        if (!m_owned_queues) m_owned_queues = std::make_unique<OwnedQueues>();
        if (!m_services.core || !m_services.utility) {
            m_services.core = m_owned_queues->core;
            m_services.utility = m_owned_queues->utility;
        }
        if (!m_services.value_cleanup) {
            m_services.value_cleanup = [values = m_owned_queues->values](auto cleanup) { return values->hold(std::move(cleanup)); };
        }
    }
    if (!m_services.post) {
        m_bridge = presentation::MakeGuiObject();
        m_services.post = [bridge = m_bridge](std::function<void()> turn) {
            QMetaObject::invokeMethod(bridge.get(), std::move(turn), Qt::QueuedConnection);
        };
    }
    if (!m_document_owner) {
        m_document_owner = RetainValue<psbtdocument::Store>(m_services, m_opening,
            [generation = m_generation] { return std::make_unique<psbtdocument::Store>(generation); });
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
void PsbtQmlModel::close()
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
    close();
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
    if (m_summary_query || !m_snapshot || !m_snapshot->summary_source) return;

    const auto state = RetainValue<SummaryRequestState>(m_services, m_opening,
        [] { return std::make_unique<SummaryRequestState>(); });
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
    execution::SnapshotQuery<psbtwork::SummaryPage>::Post submit;
    if (m_opening) {
        submit = [opening = m_opening](auto task) {
            return opening->postDomainTask(std::move(task), execution::WorkClass::Observation);
        };
    } else if (m_services.utility) {
        submit = [utility = m_services.utility](auto task) { return PostTask(*utility, std::move(task), utility); };
    } else {
        return;
    }
    m_summary_query = std::make_shared<execution::SnapshotQuery<psbtwork::SummaryPage>>(
        [state] { return state->read(); },
        [post = m_services.post, observer, generation, epoch] {
            post([observer, generation, epoch] {
                if (observer) observer->publishSummaryPage(generation, epoch);
            });
        }, std::move(submit));
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
quint64 PsbtQmlModel::requestSign() { psbtwork::Request request; request.kind = psbtwork::Kind::Sign; return start(std::move(request)); }
QString PsbtQmlModel::unverifiedResultToken() const
{
    if (m_closed || !m_quarantined_document || m_quarantined_generation != m_generation ||
        (m_opening && !m_opening->available())) return {};
    return QString::number(m_quarantined_operation) + QLatin1Char(':') + QString::number(m_quarantined_generation);
}
quint64 PsbtQmlModel::requestSignWithPassphrase(const QString& passphrase)
{
    if(busy()||m_closed||!canSign())return 0;
    ensureServices();
    if (!m_document_owner) {
        m_phase = Phase::Failed; m_error = tr("PSBT result is unavailable."); Q_EMIT changed(); return 0;
    }
    const auto captured = m_opening
        ? secretinput::Capture(m_opening, passphrase)
        : secretinput::Capture(m_services.value_cleanup, passphrase);
    if(!captured){m_phase=Phase::Failed;m_error=tr("The password could not be used for signing. Try again.");Q_EMIT changed();return 0;}
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
    const auto opening = m_opening;
    auto input = RetainValue<psbtwork::Request>(m_services, opening, [] { return std::make_unique<psbtwork::Request>(); });
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
    auto operation = std::make_shared<Operation>([post = m_services.post, observer, ticket, generation, receipt, operation_ref] {
        if (const auto operation = operation_ref->lock()) if (const auto result = operation->result()) {
            if (result->value) receipt->record(*result->value);
            else {
                psbtwork::Result failed;
                failed.code = psbtwork::Error::Uncertain;
                failed.error = tr("The PSBT action may not have finished. Check its status before trying again.");
                receipt->record(failed);
            }
        }
        post([observer, ticket, generation] { if (observer) observer->publish(ticket, generation); });
    });
    *operation_ref = operation;
    m_receipt = receipt;
    if (input->kind == psbtwork::Kind::Broadcast) m_submission_receipt = receipt;
    {
        // Every action result retains Native destruction, including an accepted
        // raw transaction or a large clipboard value. Reserve before dispatch.
        auto held = RetainValue<std::shared_ptr<Operation>>(m_services, opening,
            [&operation] { return std::make_unique<std::shared_ptr<Operation>>(operation); });
        if (!held) { m_phase = Phase::Failed; m_error = tr("PSBT result is unavailable."); Q_EMIT changed(); return ticket; }
        m_active_lease = std::move(held);
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
    execution::SubmitResult started;
    if (m_opening) {
        const bool observation = input->kind == psbtwork::Kind::Load || input->kind == psbtwork::Kind::Inspect;
        const auto action = observation ? nullptr : m_opening->startAction();
        if (const auto utility = m_opening->utilityQueue(); input->kind == psbtwork::Kind::Load && utility) {
            started = operation->startWith([opening = m_opening, utility, input, context, consume](auto done, auto failed) {
                auto analyzed = consume(std::move(done), failed);
                const bool accepted = utility->post([opening, input, context, analyzed = std::move(analyzed), failed]() mutable {
                    if (!opening->observationOpen()) {
                        failed(std::make_exception_ptr(execution::NotStarted{execution::NotStarted::Cause::Cancelled}));
                        return;
                    }
                    auto result = ReadForInspection(*input);
                    if (!result.error.isEmpty()) {
                        analyzed(std::move(result));
                        return;
                    }
                    try {
                        const auto review = opening->read([input, context](interfaces::Wallet& wallet) {
                            return psbtwork::Execute(&wallet, std::move(*input), context);
                        }, std::move(analyzed), failed);
                        if (!review) {
                            const auto cause = review.rejection == execution::Rejection::Closed
                                ? execution::NotStarted::Cause::Cancelled : execution::NotStarted::Cause::Transport;
                            failed(std::make_exception_ptr(execution::NotStarted{cause}));
                        }
                    } catch (...) {
                        failed(std::current_exception());
                    }
                });
                return execution::SubmitResult{{}, accepted ? execution::Rejection::None : execution::Rejection::Transport};
            });
        } else if (observation) started = operation->startWith([opening = m_opening, input, context,consume](auto done, auto failed) {
            auto analyzed = consume(std::move(done), failed);
            return opening->read([input, context](interfaces::Wallet& wallet) {
                return psbtwork::Execute(&wallet, std::move(*input), context);
            }, std::move(analyzed), std::move(failed),execution::WorkClass::Observation);
        });
        else if (action) started = operation->startWith([opening = m_opening, action, input, context,consume](auto done, auto failed) {
            auto analyzed = consume(std::move(done), failed);
            return opening->perform(action, [input, context](interfaces::Wallet& wallet) {
                return psbtwork::Execute(&wallet, std::move(*input), context);
            }, std::move(analyzed), std::move(failed));
        });
        else started = {{}, execution::Rejection::Closed};
    } else {
        started = operation->startWith([core = m_services.core, utility = m_services.utility, input, context, consume](auto done, auto failed) {
            auto analyzed = consume(std::move(done), failed);
            auto work = [input, context] { return psbtwork::Execute(nullptr, std::move(*input), context); };
            if (input->kind == psbtwork::Kind::Clipboard) {
                return PostTask(*utility, execution::MakeTask(std::move(work), std::move(analyzed), std::move(failed)));
            }
            if (input->kind != psbtwork::Kind::Load) {
                return PostTask(*core, execution::MakeTask(std::move(work), std::move(analyzed), std::move(failed)), utility);
            }
            const bool accepted = utility->post([core, utility, input, work = std::move(work), analyzed = std::move(analyzed), failed]() mutable {
                auto result = ReadForInspection(*input);
                if (!result.error.isEmpty()) {
                    analyzed(std::move(result));
                    return;
                }
                try {
                    if (PostTask(*core, execution::MakeTask(std::move(work), std::move(analyzed), failed), utility)) return;
                    failed(std::make_exception_ptr(execution::NotStarted{execution::NotStarted::Cause::Transport}));
                } catch (...) {
                    failed(std::current_exception());
                }
            });
            return execution::SubmitResult{{}, accepted ? execution::Rejection::None : execution::Rejection::Transport};
        });
    }
    if (!started) {
        m_active.reset(); m_active_lease.reset();
        m_phase = Phase::Failed; m_error = tr("The PSBT action could not start. Try again."); Q_EMIT changed(); return ticket;
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
        m_error = not_started ? tr("The PSBT action did not start.") : tr("The PSBT action may not have finished. Check its status before trying again.");
        if (!not_started && m_active_kind == psbtwork::Kind::Broadcast) m_broadcast_blocked = true;
        if (!not_started && m_active_kind == psbtwork::Kind::Sign) m_signing_blocked = true;
    } else {
        const auto& result = *outcome->value;
        if (m_active_kind == psbtwork::Kind::Broadcast && generation == m_generation) m_broadcast_blocked = true;
        if (result.submitted) m_submitted = true;
        if (result.submitted || result.code == psbtwork::Error::Uncertain) {
            m_submitted_txid = result.txid;
            if (m_active_kind == psbtwork::Kind::Broadcast && generation == m_generation) m_broadcast_blocked = true;
        }
        if ((result.code == psbtwork::Error::Uncertain || result.code == psbtwork::Error::Relock) && m_active_kind == psbtwork::Kind::Sign) m_signing_blocked = true;
        if (!m_closed && generation == m_generation) {
            if (result.quarantined_document) {
                releaseQuarantine();
                m_quarantined_document = result.quarantined_document;
                m_quarantined_operation = ticket;
                m_quarantined_generation = generation;
                if (m_active_lease) m_quarantine_lease = std::move(m_active_lease);
                m_signing_blocked = true;
                m_broadcast_blocked = true;
            }
            if (result.snapshot) {
                releaseSnapshot();
                m_snapshot = result.snapshot;
                if (m_active_lease) m_snapshot_lease = std::move(m_active_lease);
                prepareSummaryPaging();
                m_status = result.snapshot->status; m_error = result.snapshot->error;
                if (m_active_kind == psbtwork::Kind::Load) { m_broadcast_blocked = false; m_signing_blocked = false; }
            }
            if (!result.error.isEmpty()) { m_error = result.error; m_status = result.error; }
            if (result.submitted) m_status = tr("Transaction accepted by the local node. Transaction ID: %1").arg(result.txid);
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
