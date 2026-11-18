// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_QML_MODELS_PAYMENTREQUESTEDITORMODEL_H
#define BITCOIN_QML_MODELS_PAYMENTREQUESTEDITORMODEL_H

#include <qml/bitcoinamount.h>
#include <qml/models/texteditsessionmodel.h>
#include <qml/models/receiptreconciliationmodel.h>
#include <qml/walletrequests.h>

namespace requestedit {
enum Field : size_t { Label, Message, Note, Amount, Mutation, Fields };
struct Value {
    QString text;
    CAmount amount{0};
    bool intent{false};
    bool acceptable{true};
    bool operator==(const Value&) const = default;
};
using Editor = persistentedit::Editor<Value, Fields>;
struct Input { Value value; uint64_t sequence; };
using Inputs = std::array<std::optional<Input>, 4>;
/** Prepared by the single request/reservation writer on Domain. No Wallet or
 * live GUI object is retained in this original source capability. */
struct Source {
    int64_t id{0};
    uint64_t revision{0}, view{0};
    QString address, address_type, uri;
    std::optional<QString> current_label;
    Editor::Values values{};
    std::shared_ptr<const walletrequests::AddressIntent> reservation;
    walletrequests::Error error{walletrequests::Error::None};
    bool loaded{false}, current{false}, exists{false}, used{false}, closed{false};
    // Optional value-only freshness capability for delayed view admission.
    std::function<bool()> permission;
};
Source PrepareSource(const walletrequests::SharedRecord& record, uint64_t view);
struct Intent {
    Editor::Attempt edit;
    Source source;
    bool erase{false};
};
struct Write {
    walletrequests::Result actual;
    std::optional<Source> current;
};
} // namespace requestedit

class RequestSaveResult {
    Q_GADGET
    Q_PROPERTY(bool present MEMBER present)
    Q_PROPERTY(bool outcomeKnown MEMBER outcomeKnown)
    Q_PROPERTY(bool requestSaved MEMBER requestSaved)
    Q_PROPERTY(bool erased MEMBER erased)
    Q_PROPERTY(bool labelSaved MEMBER labelSaved)
    Q_PROPERTY(QString operationId MEMBER operationId)
    Q_PROPERTY(QString editorId MEMBER editorId)
    Q_PROPERTY(QString requestId MEMBER requestId)
    Q_PROPERTY(ReceiveError::Code error MEMBER error)
    Q_PROPERTY(QString errorText MEMBER errorText)
public:
    bool present{false}, outcomeKnown{true}, requestSaved{false}, erased{false}, labelSaved{false};
    QString operationId, editorId, requestId, errorText;
    walletrequests::Error error{walletrequests::Error::None};
};
Q_DECLARE_METATYPE(RequestSaveResult)

/** Bounded typed presentation of the shared persistent-editor protocol.
 * Source, validation, conflict and persistence decisions run on Domain.
 * Destroying this object never cancels or releases an admitted native write. */
class PaymentRequestEditorModel final : public QObject
{
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
    Q_PROPERTY(bool publicFieldsEditable READ publicFieldsEditable NOTIFY changed)
    Q_PROPERTY(MutationKind mutationKind READ mutationKind NOTIFY changed)
    Q_PROPERTY(TextEditError error READ error NOTIFY changed)
    Q_PROPERTY(QList<TextEditConflict> conflicts READ conflicts NOTIFY changed)
    Q_PROPERTY(TextEditClosePrompt* closePrompt READ closePrompt NOTIFY changed)
    Q_PROPERTY(RequestSaveResult saveResult READ saveResult NOTIFY changed)
    Q_PROPERTY(TextEditInput* publicLabel READ publicLabel CONSTANT)
    Q_PROPERTY(TextEditInput* message READ message CONSTANT)
    Q_PROPERTY(TextEditInput* privateNote READ privateNote CONSTANT)
    Q_PROPERTY(BitcoinAmount* amount READ amount CONSTANT)
    Q_PROPERTY(QString requestId READ requestId NOTIFY changed)
    Q_PROPERTY(QString address READ address NOTIFY changed)
    Q_PROPERTY(QString addressType READ addressType NOTIFY changed)
    Q_PROPERTY(QString reservationId READ reservationId NOTIFY changed)
    Q_PROPERTY(QString paymentUri READ paymentUri NOTIFY changed)
    Q_PROPERTY(bool historicallyUsed READ historicallyUsed NOTIFY changed)
    Q_PROPERTY(RequestReceiptModel* receipts READ receipts CONSTANT)
public:
    enum Phase { Loading, Editing, Checking, Saving, Conflict, Uncertain, Retiring, Closed };
    Q_ENUM(Phase)
    enum MutationKind { Create, Update, Delete };
    Q_ENUM(MutationKind)
    using Writer = std::function<execution::SubmitResult(const requestedit::Intent&, std::function<void(requestedit::Write)>, std::shared_ptr<execution::Scheduler>)>;
    struct Ports {
        execution::SnapshotServices execution;
        std::function<execution::SubmitResult(int64_t, uint64_t, std::function<void(requestedit::Source)>)> read;
        std::function<Writer()> admitWrite;
        std::function<execution::SubmitResult(const requestedit::Intent&, std::function<void(requestedit::Write)>)> query;
    };
    PaymentRequestEditorModel(int64_t request_id, uint64_t view, bool erase, Ports ports,
                              std::optional<requestedit::Editor::Values> initial = {}, QObject* parent = nullptr);
    ~PaymentRequestEditorModel() override;
    QString editorId() const { return m_editor_id; }
    QString entityId() const { return m_entity_id; }
    QString baseRevision() const;
    QString editRevision() const;
    Phase phase() const;
    bool available() const;
    bool dirty() const;
    bool valid() const;
    bool busy() const;
    bool canSave() const;
    bool canDiscard() const;
    bool publicFieldsEditable() const;
    MutationKind mutationKind() const;
    TextEditError error() const;
    QList<TextEditConflict> conflicts() const;
    TextEditClosePrompt* closePrompt() const { return m_prompt; }
    RequestSaveResult saveResult() const;
    TextEditInput* publicLabel() { return &m_label; }
    TextEditInput* message() { return &m_message; }
    TextEditInput* privateNote() { return &m_note; }
    BitcoinAmount* amount() { return &m_amount; }
    QString requestId() const;
    QString address() const;
    QString addressType() const;
    QString reservationId() const;
    QString paymentUri() const;
    bool historicallyUsed() const;
    RequestReceiptModel* receipts() { return &m_receipts; }
    void setReceiptReconciler(ReceiptReconciliationModel* reader) { m_receipts.setReader(reader); }
    std::shared_ptr<std::atomic_bool> sharingInputGuard() const { return m_sharing_input; }
    std::shared_ptr<ChangeCounter> sharingInputEpoch() const { return m_sharing_epoch; }
    Q_INVOKABLE void save();
    Q_INVOKABLE void discard();
    Q_INVOKABLE void requestClose();
    Q_INVOKABLE void resolveConflict(const QString& field, TextEditSessionModel::ConflictChoice choice);
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void reconcile();
    void retire();
    void reservationChanged();
Q_SIGNALS:
    void changed();
    void closed(const QString& editor_id);
    void keptEditing(const QString& editor_id, const QString& close_token);
    void commandRejected(const QString& command_id, const TextEditError& error);
    void persisted(const RequestSaveResult& result);
private:
    struct State;
    struct Projection;
    struct Mailbox;
    void publish();
    void edit(size_t field, requestedit::Value value);
    bool submit(std::function<void(State&)> command, const QString& name);
    void saveCommand(std::optional<uint64_t> close_token = {});
    void answerClose(const QString& token, TextEditClosePrompt::Choice choice);
    bool reject(const QString& command, TextEditError error);
    const QString m_editor_id, m_entity_id;
    execution::SnapshotServices m_execution;
    TextEditInput m_label{this}, m_message{this}, m_note{this};
    BitcoinAmount m_amount{this};
    RequestReceiptModel m_receipts{this};
    std::weak_ptr<State> m_state;
    std::shared_ptr<std::shared_ptr<State>> m_owner;
    std::shared_ptr<Mailbox> m_mailbox;
    std::shared_ptr<const Projection> m_projection;
    TextEditClosePrompt* m_prompt{nullptr};
    TextEditError m_immediate_error;
    const std::shared_ptr<std::atomic_bool> m_sharing_input{std::make_shared<std::atomic_bool>(false)};
    const std::shared_ptr<ChangeCounter> m_sharing_epoch{std::make_shared<ChangeCounter>()};
    std::array<bool, 3> m_text_invalid{};
    requestedit::Inputs m_inputs;
    uint64_t m_input{0}, m_pending_save{0}, m_next_save{0}, m_seen_rejection{0}, m_seen_result{0}, m_seen_keep{0};
    uint64_t m_pending_reset{0}, m_reset_amount_at{0};
    bool m_retired{false}, m_projecting{false}, m_publishing{false}, m_publish_again{false};
    bool m_invalid_input{false}, m_amount_edit_intent{false}, m_closed_notified{false};
};

#endif // BITCOIN_QML_MODELS_PAYMENTREQUESTEDITORMODEL_H
