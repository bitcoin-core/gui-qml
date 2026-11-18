// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/models/activitydetailmodel.h>
#include <qml/models/transactionactivitymodel.h>
#include <qml/models/transactionflow.h>
#include <qml/models/walletqmlmodel.h>
#include <qml/flowpresentation.h>
#include <qml/guipresentation.h>
#include <qml/retainedvalue.h>
#include <qml/walletrequests.h>
#include <core_io.h>
#include <QClipboard>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QQmlEngine>
#include <QScopeGuard>
#include <stdexcept>
#include <utility>

struct ActivityDetailModel::Input {
    std::shared_ptr<const activitystore::Snapshot> core, result;
    std::shared_ptr<execution::ResourceLease> core_lease;
    std::shared_ptr<const activityview::Hold> previous;
    std::shared_ptr<const walletrequests::Snapshot> requests;
    activityview::Hold hold;
    QString txid;
    int unit{0};
    quint64 generation{0};
};

ActivityDetailModel* TransactionActivityModel::createDetailView(QObject* owner)
{
    return owner && !m_stopped ? new ActivityDetailModel{*this,owner}
    : nullptr;
}

ActivityDetailModel::ActivityDetailModel(TransactionActivityModel& source, QObject* owner)
: QObject(owner), m_source(&source), m_opening(source.m_opening),
m_post(presentation::ReliablePost(source.m_services.post)),
m_inputs(std::make_unique<ActivityValueListModel>(ActivityValueListModel::Kind::Inputs, this)),
m_outputs(std::make_unique<ActivityValueListModel>(ActivityValueListModel::Kind::Outputs, this)),
m_actions(std::make_unique<ActivityValueListModel>(ActivityValueListModel::Kind::Actions, this)),
m_raw(std::make_unique<ActivityValueListModel>(ActivityValueListModel::Kind::Raw, this)),
m_document(std::make_unique<ActivityValueListModel>(ActivityValueListModel::Kind::EntryDocument, this))
{
    QQmlEngine::setObjectOwnership(this, QQmlEngine::CppOwnership);
    connect(&source, &TransactionActivityModel::snapshotChanged, this, &ActivityDetailModel::refresh);
    connect(&source, &TransactionActivityModel::loadingChanged, this, [this] {
        if (!m_source || m_source->m_stopped) { closeAdmission();
            return; }
        if (m_pending && !m_operation) {
            const QPointer<ActivityDetailModel> observer{this};
            start();
            if (observer && !m_closed) Q_EMIT transactionDetailsChanged();
        }
    });
    connect(&source, &QObject::destroyed, this, &ActivityDetailModel::closeAdmission);
    connect(source.m_wallet_model, &WalletQmlModel::availableChanged, this, [this] {
        if (!m_opening || !m_opening->observationOpen()) closeAdmission();
    });
}
ActivityDetailModel::~ActivityDetailModel() { closeAdmission(); }
QAbstractItemModel* ActivityDetailModel::detailInputs() { return m_inputs.get(); }
QAbstractItemModel* ActivityDetailModel::detailOutputs() { return m_outputs.get(); }
QAbstractItemModel* ActivityDetailModel::detailActions() { return m_actions.get(); }
QAbstractItemModel* ActivityDetailModel::rawTransactionLines() { return m_raw.get(); }
QAbstractItemModel* ActivityDetailModel::entryDocument() { return m_document.get(); }
QVariantMap ActivityDetailModel::selectedTransactionDetails() const
{
    QVariantMap result;
    if (hasCurrentSnapshot() && m_hold->snapshot->selected_details) result = *m_hold->snapshot->selected_details;
    if (!m_txid.isEmpty()) result.insert("txid", m_txid);
    result.insert("detailsLoading", m_pending && active());
    if (!m_error.isEmpty()) result.insert("detailsError", m_error);
    if (m_pending || !m_full || !active() || !hasCurrentSnapshot() || !m_error.isEmpty()) result.insert("canBump", false);
    return result;
}
void ActivityDetailModel::setTransactionId(const QString& txid)
{
    if (m_closed || txid == m_txid || (!txid.isEmpty() && txid.size() != 64)) return;
    m_txid = txid;
    const auto requested = QString{txid};
    const QPointer<ActivityDetailModel> observer{this};
    refresh();
    if (observer && !m_closed && m_txid == requested) Q_EMIT transactionIdChanged();
}
void ActivityDetailModel::setActive(bool value)
{
    if (m_closed || value == m_active_interest) return;
    m_active_interest = value;
    const QPointer<ActivityDetailModel> observer{this};
    Q_EMIT activeChanged();
    if (observer && !m_closed) refresh();
}
void ActivityDetailModel::clearRows(bool quiet)
{
    const bool was_publishing = std::exchange(m_publishing,true);
    m_hold.reset();
    const QPointer<ActivityDetailModel> observer{this};
    const auto restore = qScopeGuard([observer,was_publishing] { if (observer) observer->m_publishing = was_publishing; });
    m_inputs->setSnapshot({}, quiet);
    if (!observer) return;
    m_outputs->setSnapshot({}, quiet);
    if (!observer) return;
    m_actions->setSnapshot({}, quiet);
    if (!observer) return;
    m_raw->setSnapshot({}, quiet);
    if (!observer) return;
    m_document->setSnapshot({}, quiet);
}
void ActivityDetailModel::closeAdmission()
{
    if (std::exchange(m_closed, true)) return;
    ++m_generation;
    ++m_entry_generation;
    m_pending = m_full = m_again = false;
    m_operation.reset();
    m_preview_operation.reset();
    m_entry_operation.reset();
    clearRows(true);
}
void ActivityDetailModel::refresh()
{
    if (m_closed) return;
    if (!m_opening || !m_opening->observationOpen()) { closeAdmission();
        return; }
    if (m_publishing) {
        ++m_generation;
        m_full = false;
        m_pending = active() && !m_txid.isEmpty();
        clearRows(true);
        // Revoke an in-progress model reset without nesting it.
        if (!std::exchange(m_refresh_queued,true)) {
            const QPointer<ActivityDetailModel> observer{this};
            m_post([observer] { if(observer){observer->m_refresh_queued=false;
                    observer->refresh();} });
        }
        return;
    }
    ++m_generation;
    m_full = false;
    ++m_entry_generation;
    m_preview_operation.reset();
    m_entry_operation.reset();
    m_entry_error.clear();
    m_error.clear();
    m_pending = active() && !m_txid.isEmpty();
    const QPointer<ActivityDetailModel> observer{this};
    clearRows(false);
    if (!observer || m_closed) return;
    if (m_operation) m_again = true;
    else if (m_pending) start();
    if (!observer || m_closed) return;
    if (m_pending) startPreview();
    if (observer && !m_closed) Q_EMIT transactionDetailsChanged();
}
void ActivityDetailModel::reload() { refresh(); }
std::shared_ptr<ActivityDetailModel::Input> ActivityDetailModel::captureInput()
{
    if (!m_source || !m_source->m_published || !m_source->m_published->snapshot->detail_source) return {};
    const QPointer<ActivityDetailModel> observer{this};
    const auto opening = m_opening;
    const auto generation = m_generation;
    auto input = walletopening::RetainValues<Input>(opening, [observer,generation] {
        if (!observer || observer->m_closed || observer->m_generation != generation || !observer->m_source || observer->m_source->m_stopped || !observer->m_source->m_published || !observer->m_source->m_published->snapshot->detail_source) return std::unique_ptr<Input>{};
        auto result = std::make_unique<Input>();
        result->previous = observer->m_source->m_published;
        const auto frame = std::static_pointer_cast<const TransactionActivityModel::DetailSource>(result->previous->snapshot->detail_source);
        if (!frame) return std::unique_ptr<Input>{};
        result->core = frame->core;
        result->core_lease = result->previous->lease;
        result->requests = frame->requests;
        result->txid = observer->m_txid;
        result->unit = result->previous->snapshot->display_unit;
        result->generation = generation;
        return result;
    });
    return input;
}
ActivityDetailModel::Projection ActivityDetailModel::prepare(const std::shared_ptr<Input>& input)
{
    if (input->result->detail_missing) throw std::runtime_error("This transaction is no longer available in this wallet.");
    auto view = std::make_shared<activityview::Snapshot>();
    view->display_unit = input->unit;
    view->selected_txid = input->txid;
    view->selected_raw = input->result->raw_transaction;
    view->raw = [state=input->result](const QString& id) {
        const auto found = state->records.constFind(id);
        return found != state->records.cend() && found->transaction ? QString::fromStdString(EncodeHexTx(*found->transaction)) : QString{};
    };
    const auto found = input->result->records.constFind(input->txid);
    if (found == input->result->records.cend()) throw std::runtime_error("This transaction is no longer available in this wallet.");
    QSet<QString> fulfilled;
    auto row = TransactionActivityModel::buildTransactionRow(*found,{},input->unit,fulfilled).second;
    if (input->previous && input->previous->snapshot) {
        const auto original = input->previous->snapshot->rows.find(QStringLiteral("tx:")+input->txid);
        if (original != input->previous->snapshot->rows.end()) row = *original->second;
    }
    TransactionActivityModel::prepareDetails(*view,TransactionActivityModel::buildDetails(input->txid,row,*input->result,input->requests,input->unit,true),input->unit,row);
    input->hold.snapshot = std::move(view);
    // The completed observer retains its selected record/document,
    // not an obsolete version of the complete shared history.
    input->core.reset();
    input->core_lease.reset();
    input->previous.reset();
    input->requests.reset();
    return Projection{input->generation, input->result->transaction_revision,
        std::shared_ptr<const activityview::Hold>{input,&input->hold}};
}
void ActivityDetailModel::startPreview()
{
    const QPointer<ActivityDetailModel> observer{this};
    const auto generation=m_generation;
    auto input=captureInput();
    if (!observer || m_closed || !input || generation!=m_generation) return;
    auto command=std::make_shared<execution::OwnedCommand<Projection>>(m_opening->scheduler(),[post=m_post,observer] {
        post([observer] {if(observer)observer->publishPreview();});
    });
    m_preview_operation=command;
    const auto admitted=command->start([input] {
        const auto found=input->core->records.constFind(input->txid);
        if(found==input->core->records.cend())throw std::runtime_error("This transaction is no longer available in this wallet.");
        activitystore::Snapshot state;
        state.history_loaded=true;
        state.detail_txid=input->txid;
        state.records.insert(input->txid,*found);
        interfaces::WalletTx cached{};
        cached.tx=found->transaction;
        cached.debit=found->activity.wallet_debit;
        cached.txin_is_mine=found->inputs_mine;
        cached.txout_is_mine=found->outputs_mine;
        cached.txout_is_change=found->outputs_change;
        state.flow=BuildTransactionFlow(cached,{},true);
        state.transaction_revision = input->core->transaction_revision;
        input->result=std::make_shared<const activitystore::Snapshot>(std::move(state));
        return prepare(input);
    },{execution::Lane::Domain,{},execution::WorkClass::Observation});
    if(observer&&!admitted&&m_preview_operation==command)m_preview_operation.reset();
}
void ActivityDetailModel::publishPreview()
{
    if(m_closed||!m_preview_operation)return;
    const auto result=m_preview_operation->result();
    if(!result)return;
    m_preview_operation.reset();
    if(!active()||!m_pending||!m_opening->observationOpen()||result->error||!result->value||result->value->generation!=m_generation)return;
    m_hold=result->value->hold;
    if(!publishRows())return;
    Q_EMIT transactionDetailsChanged();
}
void ActivityDetailModel::start()
{
    if (!m_pending || m_operation || !m_source) return;
    if (!m_source->m_core_state || !m_source->m_published) {
        if (!m_source->m_load_error.isEmpty()) { m_pending=false;
            m_error=m_source->m_load_error; }
        return;
    }
    if (m_source->m_core_state->transaction_revision != m_opening->transactionRevision()) return;
    const QPointer<ActivityDetailModel> observer{this};
    const auto generation=m_generation;
    auto input=captureInput();
    if (!observer || m_closed || m_generation != generation) return;
    if (!input) {
        m_pending=false;
        m_error=tr("Transaction details could not be loaded right now. Try again after pending operations finish.");
        return;
    }
    m_operation_native_revision = m_opening->transactionRevision();
    if (input->core->transaction_revision != m_operation_native_revision) return;
    auto command = std::make_shared<execution::OwnedCommand<Projection>>(m_opening->scheduler(), [post=m_post,observer] {
        post([observer] { if (observer) observer->publish(); });
    });
    m_operation = command;
    const auto native_revision = input->core->transaction_revision;
    const auto admission = command->startWith([input,opening=m_opening,native_revision](auto done,auto failed) {
        const auto failure = failed;
        return opening->read([input, native_revision](interfaces::Wallet& wallet) {
            activitystore::Snapshot state;
            state.history_loaded = true;
            state.detail_txid = input->txid;
            const auto selected = input->core->records.constFind(input->txid);
            if (selected != input->core->records.cend()) state.records.insert(input->txid,*selected);
            activitystore::Work work;
            work.details = true;
            const std::atomic_bool cancelled{false};
            work.revisions = input->core->revisions;
            input->result = std::make_shared<const activitystore::Snapshot>(
                activitystore::Reader::read(wallet, std::move(state), work, cancelled, native_revision));
            return true;
        },[input,done=std::move(done),failure](bool) {
            try {
                done(prepare(input));
            } catch (...) { failure(std::current_exception()); }
        },std::move(failed));
    });
    if (!observer || m_closed || m_operation != command) return;
    if (!admission) { m_operation.reset();
        m_again = false; // Coalesced observations share this admission failure.
        m_pending = false;
        m_error = tr("Transaction details could not be loaded right now. Try again after pending operations finish."); }
}
bool ActivityDetailModel::publishRows(bool quiet)
{
    const bool was_publishing = std::exchange(m_publishing,true);
    const QPointer<ActivityDetailModel> observer{this};
    const auto restore = qScopeGuard([observer,was_publishing] { if(observer)observer->m_publishing=was_publishing; });
    const auto generation = m_generation;
    m_inputs->setSnapshot(m_hold,quiet);
    if (!observer || m_closed || generation != m_generation) return false;
    m_outputs->setSnapshot(m_hold,quiet);
    if (!observer || m_closed || generation != m_generation) return false;
    m_actions->setSnapshot(m_hold,quiet);
    if (!observer || m_closed || generation != m_generation) return false;
    m_raw->setSnapshot(m_hold,quiet);
    return observer && !m_closed && generation == m_generation;
}
void ActivityDetailModel::publish()
{
    if (m_closed || !m_operation) return;
    const auto result = m_operation->result();
    if (!result) return;
    m_operation.reset();
    if (!m_opening->observationOpen()) { closeAdmission();
        return; }
    if (m_again || (result->value && result->value->generation != m_generation)) {
        m_again = false;
        const QPointer<ActivityDetailModel> observer{this};
        if (m_pending) start();
        if (observer && !m_closed) Q_EMIT transactionDetailsChanged();
        return;
    }
    const auto current_native_revision = m_opening->transactionRevision();
    const bool stale_native_result = result->value
        ? result->value->transaction_revision != current_native_revision
        : result->error && m_operation_native_revision != current_native_revision;
    if (stale_native_result) {
        m_pending = true;
        m_full = false;
        m_error.clear();
        const QPointer<ActivityDetailModel> observer{this};
        clearRows(false);
        if (observer && !m_closed) Q_EMIT transactionDetailsChanged();
        return;
    }
    m_pending = false;
    m_preview_operation.reset();
    if (result->error) m_error = tr("Transaction details could not be loaded right now. Try again after pending operations finish.");
    else if (result->value) {
        m_hold = result->value->hold;
        m_full = true;
    }
    if (!publishRows()) return;
    Q_EMIT transactionDetailsChanged();
}
quint64 ActivityDetailModel::requestEntryDetails(const QString& side,int row)
{
    if (!active() || !m_error.isEmpty() || !m_opening->available() || !hasCurrentSnapshot() || row < 0 || (side != QLatin1String("inputs") && side != QLatin1String("outputs") && side != QLatin1String("actions"))) return 0;
    const auto& entries = side == QLatin1String("inputs") ? m_hold->snapshot->full_inputs : side == QLatin1String("outputs") ? m_hold->snapshot->full_outputs : m_hold->snapshot->full_actions;
    if (!entries || row >= entries->size()) return 0;
    const QPointer<ActivityDetailModel> observer{this};
    const auto opening=m_opening;
    const auto generation=++m_entry_generation;
    auto input = walletopening::RetainValues<Input>(opening,[observer,generation] {
        if (!observer || !observer->active() || !observer->m_error.isEmpty() || observer->m_entry_generation != generation || !observer->hasCurrentSnapshot()) return std::unique_ptr<Input>{};
        auto value=std::make_unique<Input>();
        value->previous=observer->m_hold;
        value->generation=generation;
        return value; });
    if (!observer || !active() || !m_error.isEmpty() || m_entry_generation != generation) return 0;
    if (!input) return 0;
    auto command=std::make_shared<execution::OwnedCommand<Projection>>(m_opening->scheduler(),[post=m_post,observer] {post([observer] {if(observer)observer->publishEntry();});});
    m_entry_operation=command;
    m_entry_error.clear();
    const auto ticket=++m_ticket;
    const auto admitted=command->start([input,side,row] {
        const auto& snapshot=*input->previous->snapshot;
        const auto rows=side==QLatin1String("inputs")?snapshot.full_inputs:side==QLatin1String("outputs")?snapshot.full_outputs:snapshot.full_actions;
        auto view=std::make_shared<activityview::Snapshot>();
        view->entry_document=std::make_shared<const QVariantList>(FlowEntryDocument(rows->at(row)));
        input->hold.snapshot=std::move(view);
        return Projection{input->generation, 0, std::shared_ptr<const activityview::Hold>{input,&input->hold}};
    },{execution::Lane::Domain,{},execution::WorkClass::Observation});
    if (!observer || !active() || !m_error.isEmpty() || m_entry_generation != generation || m_entry_operation != command) return 0;
    if(!admitted){m_entry_operation.reset();
        m_entry_error=tr("Entry details could not be loaded right now. Try again after pending operations finish.");
        Q_EMIT entryDetailsChanged();
        return 0;}
    Q_EMIT entryDetailsChanged();
    return ticket;
}
void ActivityDetailModel::publishEntry()
{
    if(m_closed||!m_entry_operation)return;
    const auto result=m_entry_operation->result();
    if(!result)return;
    m_entry_operation.reset();
    if(!active()||!m_opening->observationOpen())return;
    if(result->value&&result->value->generation!=m_entry_generation)return;
    const QPointer<ActivityDetailModel> observer{this};
    if(result->error)m_entry_error=tr("Entry details could not be loaded right now. Try again after pending operations finish.");
    else if(result->value)m_document->setSnapshot(result->value->hold);
    if(observer&&!m_closed)Q_EMIT entryDetailsChanged();
}
quint64 ActivityDetailModel::requestCopyRawTransaction(const QString& txid)
{
    if(!active()||m_pending||!m_error.isEmpty()||!m_opening->available()||txid!=m_txid||!hasCurrentSnapshot()||m_hold->snapshot->selected_raw.isEmpty()||!m_hold->snapshot->raw)return 0;
    struct Copy {std::shared_ptr<const activityview::Hold> hold;
        QString text;};
    const QPointer<ActivityDetailModel> observer{this};
    const auto opening=m_opening;
    const auto generation=m_generation;
    auto copy=walletopening::RetainValues<Copy>(opening,[observer,generation] {
        if (!observer || !observer->active() || observer->m_generation != generation || !observer->hasCurrentSnapshot()) return std::unique_ptr<Copy>{};
        return std::make_unique<Copy>(Copy{observer->m_hold,{}});
    });
    if (!observer || !active() || m_generation != generation) return 0;
    if(!copy)return 0;
    const auto ticket=++m_ticket;
    const auto complete=[copy,observer,ticket](bool prepared) {
        QMetaObject::invokeMethod(QCoreApplication::instance(),[copy,observer,ticket,prepared] {
            const bool copied=prepared&&!copy->text.isEmpty()&&QGuiApplication::clipboard();
            if(copied)QGuiApplication::clipboard()->setText(copy->text);
            if(observer&&observer->active()&&observer->m_opening->observationOpen())Q_EMIT observer->copyFinished(ticket,copied,copied?QString{}
            :tr("The raw transaction could not be copied."));
        },Qt::QueuedConnection);
    };
    const auto admitted=execution::Submit(*m_opening->scheduler(),[copy,txid] {copy->text=copy->hold->snapshot->raw(txid);
        return true;},complete,[complete](std::exception_ptr) {complete(false);},{execution::Lane::Domain,{},execution::WorkClass::Action});
    return admitted?ticket:0;
}
void ActivityDetailModel::retryPublications() { publishPreview();
    publish();
    publishEntry(); }
