// Copyright (c) 2024 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_WALLETQMLMODEL_H
#define BITCOIN_QML_MODELS_WALLETQMLMODEL_H

#include <qml/models/backuppolicyoperation.h>
#include <qml/models/walletbackups.h>
#include <qml/models/feebumps.h>
#include <qml/models/fileexportoperation.h>
#include <qml/models/psbtfeeservices.h>
#include <qml/models/senddraftinterface.h>
#include <qml/models/transactionreviewinterface.h>
#include <qml/models/addresslistmodel.h>
#include <qml/models/texteditsessionmodel.h>
#include <qml/backendexecutor.h>
#include <qml/reviewowner.h>
#include <qml/walletopening.h>
#include <qml/senddraft.h>
#include <qml/models/walletsecuritymodel.h>
#include <qml/transactionattempt.h>
#include <qml/walletreceipts.h>
#include <qml/psbtwork.h>
#include <qml/models/feereadservices.h>
#include <qml/models/feeestimatevalues.h>
#include <qml/models/feeestimatereader.h>
#include <qml/models/feeselectionmodel.h>
#include <qml/models/feereadstate.h>
#include <qml/models/flowdetailsmodel.h>
#include <qml/models/bumptransactionmodel.h>
#include <qml/models/coinslistmodel.h>
#include <qml/models/paymentrequest.h>
#include <qml/models/imageexportoperation.h>
#include <qml/models/receiverequestoperation.h>
#include <qml/models/receiverequesthistorymodel.h>
#include <qml/models/sendrecipient.h>
#include <qml/models/sendrecipientslistmodel.h>
#include <qml/models/signverifymessagemodel.h>
#include <qml/models/transactionactivitymodel.h>
#include <qml/models/walletqmlmodeltransaction.h>
#include <qml/models/walletbalancereader.h>
#include <qml/models/walletreceivemodel.h>

#include <consensus/amount.h>
#include <interfaces/handler.h>
#include <interfaces/wallet.h>
#include <policy/feerate.h>
#include <psbt.h>
#include <support/allocators/secure.h>
#include <wallet/coincontrol.h>

#include <atomic>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QStringList>
#include <QThread>
#include <QTimer>
#include <QVariantList>

namespace interfaces {
class Node;
} // namespace interfaces

namespace walletsend { class Operation; class CommitConsent; }
namespace walletsigning { class ApprovalOwner; struct ApprovalSnapshot; }
namespace walletprepare { struct Request; struct Result; }

class PsbtQmlModel;
class TransactionReviewInterface;
class WalletSendOperation;
class WalletCloseQuestion;
class WalletTransactionReviewInterface;
class WalletSendDraftInterface;

class WalletQmlModel : public QObject, public ReviewOwner
{
    Q_OBJECT
    Q_INTERFACES(ReviewOwner)

public:
    enum class SendSubmission { SubmissionUnknown, SubmissionMempool, SubmissionConfirmed, SubmissionNotInMempool, SubmissionConflicted, SubmissionAbandoned, SubmissionAccepted };
    Q_ENUM(SendSubmission)
    enum class SendRecord { SendRecordUnknown, SendRecordAbsentAtObservation, SendRecordRecorded };
    Q_ENUM(SendRecord)
    enum class ApprovalState { ApprovalInitial, ApprovalWaiting, ApprovalSigned, ApprovalPartiallySigned, ApprovalFailed, ApprovalClosed, ApprovalOtherReview, ApprovalUncertain, ApprovalUnverified };
    Q_ENUM(ApprovalState)
    enum class PreparationState { PreparationIdle, PreparationPending, PreparationNeedsUnlock, PreparationReady, PreparationFailed, PreparationStale, PreparationClosed, PreparationUncertain };
    Q_ENUM(PreparationState)
    enum class BalanceState { Unknown, Ready, Busy, Unavailable, Failed, Rejected, Closed };
    Q_ENUM(BalanceState)
    enum class KeyScheme {
        SingleKey = 0,
        WatchOnly,
        MultiKey,
        ExternalSigner,
        Software,
        UnknownScheme,
    };
    Q_ENUM(KeyScheme)

    enum class SecurityState { SecurityIdle, SecurityPending, SecuritySucceeded, SecurityFailed, SecurityUncertain };
    Q_ENUM(SecurityState)

    enum class PsbtImportResult {
        PsbtUnsupported = 0,
        WalletCanSign,
        WalletCannotSign,
        TransactionAlreadyKnown,
        ReviewPending,
    };
    Q_ENUM(PsbtImportResult)
    enum class PsbtActionState { PsbtIdle, PsbtPending, PsbtImported, PsbtKnown, PsbtSaved, PsbtSubmitted, PsbtFailed, PsbtUncertain, PsbtStale, PsbtClosed };
    Q_ENUM(PsbtActionState)

private:
    Q_PROPERTY(bool recipientLabelsPending READ recipientLabelsPending NOTIFY recipientLabelsChanged)
    Q_PROPERTY(QString recipientLabelsError READ recipientLabelsError NOTIFY recipientLabelsChanged)
    Q_PROPERTY(QString recipientLabelsTransactionId READ recipientLabelsTransactionId NOTIFY recipientLabelsChanged)
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)
    Q_PROPERTY(QString name READ name NOTIFY nameChanged)
    Q_PROPERTY(QString rpcName READ rpcName NOTIFY openingMetadataChanged)
    Q_PROPERTY(QString displayName READ displayName NOTIFY displayNameChanged)
    Q_PROPERTY(QString backupFileName READ backupFileName NOTIFY nameChanged)
    Q_PROPERTY(QString balance READ balance NOTIFY balanceChanged)
    Q_PROPERTY(qint64 balanceSatoshi READ displayedBalanceSatoshi NOTIFY balanceChanged)
    Q_PROPERTY(bool balanceKnown READ balanceKnown NOTIFY balanceObservationChanged)
    Q_PROPERTY(BalanceState balanceState READ balanceState NOTIFY balanceObservationChanged)
    Q_PROPERTY(bool hasExternalSigner READ hasExternalSigner NOTIFY openingMetadataChanged)
    Q_PROPERTY(QVariantList receiveAddressTypes READ availableReceiveAddressTypes NOTIFY openingMetadataChanged)
    Q_PROPERTY(bool receivePreferencePending MEMBER m_receive_preference_pending NOTIFY receivePreferenceChanged)
    Q_PROPERTY(QString receivePreferenceError MEMBER m_receive_preference_error NOTIFY receivePreferenceChanged)
    Q_PROPERTY(bool capabilitiesKnown READ capabilitiesKnown NOTIFY openingMetadataChanged)
    //! The open "close this wallet?" question, or null.
    Q_PROPERTY(WalletCloseQuestion* closeQuestion READ closeQuestion NOTIFY closeQuestionChanged)
    Q_PROPERTY(TransactionActivityModel* transactionActivityModel READ transactionActivityModel CONSTANT)
    Q_PROPERTY(AddressListModel* addressListModel READ addressListModel CONSTANT)
    Q_PROPERTY(CoinsListModel* coinsListModel READ coinsListModel CONSTANT)
    Q_PROPERTY(SendRecipientsListModel* recipients READ sendRecipientList CONSTANT)
    Q_PROPERTY(quint64 importedReviewOwner READ importedReviewOwner NOTIFY currentTransactionChanged)
    Q_PROPERTY(quint64 importedReviewChild READ importedReviewChild NOTIFY currentTransactionChanged)
    Q_PROPERTY(QString importedReviewToken READ importedReviewToken NOTIFY currentTransactionChanged)
    Q_PROPERTY(SignVerifyMessageModel* signVerifyMessageModel READ signVerifyMessageModel CONSTANT)
    Q_PROPERTY(PaymentRequest* currentPaymentRequest READ currentPaymentRequest CONSTANT)
    Q_PROPERTY(PaymentRequest* receivingAddress READ receivingAddress CONSTANT)
    Q_PROPERTY(PaymentRequest* detailPaymentRequest READ detailPaymentRequest CONSTANT)
    Q_PROPERTY(ReceiveRequestHistoryModel* receiveRequests READ receiveRequests CONSTANT)
    Q_PROPERTY(bool receiveRequestReconciliationPending READ receiveRequestReconciliationPending NOTIFY receiveRequestReconciliationPendingChanged)
    Q_PROPERTY(QString receiveRequestReconciliationError READ receiveRequestReconciliationError NOTIFY receiveRequestReconciliationPendingChanged)
    Q_PROPERTY(bool receiveRequestsLoaded MEMBER m_request_storage_loaded NOTIFY receiveRequestStorageChanged)
    Q_PROPERTY(bool receiveRequestsBusy MEMBER m_request_storage_busy NOTIFY receiveRequestStorageChanged)
    Q_PROPERTY(QString receiveRequestStorageError MEMBER m_request_storage_error NOTIFY receiveRequestStorageChanged)
    Q_PROPERTY(bool receivingAddressReady MEMBER m_receiving_address_ready NOTIFY receivingAddressStateChanged)
    Q_PROPERTY(bool receivingAddressPending MEMBER m_receiving_address_pending NOTIFY receivingAddressStateChanged)
    Q_PROPERTY(WalletQmlModelTransaction* currentTransaction READ currentTransaction NOTIFY currentTransactionChanged)
    Q_PROPERTY(QVariantMap currentTransactionFlow READ currentTransactionFlowPreview NOTIFY currentTransactionFlowChanged)
    Q_PROPERTY(FlowDetailsModel* currentTransactionFlowDetails READ currentTransactionFlowDetails CONSTANT)
    Q_PROPERTY(quint64 currentTransactionFlowRevision READ currentTransactionFlowRevision NOTIFY currentTransactionFlowChanged)
    Q_PROPERTY(bool sendDraftSweepsWallet READ sendDraftSweepsWallet NOTIFY feeEstimateRevisionChanged)
    Q_PROPERTY(bool currentTransactionSweepsWallet READ currentTransactionSweepsWallet NOTIFY currentTransactionChanged)
    Q_PROPERTY(bool currentTransactionIsImportedPsbt READ currentTransactionIsImportedPsbt NOTIFY currentTransactionChanged)
    Q_PROPERTY(unsigned int targetBlocks READ feeTargetBlocks WRITE setFeeTargetBlocks NOTIFY feeTargetBlocksChanged)
    Q_PROPERTY(qint64 estimatedFeeSatoshi READ estimatedFeeSatoshi NOTIFY estimatedFeeChanged)
    Q_PROPERTY(QString estimatedFeeRate READ estimatedFeeRate NOTIFY estimatedFeeChanged)
    Q_PROPERTY(int estimatedInputCount READ estimatedInputCount NOTIFY estimatedFeeChanged)
    Q_PROPERTY(qint64 sendTotalSatoshi READ sendTotalSatoshi NOTIFY sendAmountExhaustsBalanceChanged)
    //! The amount coin selection must cover: the send total, or the last one
    //! with a ready fee while the fee is estimated again.
    Q_PROPERTY(qint64 coinSelectionTargetSatoshi READ coinSelectionTargetSatoshi NOTIFY coinSelectionTargetChanged)
    Q_PROPERTY(qint64 availableSendBalanceSatoshi READ availableSendBalanceSatoshi NOTIFY sendFactsChanged)
    Q_PROPERTY(bool availableSendBalanceKnown READ availableSendBalanceKnown NOTIFY sendFactsChanged)
    Q_PROPERTY(SendRecipient* maximumRecipient READ maximumRecipient NOTIFY maximumRecipientChanged)
    Q_PROPERTY(QString estimatedFee READ estimatedFee NOTIFY estimatedFeeChanged)
    Q_PROPERTY(bool sendAmountExhaustsBalance READ sendAmountExhaustsBalance NOTIFY sendAmountExhaustsBalanceChanged)
    Q_PROPERTY(FeeSelectionModel* fees READ fees CONSTANT)
    Q_PROPERTY(bool coinSelectionPending READ coinSelectionPending NOTIFY coinSelectionChanged)
    Q_PROPERTY(bool coinSelectionBlocked READ coinSelectionBlocked NOTIFY coinSelectionChanged)
    Q_PROPERTY(bool customFeeEnabled READ customFeeEnabled WRITE setCustomFeeEnabled NOTIFY customFeeEnabledChanged)
    Q_PROPERTY(QString customFeeRate READ customFeeRate WRITE setCustomFeeRate NOTIFY customFeeRateChanged)
    Q_PROPERTY(bool customFeeRateValid READ customFeeRateValid NOTIFY customFeeRateValidChanged)
    Q_PROPERTY(double minimumCustomFeeRate READ minimumCustomFeeRate NOTIFY sendFactsChanged)
    Q_PROPERTY(bool customFeeTargetPending READ customFeeTargetPending NOTIFY customFeeTargetPendingChanged)
    Q_PROPERTY(bool feeEstimatePending READ feeEstimatePending NOTIFY feeEstimatePendingChanged)
    Q_PROPERTY(int feeEstimateRevision READ feeEstimateRevision NOTIFY feeEstimateRevisionChanged)
    //! This wallet's fee bumps; a page bumps a transaction through its FeeBumpController.
    Q_PROPERTY(FeeBumpInterface* feeBumps READ feeBumps CONSTANT)
    Q_PROPERTY(int displayUnit READ displayUnit WRITE setDisplayUnit NOTIFY displayUnitChanged)
    Q_PROPERTY(bool isEncrypted READ isEncrypted NOTIFY securityStateChanged)
    Q_PROPERTY(bool isLocked READ isLocked NOTIFY securityStateChanged)
    Q_PROPERTY(QString keyScheme READ keyScheme NOTIFY openingMetadataChanged)
    Q_PROPERTY(WalletQmlModel::KeyScheme keySchemeKind READ keySchemeKind NOTIFY openingMetadataChanged)
    Q_PROPERTY(QString privateKeysStatus READ privateKeysStatus NOTIFY openingMetadataChanged)
    Q_PROPERTY(QString externalSignerStatus READ externalSignerStatus NOTIFY openingMetadataChanged)
    Q_PROPERTY(bool canManagePassphrase READ canManagePassphrase NOTIFY openingMetadataChanged)
    Q_PROPERTY(QString transactionError READ transactionError NOTIFY transactionErrorChanged)
    Q_PROPERTY(PsbtActionState psbtActionState READ psbtActionState NOTIFY psbtActionChanged)
    Q_PROPERTY(quint64 psbtActionId READ psbtActionId NOTIFY psbtActionChanged)
    Q_PROPERTY(bool psbtActionBusy READ psbtActionBusy NOTIFY psbtActionChanged)
    Q_PROPERTY(PsbtImportResult psbtImportResult READ psbtImportResult NOTIFY psbtActionChanged)
    Q_PROPERTY(QString psbtActionError READ psbtActionError NOTIFY psbtActionChanged)
    Q_PROPERTY(QString psbtSavedPath READ psbtSavedPath NOTIFY psbtActionChanged)
    Q_PROPERTY(QString psbtSubmittedTxid READ psbtSubmittedTxid NOTIFY psbtActionChanged)
    Q_PROPERTY(FileSaveOperation* fileExportOperation READ fileExportOperation NOTIFY fileExportOperationChanged)
    Q_PROPERTY(PreparationState preparationState READ preparationState NOTIFY preparationChanged)
    Q_PROPERTY(quint64 preparationId READ preparationId NOTIFY preparationChanged)
    Q_PROPERTY(bool preparationBusy READ preparationBusy NOTIFY preparationChanged)
    Q_PROPERTY(ApprovalState signerApprovalState READ signerApprovalState NOTIFY signerApprovalChanged)
    Q_PROPERTY(QString signerUnverifiedResultToken READ signerUnverifiedResultToken NOTIFY unverifiedSigningResultChanged)
    Q_PROPERTY(quint64 signerApprovalId READ signerApprovalId NOTIFY signerApprovalChanged)
    Q_PROPERTY(QString signerApprovalError READ signerApprovalError NOTIFY signerApprovalChanged)
    Q_PROPERTY(bool signerApprovalSignerNotFound READ signerApprovalSignerNotFound NOTIFY signerApprovalChanged)
    Q_PROPERTY(bool sendBusy READ sendBusy NOTIFY sendOperationChanged)
    //! Sends and signs the transaction under review for a TransactionReviewController.
    Q_PROPERTY(TransactionReviewInterface* transactionReview READ transactionReview CONSTANT)
    //! Prepares and imports reviews for a SendDraftController.
    Q_PROPERTY(SendDraftInterface* sendDraft READ sendDraft CONSTANT)
    Q_PROPERTY(bool transactionNeedsUnlock READ transactionNeedsUnlock NOTIFY transactionNeedsUnlockChanged)
    Q_PROPERTY(bool currentTransactionCanSend READ currentTransactionCanSend NOTIFY currentTransactionChanged)
    Q_PROPERTY(bool currentTransactionCanSignOnly READ currentTransactionCanSignOnly NOTIFY currentTransactionChanged)
    Q_PROPERTY(bool currentTransactionNeedsRevalidation READ currentTransactionNeedsRevalidation NOTIFY preparationChanged)
    Q_PROPERTY(bool transactionFeePending READ transactionFeePending NOTIFY currentTransactionChanged)
    Q_PROPERTY(bool currentTransactionCanBroadcast READ currentTransactionCanBroadcast NOTIFY currentTransactionChanged)
    Q_PROPERTY(QString currentTransactionReviewMessage READ currentTransactionReviewMessage NOTIFY currentTransactionChanged)
    //! This wallet's backups; a page starts one through its WalletBackupController.
    Q_PROPERTY(WalletBackupInterface* backups READ backups CONSTANT)
    Q_PROPERTY(QString settingsError READ settingsError NOTIFY settingsErrorChanged)
    Q_PROPERTY(WalletSecurityModel* security READ security CONSTANT)
    Q_PROPERTY(WalletReceiveModel* receive READ receive CONSTANT)
    Q_PROPERTY(SecurityState securityState READ securityState NOTIFY securityOperationChanged)
    Q_PROPERTY(bool securityBusy READ securityBusy NOTIFY securityOperationChanged)
    Q_PROPERTY(QString securityError READ securityError NOTIFY securityOperationChanged)
    Q_PROPERTY(PsbtQmlModel* importedPsbt READ importedPsbt CONSTANT)

public:
    WalletQmlModel(std::unique_ptr<interfaces::Wallet> wallet, interfaces::Node* node = nullptr, QObject* parent = nullptr);
    WalletQmlModel(std::unique_ptr<interfaces::Wallet> wallet, interfaces::Node* node,
                   FeeReadServices fee_services, QObject* parent = nullptr);
    WalletQmlModel(std::unique_ptr<interfaces::Wallet> wallet, interfaces::Node* node,
                   FeeReadServices fee_services, psbtfee::Services proof_services, QObject* parent = nullptr);
    WalletQmlModel(std::unique_ptr<interfaces::Wallet> wallet, interfaces::Node* node,
                   FeeReadServices fee_services, psbtfee::Services proof_services, backup::Services backup_services,
                   QObject* parent = nullptr);
    struct SharedWallet {
        std::shared_ptr<interfaces::Wallet> wallet;
        // Production composition shares the controller's bounded R08 lanes.
        std::shared_ptr<execution::Scheduler> observations{};
        QString captured_name{};
        walletopening::Session::Preferences preferences{};
        walletopening::FeePolicy fee_policy{};
        std::shared_ptr<std::atomic_bool> shutdown_gate{};
        walletopening::NativeEvents native_events{};
        bool canonical_identity{false};
    };
    WalletQmlModel(SharedWallet wallet, interfaces::Node* node,
                   FeeReadServices fee_services, psbtfee::Services proof_services, backup::Services backup_services,
                   QObject* parent = nullptr);
    WalletQmlModel(interfaces::Node* node, QObject* parent = nullptr);
    WalletQmlModel(QObject *parent = nullptr);
    ~WalletQmlModel();

    QString name() const;
    QString rpcName() const { return m_rpc_name; }
    QString displayName() const;
    QString backupFileName() const;
    void setDisplayName(const QString& display_name);
    QString balance() const;
    qint64 displayedBalanceSatoshi() const { return m_wallet ? m_balance : 0; }
    // Native balance reads are background-only; all facade readers use cache.
    qint64 balanceSatoshi() const;
    bool balanceKnown() const { return m_balance_known; }
    BalanceState balanceState() const { return m_balance_state; }
    void refreshBalance();
    bool hasExternalSigner() const { return m_metadata_known && m_external_signer; }
    bool capabilitiesKnown() const { return m_metadata_known; }
    WalletCloseQuestion* closeQuestion() const { return m_close_prompt; }
    Q_INVOKABLE void requestClose();
    std::shared_ptr<walletopening::Session> opening() const { return m_opening; }
    std::shared_ptr<const paymentimport::TargetPort> importTarget(SendRecipient* recipient);
    quint64 importedReviewOwner() const { return m_imported_review_owner; }
    quint64 importedReviewChild() const { return m_imported_review_child; }
    QString importedReviewToken() const { return m_imported_review_owner && m_imported_review_child
        ? QString::number(m_imported_review_owner) + QLatin1Char(':') + QString::number(m_imported_review_child) : QString{}; }
    WalletReceiveModel* receive() const { return m_receive; }
    Q_INVOKABLE ReceiveRequestOperation* commitPaymentRequest();
    Q_INVOKABLE ReceiveRequestOperation* commitPaymentRequestWithPassphrase(const QString& passphrase);
    Q_INVOKABLE void reloadReceiveRequests();
    Q_INVOKABLE ReceiveRequestOperation* removeReceiveRequest(const QString& request_id);
    Q_INVOKABLE bool loadPaymentRequest(const QString& request_id);
    Q_INVOKABLE bool loadPaymentRequestDetail(const QString& request_id);
    Q_INVOKABLE ReceiveRequestOperation* updatePaymentRequest(const QString& request_id, qint64 amount, const QString& label, const QString& message, const QString& note);
    Q_INVOKABLE void usePaymentRequestAsTemplate(const QString& request_id);

    TransactionActivityModel* transactionActivityModel();
    AddressListModel* addressListModel() const { return m_address_list_model; }
    FeeBumps* feeBumps() const { return m_fee_bumps; }
    CoinsListModel* coinsListModel() const { return m_coins_list_model; }
    SendRecipientsListModel* sendRecipientList() const { return m_send_recipients; }
    SignVerifyMessageModel* signVerifyMessageModel() const { return m_sign_verify_message_model; }
    PaymentRequest* currentPaymentRequest() const { return m_current_payment_request; }
    PaymentRequest* receivingAddress() const { return m_receiving_address; }
    Q_INVOKABLE ReceiveRequestOperation* ensureReceivingAddress(bool next = false, const QString& address_type = {});
    Q_INVOKABLE ReceiveRequestOperation* resumeReceivingAddress(ReceiveRequestOperation* original, const QString& passphrase);
    Q_INVOKABLE ReceiveRequestOperation* retryReceivingAddress(ReceiveRequestOperation* original);
    Q_INVOKABLE ReceiveRequestOperation* commitReceivingPaymentRequest();
    PaymentRequest* detailPaymentRequest() const { return m_detail_payment_request; }
    ReceiveRequestHistoryModel* receiveRequests() const { return m_receive_requests; }
    bool receiveRequestReconciliationPending() const;
    QString receiveRequestReconciliationError() const { return m_receipt_projection ? m_receipt_projection->errorText() : QString{}; }
    WalletQmlModelTransaction* currentTransaction() const { return m_current_transaction; }
    QVariantMap currentTransactionFlow() const;
    QVariantMap currentTransactionFlowPreview() const;
    FlowDetailsModel* currentTransactionFlowDetails() const { return m_flow_details; }
    quint64 currentTransactionFlowRevision() const { return m_flow_revision; }
    bool sendDraftSweepsWallet() const;
    bool currentTransactionSweepsWallet() const { return m_current_transaction && m_current_transaction->source() == CurrentTransactionSource::SendDraft && m_current_transaction->sweepsWallet(); }
    bool currentTransactionIsImportedPsbt() const { return m_current_transaction && m_current_transaction->source() == CurrentTransactionSource::ImportedPsbt; }
    QString estimatedFee() const;
    qint64 estimatedFeeSatoshi() const;
    QString estimatedFeeRate() const;
    int estimatedInputCount() const;
    qint64 sendTotalSatoshi() const;
    qint64 coinSelectionTargetSatoshi() const;
    qint64 availableSendBalanceSatoshi() const;
    bool availableSendBalanceKnown() const { return m_send_facts_known; }
    SendRecipient* maximumRecipient() const { return m_maximum_recipient.data(); }
    Q_INVOKABLE void useMaximum();
    Q_INVOKABLE void useMaximumFor(SendRecipient* recipient);
    Q_INVOKABLE void setCustomFeeTarget(unsigned int target);
    CFeeRate dustRelayFee() const;
    bool sendAmountExhaustsBalance() const;
    FeeSelectionModel* fees() const { return m_fees; }
    bool customFeeEnabled() const { return m_fees && m_fees->mode() == FeeSelectionModel::Manual; }
    QString customFeeRate() const { return m_fees ? m_fees->customRate()->text() : QString{}; }
    bool customFeeRateValid() const;
    double minimumCustomFeeRate() const;
    bool customFeeTargetPending() const { return m_fees && m_fees->policyPending(); }
    bool feeEstimatePending() const { return m_fees && m_fees->estimatePending(); }
    int feeEstimateRevision() const { return m_fee_estimate_revision; }
    Q_INVOKABLE quint64 requestPrepareTransaction();
    Q_INVOKABLE quint64 approveExternalSignerTransaction();
    Q_INVOKABLE quint64 requestPrepareTransactionWithPassphrase(const QString& passphrase);
    quint64 preparationId() const;
    PreparationState preparationState() const;
    bool preparationBusy() const;
    TransactionReviewInterface* transactionReview() const;
    SendDraftInterface* sendDraft() const;
    //! The wallet's latest send work.
    std::shared_ptr<walletsend::Operation> sendOperation() const { return m_send_operation; }
    quint64 requestBroadcastCurrentTransaction();
    quint64 requestRecheckCurrentTransaction();
    Q_INVOKABLE QVariantList availableReceiveAddressTypes() const;
    Q_INVOKABLE QString defaultReceiveAddressType() const;
    Q_INVOKABLE QString estimatedFeeForTarget(unsigned int target_blocks) const;
    Q_INVOKABLE int feeTargetIndex(unsigned int target_blocks) const;
    Q_INVOKABLE void scheduleFeeEstimates();
    Q_INVOKABLE ReceiveRequestOperation* setCurrentPaymentRequestAddress(const QString& address);
    WalletSecurityModel* security() const { return m_security; }
    SecurityState securityState() const;
    bool securityBusy() const;
    QString securityError() const;
    Q_INVOKABLE bool encryptWallet(const QString& passphrase);
    Q_INVOKABLE bool changeWalletPassphrase(const QString& old_passphrase, const QString& new_passphrase);
    WalletBackups* backups() const { return m_backups; }
    Q_INVOKABLE ImageExportOperation* savePaymentRequestImage(PaymentRequest* request, const QImage& image, const QUrl& path);
    FileExportOperationModel* fileExportOperation() const { return m_file_export_operation; }
    Q_INVOKABLE void clearSettingsError();
    Q_INVOKABLE bool setDefaultReceiveAddressType(const QString& address_type);
    Q_INVOKABLE QString receiveAddressTypeLabel(const QString& address_type) const;
    Q_INVOKABLE quint64 requestImportPsbtFromFile(const QString& path);
    std::shared_ptr<backup::InstallCoordinator> fileInstallCoordinator() const { return m_backup_services.coordinator; }
    quint64 requestSaveCurrentTransactionAsPsbt(const QString& path);
    //! Ends the save that PSBT action \p action_id started, unless it is writing.
    void retirePsbtFileExport(quint64 action_id);
    quint64 requestSaveUnverifiedSignerResult(const QString& token, const QString& path);
    PsbtActionState psbtActionState() const { return m_psbt_action_state; }
    quint64 psbtActionId() const { return m_psbt_action_id; }
    bool psbtActionBusy() const { return m_psbt_action_state == PsbtActionState::PsbtPending; }
    PsbtImportResult psbtImportResult() const { return m_psbt_import_result; }
    QString psbtActionError() const { return m_psbt_action_error; }
    QString psbtSavedPath() const;
    QString psbtSubmittedTxid() const;
    Q_INVOKABLE void discardCurrentTransaction();
    PsbtQmlModel* importedPsbt() const { return m_imported_psbt_model; }
    interfaces::Wallet* wallet() const { return m_wallet.get(); }
    interfaces::Node* node() const { return m_node; }
    std::shared_ptr<BackendExecutor> backendExecutor() const { return m_backend_executor; }
    // False once the wallet is unloading or closing. Set from the unload
    // notification's own thread, before the GUI hears about it.
    bool shutdownComplete() const { return m_shutdown_finished; }
    bool observationsAllowed() const { return !m_stopping && !m_closed; }
    bool available() const { return m_wallet && !m_closed && !m_unload_requested->load() && (!m_opening_native_events.unloaded || !m_opening_native_events.unloaded->load()); }
    ApprovalState signerApprovalState() const;
    QString signerUnverifiedResultToken() const;
    bool signerApprovalBusy() const;
    quint64 signerApprovalId() const { return m_approval_id; }
    QString signerApprovalError() const;
    bool signerApprovalSignerNotFound() const;
    void close();
    void registerReview(WalletQmlModelTransaction* review) override;
    void setReviewRetirementPostForTesting(std::function<void(std::function<void()>)> post);
    void beginShutdown(bool remove_wallet = false);
    void removeWallet();

    ReceiveRequestOperation* setAddressLabel(const QString& address, const QString& label);
    TextEditSessionModel* editAddressLabel(const QString& address, QObject* parent);
    // Write a label to the address book only, without touching any payment
    // request saved for the address. This is the request-save direction: a
    // request's label is its own, so saving one request must not rewrite the
    // labels of sibling requests on the same address.
    // Propagate an edited address book label to any payment request saved for
    // that address (the reverse of the request-save address book sync).
    void selectCoin(const COutPoint& output);
    void unselectCoin(const COutPoint& output);
    bool isSelectedCoin(const COutPoint& output);
    std::shared_ptr<const CoinsListModel::Selection> selectedCoins() const;
    bool coinSelectionPending() const;
    bool coinSelectionBlocked() const;
    Q_INVOKABLE void clearSelectedCoins();
    unsigned int feeTargetBlocks() const;
    void setFeeTargetBlocks(unsigned int target_blocks);
    void setCustomFeeEnabled(bool enabled);
    void setCustomFeeRate(const QString& fee_rate);

    int displayUnit() const { return m_display_unit; }
    void setDisplayUnit(int unit);
    bool isEncrypted() const { return m_security->isEncrypted(); }
    bool isLocked() const { return m_security->isLocked(); }
    QString keyScheme() const;
    KeyScheme keySchemeKind() const;
    static KeyScheme keySchemeForWallet(interfaces::Wallet& wallet);
    static QString keySchemeDisplayText(KeyScheme scheme);
    QString privateKeysStatus() const;
    QString externalSignerStatus() const;
    bool canManagePassphrase() const;
    bool recipientLabelsPending() const { return m_sent_labels_pending; }
    QString recipientLabelsError() const { return m_sent_labels_error; }
    QString recipientLabelsTransactionId() const { return m_sent_labels_txid; }
    //! A send is submitting or waiting for sweep consent; other actions wait.
    bool sendBusy() const { return m_send_running; }
    QString transactionError() const { return m_transaction_error; }
    bool transactionNeedsUnlock() const { return m_transaction_needs_unlock; }
    bool currentTransactionCanSend() const { return m_current_transaction && m_current_transaction->canSend(); }
    bool currentTransactionCanSignOnly() const
    {
        return available() && m_current_transaction && m_current_transaction->psbt() && m_current_transaction->canSend() && m_current_transaction->keyCanSend()
            && m_metadata_known && !m_private_keys_disabled && !currentTransactionNeedsRevalidation();
    }
    bool currentTransactionNeedsRevalidation() const;
    bool transactionFeePending() const { return m_psbt_fee_pending; }
    bool currentTransactionCanBroadcast() const { return m_current_transaction && m_current_transaction->canBroadcast(); }
    QString currentTransactionReviewMessage() const { return m_current_transaction ? m_current_transaction->reviewMessage() : QString{}; }
    QString settingsError() const { return m_settings_error; }
    void setNode(interfaces::Node* node);

Q_SIGNALS:
    void balanceObservationChanged();
    // Backend content invalidation is separate from cached presentation data.
    void walletContentsChanged();
    void shutdownFinished();
    void nameChanged();
    void displayNameChanged();
    void balanceChanged();
    void securityOperationChanged();
    void openingMetadataChanged();
    void receivePreferenceChanged();
    void closeQuestionChanged();
    void closeConfirmed();
    void currentTransactionChanged();
    //! A committed send cleared the draft it was reviewed from.
    void sentDraftCleared();
    void currentTransactionFlowChanged();
    void feeTargetBlocksChanged();
    void estimatedFeeChanged();
    void sendAmountExhaustsBalanceChanged();
    void customFeeEnabledChanged();
    void customFeeRateChanged();
    void customFeeRateValidChanged();
    void feeEstimatePendingChanged();
    void coinSelectionTargetChanged();
    void feeEstimateRevisionChanged();
    void maximumRecipientChanged();
    void receiveRequestReconciliationPendingChanged();
    void receiveRequestStorageChanged();
    void receivingAddressStateChanged();
    void externalSignerApprovalSucceeded();
    void signerApprovalChanged();
    void unverifiedSigningResultChanged();
    void preparationChanged();
    void coinSelectionChanged();
    void psbtActionChanged();
    void sendFactsChanged();
    void customFeeTargetPendingChanged();
    void externalSignerApprovalPartiallySucceeded();
    void externalSignerApprovalFailed(const QString& message, bool signerNotFound);
    void displayUnitChanged(int unit);
    void securityStateChanged();
    void recipientLabelsChanged();
    void transactionErrorChanged();
    void sendOperationChanged();
    void transactionNeedsUnlockChanged();
    void availableChanged();
    void walletUnloaded();
    void fileExportOperationChanged();
    void settingsErrorChanged();
    void addressListChanged();
    void addressInventoryChanged();
    void addressLabelsChanged();
    // Forwarded on the GUI thread; models must not read wallet state from the
    // Core callback while it holds the wallet lock.
    void transactionChanged(const QString& txid, int change);

private:
    using CurrentTransactionSource = WalletQmlModelTransaction::Source;

    void initializeBackend();
    void initializeBalanceReader();
    void checkShutdownFinished();
    void publishBalance();
    void initializeFeeEstimator();
    void initializePreparationOwner();
    void publishPreparationProjection(transactionattempt::Snapshot snapshot);
    void setMaximumRecipient(SendRecipient* recipient);
    void projectMaximumRecipient();
    void requestFeeEstimatesNow();
    void invalidateFeeRead(bool close = false, bool scheduled = false);
    void publishFeeEstimates();
    std::optional<CAmount> selectedFeeEstimate() const;
    std::optional<SendFeePreview> selectedFeePreview() const;
    void clearFeeEstimates();
    void refreshSecurityState();
    quint64 requestPrepareTransactionInternal(std::optional<QString> passphrase = {});
    void startPreparation(transactionattempt::Result queued, std::shared_ptr<walletprepare::Request> request,
                          std::optional<QString> passphrase, std::shared_ptr<walletopening::Session> opening,
                          QString draft_error);
    void applyPreparationResolution(transactionattempt::Result resolution, walletprepare::Result effect,
                                    transactionattempt::Ticket ticket, transactionattempt::CurrentFacts original_facts,
                                    std::shared_ptr<walletopening::Session> opening,
                                    std::shared_ptr<execution::OwnedCommand<walletprepare::Result>> command);
    void publishPreparation();
    void invalidatePreparation();
    bool setCurrentTransaction(WalletQmlModelTransaction* transaction, bool from_preparation = false);
    transactionattempt::CurrentFacts preparationFacts() const;
    ReceiveRequestOperation* savePaymentRequest(PaymentRequest* request, bool consume_address = false);
    ReceiveRequestOperation* submitRequest(walletrequests::Command command, PaymentRequest* draft = nullptr, bool consume_address = false);
    void publishRequestHistory();
    bool applyRequest(PaymentRequest* target, walletrequests::PreparedRequest data, bool as_template = false);
    void refreshReceiveRequestPayments();
    CAmount receivedPaymentRequestAmount(const QString& address) const;
    void updateReceivedPaymentRequestAmounts();
    friend class WalletSendOperation;
    void setSendRunning(bool running);
    //! Applies a finished send's effects on the wallet and its review, then
    //! tells the operation, if it still exists.
    //! Empties the send draft and its manual coin selection after a commit.
    void clearSentDraft();
    void finishSendWork(const std::shared_ptr<walletsend::Operation>& work, const QPointer<WalletQmlModelTransaction>& review,
                        bool sign_only, const QPointer<WalletSendOperation>& operation);
    //! Answers a prompt of send work whose operation is gone.
    void declineSendPrompt(const std::shared_ptr<walletsend::Operation>& work);
    mutable WalletTransactionReviewInterface* m_transaction_review{nullptr};
    mutable WalletSendDraftInterface* m_send_draft{nullptr};
    bool m_send_running{false};
    std::shared_ptr<walletsend::Operation> m_send_operation;
    std::shared_ptr<walletsend::CommitConsent> m_send_consent;
    QPointer<WalletQmlModelTransaction> m_send_review;
    bool m_send_sign_only{false};
    struct SentLabelCapture {
        std::shared_ptr<walletrequests::Store> store;
        std::shared_ptr<const walletopening::Session::Action> action;
        std::shared_ptr<QMap<QString, QString>> labels;
        std::optional<execution::ResourceLease> label_lifetime;
        bool valid{false};
        std::shared_ptr<QObject> bridge;
        QPointer<WalletQmlModel> observer;
        std::shared_ptr<const int> ticket;
    };
    SentLabelCapture captureSentRecipientLabels(const std::shared_ptr<const walletopening::Session::Action>& action);
    void publishSentRecipientLabels();
    std::shared_ptr<walletrequests::Operation> m_sent_labels_operation;
    std::weak_ptr<walletsend::Operation> m_sent_labels_work;
    bool m_sent_labels_pending{false};
    QString m_sent_labels_error, m_sent_labels_txid;
    const std::shared_ptr<std::atomic_bool> m_sending_commit{std::make_shared<std::atomic_bool>(false)};
    const std::shared_ptr<std::atomic<quint64>> m_approval_running{std::make_shared<std::atomic<quint64>>(0)};
    quint64 m_send_request_id{0};
    quint64 m_send_review_id{0};
    QString m_send_draft_revision;
    void clearTransactionStatus();
    void setTransactionStatus(const QString& error, bool needs_unlock = false);
    void setSettingsError(const QString& error);
    enum class PsbtActionKind { Import, Recheck, Save, Broadcast };
    struct PsbtReceipt { std::mutex mutex; QString path, txid; bool saved{false}, submitted{false}; };
    struct PsbtActionValue {
        std::shared_ptr<const void> lifetime;
        psbtwork::ImportResult imported;
        psbtwork::Result effect;
        std::shared_ptr<const PartiallySignedTransaction> export_document;
        QString error;
    };
    using PsbtAction = execution::OwnedCommand<PsbtActionValue>;
    quint64 startPsbtAction(PsbtActionKind kind, const QString& path = {},
                            std::shared_ptr<const PartiallySignedTransaction> export_document = {});
    std::shared_ptr<FileExportRequest> preparePsbtFileExport(quint64 action_id, const QString& path,
                                                             const std::shared_ptr<PsbtReceipt>& receipt);
    void startPsbtFileExport(quint64 action_id, const QString& path,
                             std::shared_ptr<const PartiallySignedTransaction> document,
                             const std::shared_ptr<PsbtReceipt>& receipt);
    void publishPsbtAction();
    std::shared_ptr<PsbtAction> m_psbt_action;
    std::shared_ptr<PsbtReceipt> m_psbt_receipt;
    std::optional<execution::ResourceLease> m_psbt_action_lease, m_psbt_document_lease, m_preparation_lease;
    PsbtActionState m_psbt_action_state{PsbtActionState::PsbtIdle};
    PsbtActionKind m_psbt_action_kind{PsbtActionKind::Import};
    PsbtImportResult m_psbt_import_result{PsbtImportResult::PsbtUnsupported};
    quint64 m_psbt_action_id{0};
    quint64 m_psbt_file_export_action_id{0};
    quint64 m_psbt_view_epoch{0}, m_psbt_action_view_epoch{0};
    std::shared_ptr<std::atomic_bool> m_import_review_current{std::make_shared<std::atomic_bool>(true)};
    void invalidateImportReview();
    quint64 m_imported_review_owner{0}, m_imported_review_child{0};
    std::weak_ptr<const PartiallySignedTransaction> m_psbt_action_document;
    bool m_psbt_action_had_document{false};
    QPointer<WalletQmlModelTransaction> m_psbt_action_review;
    bool m_psbt_action_had_review{false};
    QString m_psbt_action_error;
    QString m_psbt_action_path;


    bool startSecurity(walletsecurity::Kind kind, const QString& current, const QString& replacement);
    WalletSecurityModel* const m_security{new WalletSecurityModel(this)};
    void initializeOpening();
    void publishOpening();
    struct ReviewRetirement;
    std::shared_ptr<ReviewRetirement> m_review_retirement;
    bool m_reviews_drained{true};
    std::shared_ptr<walletopening::Session> m_opening;
    std::optional<qint64> m_ready_send_total;
    qint64 m_coin_selection_target{0};
    void refreshCoinSelectionTarget();
    walletopening::ClosedFlag m_closed;
    walletopening::Session::Preferences m_opening_preferences;
    walletopening::NativeEvents m_opening_native_events;
    walletopening::FeePolicy m_fee_policy;
    std::optional<walletopening::Question> m_close_question;
    //! Presents m_close_question; replaced with each new question.
    WalletCloseQuestion* m_close_prompt{nullptr};
    friend class WalletCloseQuestion;
    void answerCloseQuestion(const WalletCloseQuestion* question, bool accept);
    bool m_close_confirmed{false}, m_available_published{false};
    QString m_name, m_rpc_name;
    bool m_canonical_identity{false};
    bool m_can_get_addresses{false}, m_taproot{false}, m_receive_preference_pending{false};
    QSet<QString> m_proven_receive_types;
    QString m_receive_preference, m_receive_preference_error;
    bool m_metadata_known{false}, m_private_keys_disabled{false}, m_external_signer{false}, m_opening_drained{true};
    std::shared_ptr<BackendExecutor> m_backend_executor{std::make_shared<BackendExecutor>(execution::Options{})};
    std::shared_ptr<QObject> m_notification_bridge;
    std::shared_ptr<execution::Scheduler> m_balance_scheduler;
    std::shared_ptr<walletbalance::Reader> m_balance_reader;
    QTimer m_balance_timer;
    qint64 m_balance{0};
    bool m_balance_known{false};
    BalanceState m_balance_state{BalanceState::Unavailable};
    bool m_stopping{false};
    bool m_backend_drained{false}, m_balance_drained{true}, m_shutdown_finished{false};
    std::shared_ptr<interfaces::Wallet> m_wallet;
    interfaces::Node* m_node{nullptr};
    TransactionActivityModel* m_transaction_activity_model{nullptr};
    AddressListModel* m_address_list_model{nullptr};
    FeeBumps* m_fee_bumps{nullptr};
    void createFeeBumps();
    CoinsListModel* m_coins_list_model{nullptr};
    SendRecipientsListModel* m_send_recipients{nullptr};
    SignVerifyMessageModel* m_sign_verify_message_model{nullptr};
    PaymentRequest* m_current_payment_request{nullptr};
    PaymentRequest* m_receiving_address{nullptr};
    PaymentRequest* m_detail_payment_request{nullptr};
    ReceiveRequestHistoryModel* m_receive_requests{nullptr};
    std::shared_ptr<walletreceipts::Reader> m_receipts;
    std::shared_ptr<walletrequests::Store> m_request_store;
    WalletReceiveModel* m_receive{nullptr};
    bool m_request_storage_loaded{false}, m_request_storage_busy{false};
    QString m_request_storage_error;
    bool m_receiving_address_ready{false}, m_receiving_address_pending{false};
    std::shared_ptr<const walletrequests::AddressIntent> m_receiving_intent;
    ReceiptReconciliationModel* m_receipt_projection{nullptr};
    void publishReceipts();
    WalletQmlModelTransaction* m_current_transaction{nullptr};
    // Send draft intent when an imported review opened; a newer draft survives its close (#762).
    std::optional<quint64> m_import_draft_revision;
    wallet::CCoinControl m_coin_control;
    QPointer<SendRecipient> m_maximum_recipient;
    QMetaObject::Connection m_maximum_recipient_destroyed_connection;
    bool m_updating_maximum{false};
    struct AutomaticMaximum {
        QPointer<SendRecipient> recipient;
        quint64 token{0}, revision{0}, fee_request{0};
        CAmount amount{0};
        int count{0};
        quint8 capabilities{0};
    };
    std::optional<AutomaticMaximum> m_automatic_maximum;
    void publishAutomaticMaximum(CAmount amount);
    void scheduleFeesForRecipientValidation();
    quint8 automaticMaximumCapabilities() const;
    void invalidateAutomaticMaximum();
    void initializePsbtFeeReader();
    bool requestPsbtFeeProof();
    void applyPsbtFeeProof(const std::shared_ptr<const psbtfee::RequestIdentity>& token, psbtfee::Resolution result, bool publish = true);
    backup::Services m_backup_services;
    WalletBackups* m_backups{nullptr};
    BackupFileOperation* startBackup(const QString& path, QString& why, std::function<void(backup::Snapshot)> outcome);
    void createBackups();
    QPointer<FileExportOperationModel> m_file_export_operation;
    psbtfee::Services m_psbt_fee_services;
    bool m_custom_psbt_lookup{false};
    std::shared_ptr<const psbtfee::RequestIdentity> m_psbt_fee_request;
    bool m_psbt_fee_pending{false};
    quint64 m_approval_id{0};
    ApprovalState m_approval_state{ApprovalState::ApprovalInitial};
    QString m_approval_error;
    bool m_approval_signer_not_found{false};
    bool m_approval_local_pending{false};
    bool m_approval_command_admitted{false};
    std::shared_ptr<walletsigning::ApprovalOwner> m_signer_approval_operation;
    std::shared_ptr<const walletsigning::ApprovalSnapshot> m_approval_projection;
    quint64 m_approval_attempt_review{0}, m_approval_attempt_view{0};
    std::shared_ptr<execution::OwnedCommand<walletprepare::Result>> m_preparation;
    std::shared_ptr<transactionattempt::Owner> m_transaction_attempt;
    transactionattempt::Snapshot m_preparation_projection;
    quint64 m_preparation_projection_sequence{0};
    quint64 m_next_preparation_id{0};
    quint64 m_gui_preparation_intent_revision{0};
    quint64 m_current_review_identity{0};
    void publishApproval(walletsigning::ApprovalSnapshot snapshot);
    struct SendFacts {
        CAmount available{0}, selected{0}, dust{0}, minimum_relay{0};
        QHash<unsigned int, CAmount> target_rates;
        CAmount selected_target_rate{0};
        std::optional<CAmount> gross_remainder;
        quint64 revision{0};
    };
    struct SendFactsInput {
        std::mutex mutex;
        wallet::CCoinControl control;
        std::shared_ptr<const CoinsListModel::Selection> selected;
        coinselection::InputMode input_mode{coinselection::InputMode::Automatic};
        std::shared_ptr<const senddraft::Snapshot> draft;
        quint64 maximum_token{0};
        quint64 revision{0};
    };
    std::shared_ptr<SendFactsInput> m_send_facts_input{std::make_shared<SendFactsInput>()};
    std::shared_ptr<execution::SnapshotQuery<SendFacts>> m_send_facts_query;
    CAmount m_available_send_balance{0}, m_selected_send_balance{0};
    std::optional<CAmount> m_dust_relay_fee;
    std::optional<CAmount> m_minimum_relay_fee;
    bool m_send_facts_known{false};
    void requestSendFacts();
    void publishSendFacts();
    struct ReviewFlowInput {
        std::mutex mutex;
        CTransactionRef transaction;
        std::shared_ptr<const PartiallySignedTransaction> document;
        std::shared_ptr<const psbtfee::Analysis> proof;
        QSet<QString> recipients;
        QMap<QString, QString> notes;
        int unit{0};
        quint64 revision{0};
    };
    struct ReviewFlowResult {
        std::shared_ptr<const void> lifetime;
        QVariantMap flow;
        QString error;
        quint64 revision{0};
    };
    std::shared_ptr<ReviewFlowInput> m_flow_input{std::make_shared<ReviewFlowInput>()};
    std::shared_ptr<execution::SnapshotQuery<ReviewFlowResult>> m_flow_query;
    QPointer<WalletQmlModelTransaction> m_flow_review;
    CTransactionRef m_flow_transaction;
    std::shared_ptr<const PartiallySignedTransaction> m_flow_document;
    std::shared_ptr<const psbtfee::Analysis> m_flow_proof;
/** One "close this wallet?" question. Answering a question the wallet has
 * replaced or closed does nothing. */
class WalletCloseQuestion final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(WalletQmlModel* wallet READ wallet CONSTANT)
public:
    explicit WalletCloseQuestion(WalletQmlModel* wallet) : QObject(wallet), m_wallet(wallet) {}
    WalletQmlModel* wallet() const { return m_wallet; }
    Q_INVOKABLE void accept() { if (m_wallet) m_wallet->answerCloseQuestion(this, true); }
    Q_INVOKABLE void decline() { if (m_wallet) m_wallet->answerCloseQuestion(this, false); }
private:
    const QPointer<WalletQmlModel> m_wallet;
};

    int m_flow_unit{-1};
    quint64 m_flow_revision{0};
    std::shared_ptr<const void> m_flow_lifetime;
    QVariantMap m_current_flow;
    FlowDetailsModel* m_flow_details{nullptr};
    void requestCurrentFlow();
    void publishCurrentFlow();
    void adoptCurrentFlow(QVariantMap flow);
    void publishFlowDetails();
    FeeReadServices m_fee_read_services;
    FeeSelectionModel* m_fees{nullptr};
    std::shared_ptr<feeestimate::Reader> m_fee_reader;
    std::optional<SendFeePreview> m_last_fee_projection;
    quint64 m_fee_estimate_request_id{0};
    int m_fee_estimate_revision{0};
    bool m_fee_estimate_pending{false};
    bool m_fee_drained{true};
    std::string m_fee_observed_block;
    quint64 m_fee_backend_revision{0};
    QString m_transaction_error;
    quint64 m_transaction_status_revision{0};
    bool m_transaction_needs_unlock{false};
    QString m_settings_error;
    QString m_display_name;
    std::shared_ptr<std::atomic<bool>> m_unload_requested{std::make_shared<std::atomic<bool>>(false)};
    int m_display_unit{0};
    PsbtQmlModel* m_imported_psbt_model{nullptr};
};

/** Wallet association for the shared text editor; persistence remains behind
 * its original-opening ports, never a later global wallet selection. */
class AddressLabelEditorModel final : public TextEditSessionModel {
    Q_OBJECT
    Q_PROPERTY(WalletQmlModel* wallet READ wallet NOTIFY walletChanged)
public:
    AddressLabelEditorModel(WalletQmlModel* wallet, QString address, Ports ports, QObject* parent)
        : TextEditSessionModel(std::move(address), std::move(ports), parent), m_wallet(wallet) {
        if (wallet) connect(wallet, &QObject::destroyed, this, [this] { Q_EMIT walletChanged(); });
    }
    WalletQmlModel* wallet() const { return m_wallet; }
Q_SIGNALS:
    void walletChanged();
private:
    const QPointer<WalletQmlModel> m_wallet;
};

#endif // BITCOIN_QML_MODELS_WALLETQMLMODEL_H
