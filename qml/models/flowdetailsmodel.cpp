// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/models/flowdetailsmodel.h>

#include <qml/flowpresentation.h>
#include <qml/bitcoinunits.h>
#include <qml/guipresentation.h>
#include <qml/retainedvalue.h>
#include <algorithm>
#include <QSignalBlocker>
#include <mutex>
#include <stdexcept>

struct FlowDetailInput {
    QVariantMap flow;
    QVariantList recipients;
    std::shared_ptr<const void> original_anchor;
    quint64 generation{0};
    int display_unit{0};
};
namespace {
QVariantMap FormatRecipient(QVariantMap entry, int display_unit)
{
    bool valid{false}; const auto value = entry.value("amountSat"); const auto amount = value.toLongLong(&valid);
    if (!valid || value.isNull() || !MoneyRange(amount)) throw std::runtime_error("Reviewed recipient has no valid raw amount");
    const auto unit = QmlBitcoinUnits::fromDisplayUnit(display_unit);
    const QString formatted_amount = QmlBitcoinUnits::formatForDisplay(unit, amount) + QLatin1Char(' ') + QmlBitcoinUnits::label(unit, amount);
    entry["amount"] = formatted_amount;
    return entry;
}
}
struct FlowDetailSnapshot {
    std::shared_ptr<const FlowDetailInput> source;
    QVariantList inputs, outputs, recipients, document;
    quint64 generation{0};
};
struct FlowDetailsModel::State {
    std::mutex mutex;
    std::shared_ptr<const FlowDetailInput> source;
    quint64 generation{0};
};
FlowDetailRowsModel::FlowDetailRowsModel(Kind kind, std::shared_ptr<const std::atomic_bool> closed, QObject* parent)
    : QAbstractListModel(parent), m_kind(kind), m_closed(std::move(closed)) {}
const QVariantList* FlowDetailRowsModel::values() const
{
    if (*m_closed || !m_snapshot) return nullptr;
    switch (m_kind) {
    case Kind::Inputs: return &m_snapshot->inputs;
    case Kind::Outputs: return &m_snapshot->outputs;
    case Kind::Recipients: return &m_snapshot->recipients;
    case Kind::Document: return &m_snapshot->document;
    }
    return nullptr;
}
int FlowDetailRowsModel::rowCount(const QModelIndex& parent) const
{ const auto* list = values(); return parent.isValid() || !list ? 0 : static_cast<int>(list->size()); }
QVariant FlowDetailRowsModel::data(const QModelIndex& index, int role) const
{
    const auto* list = values();
    return list && index.isValid() && index.model() == this && index.column() == 0 && index.row() >= 0 && index.row() < list->size()
        && (role == Qt::DisplayRole || role == Qt::UserRole) ? list->at(index.row()) : QVariant{};
}
void FlowDetailRowsModel::setSnapshot(std::shared_ptr<const FlowDetailSnapshot> snapshot, bool quiet)
{
    const auto sequence = ++m_sequence;
    if (quiet || m_resetting) { m_snapshot = std::move(snapshot); return; }
    m_resetting = true; const QPointer<FlowDetailRowsModel> guard{this};
    beginResetModel(); if (!guard) return;
    if (*m_closed) { QSignalBlocker blocked{this}; m_snapshot.reset(); m_resetting = false; endResetModel(); return; }
    if (sequence == m_sequence) m_snapshot = std::move(snapshot);
    m_resetting = false; endResetModel();
}
FlowDetailsModel::FlowDetailsModel(QObject* parent) : QObject(parent), m_state(std::make_shared<State>()) {}
FlowDetailsModel::~FlowDetailsModel() { close(); }
void FlowDetailsModel::setServices(presentation::Post publish, execution::SnapshotQuery<FlowDetailProjection>::Post submit,
                                   execution::ValueCleanup hold)
{
    if (*m_closed || m_query || !submit || !hold) return;
    m_publish = presentation::ReliablePost(std::move(publish));
    m_submit = std::move(submit);
    m_hold = std::move(hold);
    const QPointer<FlowDetailsModel> observer{this};
    m_query = std::make_shared<execution::SnapshotQuery<FlowDetailProjection>>([state = m_state, hold = m_hold] {
            std::shared_ptr<const FlowDetailInput> source; quint64 generation;
            { std::lock_guard lock{state->mutex}; source = state->source; generation = state->generation; }
            if (!source) return FlowDetailProjection{generation, {}};
            auto snapshot = execution::RetainValues<FlowDetailSnapshot>(hold, [&] {
                auto result = std::make_unique<FlowDetailSnapshot>(); result->source = source; result->generation = source->generation;
                for (const auto& value : source->flow.value("inputs").toList()) result->inputs.append(BoundedFlowEntry(value.toMap()));
                for (const auto& value : source->flow.value("outputs").toList()) result->outputs.append(BoundedFlowEntry(value.toMap()));
                for (const auto& value : source->recipients) result->recipients.append(BoundedFlowEntry(FormatRecipient(value.toMap(), source->display_unit)));
                return result;
            });
            return FlowDetailProjection{generation, std::move(snapshot)};
        }, [post = m_publish, observer] { post([observer] { if (observer) observer->publishSnapshot(); }); },
        m_submit);
}
void FlowDetailsModel::publishSource(const QVariantMap& flow, const QVariantList& recipients, QObject* context,
                                    quint64 revision, std::shared_ptr<const void> original_anchor, int display_unit)
{
    if (*m_closed) return;
    const auto generation = ++m_generation;
    const QPointer<FlowDetailsModel> observer{this};
    const QPointer<QObject> original_context{context};
    // Keep original inputs and their Native backing owner independent of GUI
    // reset/admission callbacks. Implicit Qt value copies are bounded here.
    const auto anchor = std::move(original_anchor);
    const auto original_flow = QVariantMap{flow};
    const auto original_recipients = QVariantList(recipients);
    const auto hold = m_hold;
    const auto state = m_state;
    const auto query = m_query;
    const auto owns = [observer, original_context, generation, revision] {
        return observer && !*observer->m_closed && observer->m_generation == generation &&
            observer->m_context == original_context && observer->m_revision == revision;
    };
    QObject::disconnect(m_context_connection); m_context = context; m_revision = revision;
    if (context) m_context_connection = connect(context, &QObject::destroyed, this, &FlowDetailsModel::clearSource);
    m_snapshot.reset(); m_document_command.reset(); m_document_pending = false; m_document_error.clear(); clearRows(false);
    if (!owns()) return;
    auto source = query && original_context ? execution::RetainValues<FlowDetailInput>(hold,
        [original_flow, original_recipients, anchor, generation, display_unit] {
        return std::make_unique<FlowDetailInput>(FlowDetailInput{original_flow, original_recipients, anchor, generation, display_unit});
    }) : std::shared_ptr<const FlowDetailInput>{};
    if (!owns()) return;
    { std::lock_guard lock{state->mutex}; state->source = source; state->generation = generation; }
    m_pending = bool(source && query); m_error = m_pending ? QString{} : tr("Review details are not available.");
    if (m_pending) query->request();
    if (!owns()) return;
    Q_EMIT snapshotChanged();
    if (owns()) Q_EMIT entryDetailsChanged();
}
void FlowDetailsModel::publishSnapshot()
{
    if (*m_closed || !m_query) return;
    const auto result = m_query->take(); if (!result) return;
    if (!m_context) return;
    if (result->snapshot && result->snapshot->generation != m_generation) return;
    const auto snapshot = result->snapshot ? result->snapshot->snapshot : std::shared_ptr<const FlowDetailSnapshot>{};
    if (snapshot && snapshot->generation != m_generation) return;
    m_pending = false;
    if (!snapshot) { m_error = tr("Review details could not be prepared."); Q_EMIT snapshotChanged(); return; }
    m_snapshot = snapshot; m_error.clear();
    const QPointer<FlowDetailsModel> guard{this};
    m_inputs.setSnapshot(snapshot); if (!guard || *m_closed || snapshot->generation != m_generation) return;
    m_outputs.setSnapshot(snapshot); if (!guard || *m_closed || snapshot->generation != m_generation) return;
    m_recipients.setSnapshot(snapshot); if (!guard || *m_closed || snapshot->generation != m_generation) return;
    Q_EMIT snapshotChanged();
}
bool FlowDetailsModel::requestEntryDetails(const QString& side, int row, QObject* expected_context)
{
    if (*m_closed || m_pending || !m_snapshot || !m_context || m_context != expected_context || row < 0) return false;
    const auto source = m_snapshot->source;
    const auto entries = side == "inputs" ? source->flow.value("inputs").toList() : side == "outputs" ? source->flow.value("outputs").toList()
        : side == "recipients" ? source->recipients : QVariantList{};
    if (row >= entries.size()) return false;
    const auto generation = m_generation;
    const QPointer<FlowDetailsModel> observer{this};
    auto command = std::make_shared<execution::OwnedCommand<std::shared_ptr<const FlowDetailSnapshot>>>(
        [post = m_publish, observer] { post([observer] { if (observer) observer->publishDocument(); }); });
    m_document_generation = generation; m_document_command = command; m_document_pending = true; m_document_error.clear();
    const QPointer<FlowDetailsModel> guard{this}; m_document.setSnapshot({});
    if (!guard || *m_closed || generation != m_generation || m_document_command != command) return false;
    auto work = [source, side, row, hold = m_hold] {
        return execution::RetainValues<FlowDetailSnapshot>(hold, [&] {
            auto result = std::make_unique<FlowDetailSnapshot>(); result->source = source; result->generation = source->generation;
            const auto full = side == "recipients" ? source->recipients : source->flow.value(side).toList();
            result->document = FlowEntryDocument(side == "recipients" ? QVariant{FormatRecipient(full.at(row).toMap(), source->display_unit)} : full.at(row)); return result;
        });
    };
    const auto submit = m_submit;
    const auto started = command->startWith([&](auto done, auto failed) {
        return submit(execution::MakeTask(std::move(work), std::move(done), std::move(failed)));
    });
    if (!guard || *m_closed || generation != m_generation || m_document_command != command) return false;
    if (!started) {
        m_document_pending = false; m_document_error = tr("Entry details could not be loaded. Try again."); Q_EMIT entryDetailsChanged(); return false;
    }
    Q_EMIT entryDetailsChanged(); return true;
}
void FlowDetailsModel::publishDocument()
{
    if (*m_closed || !m_document_command || !m_document_pending) return;
    const auto result = m_document_command->result(); if (!result) return;
    m_document_pending = false;
    const auto snapshot = result->value ? *result->value : std::shared_ptr<const FlowDetailSnapshot>{};
    if (m_document_generation != m_generation || !m_context) return;
    if (!snapshot || snapshot->generation != m_generation) m_document_error = tr("Entry details could not be prepared.");
    else {
        m_document_error.clear(); const QPointer<FlowDetailsModel> guard{this}; m_document.setSnapshot(snapshot);
        if (!guard || *m_closed || snapshot->generation != m_generation) return;
    }
    Q_EMIT entryDetailsChanged();
}
void FlowDetailsModel::clearRows(bool quiet)
{
    const QPointer<FlowDetailsModel> guard{this};
    const auto generation = m_generation;
    m_inputs.setSnapshot({}, quiet); if (!guard || generation != m_generation) return;
    m_outputs.setSnapshot({}, quiet); if (!guard || generation != m_generation) return;
    m_recipients.setSnapshot({}, quiet); if (!guard || generation != m_generation) return;
    m_document.setSnapshot({}, quiet);
}
void FlowDetailsModel::clearSource()
{
    if (*m_closed) return;
    const auto generation = ++m_generation; QObject::disconnect(m_context_connection); m_context = nullptr; m_revision = 0;
    { std::lock_guard lock{m_state->mutex}; m_state->source.reset(); m_state->generation = m_generation; }
    m_snapshot.reset(); m_document_command.reset(); m_pending = m_document_pending = false; m_error.clear(); m_document_error.clear();
    const QPointer<FlowDetailsModel> guard{this}; clearRows(false);
    if (!guard || *m_closed || generation != m_generation) return;
    Q_EMIT snapshotChanged(); if (guard && !*m_closed && generation == m_generation) Q_EMIT entryDetailsChanged();
}
void FlowDetailsModel::close()
{
    if (m_closed->exchange(true)) return;
    ++m_generation; if (m_query) m_query->close(); m_query.reset();
    { std::lock_guard lock{m_state->mutex}; m_state->source.reset(); m_state->generation = m_generation; }
    m_context = nullptr; m_snapshot.reset(); m_document_command.reset(); m_pending = m_document_pending = false; clearRows(true);
}
void FlowDetailsModel::retryPublications() { publishSnapshot(); publishDocument(); }
