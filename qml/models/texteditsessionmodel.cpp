// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/models/texteditsessionmodel.h>
#include <qml/guipresentation.h>
#include <QPointer>
#include <QUuid>
#include <limits>
#include <atomic>
#include <mutex>

void TextEditInput::edit(const QString& value)
{
    if (!m_available) { Q_EMIT rejected(); return; }
    if (value.size() > 32768) {
        const QPointer<TextEditInput> guard{this};
        Q_EMIT rejected(); if (guard) Q_EMIT changed(); return;
    }
    if (value == m_text) { Q_EMIT edited(value); return; }
    m_text = value;
    const QPointer<TextEditInput> guard{this};
    Q_EMIT changed();
    if (guard) Q_EMIT edited(value);
}
void TextEditInput::project(const QString& value)
{
    if (m_text == value) return;
    m_text = value; Q_EMIT changed();
}

struct TextEditSessionModel::Projection {
    Reducer editor;
    TextEditError error;
    TextEditSaveResult result;
    uint64_t input{0}, seen_request{0};
    bool valid{false};
    uint64_t rejection{0};
    TextEditError rejected_error;
    // Typed while the current value was still loading; not yet in the draft.
    bool pending_input{false};
};
struct TextEditSessionModel::Mailbox {
    std::mutex mutex;
    std::shared_ptr<const Projection> latest;
    std::atomic_bool retired{false}, drained{false}, notice_pending{false};
};
struct TextEditSessionModel::State : std::enable_shared_from_this<State> {
    State(QString editor_id, QString entity, Ports ports, std::shared_ptr<Mailbox> mailbox, std::function<void()> notify)
        : editor({editor_id.toStdString(), entity.toStdString()}), ports(std::move(ports)),
          notify(std::move(notify)), mailbox(std::move(mailbox)) {}
    Reducer editor;
    const Ports ports;
    const std::function<void()> notify;
    TextEditError error;
    TextEditSaveResult result;
    uint64_t observation{0}, input{0}, read_token{0}, seen_request{0};
    bool reading{false}, error_from_save{false}, validation_valid{false};
    uint64_t rejection{0};
    TextEditError rejected_error;
    struct PendingSave { QString value; uint64_t input; Writer writer; uint64_t request; };
    std::optional<std::pair<QString, uint64_t>> pending_input;
    // Close was asked while input typed during Loading waited for the value;
    // it is asked again once that input is in the draft, so Close can ask.
    bool close_after_load{false}, read_failed{false};
    std::optional<PendingSave> pending_save;
    const std::shared_ptr<Mailbox> mailbox;
    ~State() { mailbox->drained.store(true); try { notify(); } catch (...) {} }

    bool post(std::function<void(State&)> command)
    {
        auto work = [self = shared_from_this(), command = std::move(command)] {
            if (self->mailbox->retired.load()) self->retire();
            else command(*self);
        };
        const auto task = execution::MakeTask(std::move(work), [] {}, {});
        return ports.postWork([task] {
            task->execute();
            task->deliver();
        });
    }
    bool evaluate() const
    {
        try { return editor.draft()[0].size() <= 32768 && (!ports.validate || ports.validate(editor.draft()[0])); }
        catch (...) { return false; }
    }
    void publish()
    {
        auto value = std::make_shared<const Projection>(Projection{editor, error, result, input, seen_request, !reading && validation_valid, rejection, rejected_error, bool(pending_input)});
        { std::lock_guard lock{mailbox->mutex}; mailbox->latest = std::move(value); }
        notify();
    }
    void edit(QString value, uint64_t sequence)
    {
        if (sequence <= input) return;
        if (editor.phase() == persistentedit::Phase::Loading) { pending_input = {std::move(value), sequence}; publish(); return; }
        if (editor.edit(0, std::move(value))) { input = sequence; error = {}; error_from_save = false; validation_valid = evaluate();
            if (!validation_valid) error = {TextEditError::Invalid, tr("The text is too long or is not valid.")}; }
        publish();
    }
    void read()
    {
        if (reading || editor.phase() == persistentedit::Phase::Saving || editor.phase() == persistentedit::Phase::Closed || editor.phase() == persistentedit::Phase::Retiring || read_token == UINT64_MAX) return;
        const auto self = shared_from_this();
        reading = true;
        const auto token = ++read_token;
        const auto done = [self, token](Read value) {
            if (self->mailbox->retired.load()) { self->retire(); return; }
            if (token != self->read_token) return;
            self->reading = false;
            if (value.error.code == TextEditError::None && value.value.size() <= 32768 && self->observation != UINT64_MAX) {
                if (self->editor.phase() == persistentedit::Phase::Loading || value.value != self->editor.base()[0])
                    self->editor.observe({std::move(value.value)}, ++self->observation);
                self->validation_valid = self->evaluate();
                if (!self->error_from_save && self->editor.phase() != persistentedit::Phase::Uncertain)
                    self->error = self->validation_valid ? TextEditError{} : TextEditError{TextEditError::Invalid, tr("The text is too long or is not valid.")};
                const auto queued = std::exchange(self->pending_input, {});
                if (auto save = std::exchange(self->pending_save, {})) {
                    self->edit(save->value, save->input); self->save(std::move(save->writer), save->request);
                }
                if (queued) self->edit(queued->first, queued->second);
                self->read_failed = false;
                if (std::exchange(self->close_after_load, false)) self->editor.requestClose();
            } else {
                // A second Close closes: there is no value to save the input against.
                self->read_failed = true; self->close_after_load = false;
                if (self->pending_save) self->seen_request = std::max(self->seen_request, self->pending_save->request);
                self->pending_save.reset();
                if (!self->error_from_save) self->error = value.error.code == TextEditError::None
                    ? TextEditError{TextEditError::Invalid, tr("The saved text is too long to edit here.")} : value.error;
            }
            self->publish();
        };
        bool admitted{false};
        try { admitted = ports.read && bool(ports.read(done)); } catch (...) {}
        if (!admitted && reading) {
            reading = false;
            if (!error_from_save) error = {TextEditError::Refused, tr("The current value could not be read. Try again.")};
            publish();
        }
    }
    void reject(TextEditError value)
    {
        if (rejection != UINT64_MAX) ++rejection;
        rejected_error = std::move(value);
    }
    void setResult(const Reducer::Attempt& attempt, persistentedit::Outcome outcome, bool persisted, const QString& value, const TextEditError& failure)
    {
        result.editorId = QString::fromStdString(attempt.identity.editor); result.entityId = QString::fromStdString(attempt.identity.entity);
        result.operationId = result.editorId + "/" + QString::number(attempt.token);
        result.present = true; result.persisted = persisted; result.outcomeKnown = outcome != persistentedit::Outcome::Unknown;
        result.outcome = static_cast<TextEditSaveResult::Outcome>(outcome);
        result.value = persisted ? value : QString{}; result.error = failure;
    }
    void save(Writer writer, uint64_t request)
    {
        if (editor.phase() == persistentedit::Phase::Loading && pending_input) {
            pending_save = PendingSave{pending_input->first, pending_input->second, std::move(writer), request}; return;
        }
        seen_request = std::max(seen_request, request);
        // A loaded editor can save its captured draft while a refresh is in
        // flight: the writer checks the original base against actual storage.
        // The read token below fences that older read from the save outcome.
        const auto attempt = editor.check();
        if (!attempt) {
            if (editor.conflicts().any()) { error = {TextEditError::Changed, tr("Resolve the changed value before saving.")}; reject(error); }
            else if (editor.busy() || editor.phase() == persistentedit::Phase::Uncertain)
                reject({TextEditError::Refused, tr("The save is still in progress. It will not be repeated.")});
            publish(); return;
        }
        publish(); // Checking is observable while validation runs on Domain.
        const bool checked = evaluate(); validation_valid = checked;
        if (!editor.validated(attempt->token, checked)) {
            error = {TextEditError::Invalid, tr("The text is too long or is not valid.")};
            setResult(*attempt, persistentedit::Outcome::Rejected, false, {}, error); reject(error); publish(); return;
        }
        const auto accepted = *editor.active();
        // Do not let an observation begun before admission masquerade as a
        // post-write current value when its GUI/Domain handoff arrives late.
        if (read_token != UINT64_MAX) ++read_token;
        reading = false;
        publish();
        const auto self = shared_from_this();
        const auto done = [self, accepted](Write value) {
            self->finish(accepted, value, false);
        };
        try {
            if (writer && writer(accepted, done)) return;
            Write rejected; rejected.outcome = persistentedit::Outcome::Rejected;
            rejected.error = {TextEditError::Refused, tr("Saving could not start. Try again.")};
            finish(accepted, rejected, false);
        } catch (...) {
            Write uncertain; uncertain.outcome = persistentedit::Outcome::Unknown;
            uncertain.error = {TextEditError::Unknown, tr("The save may not have finished. It will not be repeated.")};
            finish(accepted, uncertain, false);
        }
    }
    void finish(const Reducer::Attempt& attempt, const Write& value, bool reconcile)
    {
        if (mailbox->retired.load() && editor.phase() != persistentedit::Phase::Closed && editor.phase() != persistentedit::Phase::Retiring) retire();
        Reducer::Receipt receipt{attempt, value.outcome, {}, {value.value}, {}};
        receipt.persisted[0] = value.persisted;
        if (!reconcile && value.current && value.current->error.code == TextEditError::None && observation != UINT64_MAX)
            receipt.observed = Reducer::Observation{{value.current->value}, ++observation, attempt.token};
        if (!(reconcile ? editor.reconciled(receipt) : editor.finished(receipt))) return;
        validation_valid = evaluate();
        error = value.error; error_from_save = error.code != TextEditError::None;
        setResult(attempt, value.outcome, value.persisted, value.value, value.error);
        read();
        publish();
    }
    void retire()
    {
        if (pending_save) seen_request = std::max(seen_request, pending_save->request);
        pending_save.reset(); pending_input.reset();
        error = {TextEditError::Closed, tr("This editor is closed.")};
        editor.retire(); publish();
    }
    void reconcile()
    {
        if (editor.phase() != persistentedit::Phase::Uncertain || !editor.active()) return;
        const auto attempt = *editor.active();
        const auto self = shared_from_this();
        bool admitted{false};
        try { admitted = ports.query && bool(ports.query(attempt, [self, attempt](const Write& value) {
            // Query retains the original receipt, not a new current-value read.
            // finish() copies only its authoritative fields while this owned
            // callback argument lives; historical readback is never observed.
            self->finish(attempt, value, true);
        })); } catch (...) {}
        if (!admitted && editor.phase() == persistentedit::Phase::Uncertain) {
            error = {TextEditError::Unknown, tr("The save may not have finished. It will not be repeated.")}; publish();
        }
    }
};

TextEditSessionModel::TextEditSessionModel(QString entity, Ports ports, QObject* parent)
    : QObject(parent), m_editor_id(QUuid::createUuid().toString(QUuid::WithoutBraces)), m_entity_id(std::move(entity))
{
    Q_ASSERT(ports.postWork && ports.value_cleanup);
    auto post = presentation::ReliablePost(std::move(ports.post));
    const QPointer<TextEditSessionModel> observer{this};
    m_mailbox = std::make_shared<Mailbox>();
    const auto value_cleanup = ports.value_cleanup;
    const bool can_post = bool(ports.postWork);
    auto make_state =
        [editor_id = m_editor_id, entity_id = m_entity_id, ports = std::move(ports), mailbox = m_mailbox, observer, post = std::move(post)]() mutable {
            return std::make_unique<std::shared_ptr<State>>(std::make_shared<State>(editor_id, entity_id, std::move(ports), mailbox, [mailbox, observer, post] {
                if (mailbox->notice_pending.exchange(true)) return;
                try { post([mailbox, observer] {
                    mailbox->notice_pending.store(false);
                    if (observer) observer->publish();
                }); } catch (...) { mailbox->notice_pending.store(false); throw; }
            }));
        };
    if (value_cleanup && can_post) m_state_owner = execution::RetainValues<std::shared_ptr<State>>(value_cleanup, std::move(make_state));
    if (m_state_owner) {
        // GUI references release the original owner on its worker queue.
        m_state = std::shared_ptr<State>{m_state_owner, m_state_owner->get()};
        m_mailbox = (*m_state_owner)->mailbox;
    }
    else { m_retired = true; m_immediate_error = {TextEditError::Refused, tr("The editor could not be opened. Try again.")}; }
    connect(&m_text, &TextEditInput::edited, this, [this](const QString& value) {
        if (m_retired || m_input_sequence == UINT64_MAX) return;
        m_input_invalid = false;
        const auto sequence = ++m_input_sequence;
        const QPointer<TextEditSessionModel> guard{this};
        // The first edit flips hasLocalInput; views must stop showing the
        // pre-load fallback before any later publish can re-evaluate them.
        if (sequence == 1) { Q_EMIT changed(); if (!guard) return; }
        submit([value, sequence](State& state) { state.edit(value, sequence); }, QStringLiteral("edit"));
    });
    connect(&m_text, &TextEditInput::rejected, this, [this] {
        m_input_invalid = true;
        m_immediate_error = {available() ? TextEditError::Invalid : TextEditError::Closed,
            available() ? tr("This text is too long.") : tr("This editor is closed.")};
        const auto rejected = m_immediate_error;
        const QPointer<TextEditSessionModel> guard{this};
        Q_EMIT changed();
        if (guard) Q_EMIT commandRejected(QStringLiteral("edit"), rejected);
    });
    if (m_retired) m_text.setAvailable(false);
    else refresh();
}
TextEditSessionModel::~TextEditSessionModel()
{
    if (!std::exchange(m_retired, true)) {
        if (m_mailbox) m_mailbox->retired.store(true);
        if (const auto state = m_state.lock()) state->post([](State& owner) { owner.retire(); });
    }
    m_state_owner.reset();
}

bool TextEditSessionModel::submit(std::function<void(State&)> command, const QString& name)
{
    const QPointer<TextEditSessionModel> guard{this};
    bool admitted{false};
    {
        const auto state = m_state.lock();
        admitted = available() && state && state->post(std::move(command));
    }
    if (!guard) return false;
    if (admitted) { m_immediate_error = {}; return true; }
    m_immediate_error = {m_retired ? TextEditError::Closed : TextEditError::Refused, tr("This change could not be applied. Try again.")};
    const auto rejected = m_immediate_error;
    Q_EMIT changed();
    if (guard) Q_EMIT commandRejected(name, rejected);
    return false;
}
void TextEditSessionModel::publish()
{
    if (!m_mailbox) return;
    std::shared_ptr<const Projection> value;
    { std::lock_guard lock{m_mailbox->mutex}; value = m_mailbox->latest; }
    if (!value) return;
    m_projection = std::move(value);
    if (m_pending_save_sequence && m_projection->seen_request >= m_pending_save_sequence) m_pending_save_sequence = 0;
    if (m_projection->editor.closeQuestion()) {
        const auto token = closeToken();
        if (!m_prompt || m_prompt->token() != token) {
            if (m_prompt) m_prompt->deleteLater();
            m_prompt = new TextEditClosePrompt(token, this);
            connect(m_prompt, &TextEditClosePrompt::answered, this, [this](const QString& token, TextEditClosePrompt::Choice choice) {
                answerClose(token, static_cast<CloseChoice>(choice));
            });
        }
    } else if (m_prompt) { m_prompt->deleteLater(); m_prompt = nullptr; }
    const QPointer<TextEditSessionModel> guard{this};
    m_text.setAvailable(available());
    if (!guard) return;
    if (m_projection->input >= m_input_sequence) m_text.project(m_projection->editor.draft()[0]);
    if (!guard) return;
    Q_EMIT changed();
    if (!guard) return;
    if (m_projection->rejection > m_seen_rejection) {
        m_seen_rejection = m_projection->rejection;
        const auto rejected = m_projection->rejected_error;
        Q_EMIT commandRejected(m_editor_id + "/command-" + QString::number(m_seen_rejection), rejected);
        if (!guard) return;
    }
    if (!m_closed_notified && phase() == Closed) { m_closed_notified = true; Q_EMIT closed(m_editor_id); }
}
QString TextEditSessionModel::baseRevision() const { return QString::number(m_projection ? m_projection->editor.baseRevision() : 0); }
QString TextEditSessionModel::editRevision() const { return QString::number(m_projection ? m_projection->editor.editRevision() : 0); }
TextEditSessionModel::Phase TextEditSessionModel::phase() const {
    if (m_retired && m_mailbox && m_mailbox->drained.load()) return Closed;
    if (m_pending_save_sequence && m_projection && m_projection->editor.phase() == persistentedit::Phase::Editing) return Checking;
    return m_projection ? static_cast<Phase>(m_projection->editor.phase()) : m_retired ? Closed : Loading;
}
bool TextEditSessionModel::dirty() const { return m_projection && (m_projection->editor.dirty() || m_projection->pending_input); }
bool TextEditSessionModel::valid() const { return !m_input_invalid && m_projection && m_projection->valid; }
bool TextEditSessionModel::busy() const {
    return !(m_mailbox && m_mailbox->drained.load()) && (m_pending_save_sequence || (m_projection && m_projection->editor.busy()));
}
bool TextEditSessionModel::canSave() const { return !m_retired && !m_pending_save_sequence && valid() && m_projection->editor.canSave(); }
bool TextEditSessionModel::canDiscard() const { return !m_retired && !m_pending_save_sequence && m_projection && m_projection->editor.canDiscard(); }
TextEditError TextEditSessionModel::error() const { if (m_retired) return {TextEditError::Closed, tr("This editor is closed.")}; return m_immediate_error.code != TextEditError::None ? m_immediate_error : m_projection ? m_projection->error : TextEditError{}; }
bool TextEditSessionModel::conflict() const { return m_projection && m_projection->editor.conflicts().any(); }
QList<TextEditConflict> TextEditSessionModel::conflicts() const
{
    return conflict() ? QList<TextEditConflict>{{QStringLiteral("text"), committedText(), m_projection->editor.draft()[0]}} : QList<TextEditConflict>{};
}
QString TextEditSessionModel::committedText() const { return m_projection ? m_projection->editor.base()[0] : QString{}; }
QString TextEditSessionModel::closeToken() const { return QString::number(m_projection ? m_projection->editor.closeToken() : 0); }
TextEditSaveResult TextEditSessionModel::saveResult() const { return m_projection ? m_projection->result : TextEditSaveResult{}; }
bool TextEditSessionModel::refuseBusy(const QString& command)
{
    if (!available() || !busy()) return false;
    Q_EMIT commandRejected(m_editor_id + "/" + command + "/" + QUuid::createUuid().toString(QUuid::WithoutBraces),
        (TextEditError{TextEditError::Refused, tr("The save is still in progress.")}));
    return true;
}
void TextEditSessionModel::save() { saveCommand(); }
void TextEditSessionModel::saveCommand(std::optional<uint64_t> close_token)
{
    if (refuseBusy(QStringLiteral("save"))) return;
    const bool can_submit = available() && !m_input_invalid && m_next_save_sequence != UINT64_MAX;
    const auto request = m_next_save_sequence == UINT64_MAX ? 0 : m_next_save_sequence + 1;
    const auto value = m_text.text(); const auto sequence = m_input_sequence;
    const QPointer<TextEditSessionModel> observer{this};
    bool admitted{false};
    {
        const auto state = m_state.lock();
        Writer writer;
        try {
            if (can_submit && state) writer = state->ports.admitWrite ? state->ports.admitWrite() : state->ports.write;
        }
        catch (...) {}
        if (!observer) return;
        admitted = writer && state->post([writer = std::move(writer), close_token, value, sequence, request](State& state) mutable {
            state.edit(value, sequence);
            if (close_token && !state.editor.answerClose(*close_token, persistentedit::CloseChoice::Save)) {
                state.seen_request = std::max(state.seen_request, request);
                state.reject({TextEditError::Changed, tr("The close question changed. Review the current edit before answering.")}); state.publish(); return;
            }
            state.save(std::move(writer), request);
        });
    }
    if (!observer) return;
    if (admitted) {
        m_next_save_sequence = request; m_pending_save_sequence = request;
        m_immediate_error = {}; Q_EMIT changed(); return;
    }
    m_immediate_error = {!available() ? TextEditError::Closed : m_input_invalid ? TextEditError::Invalid : TextEditError::Refused,
        !available() ? tr("This editor is closed.") : m_input_invalid ? tr("Correct the rejected text before saving.") : tr("Saving could not start. Try again.")};
    const auto rejected = m_immediate_error;
    const QPointer<TextEditSessionModel> guard{this};
    Q_EMIT changed(); if (guard) Q_EMIT commandRejected(QStringLiteral("save"), rejected);
}
void TextEditSessionModel::discard() { if (refuseBusy(QStringLiteral("discard"))) return; m_input_invalid = false; submit([sequence = m_input_sequence](State& state) {
    state.input = std::max(state.input, sequence);
    state.pending_input.reset(); state.pending_save.reset(); state.close_after_load = false;
    if (state.editor.discard()) { state.error = {}; state.error_from_save = false; state.validation_valid = state.evaluate(); }
    state.publish();
}, QStringLiteral("discard")); }
void TextEditSessionModel::requestClose() { if (phase() == Closed || refuseBusy(QStringLiteral("requestClose"))) return; submit([](State& state) {
    if (state.editor.phase() == persistentedit::Phase::Loading && state.pending_input && !state.read_failed) state.close_after_load = true;
    else state.editor.requestClose();
    state.publish();
}, QStringLiteral("requestClose")); }
void TextEditSessionModel::answerClose(const QString& token, CloseChoice choice)
{
    bool okay{false}; const auto number = token.toULongLong(&okay);
    if (!okay || choice < Save || choice > KeepEditing || refuseBusy(QStringLiteral("answerClose"))) return;
    if (choice == Save) { saveCommand(number); return; }
    submit([number, choice](State& state) {
        if (state.editor.answerClose(number, static_cast<persistentedit::CloseChoice>(choice)) && choice == Discard) {
            state.error = {}; state.error_from_save = false; state.validation_valid = state.evaluate();
        }
        state.publish();
    }, QStringLiteral("answerClose"));
}
void TextEditSessionModel::resolveConflict(const QString& field, ConflictChoice choice)
{
    if (field != QStringLiteral("text") || (choice != KeepEdit && choice != UseCurrent)) return;
    submit([choice](State& state) { if (state.editor.resolve(0, choice == KeepEdit)) { state.error = {}; state.error_from_save = false; state.validation_valid = state.evaluate(); } state.publish(); }, QStringLiteral("resolveConflict"));
}
void TextEditSessionModel::refresh() { submit([](State& state) { state.read(); }, QStringLiteral("refresh")); }
void TextEditSessionModel::reconcile() { submit([](State& state) { state.reconcile(); }, QStringLiteral("reconcile")); }
void TextEditSessionModel::retire()
{
    if (std::exchange(m_retired, true)) return;
    if (m_mailbox) m_mailbox->retired.store(true);
    if (const auto state = m_state.lock()) state->post([](State& owner) { owner.retire(); });
    // Accepted effects retain their original owner through worker delivery.
    m_state_owner.reset();
    const QPointer<TextEditSessionModel> guard{this};
    m_text.setAvailable(false);
    if (guard) Q_EMIT changed();
}
