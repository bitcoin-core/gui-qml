// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/models/paymentrequesteditormodel.h>
#include <qml/atomicsharedptr.h>
#include <qml/guipresentation.h>
#include <qml/retainedsnapshot.h>
#include <QPointer>
#include <QScopeGuard>
#include <QUuid>
#include <array>
#include <algorithm>
#include <mutex>

namespace {
constexpr std::array<const char*, 4> FIELD_IDS{"label", "message", "note", "amount"};
QString ErrorText(walletrequests::Error error, bool saved = false) { return walletrequests::ErrorText(error, saved); }
TextEditError Failure(walletrequests::Error error, bool saved = false)
{
    if (error == walletrequests::Error::None) return {};
    const auto code = error == walletrequests::Error::Closed ? TextEditError::Closed :
        error == walletrequests::Error::Uncertain ? TextEditError::Unknown :
        (error == walletrequests::Error::Changed || error == walletrequests::Error::Missing || error == walletrequests::Error::Paid) ? TextEditError::Changed :
        error == walletrequests::Error::Invalid ? TextEditError::Invalid :
        error == walletrequests::Error::Rejected ? TextEditError::Refused : TextEditError::Persistence;
    return {code, ErrorText(error, saved)};
}
bool ValidValues(const requestedit::Editor::Values& values)
{
    using namespace requestedit;
    size_t bytes{2048};
    for (size_t i{Label}; i <= Note; ++i) {
        if (values[i].text.size() > 16'384) return false;
        bytes += static_cast<size_t>(values[i].text.size()) * sizeof(QChar);
    }
    return bytes <= 60'000 && values[Amount].acceptable && MoneyRange(values[Amount].amount);
}
QString FieldText(const requestedit::Value& value, size_t field)
{
    return field == requestedit::Amount && value.acceptable ? BitcoinAmount::satsToBtcString(value.amount) : value.text;
}
}

requestedit::Source requestedit::PrepareSource(const walletrequests::SharedRecord& record, uint64_t view)
{
    Source source; source.view = view; source.loaded = source.current = true;
    if (!record) return source;
    source.id = record->entry.id; source.revision = record->revision; source.exists = true;
    source.address = record->display.address; source.address_type = record->display.address_type;
    source.values[Label].text = record->display.label; source.values[Message].text = record->display.message;
    source.values[Note].text = record->display.note; source.values[Amount].amount = record->entry.recipient.amount;
    source.used = record->entry.payment_received;
    if (source.id <= 0 || source.revision == 0 || source.address.size() > 128 || source.address_type.size() > 16 || !ValidValues(source.values)) {
        source.error = walletrequests::Error::Invalid; source.values = {}; source.address.clear(); source.address_type.clear(); return source;
    }
    source.uri = source.used ? QString{} : record->display.uri;
    return source;
}

struct PaymentRequestEditorModel::Projection {
    requestedit::Editor editor;
    requestedit::Source source;
    TextEditError error, rejected_error;
    RequestSaveResult result;
    uint64_t input{0}, seen_save{0}, rejection{0}, result_sequence{0};
    bool valid{false}, erase{false};
    uint64_t keep_sequence{0};
    QString kept_token;
    std::array<uint64_t, 4> applied{};
};
struct PaymentRequestEditorModel::Mailbox {
    std::mutex mutex;
    std::shared_ptr<const Projection> latest;
    std::atomic_bool retired{false}, drained{false}, notice_pending{false};
};
struct PaymentRequestEditorModel::State : std::enable_shared_from_this<State> {
    State(QString editor_id, QString entity_id, int64_t id, uint64_t view, bool erase, Ports ports,
          std::optional<requestedit::Editor::Values> initial, std::shared_ptr<Mailbox> mailbox, std::function<void()> notify)
        : editor({editor_id.toStdString(), entity_id.toStdString()}), id(id), view(view), erase(erase), ports(std::move(ports)),
          scope(this->ports.execution.scheduler->openActionScope()), key(this->ports.execution.scheduler->allocateKey()),
          initial(std::move(initial)), mailbox(std::move(mailbox)), notify(std::move(notify)) {}
    requestedit::Editor editor;
    int64_t id;
    const uint64_t view;
    const bool erase;
    const Ports ports;
    ownership::AtomicSharedPtr<execution::Scheduler> scope;
    const execution::ResourceKey key;
    std::optional<requestedit::Editor::Values> initial;
    requestedit::Source source;
    TextEditError error, rejected_error;
    RequestSaveResult result;
    uint64_t observation{0}, input{0}, read_token{0}, seen_save{0}, rejection{0}, result_sequence{0};
    bool reading{false}, read_again{false}, initialized{false}, read_error{false};
    // Protect an original save's queued validation from an earlier ordinary
    // edit/read noticing projection retirement. This is admission accounting,
    // not a right to admit a new mutation after the parent fence closes.
    std::atomic_bool validation_admitted{false};
    uint64_t keep_sequence{0};
    QString kept_token;
    requestedit::Inputs loading_input;
    std::array<uint64_t, 4> applied{};
    const std::shared_ptr<Mailbox> mailbox;
    const std::function<void()> notify;
    ~State() { mailbox->drained.store(true); try { notify(); } catch (...) {} }
    execution::Admission postUsing(const std::shared_ptr<execution::Scheduler>& target, std::function<void(State&)> command, bool original = false)
    {
        if (!target) return {{}, execution::Rejection::Closed};
        const auto self = shared_from_this();
        return execution::Submit(*target, [self, command = std::move(command), original] {
            if (self->mailbox->retired.load() && !original) self->retire(); else command(*self);
        }, [] {}, {}, {execution::Lane::Domain, key, execution::WorkClass::Action});
    }
    execution::Admission ingress(std::function<void(State&)> command) { return postUsing(ports.execution.scheduler, std::move(command)); }
    execution::Admission post(std::function<void(State&)> command) { return postUsing(scope.load(), std::move(command)); }
    bool publicConflict() const
    {
        if (!source.used || erase) return false;
        for (const auto field : {requestedit::Label, requestedit::Message, requestedit::Amount})
            if (editor.draft()[field] != source.values[field]) return true;
        return false;
    }
    bool valid() const
    {
        return initialized && source.loaded && source.current && !source.closed && source.error == walletrequests::Error::None &&
            (id > 0 ? source.exists : source.reservation && !source.reservation->revoked->load() && !source.used) &&
            !source.address.isEmpty() && !publicConflict() && ValidValues(editor.draft());
    }
    void publish()
    {
        auto value = std::make_shared<const Projection>(Projection{editor, source, error, rejected_error, result, input, seen_save, rejection, result_sequence, valid(), erase, keep_sequence, kept_token, applied});
        { std::lock_guard lock{mailbox->mutex}; mailbox->latest = std::move(value); }
        notify();
    }
    void refuse(TextEditError failure) { rejected_error = failure; error = std::move(failure); if (rejection != UINT64_MAX) ++rejection; }
    void edit(size_t field, requestedit::Value value, uint64_t sequence)
    {
        if (field >= 4 || sequence <= applied[field]) return;
        if (!initialized) { loading_input[field] = requestedit::Input{std::move(value), sequence}; return; }
        if (field != requestedit::Note && erase && value != editor.draft()[field]) {
            applied[field] = sequence; input = std::max(input, sequence);
            refuse({TextEditError::Changed, tr("The original delete operation does not edit public payment details.")}); publish(); return;
        }
        editor.edit(field, std::move(value));
        applied[field] = sequence; input = std::max(input, sequence);
        if (publicConflict()) error = {TextEditError::Changed, tr("This address was used. Resolve the public payment edits before saving.")};
        else if (!read_error) error = ValidValues(editor.draft()) ? TextEditError{} : TextEditError{TextEditError::Invalid, tr("Correct the request fields before saving.")};
        publish();
    }
    void apply(requestedit::Inputs inputs)
    {
        std::array<size_t, 4> order{0, 1, 2, 3};
        std::sort(order.begin(), order.end(), [&inputs](size_t a, size_t b) {
            return (inputs[a] ? inputs[a]->sequence : 0) < (inputs[b] ? inputs[b]->sequence : 0);
        });
        for (const auto field : order) if (inputs[field]) edit(field, std::move(inputs[field]->value), inputs[field]->sequence);
    }
    void read()
    {
        if (editor.busy() || editor.phase() == persistentedit::Phase::Closed || read_token == UINT64_MAX) return;
        if (reading) { read_again = true; return; }
        reading = true; read_again = false; const auto token = ++read_token; const auto self = shared_from_this();
        bool admitted{false};
        try { admitted = ports.read && bool(ports.read(id, view, [self, token](requestedit::Source value) { self->readDone(token, std::move(value)); })); }
        catch (...) {}
        if (!admitted) { reading = false; source.current = false; read_error = true; refuse({TextEditError::Refused, tr("The request could not be read. Try again.")}); publish(); }
    }
    void readDone(uint64_t token, requestedit::Source value)
    {
        if (token != read_token) return;
        reading = false;
        const bool again = std::exchange(read_again, false);
        if (mailbox->retired.load()) { retire(); return; }
        if (value.closed) { source.closed = true; error = {TextEditError::Closed, tr("This wallet opening has closed.")}; retire(); return; }
        if (!value.loaded || !value.current || value.error != walletrequests::Error::None || (id > 0 && !value.exists)) {
            source.current = false;
            read_error = true;
            error = value.error != walletrequests::Error::None ? Failure(value.error) :
                TextEditError{TextEditError::Changed, id > 0 && value.loaded && !value.exists ? tr("This request no longer exists.") : tr("Payment request state is being reconciled.")};
            if (value.error == walletrequests::Error::Invalid || value.error == walletrequests::Error::Missing || value.error == walletrequests::Error::Closed ||
                (id > 0 && value.loaded && value.current && !value.exists)) { retire(); return; }
            publish(); if (again) read(); return;
        }
        source = std::move(value);
        if (std::exchange(read_error, false)) error = result.present && result.requestSaved ? Failure(result.error, true) : TextEditError{};
        if (observation == UINT64_MAX) { refuse({TextEditError::Refused, tr("This editor has reached its revision limit.")}); publish(); return; }
        editor.observe(source.values, ++observation);
        if (!initialized) {
            initialized = true;
            if (initial) { for (size_t i{0}; i < 4; ++i) editor.edit(i, (*initial)[i]); initial.reset(); }
            if (id == 0 || erase) { auto intent = editor.draft()[requestedit::Mutation]; intent.intent = true; editor.edit(requestedit::Mutation, intent); }
            auto pending = std::move(loading_input); loading_input = {}; apply(std::move(pending));
            if (erase) editor.requestClose();
        }
        if (publicConflict()) error = {TextEditError::Changed, tr("This address was used. Discard changes to the public payment details; the private note can still be saved.")};
        else if ((error.code == TextEditError::Changed || error.code == TextEditError::Refused) &&
            !(result.present && result.requestSaved && result.error != walletrequests::Error::None)) error = {};
        publish();
        if (again) read();
    }
    void save(requestedit::Inputs inputs, requestedit::Source captured, Writer writer,
              std::shared_ptr<execution::Scheduler> action, uint64_t request, std::optional<uint64_t> close_token);
    void finish(const requestedit::Intent& intent, requestedit::Write outcome, bool reconcile);
    void reconcile();
    void retire()
    {
        if (validation_admitted.load()) return;
        loading_input = {}; initial.reset(); editor.retire(); scope.store({}); publish();
    }
};

void PaymentRequestEditorModel::State::save(requestedit::Inputs inputs, requestedit::Source captured,
    Writer writer, std::shared_ptr<execution::Scheduler> action, uint64_t request, std::optional<uint64_t> close_token)
{
    const auto consumed = qScopeGuard([this] {
        validation_admitted.store(false);
        if (mailbox->retired.load()) retire();
    });
    seen_save = std::max(seen_save, request); apply(std::move(inputs));
    if (captured.id != source.id || captured.revision != source.revision || captured.reservation != source.reservation ||
        captured.address != source.address || !valid()) {
        refuse({TextEditError::Changed, tr("The original request or reservation changed. Review it before saving.")}); publish(); return;
    }
    if (close_token && !editor.answerClose(*close_token, persistentedit::CloseChoice::Save)) {
        refuse({TextEditError::Changed, tr("The close question changed. Review the current request.")}); publish(); return;
    }
    const auto attempt = editor.check();
    if (!attempt) { refuse({TextEditError::Refused, tr("Resolve the current conflict or pending operation before saving.")}); publish(); return; }
    publish();
    if (!editor.validated(attempt->token, ValidValues(attempt->values))) {
        refuse({TextEditError::Invalid, tr("Correct the request fields before saving.")}); publish(); return;
    }
    const requestedit::Intent intent{*editor.active(), source, erase};
    if (read_token != UINT64_MAX) ++read_token;
    reading = false; publish();
    const auto self = shared_from_this();
    try {
        if (writer && writer(intent, [self, intent, action](requestedit::Write result) {
            (void)action; self->finish(intent, std::move(result), false);
        }, action)) return;
        walletrequests::Result result; result.error = walletrequests::Error::Rejected;
        finish(intent, {std::move(result), {}}, false);
    } catch (...) {
        walletrequests::Result result; result.error = walletrequests::Error::Uncertain;
        finish(intent, {std::move(result), {}}, false);
    }
}

void PaymentRequestEditorModel::State::finish(const requestedit::Intent& intent, requestedit::Write outcome, bool reconcile)
{
    if (mailbox->retired.load()) retire();
    if (outcome.actual.error == walletrequests::Error::None && !outcome.actual.request_saved && !outcome.actual.erased) {
        // A transport acknowledgment is not a persistence receipt.
        outcome.actual.error = walletrequests::Error::Uncertain;
    }
    const auto& actual = outcome.actual;
    const bool persisted = actual.request_saved || actual.erased;
    const auto kind = actual.error == walletrequests::Error::Uncertain ? persistentedit::Outcome::Unknown :
        actual.error == walletrequests::Error::None ? persistentedit::Outcome::Saved :
        persisted || actual.label_saved ? persistentedit::Outcome::Partial :
        actual.error == walletrequests::Error::Rejected ? persistentedit::Outcome::Rejected : persistentedit::Outcome::Failed;
    requestedit::Editor::Receipt receipt{intent.edit, kind, {}, intent.edit.values, {}};
    if (persisted) {
        receipt.persisted.set();
        if (actual.record) receipt.values = requestedit::PrepareSource(actual.record, view).values;
        receipt.values[requestedit::Mutation].intent = false;
    }
    if (outcome.current && observation != UINT64_MAX)
        receipt.observed = requestedit::Editor::Observation{outcome.current->values, ++observation, intent.edit.token};
    const bool resolving = reconcile || (editor.phase() == persistentedit::Phase::Uncertain && editor.active() && editor.active()->token == intent.edit.token);
    if (!(resolving ? editor.reconciled(receipt) : editor.finished(receipt))) return;
    error = Failure(actual.error, actual.request_saved);
    read_error = false;
    result = {true, kind != persistentedit::Outcome::Unknown, actual.request_saved, actual.erased, actual.label_saved,
        QString::fromStdString(intent.edit.identity.editor) + "/" + QString::number(intent.edit.token),
        QString::fromStdString(intent.edit.identity.editor), actual.id > 0 ? QString::number(actual.id) : intent.source.id > 0 ? QString::number(intent.source.id) : QString{},
        ErrorText(actual.error, actual.request_saved), actual.error};
    if (result_sequence != UINT64_MAX) ++result_sequence;
    if (actual.request_saved && actual.id > 0) {
        id = actual.id;
        if (actual.record) source = requestedit::PrepareSource(actual.record, view);
    }
    if (actual.erased) editor.retire();
    if (mailbox->retired.load() || actual.erased) scope.store({}); else read();
    publish();
}

void PaymentRequestEditorModel::State::reconcile()
{
    if (editor.phase() != persistentedit::Phase::Uncertain || !editor.active()) return;
    const requestedit::Intent intent{*editor.active(), source, erase}; const auto self = shared_from_this();
    bool admitted{false};
    try { admitted = ports.query && bool(ports.query(intent, [self, intent](requestedit::Write value) {
        value.current.reset(); self->finish(intent, std::move(value), true);
    })); } catch (...) {}
    if (!admitted) { error = {TextEditError::Unknown, tr("The original write outcome remains unknown. It will not be repeated.")}; publish(); }
}

PaymentRequestEditorModel::PaymentRequestEditorModel(int64_t id, uint64_t view, bool erase, Ports ports,
    std::optional<requestedit::Editor::Values> initial, QObject* parent)
    : QObject(parent), m_editor_id(QUuid::createUuid().toString(QUuid::WithoutBraces)),
      m_entity_id(id > 0 ? QStringLiteral("request/%1").arg(id) : QStringLiteral("draft/%1").arg(m_editor_id)), m_execution(ports.execution)
{
    m_execution.post = presentation::ReliablePost(std::move(m_execution.post)); ports.execution = m_execution;
    m_mailbox = std::make_shared<Mailbox>(); const QPointer<PaymentRequestEditorModel> observer{this};
    m_owner = execution::RetainSnapshot<std::shared_ptr<State>>(m_execution.scheduler,
        [editor = m_editor_id, entity = m_entity_id, id, view, erase, ports = std::move(ports), initial = std::move(initial),
         mailbox = m_mailbox, observer, post = m_execution.post]() mutable {
            return std::make_unique<std::shared_ptr<State>>(std::make_shared<State>(editor, entity, id, view, erase, std::move(ports), std::move(initial), mailbox,
                [mailbox, observer, post] {
                    if (mailbox->notice_pending.exchange(true)) return;
                    try { post([mailbox, observer] { mailbox->notice_pending.store(false); if (observer) observer->publish(); }); }
                    catch (...) { mailbox->notice_pending.store(false); throw; }
                }));
        });
    if (m_owner) m_state = std::shared_ptr<State>{m_owner, m_owner->get()}; else m_retired = true;
    connect(&m_label, &TextEditInput::edited, this, [this](const QString& value) { edit(requestedit::Label, {value}); });
    connect(&m_message, &TextEditInput::edited, this, [this](const QString& value) { edit(requestedit::Message, {value}); });
    connect(&m_note, &TextEditInput::edited, this, [this](const QString& value) { edit(requestedit::Note, {value}); });
    const std::array<TextEditInput*, 3> fields{&m_label, &m_message, &m_note};
    for (size_t i{0}; i < fields.size(); ++i) {
        auto* field = fields[i];
        connect(field, &TextEditInput::changed, this, [this, field, i] {
            m_sharing_input->store(false);
            if (!m_projecting) edit(i, {field->text()});
        });
        connect(field, &TextEditInput::rejected, this, [this, field, i] {
            m_sharing_epoch->advance();
            m_text_invalid[i] = m_text_invalid[i] || field->available(); m_sharing_input->store(false);
            reject(QStringLiteral("edit"), {field->available() ? TextEditError::Invalid : TextEditError::Changed,
                field->available() ? tr("Correct the rejected text before saving.") : tr("This request field is read only.")});
        });
    }
    // A closed editor ignores its amount; the field is read-only by then.
    connect(&m_amount, &BitcoinAmount::valueEdited, this, [this] {
        if (!available()) return;
        m_sharing_epoch->advance(); m_amount_edit_intent = true; m_sharing_input->store(false);
    });
    connect(&m_amount, &BitcoinAmount::inputChanged, this, [this] {
        if (!std::exchange(m_amount_edit_intent, false) || !available()) return;
        m_invalid_input = !m_amount.inputAcceptable();
        edit(requestedit::Amount, {m_invalid_input ? m_amount.inputText() : QString{}, m_amount.satoshi(), false, !m_invalid_input});
    });
    refresh();
}

PaymentRequestEditorModel::~PaymentRequestEditorModel()
{
    m_sharing_epoch->closed.store(true);
    m_sharing_input->store(false);
    if (!std::exchange(m_retired, true)) {
        m_mailbox->retired.store(true);
        if (const auto state = m_state.lock()) state->post([](State& owner) { owner.retire(); });
    }
    m_owner.reset();
}
bool PaymentRequestEditorModel::reject(const QString& command, TextEditError failure)
{
    m_immediate_error = failure; const QPointer<PaymentRequestEditorModel> guard{this};
    m_sharing_input->store(available() && !dirty() && !busy() && valid() && m_projection && m_projection->source.id > 0 && !m_projection->source.used);
    Q_EMIT changed(); if (guard) Q_EMIT commandRejected(command, failure); return false;
}
bool PaymentRequestEditorModel::submit(std::function<void(State&)> command, const QString& name)
{
    bool admitted{false};
    { const auto state = m_state.lock(); admitted = available() && state && bool(state->ingress(std::move(command))); }
    if (admitted) { m_immediate_error = {}; return true; }
    return reject(name, {m_retired ? TextEditError::Closed : TextEditError::Refused, tr("The editor command could not be scheduled.")});
}
void PaymentRequestEditorModel::edit(size_t field, requestedit::Value value)
{
    if (m_retired) return;
    if (field >= 4) return;
    if (m_input == UINT64_MAX) { reject(QStringLiteral("edit"), {TextEditError::Refused, tr("This editor has reached its revision limit.")}); return; }
    m_sharing_epoch->advance();
    m_sharing_input->store(false);
    if (field < m_text_invalid.size()) m_text_invalid[field] = value.text.size() > 16'384;
    const uint64_t observed = m_projection ? m_projection->applied[field] : 0;
    if ((m_inputs[field] && m_inputs[field]->value == value && m_inputs[field]->sequence > observed) ||
        (m_projection && value == m_projection->editor.draft()[field] && (!m_inputs[field] || m_inputs[field]->sequence <= observed))) {
        if (m_immediate_error.code == TextEditError::Invalid) m_immediate_error = {};
        m_sharing_input->store(available() && !dirty() && !busy() && valid() && m_projection && m_projection->source.id > 0 && !m_projection->source.used);
        Q_EMIT changed(); return;
    }
    const auto sequence = ++m_input;
    m_inputs[field] = requestedit::Input{value, sequence};
    const QPointer<PaymentRequestEditorModel> guard{this};
    const bool admitted = submit([field, value = std::move(value), sequence](State& owner) mutable { owner.edit(field, std::move(value), sequence); }, QStringLiteral("edit"));
    if (guard && admitted) Q_EMIT changed();
}

void PaymentRequestEditorModel::publish()
{
    if (m_publishing) { m_publish_again = true; return; }
    { std::lock_guard lock{m_mailbox->mutex}; if (!m_mailbox->latest) return; m_projection = m_mailbox->latest; }
    m_publishing = true;
    m_sharing_input->store(false);
    const auto update = m_projection;
    if (m_pending_save && m_projection->seen_save >= m_pending_save) m_pending_save = 0;
    if (m_pending_reset && m_projection->input >= m_pending_reset) m_pending_reset = 0;
    const QPointer<PaymentRequestEditorModel> guard{this};
    if (m_projection->editor.closeQuestion()) {
        const auto token = QString::number(m_projection->editor.closeToken());
        if (!m_prompt || m_prompt->token() != token) {
            if (m_prompt) m_prompt->deleteLater();
            m_prompt = new TextEditClosePrompt(token, this);
            connect(m_prompt, &TextEditClosePrompt::answered, this, &PaymentRequestEditorModel::answerClose);
        }
    } else if (m_prompt) { m_prompt->deleteLater(); m_prompt = nullptr; }
    m_projecting = true;
    m_label.setAvailable(available() && publicFieldsEditable()); if (!guard) return;
    m_message.setAvailable(available() && publicFieldsEditable()); if (!guard) return;
    m_note.setAvailable(available() && mutationKind() != Delete); if (!guard) return;
    const std::array<TextEditInput*, 3> fields{&m_label, &m_message, &m_note};
    for (size_t i{0}; i < fields.size(); ++i) if (!m_inputs[i] || update->applied[i] >= m_inputs[i]->sequence) {
        fields[i]->project(update->editor.draft()[i].text); if (!guard) return;
    }
    if (!m_inputs[requestedit::Amount] || update->applied[requestedit::Amount] >= m_inputs[requestedit::Amount]->sequence) {
        const auto& amount = update->editor.draft()[requestedit::Amount];
        const bool reset = m_reset_amount_at && update->applied[requestedit::Amount] >= m_reset_amount_at &&
            (!m_inputs[requestedit::Amount] || m_inputs[requestedit::Amount]->sequence <= m_reset_amount_at);
        if (amount.acceptable && (reset || m_amount.satoshi() != amount.amount)) m_amount.setSatoshi(amount.amount, BitcoinAmount::Origin::Model);
        if (!guard) return;
        if (m_reset_amount_at && update->applied[requestedit::Amount] >= m_reset_amount_at) m_reset_amount_at = 0;
    }
    m_projecting = false;
    // Input/property observers cannot admit a successor mutation before this
    // original snapshot and terminal receipt have finished bounded publication.
    const bool has_result = update->result_sequence > m_seen_result;
    if (has_result) m_seen_result = update->result_sequence;
    m_receipts.setSource(requestId(), address(), historicallyUsed(), available()); if (!guard) return;
    m_publishing = false;
    m_sharing_input->store(available() && !dirty() && !busy() && valid() && update->source.id > 0 && !update->source.used);
    if (has_result) { const auto actual = update->result; Q_EMIT persisted(actual); if (!guard) return; }
    Q_EMIT changed(); if (!guard) return;
    if (update->keep_sequence > m_seen_keep) {
        m_seen_keep = update->keep_sequence; const auto token = update->kept_token;
        Q_EMIT keptEditing(m_editor_id, token); if (!guard) return;
    }
    if (m_projection->rejection > m_seen_rejection) {
        m_seen_rejection = m_projection->rejection; const auto failure = m_projection->rejected_error;
        Q_EMIT commandRejected(m_editor_id + "/command-" + QString::number(m_seen_rejection), failure); if (!guard) return;
    }
    if (phase() == Closed && m_owner) {
        m_mailbox->retired.store(true);
        { const auto state = m_state.lock(); if (state) state->post([](State& owner) { owner.retire(); }); }
        m_owner.reset(); if (!guard) return;
    }
    if (!m_closed_notified && phase() == Closed) { m_closed_notified = true; Q_EMIT closed(m_editor_id); }
    if (!guard) return;
    if (std::exchange(m_publish_again, false)) m_execution.post([guard] { if (guard) guard->publish(); });
}

QString PaymentRequestEditorModel::baseRevision() const { return QString::number(m_projection ? m_projection->editor.baseRevision() : 0); }
QString PaymentRequestEditorModel::editRevision() const { return QString::number(m_projection ? m_projection->editor.editRevision() : 0); }
PaymentRequestEditorModel::Phase PaymentRequestEditorModel::phase() const
{
    if (m_retired && m_mailbox->drained.load()) return Closed;
    if (m_pending_save) return Checking;
    if (m_projection && m_projection->editor.phase() == persistentedit::Phase::Editing && m_projection->source.used && !m_projection->erase) {
        for (const auto field : {requestedit::Label, requestedit::Message, requestedit::Amount})
            if (m_projection->editor.draft()[field] != m_projection->source.values[field]) return Conflict;
    }
    return m_projection ? static_cast<Phase>(m_projection->editor.phase()) : m_retired ? Closed : Loading;
}
bool PaymentRequestEditorModel::available() const { return !m_retired && phase() != Closed && phase() != Retiring; }
bool PaymentRequestEditorModel::dirty() const
{
    if (m_invalid_input || std::any_of(m_text_invalid.begin(), m_text_invalid.end(), [](bool invalid) { return invalid; })) return true;
    for (size_t field{0}; field < m_inputs.size(); ++field)
        if (m_inputs[field] && m_inputs[field]->sequence > (m_projection ? m_projection->applied[field] : 0)) return true;
    if (!m_projection) return false;
    if (m_projection->erase || m_projection->source.id > 0) return m_projection->editor.dirty();
    for (size_t field{0}; field < 4; ++field) if (m_projection->editor.dirtyFields()[field]) return true;
    return false;
}
bool PaymentRequestEditorModel::valid() const
{
    if (!m_projection || !m_projection->valid) return false;
    const auto& source = m_projection->source;
    if (source.id == 0 && (!source.reservation || source.reservation->revoked->load())) return false;
    return !m_invalid_input && std::none_of(m_text_invalid.begin(), m_text_invalid.end(), [](bool invalid) { return invalid; }) && m_amount.inputAcceptable();
}
bool PaymentRequestEditorModel::busy() const { return !m_mailbox->drained.load() && (m_publishing || m_pending_save || m_pending_reset || (m_projection && m_projection->editor.busy())); }
bool PaymentRequestEditorModel::canSave() const { return available() && !busy() && valid() && m_projection->editor.canSave(); }
bool PaymentRequestEditorModel::canDiscard() const { return available() && !busy() && dirty() && m_projection && m_projection->editor.canDiscard(); }
bool PaymentRequestEditorModel::publicFieldsEditable() const { return available() && m_projection && !m_projection->source.used && !m_projection->erase; }
PaymentRequestEditorModel::MutationKind PaymentRequestEditorModel::mutationKind() const { return m_projection && m_projection->erase ? Delete : m_projection && m_projection->source.id > 0 ? Update : Create; }
TextEditError PaymentRequestEditorModel::error() const
{
    if (m_retired) return {TextEditError::Closed, tr("This editor is closed.")};
    if (m_invalid_input || std::any_of(m_text_invalid.begin(), m_text_invalid.end(), [](bool invalid) { return invalid; }))
        return {TextEditError::Invalid, tr("Correct the rejected request input before saving.")};
    return m_immediate_error.code != TextEditError::None ? m_immediate_error : m_projection ? m_projection->error : TextEditError{};
}
QList<TextEditConflict> PaymentRequestEditorModel::conflicts() const
{
    QList<TextEditConflict> result;
    if (m_projection) for (size_t i{0}; i < 4; ++i) {
        const bool frozen = m_projection->source.used && i != requestedit::Note && m_projection->editor.draft()[i] != m_projection->source.values[i];
        if (m_projection->editor.conflicts()[i] || frozen)
            result.push_back({QString::fromLatin1(FIELD_IDS[i]), FieldText(m_projection->editor.base()[i], i), FieldText(m_projection->editor.draft()[i], i)});
    }
    return result;
}
RequestSaveResult PaymentRequestEditorModel::saveResult() const { return m_projection ? m_projection->result : RequestSaveResult{}; }
QString PaymentRequestEditorModel::requestId() const { return m_projection && m_projection->source.id > 0 ? QString::number(m_projection->source.id) : QString{}; }
QString PaymentRequestEditorModel::address() const { return m_projection ? m_projection->source.address : QString{}; }
QString PaymentRequestEditorModel::addressType() const { return m_projection ? m_projection->source.address_type : QString{}; }
QString PaymentRequestEditorModel::reservationId() const { return m_projection && m_projection->source.reservation ? QString::number(m_projection->source.reservation->token) : QString{}; }
QString PaymentRequestEditorModel::paymentUri() const { return m_projection ? m_projection->source.uri : QString{}; }
bool PaymentRequestEditorModel::historicallyUsed() const { return m_projection && m_projection->source.used; }

void PaymentRequestEditorModel::save() { saveCommand(); }
void PaymentRequestEditorModel::saveCommand(std::optional<uint64_t> close_token)
{
    if (busy() || !available() || !valid() || m_next_save == UINT64_MAX || m_input == UINT64_MAX) {
        reject(QStringLiteral("save"), {TextEditError::Refused, tr("This request cannot be saved while its input, source or outcome is unresolved.")}); return;
    }
    auto action = m_execution.scheduler->openActionScope(); Writer writer; bool admitted{false};
    const QPointer<PaymentRequestEditorModel> guard{this};
    const auto captured = m_projection->source; const auto inputs = m_inputs;
    const auto request = m_next_save + 1;
    {
        const auto state = m_state.lock();
        try { if (action && state && state->ports.admitWrite) writer = state->ports.admitWrite(); } catch (...) {}
        if (!guard) return;
        if (action && writer && state && !state->validation_admitted.exchange(true)) {
            admitted = bool(state->postUsing(action,
                [inputs, captured, writer = std::move(writer), action, request, close_token](State& owner) mutable {
                    owner.save(inputs, captured, std::move(writer), action, request, close_token);
                }, true));
            if (!admitted) state->validation_admitted.store(false);
        }
    }
    if (!guard) return;
    if (admitted) { m_pending_save = m_next_save = request; m_immediate_error = {}; Q_EMIT changed(); }
    else reject(QStringLiteral("save"), {TextEditError::Refused, tr("The save could not be admitted.")});
}
void PaymentRequestEditorModel::discard()
{
    if (!canDiscard()) { reject(QStringLiteral("discard"), {TextEditError::Refused, tr("This editor cannot discard its current state.")}); return; }
    if (m_input == UINT64_MAX) { reject(QStringLiteral("discard"), {TextEditError::Refused, tr("This editor has reached its revision limit.")}); return; }
    m_sharing_epoch->advance(); m_sharing_input->store(false);
    const auto input = m_input + 1; const QPointer<PaymentRequestEditorModel> guard{this};
    const bool admitted = submit([input](State& owner) {
        owner.input = std::max(owner.input, input);
        const bool had_question = owner.editor.closeQuestion(); const auto question = owner.editor.closeToken();
        if (owner.editor.discard()) {
            owner.applied.fill(input); owner.loading_input = {};
            owner.error = {};
            if (had_question && owner.keep_sequence != UINT64_MAX) { ++owner.keep_sequence; owner.kept_token = QString::number(question); }
            if (owner.id == 0 && !owner.erase) { auto value = owner.editor.draft()[requestedit::Mutation]; value.intent = true; owner.editor.edit(requestedit::Mutation, value); }
        } else owner.refuse({TextEditError::Refused, tr("The original discard could not be applied.")});
        owner.publish();
    }, QStringLiteral("discard"));
    if (guard && admitted) {
        m_input = m_pending_reset = m_reset_amount_at = input; m_invalid_input = false; m_text_invalid.fill(false); Q_EMIT changed();
    }
}
void PaymentRequestEditorModel::requestClose()
{
    if (phase() == Closed) return;
    if (busy()) { reject(QStringLiteral("requestClose"), {TextEditError::Refused, tr("The original save has not settled.")}); return; }
    m_sharing_epoch->advance(); m_sharing_input->store(false);
    const auto inputs = m_inputs;
    submit([inputs](State& owner) {
        owner.apply(inputs);
        bool fields_dirty{false}; for (size_t field{0}; field < 4; ++field) fields_dirty = fields_dirty || owner.editor.dirtyFields()[field];
        if (owner.id == 0 && !owner.erase && !fields_dirty && ValidValues(owner.editor.draft())) {
            auto marker = owner.editor.draft()[requestedit::Mutation]; marker.intent = false; owner.editor.edit(requestedit::Mutation, marker);
        }
        owner.editor.requestClose(); owner.publish();
    }, QStringLiteral("requestClose"));
}
void PaymentRequestEditorModel::answerClose(const QString& token, TextEditClosePrompt::Choice choice)
{
    bool okay{false}; const auto number = token.toULongLong(&okay);
    if (!okay || choice < TextEditClosePrompt::Save || choice > TextEditClosePrompt::KeepEditing) return;
    if (choice == TextEditClosePrompt::Save) { saveCommand(number); return; }
    if (busy()) { reject(QStringLiteral("answerClose"), {TextEditError::Refused, tr("The original save has not settled.")}); return; }
    submit([number, choice](State& owner) {
        if (!owner.editor.answerClose(number, static_cast<persistentedit::CloseChoice>(choice)))
            owner.refuse({TextEditError::Changed, tr("The close question changed. Review the current request.")});
        else if (choice == TextEditClosePrompt::KeepEditing && owner.keep_sequence != UINT64_MAX) {
            ++owner.keep_sequence; owner.kept_token = QString::number(number);
        }
        owner.publish();
    }, QStringLiteral("answerClose"));
}
void PaymentRequestEditorModel::resolveConflict(const QString& field, TextEditSessionModel::ConflictChoice choice)
{
    if (choice < TextEditSessionModel::KeepEdit || choice > TextEditSessionModel::UseCurrent || busy()) return;
    for (size_t i{0}; i < FIELD_IDS.size(); ++i) if (field == QString::fromLatin1(FIELD_IDS[i])) {
        if (choice == TextEditSessionModel::UseCurrent) {
            if (i < m_text_invalid.size()) m_text_invalid[i] = false;
            if (i == requestedit::Amount) { m_invalid_input = false; m_reset_amount_at = m_input; }
        }
        submit([i, choice](State& owner) {
            if (owner.source.used && i != requestedit::Note && owner.editor.draft()[i] != owner.source.values[i]) {
                if (choice == TextEditSessionModel::KeepEdit) owner.refuse({TextEditError::Changed, tr("Public payment details are immutable after use.")});
                else { owner.editor.edit(i, owner.source.values[i]); owner.error = {}; }
            } else { owner.editor.resolve(i, choice == TextEditSessionModel::KeepEdit); owner.error = {}; }
            owner.publish();
        }, QStringLiteral("resolveConflict")); return;
    }
    reject(QStringLiteral("resolveConflict"), {TextEditError::Invalid, tr("This request field is unknown.")});
}
void PaymentRequestEditorModel::refresh() { if (!busy()) submit([](State& owner) { owner.read(); }, QStringLiteral("refresh")); }
void PaymentRequestEditorModel::reservationChanged()
{
    if (!m_projection || m_projection->source.id > 0 || m_retired) return;
    m_sharing_input->store(false); Q_EMIT changed();
}
void PaymentRequestEditorModel::reconcile() { submit([](State& owner) { owner.reconcile(); }, QStringLiteral("reconcile")); }
void PaymentRequestEditorModel::retire()
{
    if (std::exchange(m_retired, true)) return;
    m_sharing_epoch->closed.store(true);
    m_sharing_input->store(false);
    m_mailbox->retired.store(true);
    { const auto state = m_state.lock(); if (state) state->post([](State& owner) { owner.retire(); }); }
    m_owner.reset();
    const QPointer<PaymentRequestEditorModel> guard{this};
    m_receipts.setSource(requestId(), address(), historicallyUsed(), false); if (!guard) return;
    m_label.setAvailable(false); if (!guard) return;
    m_message.setAvailable(false); if (!guard) return;
    m_note.setAvailable(false); if (!guard) return;
    if (guard) Q_EMIT changed();
}
