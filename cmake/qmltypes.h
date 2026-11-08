// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CMAKE_QMLTYPES_H
#define BITCOIN_CMAKE_QMLTYPES_H

#include <qml/appmode.h>
#include <qml/applicationmodels.h>
#include <qml/bitcoinamount.h>
#include <qml/buildinfo.h>
#include <qml/clipboard.h>
#include <qml/components/blockclockdial.h>
#include <qml/controls/linegraph.h>
#include <qml/models/activityfilterproxymodel.h>
#include <qml/models/addresslistmodel.h>
#include <qml/models/banlistmodel.h>
#include <qml/models/bitcoinaddress.h>
#include <qml/models/bitcoinurimodel.h>
#include <qml/models/blockclockmodel.h>
#include <qml/models/bumptransactionmodel.h>
#include <qml/models/chainmodel.h>
#include <qml/models/coinslistmodel.h>
#include <qml/models/debuglogmodel.h>
#include <qml/models/desktoptrayiconcontroller.h>
#include <qml/models/desktopwindowbehaviormodel.h>
#include <qml/models/networkstatusmodel.h>
#include <qml/models/networktraffictower.h>
#include <qml/models/nodemodel.h>
#include <qml/models/onboardingoptionsmodel.h>
#include <qml/models/options_model.h>
#include <qml/models/paymentrequest.h>
#include <qml/models/peerdetailsmodel.h>
#include <qml/models/peerlistmodel.h>
#include <qml/models/peerlistsortproxy.h>
#include <qml/models/psbtqmlmodel.h>
#include <qml/models/receiverequesthistorymodel.h>
#include <qml/models/rpcconsolemodel.h>
#include <qml/models/sendrecipient.h>
#include <qml/models/sendrecipientslistmodel.h>
#include <qml/models/signverifymessagemodel.h>
#include <qml/models/transaction.h>
#include <qml/models/transactionactivitymodel.h>
#include <qml/models/walletlistmodel.h>
#include <qml/models/walletqmlmodel.h>
#include <qml/models/walletqmlmodeltransaction.h>
#include <qml/qmlfiledialog.h>
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

struct SendRecipientsListModelForeign {
    Q_GADGET
    QML_FOREIGN(SendRecipientsListModel)
    QML_ANONYMOUS
};

struct SignVerifyMessageModelForeign {
    Q_GADGET
    QML_FOREIGN(SignVerifyMessageModel)
    QML_ANONYMOUS
};

struct PsbtQmlModelForeign {
    Q_GADGET
    QML_FOREIGN(PsbtQmlModel)
    QML_ANONYMOUS
};

struct NodeModelForeign {
    Q_GADGET
    QML_FOREIGN(NodeModel)
    QML_ANONYMOUS
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

#endif // BITCOIN_CMAKE_QMLTYPES_H
