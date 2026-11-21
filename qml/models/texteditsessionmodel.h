// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_QML_MODELS_TEXTEDITSESSIONMODEL_H
#define BITCOIN_QML_MODELS_TEXTEDITSESSIONMODEL_H
#include <qml/persistenteditor.h>
#include <qml/snapshotquery.h>
#include <QObject>
#include <QString>

class TextEditInput final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString text READ text NOTIFY changed)
    Q_PROPERTY(bool available READ available NOTIFY changed)
public:
    explicit TextEditInput(QObject* parent) : QObject(parent) {}
    QString text() const { return m_text; }
    Q_INVOKABLE void edit(const QString& value);
    void project(const QString& value);
    bool available() const { return m_available; }
    void setAvailable(bool value) { if (m_available != value) { m_available = value; Q_EMIT changed(); } }
Q_SIGNALS:
    void changed();
    void edited(const QString& value);
    void rejected();
private:
    QString m_text;
    bool m_available{true};
};

class TextEditError {
    Q_GADGET
    Q_PROPERTY(Code code MEMBER code)
    Q_PROPERTY(QString message MEMBER message)
public:
    enum Code { None, Invalid, Changed, Refused, Persistence, Unknown, Closed };
    Q_ENUM(Code)
    Code code{None};
    QString message;
    bool operator==(const TextEditError&) const = default;
};
Q_DECLARE_METATYPE(TextEditError)

class TextEditConflict {
    Q_GADGET
    Q_PROPERTY(QString fieldId MEMBER fieldId)
    Q_PROPERTY(QString committedText MEMBER committedText)
    Q_PROPERTY(QString draftText MEMBER draftText)
public:
    QString fieldId, committedText, draftText;
};
Q_DECLARE_METATYPE(TextEditConflict)

class TextEditSaveResult {
    Q_GADGET
    Q_PROPERTY(QString operationId MEMBER operationId)
    Q_PROPERTY(QString editorId MEMBER editorId)
    Q_PROPERTY(QString entityId MEMBER entityId)
    Q_PROPERTY(Outcome outcome MEMBER outcome)
    Q_PROPERTY(bool present MEMBER present)
    Q_PROPERTY(bool persisted MEMBER persisted)
    Q_PROPERTY(bool outcomeKnown MEMBER outcomeKnown)
    Q_PROPERTY(QString value MEMBER value)
    Q_PROPERTY(TextEditError error MEMBER error)
public:
    enum Outcome { Saved, Rejected, Failed, Unknown, Partial };
    Q_ENUM(Outcome)
    QString operationId, editorId, entityId;
    Outcome outcome{Unknown};
    bool present{false}, persisted{false}, outcomeKnown{false};
    QString value;
    TextEditError error;
};
Q_DECLARE_METATYPE(TextEditSaveResult)

class TextEditClosePrompt final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString token READ token CONSTANT)
public:
    enum Choice { Save, Discard, KeepEditing };
    Q_ENUM(Choice)
    TextEditClosePrompt(QString token, QObject* parent) : QObject(parent), m_token(std::move(token)) {}
    QString token() const { return m_token; }
    Q_INVOKABLE void answer(Choice choice) { if (choice >= Save && choice <= KeepEditing) Q_EMIT answered(m_token, choice); }
Q_SIGNALS:
    void answered(const QString& token, Choice choice);
private:
    const QString m_token;
};

/** Bounded GUI projection. The reducer and all read/validation/write decisions
 * belong to State on Domain. Ports must report actual effects, not UI guesses.
 * The owner survives a destroyed projection until an admitted write settles.
 */
class TextEditSessionModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString editorId READ editorId CONSTANT)
    Q_PROPERTY(QString entityId READ entityId CONSTANT)
    Q_PROPERTY(QString baseRevision READ baseRevision NOTIFY changed)
    Q_PROPERTY(QString editRevision READ editRevision NOTIFY changed)
    Q_PROPERTY(Phase phase READ phase NOTIFY changed)
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(bool dirty READ dirty NOTIFY changed)
    Q_PROPERTY(bool valid READ valid NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool canSave READ canSave NOTIFY changed)
    Q_PROPERTY(bool canDiscard READ canDiscard NOTIFY changed)
    Q_PROPERTY(TextEditError error READ error NOTIFY changed)
    Q_PROPERTY(bool conflict READ conflict NOTIFY changed)
    Q_PROPERTY(QList<TextEditConflict> conflicts READ conflicts NOTIFY changed)
    Q_PROPERTY(QString committedText READ committedText NOTIFY changed)
    Q_PROPERTY(TextEditInput* text READ text CONSTANT)
    Q_PROPERTY(TextEditClosePrompt* closePrompt READ closePrompt NOTIFY changed)
    Q_PROPERTY(QString closeToken READ closeToken NOTIFY changed)
    Q_PROPERTY(TextEditSaveResult saveResult READ saveResult NOTIFY changed)
public:
    enum Phase { Loading, Editing, Checking, Saving, Conflict, Uncertain, Retiring, Closed };
    Q_ENUM(Phase)
    enum ConflictChoice { KeepEdit, UseCurrent };
    Q_ENUM(ConflictChoice)
    enum CloseChoice { Save, Discard, KeepEditing };
    Q_ENUM(CloseChoice)
    using Reducer = persistentedit::Editor<QString, 1>;
    struct Read { QString value; TextEditError error; };
    struct Write {
        persistentedit::Outcome outcome{persistentedit::Outcome::Failed};
        bool persisted{false};
        QString value;
        std::optional<Read> current;
        TextEditError error;
    };
    using Writer = std::function<execution::Admission(const Reducer::Attempt&, std::function<void(Write)>, std::shared_ptr<execution::Scheduler>)>;
    struct Ports {
        execution::SnapshotServices execution;
        // Completions run on the same Domain lane as this editor. They must
        // survive quiet close; GUI delivery is never a persistence dependency.
        std::function<execution::Admission(std::function<void(Read)>)> read;
        Writer write;
        // GUI-safe admission/lifetime capture only. No Core or I/O here.
        // The returned writer is bound to this original admitted intent.
        std::function<Writer()> admitWrite;
        // Query only: never call the write port again for an uncertain attempt.
        std::function<execution::Admission(const Reducer::Attempt&, std::function<void(Write)>)> query;
        std::function<bool(const QString&)> validate;
    };
    TextEditSessionModel(QString entity, Ports ports, QObject* parent = nullptr);
    ~TextEditSessionModel() override;
    QString editorId() const { return m_editor_id; }
    QString entityId() const { return m_entity_id; }
    QString baseRevision() const;
    QString editRevision() const;
    Phase phase() const;
    bool available() const { return !m_retired && phase() != Closed && phase() != Retiring; }
    bool dirty() const;
    bool valid() const;
    bool busy() const;
    bool canSave() const;
    bool canDiscard() const;
    TextEditError error() const;
    bool conflict() const;
    QList<TextEditConflict> conflicts() const;
    QString committedText() const;
    TextEditInput* text() { return &m_text; }
    TextEditClosePrompt* closePrompt() const { return m_prompt; }
    QString closeToken() const;
    TextEditSaveResult saveResult() const;
    Q_INVOKABLE void save();
    Q_INVOKABLE void discard();
    Q_INVOKABLE void requestClose();
    Q_INVOKABLE void answerClose(const QString& token, CloseChoice choice);
    Q_INVOKABLE void resolveConflict(const QString& field, ConflictChoice choice);
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void reconcile();
    void retire();
Q_SIGNALS:
    void changed();
    void closed(const QString& editor_id);
    void commandRejected(const QString& command_id, const TextEditError& error);
private:
    struct State;
    struct Projection;
    struct Mailbox;
    void publish();
    void saveCommand(std::optional<uint64_t> close_token = {});
    bool refuseBusy(const QString& command);
    bool submit(std::function<void(State&)> command, const QString& name);
    const QString m_editor_id, m_entity_id;
    execution::SnapshotServices m_execution;
    TextEditError m_immediate_error;
    TextEditClosePrompt* m_prompt{nullptr};
    TextEditInput m_text{this};
    std::weak_ptr<State> m_state;
    std::shared_ptr<Mailbox> m_mailbox;
    std::shared_ptr<std::shared_ptr<State>> m_state_owner;
    std::shared_ptr<const Projection> m_projection;
    uint64_t m_input_sequence{0}, m_next_save_sequence{0}, m_pending_save_sequence{0}, m_seen_rejection{0};
    bool m_retired{false}, m_input_invalid{false}, m_closed_notified{false};
};
#endif // BITCOIN_QML_MODELS_TEXTEDITSESSIONMODEL_H
