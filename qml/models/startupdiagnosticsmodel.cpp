// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/models/startupdiagnosticsmodel.h>
#include <qml/guipresentation.h>
#include <qml/retainedvalue.h>
#include <QPointer>
#include <QSet>
#include <cstdlib>
#include <stdexcept>
#include <vector>
#include <utility>

bool startupdiagnostics::Collection::append(QString message, bool warning) {
    if (static_cast<size_t>(message.size()) > MAX_CHARS) return false;
    return append(QStringList{std::move(message)}, warning);
}
bool startupdiagnostics::Collection::append(QStringList messages, bool warning) {
    if (static_cast<size_t>(messages.size()) > MAX_BATCH_MESSAGES) return false;
    // At most64 O(1) string-size reads. This is bounded intake accounting,
    // never an arbitrary list walk or text normalization on GUI.
    size_t chars{0};
    for (const auto& message : messages) {
        if (static_cast<size_t>(message.size()) > MAX_CHARS - chars) return false;
        chars += static_cast<size_t>(message.size());
    }
    std::lock_guard lock{mutex};
    if (sealed || owned_batches >= MAX_BATCHES || static_cast<size_t>(messages.size()) > MAX_MESSAGES - owned_messages || chars > MAX_CHARS - owned_chars) return false;
    batches.push_back({warning, std::move(messages), {}});
    owned_chars += chars; owned_messages += static_cast<size_t>(batches.back().messages.size()); ++owned_batches;
    return true;
}
bool startupdiagnostics::Collection::adopt(std::shared_ptr<const Collection> previous) {
    if (!previous) return true;
    if (previous.get() == this || !previous->sealed) return false;
    const auto chars = previous->owned_chars.load(), messages = previous->owned_messages.load(), count = previous->owned_batches.load();
    std::lock_guard lock{mutex};
    if (sealed || count >= MAX_BATCHES - owned_batches || messages > MAX_MESSAGES - owned_messages || chars > MAX_CHARS - owned_chars) return false;
    batches.push_back({false, {}, std::move(previous)});
    owned_chars += chars; owned_messages += messages; owned_batches += count + 1;
    return true;
}
int startupdiagnostics::FinalExitStatus(int original, bool diagnostic_loss) {
    return original == EXIT_SUCCESS && diagnostic_loss ? EXIT_FAILURE : original;
}
void startupdiagnostics::Collection::seal() const { std::lock_guard lock{mutex}; sealed = true; }
std::shared_ptr<const QString> startupdiagnostics::RetainText(const std::shared_ptr<execution::Scheduler>& scheduler,
    const std::shared_ptr<TextBudget>& budget, QString message) {
    if (!budget || static_cast<uint64_t>(message.size()) > TextBudget::MAX_CHARS) return {};
    const auto chars = static_cast<uint64_t>(message.size());
    const auto before = budget->used.fetch_add(chars);
    if (before > TextBudget::MAX_CHARS - chars) { budget->used.fetch_sub(chars); return {}; }
    struct Text {
        QString value;
        std::shared_ptr<TextBudget> budget;
        uint64_t chars;
        ~Text() { budget->used.fetch_sub(chars); }
    };
    std::shared_ptr<Text> retained;
    try {
        retained = execution::RetainValues<Text>(scheduler, [&] {
            return std::unique_ptr<Text>{new Text{std::move(message), budget, chars}};
        });
    } catch (...) { budget->used.fetch_sub(chars); return {}; }
    if (!retained) { budget->used.fetch_sub(chars); return {}; }
    return std::shared_ptr<const QString>{retained, &retained->value};
}
std::shared_ptr<startupdiagnostics::Collection> startupdiagnostics::Create(const std::shared_ptr<execution::Scheduler>& scheduler) {
    return execution::RetainValues<Collection>(scheduler, [] { return std::make_unique<Collection>(); });
}
StartupDiagnosticsModel::StartupDiagnosticsModel(execution::SnapshotServices services, QObject* parent)
    : QAbstractListModel(parent), m_services(std::move(services)), m_input(startupdiagnostics::Create(m_services.scheduler)) {
    m_services.post = presentation::ReliablePost(std::move(m_services.post));
    if (!m_input) m_error = tr("Startup diagnostic is unavailable.");
}
StartupDiagnosticsModel::~StartupDiagnosticsModel() { retire(false); }
int StartupDiagnosticsModel::rowCount(const QModelIndex& parent) const { return parent.isValid() || !m_prepared ? 0 : m_prepared->rows.size(); }
QVariant StartupDiagnosticsModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount() || role != Qt::UserRole + 1) return {};
    return m_prepared->rows[index.row()];
}
QHash<int, QByteArray> StartupDiagnosticsModel::roleNames() const { return {{Qt::UserRole + 1, "content"}}; }
QString StartupDiagnosticsModel::summary() const { return m_prepared ? m_prepared->summary : QString{}; }
bool StartupDiagnosticsModel::hasWarnings() const { return m_prepared && m_prepared->warnings; }
bool StartupDiagnosticsModel::append(QString message, bool warning) {
    if (m_closed || m_busy || !m_input) return false;
    const bool accepted = m_input->append(std::move(message), warning);
    if (!accepted) m_intake_error = tr("Some complete diagnostics could not be retained within the available capacity.");
    return accepted;
}
bool StartupDiagnosticsModel::append(QStringList messages, bool warning) {
    if (m_closed || m_busy || !m_input) return false;
    const bool accepted = m_input->append(std::move(messages), warning);
    if (!accepted) m_intake_error = tr("Some complete diagnostics could not be retained within the available capacity.");
    return accepted;
}
bool StartupDiagnosticsModel::adopt(std::shared_ptr<const startupdiagnostics::Collection> previous) {
    if (m_closed || m_busy || !m_input) return false;
    const bool accepted = m_input->adopt(std::move(previous));
    if (!accepted) m_intake_error = tr("Some complete diagnostics could not be retained within the available capacity.");
    return accepted;
}
bool StartupDiagnosticsModel::project(bool success, QString failure, QString warning_heading, QString error_prefix, bool normalize) {
    if (m_closed || m_busy || !m_input) return false;
    const auto scheduler = m_diagnostic_scope ? m_diagnostic_scope : m_services.scheduler;
    auto prepared = execution::RetainValues<Prepared>(scheduler, [] { return std::make_unique<Prepared>(); });
    if (!prepared) { m_error = tr("Startup diagnostic is unavailable."); m_deferred_changed = true; flushChanges(); return false; }
    const QPointer<StartupDiagnosticsModel> observer{this};
    auto operation = std::make_shared<execution::OwnedCommand<std::shared_ptr<Prepared>>>(scheduler,
        [post = m_services.post, fallback = presentation::QueuedPost(), observer] {
            const auto wake = [observer] { if (observer) observer->publish(); };
            try { post(wake); } catch (...) { fallback(wake); }
        });
    auto input = m_input;
    const auto generation = ++m_generation;
    m_operation = operation; m_busy = true; m_error.clear(); m_authoritative_input = input;
    const auto admitted = operation->start([input, prepared, success, failure = std::move(failure), warning_heading = std::move(warning_heading), error_prefix = std::move(error_prefix), normalize, intake_error = m_intake_error] {
        QStringList warnings, errors; QSet<QString> warning_set, error_set;
        // Explicit stack avoids recursively walking adopted collections.
        struct Cursor { const startupdiagnostics::Collection* collection; std::list<startupdiagnostics::Collection::Batch>::const_iterator next; };
        input->seal();
        std::vector<Cursor> stack{{input.get(), input->batches.begin()}};
        while (!stack.empty()) {
            auto& cursor = stack.back();
            if (cursor.next == cursor.collection->batches.end()) { stack.pop_back(); continue; }
            const auto& batch = *cursor.next++;
            if (batch.previous) { batch.previous->seal(); stack.push_back({batch.previous.get(), batch.previous->batches.begin()}); continue; }
            for (const auto& raw : batch.messages) {
                const auto text = normalize ? raw.trimmed() : raw;
                auto& seen = batch.warning ? warning_set : error_set;
                if (text.isEmpty() || seen.contains(text)) continue;
                seen.insert(text); (batch.warning ? warnings : errors).push_back(text);
            }
        }
        prepared->warnings = !warnings.empty();
        QString text;
        if (success) text = warnings.join(QStringLiteral("\n\n"));
        else {
            text = errors.empty() ? failure : errors.join(QStringLiteral("\n\n"));
            if (!error_prefix.isEmpty()) text = error_prefix + (text.isEmpty() ? QString{} : QStringLiteral("\n\n") + text);
            if (!warnings.empty()) text = warning_heading + QStringLiteral("\n") + warnings.join(QStringLiteral("\n\n")) + QStringLiteral("\n\n") + text;
        }
        prepared->intake_error = intake_error;
        if (!intake_error.isEmpty()) text = intake_error + QStringLiteral("\n\n") + text;
        prepared->summary = text.left(16384);
        if (text.size() > 16384) prepared->summary += QObject::tr("\n[Complete diagnostics are available below.]");
        for (qsizetype offset = 0; offset < text.size(); offset += 1024) prepared->rows.push_back(text.mid(offset, 1024));
        return prepared;
    }, {execution::Lane::Domain, {}, execution::WorkClass::Action});
    if (generation == m_generation && m_operation == operation && !m_closed) {
        if (admitted) {
            m_input.reset();
            // Accepted work owns the original capability through its outcome.
            // The facade retains only value leases after that handoff.
            m_diagnostic_scope.reset();
        }
        else { m_busy = false; m_operation.reset(); m_error = tr("Startup diagnostics could not be queued. Retry to display the retained diagnostics."); }
    }
    m_deferred_changed = true; flushChanges();
    return bool(admitted);
}
void StartupDiagnosticsModel::publish() {
    if (m_closed || !m_operation || !m_busy) return;
    if (m_publishing) { m_publication_pending = true; return; }
    m_publication_pending = false;
    const auto operation = m_operation;
    const auto generation = m_generation;
    const auto result = operation->result(); if (!result) return;
    const QPointer<StartupDiagnosticsModel> observer{this};
    const auto current = [&] { return observer && !m_closed && generation == m_generation && m_operation == operation; };
    m_publishing = true;
    if (result->error || !result->value) {
        m_error = tr("Startup diagnostics could not be prepared. Retry to display the retained diagnostics.");
        m_input = m_authoritative_input;
    }
    else {
        beginResetModel(); if (!observer) return;
        if (current()) {
            m_prepared = *result->value; m_error = m_prepared->intake_error;
            m_rows_reset_pending = false;
        } else if (m_rows_reset_pending) {
            m_prepared.reset(); m_rows_reset_pending = false;
        }
        endResetModel(); if (!observer) return;
    }
    m_publishing = false;
    if (!current()) {
        flushChanges();
        return;
    }
    m_busy = false; m_operation.reset(); m_authoritative_input.reset(); m_deferred_changed = true;
    flushChanges();
    if (observer && !m_closed && generation == m_generation && !m_operation) Q_EMIT finished();
}
bool StartupDiagnosticsModel::resetInput(std::shared_ptr<execution::Scheduler> original_diagnostic_scope) {
    const QPointer<StartupDiagnosticsModel> observer{this};
    // Only the original lifecycle operation supplies this one-shot capability.
    // General model admission remains closed during fatal presentation.
    m_diagnostic_scope = std::move(original_diagnostic_scope);
    auto input = startupdiagnostics::Create(m_diagnostic_scope ? m_diagnostic_scope : m_services.scheduler);
    ++m_generation;
    m_operation.reset(); m_busy = false; m_closed = false; m_error.clear(); m_intake_error.clear(); m_input = input; m_authoritative_input.reset();
    m_publication_pending = false; m_rows_reset_pending = true; m_deferred_changed = true;
    if (!m_input) m_error = tr("Startup diagnostic is unavailable.");
    if (!m_publishing) resetRows();
    return observer && bool(input);
}
void StartupDiagnosticsModel::retryPublication() { publish(); }
void StartupDiagnosticsModel::close() { retire(true); }
void StartupDiagnosticsModel::retire(bool notify) {
    if (m_closed) return;
    ++m_generation;
    m_closed = true; m_busy = false;
    m_input.reset(); m_authoritative_input.reset(); m_operation.reset(); m_diagnostic_scope.reset();
    m_publication_pending = false;
    if (!notify) { m_prepared.reset(); return; }
    m_rows_reset_pending = true; m_deferred_changed = true;
    if (!m_publishing) resetRows();
}
void StartupDiagnosticsModel::resetRows() {
    if (m_publishing || !m_rows_reset_pending) return;
    const QPointer<StartupDiagnosticsModel> observer{this};
    m_publishing = true;
    beginResetModel(); if (!observer) return;
    // Apply requests only after every about-to-reset listener has seen the old
    // frame. Requests from modelReset listeners belong to a later transaction.
    if (m_rows_reset_pending) { m_prepared.reset(); m_rows_reset_pending = false; }
    endResetModel(); if (!observer) return;
    m_publishing = false;
    flushChanges();
}
void StartupDiagnosticsModel::schedulePresentation() {
    if (m_presentation_scheduled) return;
    m_presentation_scheduled = true;
    const QPointer<StartupDiagnosticsModel> observer{this};
    m_services.post([observer] { if (observer) observer->deliverPresentation(); });
}
void StartupDiagnosticsModel::deliverPresentation() {
    m_presentation_scheduled = false;
    // A nested GUI loop can deliver this turn inside a reset signal. The
    // enclosing transaction schedules any still-pending request after it ends.
    if (m_publishing) return;
    if (!m_closed && m_busy && m_operation && m_operation->result()) {
        publish(); // A current publication also consumes an older clear request.
        return;
    }
    m_publication_pending = false;
    resetRows();
}
void StartupDiagnosticsModel::flushChanges() {
    if (m_publishing) return;
    if (m_rows_reset_pending || m_publication_pending) schedulePresentation();
    if (std::exchange(m_deferred_changed, false)) Q_EMIT changed();
}
