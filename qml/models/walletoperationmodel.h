// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_QML_MODELS_WALLETOPERATIONMODEL_H
#define BITCOIN_QML_MODELS_WALLETOPERATIONMODEL_H
#include <qml/walletcreation.h>
#include <qml/walletrestoration.h>
#include <qml/walletoperationpresentation.h>
#include <QAbstractListModel>
#include <QObject>
#include <QPointer>
#include <QQmlListProperty>
class WalletQmlModel;
Q_MOC_INCLUDE("qml/models/walletqmlmodel.h")
class WalletCreationModel;
class WalletOperationModel;
class WalletOperationPrompt final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString promptId READ promptId CONSTANT)
    Q_PROPERTY(QString operationId READ operationId CONSTANT)
    Q_PROPERTY(Kind kind READ kind CONSTANT)
    Q_PROPERTY(QString title READ title NOTIFY changed)
    Q_PROPERTY(QString message READ message NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(WalletOperationError error READ error NOTIFY changed)
public:
    enum Kind { Confirmation, Passphrase }; Q_ENUM(Kind)
    enum Response { Decline, Confirm }; Q_ENUM(Response)
    WalletOperationPrompt(WalletOperationModel* operation, QString id, Kind kind);
    QString promptId() const;
    QString operationId() const;
    Kind kind() const;
    QString title() const;
    QString message() const;
    bool busy() const;
    WalletOperationError error() const;
    Q_INVOKABLE WalletCreationReceipt answer(const QString& prompt, Response response);
    Q_INVOKABLE WalletCreationReceipt submitSecret(const QString& prompt, const QString& secret);
    Q_INVOKABLE WalletCreationReceipt dismiss(const QString& prompt);
    void refresh();
Q_SIGNALS:
    void changed();
private:
    WalletOperationModel* const m_operation;
    WalletOperationError m_submission_error;
    const QString m_id;
    const Kind m_kind;
    bool m_submitting{false};
};
class WalletOperationText final : public QAbstractListModel {
    Q_OBJECT
public:
    WalletOperationText(bool warning, QObject* parent);
    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;
    void bind(std::shared_ptr<walletcreation::OperationOwner> owner);
    void refresh();
private:
    QString text() const;
    const bool m_warning;
    std::shared_ptr<walletcreation::OperationOwner> m_owner;
};
class WalletOperationModel final : public QObject, public WalletOperationPresentation {
    Q_OBJECT
    Q_INTERFACES(WalletOperationPresentation)
    Q_PROPERTY(OperationKind kind READ kind CONSTANT)
    Q_PROPERTY(Kind creationKind READ qmlCreationKind CONSTANT)
    Q_PROPERTY(QString operationId READ operationId CONSTANT)
    Q_PROPERTY(QString submittedName READ submittedName CONSTANT)
    Q_PROPERTY(quint64 editorRevision READ editorRevision CONSTANT)
    Q_PROPERTY(QString targetWalletId READ targetWalletId NOTIFY changed)
    Q_PROPERTY(Phase phase READ phase NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool terminal READ terminal NOTIFY changed)
    Q_PROPERTY(bool outcomeKnown READ outcomeKnown NOTIFY changed)
    Q_PROPERTY(bool acceptedForDelivery READ acceptedForDelivery CONSTANT)
    Q_PROPERTY(WalletOperationError error READ error NOTIFY changed)
    Q_PROPERTY(WalletOperationResult result READ result NOTIFY changed)
    Q_PROPERTY(WalletQmlModel* resultWallet READ resultWallet NOTIFY changed)
    Q_PROPERTY(QQmlListProperty<WalletQmlModel> resultWallets READ resultWallets NOTIFY changed)
    Q_PROPERTY(bool canOpenResult READ canOpenResult NOTIFY changed)
    Q_PROPERTY(bool canCancel READ canCancel NOTIFY changed)
    Q_PROPERTY(bool canRetry READ canRetry NOTIFY changed)
    Q_PROPERTY(bool canReconcile READ canReconcile NOTIFY changed)
    Q_PROPERTY(bool canAcknowledge READ canAcknowledge NOTIFY changed)
    Q_PROPERTY(bool encrypted READ encrypted CONSTANT)
    Q_PROPERTY(QString keyScheme READ keyScheme NOTIFY changed)
    Q_PROPERTY(QAbstractItemModel* errorDetails READ errorDetails CONSTANT)
    Q_PROPERTY(QAbstractItemModel* warningDetails READ warningDetails CONSTANT)
    Q_PROPERTY(WalletOperationPrompt* prompt READ prompt NOTIFY changed)
    Q_PROPERTY(QQmlListProperty<WalletOperationPrompt> prompts READ prompts NOTIFY promptsChanged)
    Q_PROPERTY(WalletOperationPrompt* confirmationPrompt READ confirmationPrompt NOTIFY promptsChanged)
    Q_PROPERTY(WalletOperationPrompt* passphrasePrompt READ passphrasePrompt NOTIFY promptsChanged)
    Q_PROPERTY(QString errorTitle READ errorTitle NOTIFY changed)
    Q_PROPERTY(QString errorDescription READ errorDescription NOTIFY changed)
    Q_PROPERTY(QUrl source READ source CONSTANT)
public:
    enum OperationKind { Create, Restore, Migrate, Open };
    Q_ENUM(OperationKind)
    OperationKind kind() const { return m_kind; }
    enum Kind { Software, WatchOnly, ExternalSigner };
    Q_ENUM(Kind)
    Kind qmlCreationKind() const { return static_cast<Kind>(m_request.kind); }
    enum Phase { Requested, Validating, Creating, SettingUp, Publishing, Succeeded, Failed, Partial, Uncertain, Rejected, Cancelled, AwaitingConfirmation, AwaitingPassphrase };
    Q_ENUM(Phase)
    struct Services {
        walletcreation::Services native;
        std::shared_ptr<walletpublication::WalletCatalogPublisher> publisher;
        QPointer<QObject> projection_parent;
    };
    static WalletOperationModel* Make(Services services, walletcreation::Request request);
    static WalletOperationModel* Make(Services services, walletrestoration::Request request);
    static WalletOperationModel* OpenNotice(Services services, quint64 id, QString target, QString warnings, std::shared_ptr<const void> lifetime, WalletQmlModel* original);
    ~WalletOperationModel() override;
    QString operationId() const { return QString::number(m_request.id); }
    quint64 sequence() const { return m_request.id; }
    walletcreation::Kind creationKind() const { return m_request.kind; }
    QString submittedName() const { return m_request.submitted_name; }
    quint64 editorRevision() const { return m_request.editor_revision; }
    QString targetWalletId() const { return m_frame.target_id; }
    Phase phase() const { return static_cast<Phase>(m_frame.phase); }
    bool busy() const { return m_command_pending || walletcreation::Busy(m_frame.phase); }
    bool terminal() const { return !m_command_pending && walletcreation::Terminal(m_frame.phase); }
    bool outcomeKnown() const { return m_frame.outcome_known; }
    bool acceptedForDelivery() const { return m_accepted; }
    WalletOperationError error() const { return m_frame.error; }
    WalletOperationResult result() const { return m_frame.result; }
    WalletQmlModel* resultWallet() const;
    QQmlListProperty<WalletQmlModel> resultWallets();
    bool canOpenResult() const;
    bool canCancel() const { return phase() == Requested || phase() == Validating || phase() == AwaitingConfirmation || phase() == AwaitingPassphrase; }
    bool canRetry() const { return false; }
    bool canReconcile() const;
    bool canAcknowledge() const { return terminal() && !m_acknowledged; }
    bool encrypted() const { return m_request.kind == walletcreation::Kind::Software && m_request.encrypt; }
    QString keyScheme() const { return m_frame.key_scheme; }
    QAbstractItemModel* errorDetails() { return &m_errors; }
    QAbstractItemModel* warningDetails() { return &m_warnings; }
    WalletOperationPrompt* prompt() { return phase() == AwaitingConfirmation || phase() == AwaitingPassphrase ? m_prompt : nullptr; }
    QQmlListProperty<WalletOperationPrompt> prompts();
    WalletOperationPrompt* confirmationPrompt() { return m_confirmation_prompt; }
    WalletOperationPrompt* passphrasePrompt() { return m_passphrase_prompt; }
    QString errorTitle() const { return m_frame.error_title; }
    QString errorDescription() const { return m_frame.error_description; }
    QUrl source() const { return QUrl{m_source}; }
    QString errorText() const;
    QString warningText() const;
    void closeAdmission();
    QString presentationId() const override { return operationId(); }
    bool presentationBusy() const override { return busy(); }
    bool presentationTerminal() const override { return terminal(); }
    bool presentationHasPrompt() const override { return phase() == AwaitingConfirmation || phase() == AwaitingPassphrase; }
    QString presentationPromptId() const override { return m_frame.prompt_id; }
    WalletCreationReceipt dismissPresentation() override { return terminal() ? acknowledge(m_frame.result.result_id) : cancel(); }
    QMetaObject::Connection observePresentation(QObject* context, std::function<void()> changed) override { return connect(this, &WalletOperationModel::changed, context, std::move(changed)); }

    Q_INVOKABLE WalletCreationReceipt cancel();
    Q_INVOKABLE WalletCreationReceipt retry();
    Q_INVOKABLE WalletCreationReceipt reconcile();
    Q_INVOKABLE WalletCreationReceipt acknowledge(const QString& result_id);
Q_SIGNALS:
    void changed();
    void promptsChanged();
    void finished(const QString& operation_id, const QString& result_id);
    void commandRejected(const QString& command_id, const WalletOperationError& error);
    void activated(WalletOperationModel* operation);
private:
    friend class WalletQmlControllerTests; // Captures an original outcome owner; never forges its state.
    friend class WalletCreationModel;
    friend class WalletQmlController;
    friend class WalletOperationPrompt;
    WalletOperationModel(Services services, walletcreation::Request request);
    WalletOperationModel(Services services, walletrestoration::Request request);
    explicit WalletOperationModel(Services services);
    WalletCreationReceipt answerPrompt(const QString& prompt, const QString& secret, bool passphrase);
    void start();
    std::shared_ptr<walletpublication::NativeObservation> nativeObservation() const { return m_ticket ? m_ticket->observation : nullptr; }
    void publish();
    WalletCreationReceipt reject(const char* command, const QString& message);
    Services m_services;
    OperationKind m_kind{Create};
    QString m_source, m_last_rejected;
    quint64 m_command_revision{0};
    walletcreation::Request m_request; // Credentials move to the Native owner before exposure.
    std::shared_ptr<walletcreation::OperationOwner> m_owner;
    walletcreation::Frame m_frame;
    std::shared_ptr<walletpublication::Ticket> m_ticket;
    QPointer<WalletQmlModel> m_result_wallet;
    std::vector<QPointer<WalletQmlModel>> m_result_wallets;
    QMetaObject::Connection m_available, m_destroyed;
    WalletOperationText m_errors{false, this}, m_warnings{true, this};
    WalletOperationPrompt* m_prompt{nullptr};
    WalletOperationPrompt* m_confirmation_prompt{nullptr};
    WalletOperationPrompt* m_passphrase_prompt{nullptr};
    std::vector<WalletOperationPrompt*> m_prompts;
    bool m_command_pending{false};
    bool m_closed{false}, m_accepted{false}, m_finished{false}, m_acknowledged{false}, m_publishing{false}, m_publication_in_flight{false}, m_reconciled{false};
};
#endif // BITCOIN_QML_MODELS_WALLETOPERATIONMODEL_H
