// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_APPLICATIONMODELS_H
#define BITCOIN_QML_APPLICATIONMODELS_H

#include <bitcoin-build-config.h> // IWYU pragma: keep

#include <qml/models/banlistmodel.h>
#include <qml/models/blockclockmodel.h>
#include <qml/models/chainmodel.h>
#include <qml/models/debuglogmodel.h>
#include <qml/models/desktopintentmodel.h>
#include <qml/models/desktoptrayiconcontroller.h>
#include <qml/models/desktopwindowbehaviormodel.h>
#include <qml/models/networkstatusmodel.h>
#include <qml/models/networktraffictower.h>
#include <qml/models/nodemodel.h>
#include <qml/models/onboardingoptionsmodel.h>
#include <qml/models/options_model.h>
#include <qml/models/peerlistmodel.h>
#include <qml/models/peerlistsortproxy.h>
#include <qml/models/rpcconsolemodel.h>
#include <qml/presentationservices.h>
#ifdef ENABLE_WALLET
#include <qml/models/walletlistmodel.h>
#include <qml/walletqmlcontroller.h>
#endif

#include <QObject>
#include <QPointer>
#include <QQmlContext>
#include <QQmlEngine>

/** Per-engine, non-owning access to the models installed by startup. */
class ApplicationModels : public QObject
{
    Q_OBJECT
    Q_PROPERTY(NodeModel* nodeModel READ nodeModel CONSTANT)
    Q_PROPERTY(DesktopIntentModel* desktopIntentModel READ desktopIntentModel CONSTANT)
    Q_PROPERTY(ChainModel* chainModel READ chainModel CONSTANT)
    Q_PROPERTY(BlockClockModel* blockClockModel READ blockClockModel CONSTANT)
    Q_PROPERTY(PresentationServices* presentationServices READ presentationServices CONSTANT)
    Q_PROPERTY(OptionsQmlModel* optionsModel READ optionsModel CONSTANT)
    Q_PROPERTY(NetworkTrafficTower* networkTrafficTower READ networkTrafficTower CONSTANT)
    Q_PROPERTY(NetworkStatusModel* networkStatusModel READ networkStatusModel CONSTANT)
    Q_PROPERTY(PeerListModel* peerTableModel READ peerTableModel CONSTANT)
    Q_PROPERTY(PeerListSortProxy* peerListModelProxy READ peerListModelProxy CONSTANT)
    Q_PROPERTY(BanListModel* banListModel READ banListModel CONSTANT)
    Q_PROPERTY(DebugLogModel* debugLogModel READ debugLogModel CONSTANT)
    Q_PROPERTY(RpcConsoleModel* rpcConsoleModel READ rpcConsoleModel CONSTANT)
#ifdef ENABLE_WALLET
    Q_PROPERTY(WalletQmlController* walletController READ walletController CONSTANT)
    Q_PROPERTY(SignerDiscoveryModel* signers READ signers NOTIFY signersChanged)
#else
    Q_PROPERTY(QObject* walletController READ walletController CONSTANT)
    Q_PROPERTY(QObject* signers READ signers CONSTANT)
#endif
#ifdef ENABLE_WALLET
    Q_PROPERTY(WalletListModel* walletListModel READ walletListModel CONSTANT)
#else
    Q_PROPERTY(QObject* walletListModel READ walletListModel CONSTANT)
#endif
    Q_PROPERTY(DesktopWindowBehaviorModel* desktopWindowBehaviorModel READ desktopWindowBehaviorModel CONSTANT)
    Q_PROPERTY(DesktopTrayIconController* desktopTrayIconController READ desktopTrayIconController CONSTANT)
    Q_PROPERTY(OnboardingOptionsModel* onboardingOptionsModel READ onboardingOptionsModel CONSTANT)
    Q_PROPERTY(bool testAutomationEnabled READ testAutomationEnabled CONSTANT)
    Q_PROPERTY(QString startupError READ startupError NOTIFY startupErrorChanged)
    Q_PROPERTY(QAbstractItemModel* startupErrorDetails READ startupErrorDetails CONSTANT)
    Q_PROPERTY(bool startupErrorPending READ startupErrorPending NOTIFY startupErrorChanged)

public:
    explicit ApplicationModels(QQmlEngine* engine) : QObject(engine), m_context(engine->rootContext()) {
        if (auto* source = startupSource()) connect(source, &StartupDiagnosticsModel::changed, this, &ApplicationModels::startupErrorChanged);
#ifdef ENABLE_WALLET
        if (auto* controller = walletController()) connect(controller, &WalletQmlController::externalSignerStatusChanged, this, &ApplicationModels::signersChanged);
#endif
    }

    NodeModel* nodeModel() const { return m_context ? m_context->contextProperty(QStringLiteral("nodeModel")).value<NodeModel*>() : nullptr; }
    DesktopIntentModel* desktopIntentModel() const { return m_context ? m_context->contextProperty(QStringLiteral("desktopIntentModel")).value<DesktopIntentModel*>() : nullptr; }
    ChainModel* chainModel() const { return m_context ? m_context->contextProperty(QStringLiteral("chainModel")).value<ChainModel*>() : nullptr; }
    BlockClockModel* blockClockModel() const { return m_context ? m_context->contextProperty(QStringLiteral("blockClockModel")).value<BlockClockModel*>() : nullptr; }
    PresentationServices* presentationServices() const { return m_context ? m_context->contextProperty(QStringLiteral("presentationServices")).value<PresentationServices*>() : nullptr; }
    OptionsQmlModel* optionsModel() const { return m_context ? m_context->contextProperty(QStringLiteral("optionsModel")).value<OptionsQmlModel*>() : nullptr; }
    NetworkTrafficTower* networkTrafficTower() const { return m_context ? m_context->contextProperty(QStringLiteral("networkTrafficTower")).value<NetworkTrafficTower*>() : nullptr; }
    NetworkStatusModel* networkStatusModel() const { return m_context ? m_context->contextProperty(QStringLiteral("networkStatusModel")).value<NetworkStatusModel*>() : nullptr; }
    PeerListModel* peerTableModel() const { return m_context ? m_context->contextProperty(QStringLiteral("peerTableModel")).value<PeerListModel*>() : nullptr; }
    PeerListSortProxy* peerListModelProxy() const { return m_context ? m_context->contextProperty(QStringLiteral("peerListModelProxy")).value<PeerListSortProxy*>() : nullptr; }
    BanListModel* banListModel() const { return m_context ? m_context->contextProperty(QStringLiteral("banListModel")).value<BanListModel*>() : nullptr; }
    DebugLogModel* debugLogModel() const { return m_context ? m_context->contextProperty(QStringLiteral("debugLogModel")).value<DebugLogModel*>() : nullptr; }
    RpcConsoleModel* rpcConsoleModel() const { return m_context ? m_context->contextProperty(QStringLiteral("rpcConsoleModel")).value<RpcConsoleModel*>() : nullptr; }
#ifdef ENABLE_WALLET
    SignerDiscoveryModel* signers() const { const auto* controller = walletController(); return controller ? controller->signers() : nullptr; }
    WalletQmlController* walletController() const { return m_context ? m_context->contextProperty(QStringLiteral("walletController")).value<WalletQmlController*>() : nullptr; }
#else
    QObject* walletController() const { return nullptr; }
    QObject* signers() const { return nullptr; }
#endif
#ifdef ENABLE_WALLET
    WalletListModel* walletListModel() const { return m_context ? m_context->contextProperty(QStringLiteral("walletListModel")).value<WalletListModel*>() : nullptr; }
#else
    QObject* walletListModel() const { return nullptr; }
#endif
    DesktopWindowBehaviorModel* desktopWindowBehaviorModel() const { return m_context ? m_context->contextProperty(QStringLiteral("desktopWindowBehaviorModel")).value<DesktopWindowBehaviorModel*>() : nullptr; }
    DesktopTrayIconController* desktopTrayIconController() const { return m_context ? m_context->contextProperty(QStringLiteral("desktopTrayIconController")).value<DesktopTrayIconController*>() : nullptr; }
    OnboardingOptionsModel* onboardingOptionsModel() const { return m_context ? m_context->contextProperty(QStringLiteral("onboardingOptionsModel")).value<OnboardingOptionsModel*>() : nullptr; }
    bool testAutomationEnabled() const { return m_context && m_context->contextProperty(QStringLiteral("testAutomationEnabled")).toBool(); }
    QString startupError() const {
        const auto* source = startupSource();
        if (source && !source->error().isEmpty()) return source->error();
        if (source && !source->summary().isEmpty()) return source->summary();
        return m_context ? m_context->contextProperty(QStringLiteral("message")).toString() : QString{};
    }
    QAbstractItemModel* startupErrorDetails() const { return startupSource(); }
    bool startupErrorPending() const { const auto* source = startupSource(); return source && source->busy(); }
Q_SIGNALS:
    void startupErrorChanged();
    void signersChanged();

private:
    StartupDiagnosticsModel* startupSource() const {
        return m_context ? qobject_cast<StartupDiagnosticsModel*>(m_context->contextProperty(QStringLiteral("startupDiagnostics")).value<QObject*>()) : nullptr;
    }
    QPointer<QQmlContext> m_context;
};

#endif // BITCOIN_QML_APPLICATIONMODELS_H
