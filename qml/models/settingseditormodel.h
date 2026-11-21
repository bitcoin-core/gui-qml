// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_QML_MODELS_SETTINGSEDITORMODEL_H
#define BITCOIN_QML_MODELS_SETTINGSEDITORMODEL_H
#include <qml/settingssession.h>
#include <qml/snapshotquery.h>
#include <QQmlListProperty>
#include <QObject>
#include <array>
class ArgsManager;
namespace interfaces { class Node; }
class SettingsSaveResult {
    Q_GADGET
    Q_PROPERTY(QString editorId MEMBER editorId)
    Q_PROPERTY(QString operationId MEMBER operationId)
    Q_PROPERTY(Outcome outcome MEMBER outcome)
    Q_PROPERTY(bool present MEMBER present)
    Q_PROPERTY(bool persisted MEMBER persisted)
    Q_PROPERTY(bool outcomeKnown MEMBER outcomeKnown)
    Q_PROPERTY(QString actualValues MEMBER actualValues)
    Q_PROPERTY(QString error MEMBER error)
public:
    enum Outcome {Saved,Rejected,Failed,Unknown,Partial};Q_ENUM(Outcome)
    QString editorId,operationId;
    Outcome outcome{Unknown};
    bool present{false},persisted{false},outcomeKnown{false};
    QString actualValues,error;
};
Q_DECLARE_METATYPE(SettingsSaveResult)
class SettingsEditorModel;
/** Bounded typed projection of one field. Commands carry intent; properties
 * never write persistent settings or copy another model's state. */
class SettingsEntryModel final:public QObject {
    Q_OBJECT
    Q_PROPERTY(QString key READ key CONSTANT)
    Q_PROPERTY(QString title READ title CONSTANT)
    Q_PROPERTY(QString section READ section CONSTANT)
    Q_PROPERTY(InputKind kind READ kind CONSTANT)
    Q_PROPERTY(bool editable READ editable NOTIFY changed)
    Q_PROPERTY(bool booleanValue READ booleanValue NOTIFY changed)
    Q_PROPERTY(QString text READ text NOTIFY changed)
    Q_PROPERTY(QString committedText READ committedText NOTIFY changed)
    Q_PROPERTY(QString effectiveText READ effectiveText NOTIFY changed)
    Q_PROPERTY(QString nextStartText READ nextStartText NOTIFY changed)
    Q_PROPERTY(QString source READ source NOTIFY changed)
    Q_PROPERTY(QString information READ information NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(bool dirty READ dirty NOTIFY changed)
    Q_PROPERTY(bool conflict READ conflict NOTIFY changed)
    Q_PROPERTY(bool overridden READ overridden NOTIFY changed)
public:
    enum InputKind {Boolean,Integer,Text};Q_ENUM(InputKind)
    SettingsEntryModel(effectivesettings::Key key,SettingsEditorModel& editor);
    QString title()const;
    QString section()const;
    InputKind kind()const;
    QString key()const;
    bool editable()const;
    bool booleanValue()const;
    QString text()const;
    QString committedText()const;
    QString effectiveText()const;
    QString nextStartText()const;
    QString source()const;
    QString information()const;
    QString error()const;
    bool dirty()const;
    bool conflict()const;
    bool overridden()const;
    Q_INVOKABLE void editBoolean(bool value);
    Q_INVOKABLE void editText(const QString& text);
    Q_INVOKABLE void useConfiguration();
    Q_INVOKABLE void resolve(bool keepEdit);
Q_SIGNALS:
    void changed();
private:
    const effectivesettings::Key m_key;
    SettingsEditorModel& m_editor;
};
class SettingsEditorModel final:public QObject {
    Q_OBJECT
    Q_PROPERTY(QString editorId READ editorId CONSTANT)
    Q_PROPERTY(SettingsSaveResult saveResult READ saveResult NOTIFY changed)
    Q_PROPERTY(QQmlListProperty<SettingsEntryModel> entries READ entries CONSTANT)
    Q_PROPERTY(QString lastSaveId READ lastSaveId NOTIFY changed)
    Q_PROPERTY(QString redownloadToken READ redownloadToken NOTIFY changed)
    Q_PROPERTY(Phase phase READ phase NOTIFY changed)
    Q_PROPERTY(bool fresh READ fresh NOTIFY changed)
    Q_PROPERTY(bool inputPending READ inputPending NOTIFY changed)
    Q_PROPERTY(bool dirty READ dirty NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool canSave READ canSave NOTIFY changed)
    Q_PROPERTY(bool canDiscard READ canDiscard NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QString closeToken READ closeToken NOTIFY changed)
    Q_PROPERTY(bool closeQuestion READ closeQuestion NOTIFY changed)
    Q_PROPERTY(bool lastSavePresent READ lastSavePresent NOTIFY changed)
    Q_PROPERTY(bool lastSavePersisted READ lastSavePersisted NOTIFY changed)
    Q_PROPERTY(bool lastSaveKnown READ lastSaveKnown NOTIFY changed)
    Q_PROPERTY(bool restartRequired READ restartRequired NOTIFY changed)
    Q_PROPERTY(bool restartNoticeVisible READ restartNoticeVisible NOTIFY changed)
    Q_PROPERTY(QString restartExplanation READ restartExplanation CONSTANT)
    Q_PROPERTY(bool redownloadWarning READ redownloadWarning NOTIFY changed)
public:
    enum Phase {Loading,Editing,Checking,Saving,Conflict,Uncertain,Retiring,Closed};Q_ENUM(Phase)
    enum CloseChoice {Save,Discard,KeepEditing};Q_ENUM(CloseChoice)
    enum PruneMode {Disabled,Manual,Automatic};Q_ENUM(PruneMode)
    struct Ports {
        execution::SnapshotServices execution;
        // Invoked only on the serialized Core lane. Results are owned values.
        // A write-completed snapshot must be a fresh post-effect read, never
        // the original/check snapshot. Retained outcome queries omit it.
        std::function<effectivesettings::Snapshot()> read;
        std::function<effectivesettings::Check(const settingsedit::Save&)> review;
        std::function<effectivesettings::Result(const settingsedit::Save&)> write;
        // Original interface owner is released after physical Native drain,
        // independently of whether this Qt projection is still alive. This
        // background acknowledgement must not access a foreign QObject.
        std::function<void()> retired{};
    };
    static Ports PublicPorts(interfaces::Node& node,ArgsManager& args,execution::SnapshotServices execution);
    explicit SettingsEditorModel(Ports ports,QObject* parent=nullptr);
    ~SettingsEditorModel()override;
    QString editorId()const{return m_id;}
    SettingsSaveResult saveResult()const;
    QQmlListProperty<SettingsEntryModel> entries();
    QString lastSaveId()const;
    QString redownloadToken()const;
    Phase phase()const;
    bool fresh()const;
    bool inputPending()const;
    bool dirty()const;
    bool busy()const;
    bool canSave()const;
    bool canDiscard()const;
    QString error()const;
    QString closeToken()const;
    bool closeQuestion()const;
    bool lastSavePresent()const;
    bool lastSavePersisted()const;
    bool lastSaveKnown()const;
    bool restartRequired()const;
    bool restartNoticeVisible()const;
    QString restartExplanation()const;
    bool redownloadWarning()const;
    Q_INVOKABLE SettingsEntryModel* entry(const QString& key)const;
    Q_INVOKABLE void save();
    Q_INVOKABLE void discard();
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void reconcile();
    Q_INVOKABLE void requestClose();
    Q_INVOKABLE void answerClose(const QString& token,CloseChoice choice);
    Q_INVOKABLE void acknowledgeRestart();
    Q_INVOKABLE void setPruneMode(PruneMode mode);
    Q_INVOKABLE void confirmRedownload(const QString& token,bool accepted);
    Q_INVOKABLE void close();
    void retire(std::function<void()> drained={});
Q_SIGNALS:
    void changed();
    void closed();
    void shutdownFinished();
    void commandRejected(const QString& command,const QString& error);
private:
    friend class SettingsEntryModel;
    struct State;struct Projection;struct Mailbox;
    bool command(std::function<void(State&)> command,const QString& name);
    void saveCommand(std::optional<uint64_t> close_token={});
    void publish();
    const effectivesettings::Field* field(effectivesettings::Key key)const;
    void editInput(effectivesettings::Key key,std::variant<bool,QString> value);
    void editBoolean(effectivesettings::Key key,bool value);
    void editText(effectivesettings::Key key,QString text);
    void useConfiguration(effectivesettings::Key key);
    void resolve(effectivesettings::Key key,bool keep);
    const QString m_id;
    execution::SnapshotServices m_execution;
    std::array<SettingsEntryModel*,settingsedit::FIELDS> m_entries{};
    std::weak_ptr<State> m_state;
    std::shared_ptr<Mailbox> m_mailbox;
    std::shared_ptr<execution::Scheduler> m_retire_scope;
    std::shared_ptr<std::shared_ptr<State>> m_owner;
    std::shared_ptr<const Projection> m_projection;
    QString m_immediate_error;
    // Only the bounded literal/toggle presentation is optimistic. Domain owns
    // parsing, validation and the draft; an older projection cannot overwrite
    // a more recent admitted input while the next key press is being composed.
    struct PendingInput {uint64_t sequence{0};std::optional<std::variant<bool,QString>> value;};
    std::array<PendingInput,settingsedit::FIELDS> m_inputs{};
    uint64_t m_next_input{0};
    uint64_t m_next_save{0},m_pending_save{0},m_seen_rejection{0};
    bool m_retired{false},m_drained{false},m_closed_emitted{false};
};
#endif // BITCOIN_QML_MODELS_SETTINGSEDITORMODEL_H
