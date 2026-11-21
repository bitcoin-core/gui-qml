// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CMAKE_QMLTYPES_H
#define BITCOIN_CMAKE_QMLTYPES_H


#include <bitcoin-build-config.h> // IWYU pragma: keep
#include <qml/appmode.h>
#include <qml/applicationmodels.h>
#include <qml/bitcoinamount.h>
#include <qml/buildinfo.h>
#include <qml/clipboard.h>
#include <qml/globalsettings.h>
#include <qml/motionpreference.h>
#include <qml/components/blockclockdial.h>
#include <qml/controls/linegraph.h>
#include <qml/models/activityfilterproxymodel.h>
#include <qml/models/activityexportoperation.h>
#include <qml/models/activitydetailmodel.h>
#include <qml/models/addresslistmodel.h>
#include <qml/models/backuppolicyoperation.h>
#include <qml/models/fileexportoperation.h>
#include <qml/models/imageexportoperation.h>
#include <qml/models/banlistmodel.h>
#include <qml/models/bitcoinaddress.h>
#include <qml/models/bitcoinurimodel.h>
#include <qml/models/sendimportmodel.h>
#include <qml/models/blockclockmodel.h>
#include <qml/models/bumptransactionmodel.h>
#include <qml/models/chainmodel.h>
#include <qml/models/coinslistmodel.h>
#include <qml/models/debuglogmodel.h>
#include <qml/models/exacttextdocument.h>
#include <qml/models/texteditsessionmodel.h>
#include <qml/models/walletreceivemodel.h>
#include <qml/components/ownedqrimage.h>
#include <qml/models/messagesigningprompt.h>
#include <qml/models/flowdetailsmodel.h>
#include <qml/models/desktoptrayiconcontroller.h>
#include <qml/models/desktopwindowbehaviormodel.h>
#include <qml/models/networkstatusmodel.h>
#include <qml/models/networktraffictower.h>
#include <qml/models/nodemodel.h>
#include <qml/models/networkmodel.h>
#include <qml/models/runtimepromptmodel.h>
#include <qml/nodelifecycle.h>
#include <qml/models/monospaceoutputmodel.h>
#include <qml/presentationservices.h>
#include <qml/models/onboardingoptionsmodel.h>
#include <qml/models/options_model.h>
#include <qml/models/settingseditormodel.h>
#include <qml/models/uipreferences.h>
#include <qml/models/paymentrequest.h>
#include <qml/models/peerdetailsmodel.h>
#include <qml/models/peercommandmodel.h>
#include <qml/models/peercommandoperation.h>
#include <qml/models/peerselectionmodel.h>
#include <qml/models/peerlistmodel.h>
#include <qml/models/peerlistsortproxy.h>
#include <qml/models/psbtqmlmodel.h>
#include <qml/models/receiverequesthistorymodel.h>
#include <qml/models/receiverequestoperation.h>
#include <qml/models/rpcconsolemodel.h>
#include <qml/models/sendrecipient.h>
#include <qml/models/feeselectionmodel.h>
#include <qml/models/sendrecipientslistmodel.h>
#include <qml/models/signverifymessagemodel.h>
#include <qml/models/signerdiscoverymodel.h>
#include <qml/models/walletselectionmodel.h>
#include <qml/models/walletcreationmodel.h>
#include <qml/models/walletoperationpresentationmodel.h>
#include <qml/models/transaction.h>
#include <qml/models/transactionactivitymodel.h>
#include <qml/models/walletlistmodel.h>
#include <qml/models/walletqmlmodel.h>
#include <qml/models/walletimporttarget.h>
#include <qml/models/walletqmlmodeltransaction.h>
#include <qml/qmlfiledialog.h>
#include <qml/nativelistmodel.h>
#include <qml/urlopener.h>
#include <qml/walletqmlcontroller.h>

#include <QtQml/qqml.h>

struct FloatQueueForeign {
    Q_GADGET
    QML_FOREIGN(QQueue<float>)
    QML_ANONYMOUS
    QML_SEQUENTIAL_CONTAINER(float)
};

struct ApplicationModelsForeign {
    Q_GADGET
    QML_FOREIGN(ApplicationModels)
    QML_NAMED_ELEMENT(ApplicationModels)
    QML_SINGLETON
};

struct DesktopIntentModelForeign {
    Q_GADGET
    QML_FOREIGN(DesktopIntentModel)
    QML_NAMED_ELEMENT(DesktopIntentModel)
    QML_UNCREATABLE("Owned by the application")
};

struct RpcOutputListModelForeign {
    Q_GADGET
    QML_FOREIGN(RpcOutputListModel)
    QML_ANONYMOUS
};

// Tooling descriptions of the types registered by RegisterQmlTypes().
// This target generates metadata only; it is never linked into the application.
struct AppModeForeign {
    Q_GADGET
    QML_FOREIGN(AppMode)
    QML_NAMED_ELEMENT(AppMode)
    QML_SINGLETON
};

struct BuildInfoForeign {
    Q_GADGET
    QML_FOREIGN(BuildInfo)
    QML_NAMED_ELEMENT(BuildInfo)
    QML_SINGLETON
};

struct ClipboardForeign {
    Q_GADGET
    QML_FOREIGN(Clipboard)
    QML_NAMED_ELEMENT(Clipboard)
    QML_SINGLETON
};

struct GlobalSettingsForeign {
    Q_GADGET
    QML_FOREIGN(GlobalSettings)
    QML_NAMED_ELEMENT(GlobalSettings)
    QML_SINGLETON
};

struct MotionPreferenceForeign {
    Q_GADGET
    QML_FOREIGN(MotionPreference)
    QML_NAMED_ELEMENT(MotionPreference)
    QML_SINGLETON
};

struct UrlOpenerForeign {
    Q_GADGET
    QML_FOREIGN(UrlOpener)
    QML_NAMED_ELEMENT(UrlOpener)
    QML_SINGLETON
};

struct BitcoinUriModelForeign {
    Q_GADGET
    QML_FOREIGN(BitcoinUriModel)
    QML_NAMED_ELEMENT(BitcoinUri)
    QML_SINGLETON
};

struct ImageExportOperationForeign {
    Q_GADGET
    QML_FOREIGN(ImageExportOperation)
    QML_NAMED_ELEMENT(ImageExportOperation)
    QML_UNCREATABLE("Owned by an image export command")
};

struct BackupFileOperationForeign {
    Q_GADGET
    QML_FOREIGN(BackupFileOperation)
    QML_NAMED_ELEMENT(BackupFileOperation)
    QML_UNCREATABLE("Owned by a wallet")
};

struct FileExportOperationModelForeign {
    Q_GADGET
    QML_FOREIGN(FileExportOperationModel)
    QML_NAMED_ELEMENT(FileExportOperationModel)
    QML_UNCREATABLE("Owned by a PSBT export request")
};

struct QmlFileDialogForeign {
    Q_GADGET
    QML_FOREIGN(QmlFileDialog)
    QML_NAMED_ELEMENT(AppFileDialog)
};

struct BlockClockDialForeign {
    Q_GADGET
    QML_FOREIGN(BlockClockDial)
    QML_NAMED_ELEMENT(BlockClockDial)
};

struct AppPreferencesForeign {
    Q_GADGET
    QML_FOREIGN(AppPreferences)
    QML_NAMED_ELEMENT(AppPreferences)
};

struct UiPreferencesStoreForeign {
    Q_GADGET
    QML_FOREIGN(UiPreferencesStore)
    QML_NAMED_ELEMENT(UiPreferencesStore)
    QML_UNCREATABLE("Owned by the application engine")
};

struct PresentationServicesForeign {
    Q_GADGET
    QML_FOREIGN(PresentationServices)
    QML_NAMED_ELEMENT(PresentationServices)
    QML_UNCREATABLE("Owned by the application")
};

struct LineGraphForeign {
    Q_GADGET
    QML_FOREIGN(LineGraph)
    QML_NAMED_ELEMENT(LineGraph)
};

struct PeerDetailsModelForeign {
    Q_GADGET
    QML_FOREIGN(PeerDetailsModel)
    QML_NAMED_ELEMENT(PeerDetailsModel)
    QML_UNCREATABLE("Owned by the application")
};

struct PeerCommandModelForeign {
    Q_GADGET
    QML_FOREIGN(PeerCommandModel)
    QML_NAMED_ELEMENT(PeerCommandModel)
    QML_UNCREATABLE("Application peer commands")
};
struct PeerCommandOperationForeign {
    Q_GADGET
    QML_FOREIGN(PeerCommandOperation)
    QML_NAMED_ELEMENT(PeerCommandOperation)
    QML_UNCREATABLE("Captured peer command")
};
struct PeerCommandResultForeign {
    Q_GADGET
    QML_FOREIGN(PeerCommandResult)
    QML_NAMED_ELEMENT(PeerCommandResult)
    QML_UNCREATABLE("Retained peer result")
};
struct PeerSelectionModelForeign {
    Q_GADGET
    QML_FOREIGN(PeerSelectionModel)
    QML_NAMED_ELEMENT(PeerSelectionModel)
};

struct DebugLogModelForeign {
    Q_GADGET
    QML_FOREIGN(DebugLogModel)
    QML_NAMED_ELEMENT(DebugLogModel)
    QML_UNCREATABLE("Owned by the application")
};

struct RpcConsoleModelForeign {
    Q_GADGET
    QML_FOREIGN(RpcConsoleModel)
    QML_NAMED_ELEMENT(RpcConsoleModel)
    QML_UNCREATABLE("Owned by the application")
};

struct MonospaceOutputModelForeign {
    Q_GADGET
    QML_FOREIGN(MonospaceOutputModel)
    QML_NAMED_ELEMENT(MonospaceOutputModel)
};

struct NativeListModelForeign {
    Q_GADGET
    QML_FOREIGN(NativeListModel)
    QML_ANONYMOUS
};

struct WalletCreationModelForeign { Q_GADGET QML_FOREIGN(WalletCreationModel) QML_NAMED_ELEMENT(WalletCreationModel) QML_UNCREATABLE("Owned by a creation form") };
struct SignerDiscoveryModelForeign { Q_GADGET QML_FOREIGN(SignerDiscoveryModel) QML_NAMED_ELEMENT(SignerDiscoveryModel) QML_UNCREATABLE("Owned by signer discovery") };
struct SignerDiscoveryErrorForeign { Q_GADGET QML_FOREIGN(SignerDiscoveryError) QML_NAMED_ELEMENT(SignerDiscoveryError) QML_UNCREATABLE("Error value") };
struct WalletCreationTextForeign { Q_GADGET QML_FOREIGN(WalletCreationText) QML_NAMED_ELEMENT(WalletCreationText) QML_UNCREATABLE("Owned by a creation form") };
struct WalletOperationModelForeign { Q_GADGET QML_FOREIGN(WalletOperationModel) QML_NAMED_ELEMENT(WalletOperationModel) QML_UNCREATABLE("Owned by an immutable operation") };
struct WalletOperationPromptForeign { Q_GADGET QML_FOREIGN(WalletOperationPrompt) QML_NAMED_ELEMENT(WalletOperationPrompt) QML_UNCREATABLE("Owned by an immutable operation") };
struct WalletOperationPresentationModelForeign { Q_GADGET QML_FOREIGN(WalletOperationPresentationModel) QML_NAMED_ELEMENT(WalletOperationPresentationModel) };
struct WalletOperationErrorForeign { Q_GADGET QML_FOREIGN(WalletOperationError) QML_ANONYMOUS };
struct WalletOperationResultForeign { Q_GADGET QML_FOREIGN(WalletOperationResult) QML_ANONYMOUS };
struct WalletCreationReceiptForeign { Q_GADGET QML_FOREIGN(WalletCreationReceipt) QML_ANONYMOUS };

struct WalletProvisioningCheckForeign {
    Q_GADGET
    QML_FOREIGN(WalletProvisioningCheck)
    QML_NAMED_ELEMENT(WalletProvisioningCheck)
    QML_UNCREATABLE("Owned by the wallet controller")
};

struct ExactTextDocumentForeign { Q_GADGET QML_FOREIGN(ExactTextDocument) QML_NAMED_ELEMENT(ExactTextDocument) };
struct TextEditSessionModelForeign { Q_GADGET QML_FOREIGN(TextEditSessionModel) QML_NAMED_ELEMENT(TextEditSessionModel) QML_UNCREATABLE("Owned by an editor factory") };
struct TextEditInputForeign { Q_GADGET QML_FOREIGN(TextEditInput) QML_NAMED_ELEMENT(TextEditInput) QML_UNCREATABLE("Owned by an editor") };
struct TextEditClosePromptForeign { Q_GADGET QML_FOREIGN(TextEditClosePrompt) QML_NAMED_ELEMENT(TextEditClosePrompt) QML_UNCREATABLE("Owned by an editor") };
struct TextEditErrorForeign { Q_GADGET QML_FOREIGN(TextEditError) QML_ANONYMOUS };
struct TextEditConflictForeign { Q_GADGET QML_FOREIGN(TextEditConflict) QML_ANONYMOUS };
struct TextEditSaveResultForeign { Q_GADGET QML_FOREIGN(TextEditSaveResult) QML_ANONYMOUS };
struct WalletReceiveModelForeign { Q_GADGET QML_FOREIGN(WalletReceiveModel) QML_NAMED_ELEMENT(WalletReceiveModel) QML_UNCREATABLE("Owned by a wallet opening") };
struct ReceiptReconciliationModelForeign { Q_GADGET QML_FOREIGN(ReceiptReconciliationModel) QML_NAMED_ELEMENT(ReceiptReconciliationModel) QML_UNCREATABLE("Owned by a wallet opening") };
struct RequestReceiptModelForeign { Q_GADGET QML_FOREIGN(RequestReceiptModel) QML_NAMED_ELEMENT(RequestReceiptModel) QML_UNCREATABLE("Owned by an original request") };
struct PaymentRequestEditorModelForeign { Q_GADGET QML_FOREIGN(PaymentRequestEditorModel) QML_NAMED_ELEMENT(PaymentRequestEditorModel) QML_UNCREATABLE("Owned by the receive editor factory") };
struct ReceiveReservationModelForeign { Q_GADGET QML_FOREIGN(ReceiveReservationModel) QML_NAMED_ELEMENT(ReceiveReservationModel) QML_UNCREATABLE("Owned by a receiving context") };
struct ReceiveAuthorizationPromptForeign { Q_GADGET QML_FOREIGN(ReceiveAuthorizationPrompt) QML_NAMED_ELEMENT(ReceiveAuthorizationPrompt) QML_UNCREATABLE("Owned by an original receiving intent") };
struct RequestSharingModelForeign { Q_GADGET QML_FOREIGN(RequestSharingModel) QML_NAMED_ELEMENT(RequestSharingModel) QML_UNCREATABLE("Owned by a request view") };
struct ShareOperationModelForeign { Q_GADGET QML_FOREIGN(ShareOperationModel) QML_NAMED_ELEMENT(ShareOperationModel) QML_UNCREATABLE("Owned by an original sharing action") };
struct RequestImageExportOperationForeign { Q_GADGET QML_FOREIGN(RequestImageExportOperation) QML_NAMED_ELEMENT(RequestImageExportOperation) QML_UNCREATABLE("Owned by an original export action") };
struct ReceiveErrorForeign { Q_GADGET QML_FOREIGN(ReceiveError) QML_NAMED_ELEMENT(ReceiveError) QML_UNCREATABLE("Error value") };
struct ReceiveTypeStatusForeign { Q_GADGET QML_FOREIGN(ReceiveTypeStatus) QML_NAMED_ELEMENT(ReceiveTypeStatus) QML_UNCREATABLE("Capability value") };
struct SharingErrorForeign { Q_GADGET QML_FOREIGN(SharingError) QML_NAMED_ELEMENT(SharingError) QML_UNCREATABLE("Error value") };
struct RequestImageExportErrorForeign { Q_GADGET QML_FOREIGN(RequestImageExportError) QML_NAMED_ELEMENT(RequestImageExportError) QML_UNCREATABLE("Error value") };
struct RequestSaveResultForeign { Q_GADGET QML_FOREIGN(RequestSaveResult) QML_ANONYMOUS };
struct ShareResultForeign { Q_GADGET QML_FOREIGN(ShareResult) QML_ANONYMOUS };
struct RequestImageExportResultForeign { Q_GADGET QML_FOREIGN(RequestImageExportResult) QML_ANONYMOUS };
struct OwnedQrImageForeign { Q_GADGET QML_FOREIGN(OwnedQrImage) QML_NAMED_ELEMENT(OwnedQrImage) };
struct MessageSigningPromptForeign { Q_GADGET QML_FOREIGN(MessageSigningPrompt) QML_NAMED_ELEMENT(MessageSigningPrompt) QML_UNCREATABLE("Owned by a signing attempt") };
struct MessageSigningErrorForeign { Q_GADGET QML_FOREIGN(MessageSigningError) QML_ANONYMOUS };
struct MessageVerificationSnapshotForeign { Q_GADGET QML_FOREIGN(MessageVerificationSnapshot) QML_ANONYMOUS };

struct BitcoinAmountForeign {
    Q_GADGET
    QML_FOREIGN(BitcoinAmount)
    QML_NAMED_ELEMENT(BitcoinAmount)
};

struct BitcoinAddressForeign {
    Q_GADGET
    QML_FOREIGN(BitcoinAddress)
    QML_NAMED_ELEMENT(BitcoinAddress)
};

struct ImportOperationForeign { Q_GADGET QML_FOREIGN(ImportOperation) QML_NAMED_ELEMENT(ImportOperation) QML_UNCREATABLE("An import owned by its source") };
struct ImportTargetForeign { Q_GADGET QML_FOREIGN(ImportTarget) QML_NAMED_ELEMENT(ImportTarget) QML_UNCREATABLE("A send draft") };
struct SendImportModelForeign { Q_GADGET QML_FOREIGN(SendImportModel) QML_NAMED_ELEMENT(SendImportModel) };

struct ActivityExportResultForeign { Q_GADGET QML_FOREIGN(ActivityExportResult) QML_ANONYMOUS };
struct ActivityExportOperationForeign { Q_GADGET QML_FOREIGN(ActivityExportOperation) QML_NAMED_ELEMENT(ActivityExportOperation) QML_UNCREATABLE("Owned by the original export action") };
struct ActivityFilterProxyModelForeign {
    Q_GADGET
    QML_FOREIGN(ActivityFilterProxyModel)
    QML_NAMED_ELEMENT(ActivityFilterProxyModel)
};

struct TransactionActivityModelForeign {
    Q_GADGET
    QML_FOREIGN(TransactionActivityModel)
    QML_NAMED_ELEMENT(TransactionActivityModel)
    QML_UNCREATABLE("Owned by the application")
};

struct ActivityDetailModelForeign {
    Q_GADGET
    QML_FOREIGN(ActivityDetailModel)
    QML_NAMED_ELEMENT(ActivityDetailModel)
    QML_UNCREATABLE("Owned by an Activity view")
};

struct AddressListModelForeign {
    Q_GADGET
    QML_FOREIGN(AddressListModel)
    QML_NAMED_ELEMENT(AddressListModel)
    QML_UNCREATABLE("Owned by the application")
};

struct PaymentRequestForeign {
    Q_GADGET
    QML_FOREIGN(PaymentRequest)
    QML_NAMED_ELEMENT(PaymentRequest)
};

struct TransactionForeign {
    Q_GADGET
    QML_FOREIGN(Transaction)
    QML_NAMED_ELEMENT(Transaction)
    QML_UNCREATABLE("Owned by the application")
};

struct FeeSelectionModelForeign {
    Q_GADGET
    QML_FOREIGN(FeeSelectionModel)
    QML_NAMED_ELEMENT(FeeSelectionModel)
    QML_UNCREATABLE("Owned by the Send draft")
};
struct FeeRateEntryForeign {
    Q_GADGET
    QML_FOREIGN(FeeRateEntry)
    QML_NAMED_ELEMENT(FeeRateEntry)
    QML_UNCREATABLE("Owned by fee selection")
};

struct SendRecipientForeign {
    Q_GADGET
    QML_FOREIGN(SendRecipient)
    QML_NAMED_ELEMENT(SendRecipient)
    QML_UNCREATABLE("Owned by the application")
};

struct BumpTransactionModelForeign {
    Q_GADGET
    QML_FOREIGN(BumpTransactionModel)
    QML_NAMED_ELEMENT(BumpTransactionModel)
    QML_UNCREATABLE("Owned by the application")
};

struct WalletSecurityModelForeign {
    Q_GADGET
    QML_FOREIGN(WalletSecurityModel)
    QML_NAMED_ELEMENT(WalletSecurityModel)
    QML_UNCREATABLE("Owned by a wallet opening")
};

struct SecurityOperationModelForeign {
    Q_GADGET
    QML_FOREIGN(SecurityOperationModel)
    QML_NAMED_ELEMENT(SecurityOperationModel)
    QML_UNCREATABLE("Owned by a security command")
};

struct WalletQmlModelForeign {
    Q_GADGET
    QML_FOREIGN(WalletQmlModel)
    QML_NAMED_ELEMENT(WalletQmlModel)
    QML_UNCREATABLE("Owned by the application")
};

struct WalletQmlModelTransactionForeign {
    Q_GADGET
    QML_FOREIGN(WalletQmlModelTransaction)
    QML_NAMED_ELEMENT(WalletQmlModelTransaction)
    QML_UNCREATABLE("Owned by the application")
};

struct WalletListModelForeign {
    Q_GADGET
    QML_FOREIGN(WalletListModel)
    QML_NAMED_ELEMENT(WalletListModel)
    QML_UNCREATABLE("Owned by the application")
};

struct WalletSelectionModelForeign {
    Q_GADGET
    QML_FOREIGN(WalletSelectionModel)
    QML_NAMED_ELEMENT(WalletSelectionModel)
    QML_UNCREATABLE("Owned by a window")
};

struct CoinsListModelForeign {
    Q_GADGET
    QML_FOREIGN(CoinsListModel)
    QML_ANONYMOUS
};

struct ReceiveRequestHistoryModelForeign {
    Q_GADGET
    QML_FOREIGN(ReceiveRequestHistoryModel)
    QML_ANONYMOUS
};

struct ReceiveRequestOperationForeign {
    Q_GADGET
    QML_FOREIGN(ReceiveRequestOperation)
    QML_NAMED_ELEMENT(ReceiveRequestOperation)
    QML_UNCREATABLE("Owned by a receive request command")
};

struct SendRecipientsListModelForeign {
    Q_GADGET
    QML_FOREIGN(SendRecipientsListModel)
    QML_ANONYMOUS
};

struct SignVerifyMessageModelForeign {
    Q_GADGET
    QML_FOREIGN(SignVerifyMessageModel)
    QML_NAMED_ELEMENT(SignVerifyMessageModel)
    QML_UNCREATABLE("Owned by a wallet")
};

struct PsbtQmlModelForeign {
    Q_GADGET
    QML_FOREIGN(PsbtQmlModel)
    QML_ANONYMOUS
};

namespace NodeLifecycleForeign {
    Q_NAMESPACE
    QML_FOREIGN_NAMESPACE(nodelifecycle)
    QML_NAMED_ELEMENT(NodeLifecycle)
}
struct NetworkModelForeign {
    Q_GADGET
    QML_FOREIGN(NetworkModel)
    QML_NAMED_ELEMENT(NetworkModel)
    QML_UNCREATABLE("Owned by node")
};
struct RuntimePromptModelForeign {
    Q_GADGET
    QML_FOREIGN(RuntimePromptModel)
    QML_NAMED_ELEMENT(RuntimePromptModel)
    QML_UNCREATABLE("Owned by original node prompt")
};

struct NodeModelForeign {
    Q_GADGET
    QML_FOREIGN(NodeModel)
    QML_NAMED_ELEMENT(NodeModel)
    QML_UNCREATABLE("Provided by ApplicationModels")
};

struct ChainModelForeign {
    Q_GADGET
    QML_FOREIGN(ChainModel)
    QML_ANONYMOUS
};

struct BlockClockModelForeign {
    Q_GADGET
    QML_FOREIGN(BlockClockModel)
    QML_ANONYMOUS
};

struct NetworkTrafficTowerForeign {
    Q_GADGET
    QML_FOREIGN(NetworkTrafficTower)
    QML_ANONYMOUS
};

struct NetworkStatusModelForeign {
    Q_GADGET
    QML_FOREIGN(NetworkStatusModel)
    QML_ANONYMOUS
};

struct BanListModelForeign {
    Q_GADGET
    QML_FOREIGN(BanListModel)
    QML_ANONYMOUS
};

struct PeerListModelForeign {
    Q_GADGET
    QML_FOREIGN(PeerListModel)
    QML_ANONYMOUS
};

struct PeerListSortProxyForeign {
    Q_GADGET
    QML_FOREIGN(PeerListSortProxy)
    QML_ANONYMOUS
};

struct OptionsQmlModelForeign {
    Q_GADGET
    QML_FOREIGN(OptionsQmlModel)
    QML_ANONYMOUS
};

struct SettingsSaveResultForeign {
    Q_GADGET
    QML_FOREIGN(SettingsSaveResult)
    QML_NAMED_ELEMENT(SettingsSaveResult)
    QML_UNCREATABLE("Retained settings outcome")
};
struct SettingsEditorModelForeign {
    Q_GADGET
    QML_FOREIGN(SettingsEditorModel)
    QML_NAMED_ELEMENT(SettingsEditorModel)
    QML_UNCREATABLE("Owned by the settings form")
};
struct SettingsEntryModelForeign {
    Q_GADGET
    QML_FOREIGN(SettingsEntryModel)
    QML_NAMED_ELEMENT(SettingsEntryModel)
    QML_UNCREATABLE("Owned by a settings editor")
};
struct CoreSettingsModelForeign {
    Q_GADGET
    QML_FOREIGN(CoreSettingsModel)
    QML_NAMED_ELEMENT(CoreSettingsModel)
    QML_UNCREATABLE("Owned by settings")
};
struct CoreProxyDraftForeign {
    Q_GADGET
    QML_FOREIGN(CoreProxyDraft)
    QML_NAMED_ELEMENT(CoreProxyDraft)
    QML_UNCREATABLE("Owned by a settings form")
};

struct OptionsTransactionLinksForeign {
    Q_GADGET
    QML_FOREIGN(OptionsTransactionLinks)
    QML_NAMED_ELEMENT(OptionsTransactionLinks)
    QML_UNCREATABLE("Owned by options")
};

struct FlowDetailsModelForeign {
    Q_GADGET
    QML_FOREIGN(FlowDetailsModel)
    QML_NAMED_ELEMENT(FlowDetailsModel)
    QML_UNCREATABLE("Owned by a transaction review")
};

struct WalletQmlControllerForeign {
    Q_GADGET
    QML_FOREIGN(WalletQmlController)
    QML_ANONYMOUS
};

struct DesktopWindowBehaviorModelForeign {
    Q_GADGET
    QML_FOREIGN(DesktopWindowBehaviorModel)
    QML_ANONYMOUS
};

struct DesktopTrayIconControllerForeign {
    Q_GADGET
    QML_FOREIGN(DesktopTrayIconController)
    QML_ANONYMOUS
};

struct OnboardingOptionsModelForeign {
    Q_GADGET
    QML_FOREIGN(OnboardingOptionsModel)
    QML_ANONYMOUS
};


#ifdef ENABLE_WALLET
struct PaymentImportOperationForeign {
    Q_GADGET
    QML_FOREIGN(PaymentImportOperation)
    QML_NAMED_ELEMENT(PaymentImportOperation)
    QML_UNCREATABLE("Owned by an import request")
};

struct WalletImportTargetForeign { Q_GADGET QML_FOREIGN(WalletImportTarget) QML_NAMED_ELEMENT(WalletImportTarget) };

#endif
#endif // BITCOIN_CMAKE_QMLTYPES_H
