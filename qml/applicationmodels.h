// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_APPLICATIONMODELS_H
#define BITCOIN_QML_APPLICATIONMODELS_H

#include <qml/models/banlistmodel.h>
#include <qml/models/blockclockmodel.h>
#include <qml/models/chainmodel.h>
#include <qml/models/debuglogmodel.h>
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
#include <qml/models/walletlistmodel.h>
#include <qml/walletqmlcontroller.h>

#include <QObject>
#include <QPointer>
#include <QQmlContext>
#include <QQmlEngine>

/** Per-engine, non-owning access to the models installed by startup. */
class ApplicationModels : public QObject
{
    Q_OBJECT
    Q_PROPERTY(NodeModel* nodeModel READ nodeModel CONSTANT)
    Q_PROPERTY(ChainModel* chainModel READ chainModel CONSTANT)
    Q_PROPERTY(BlockClockModel* blockClockModel READ blockClockModel CONSTANT)
    Q_PROPERTY(OptionsQmlModel* optionsModel READ optionsModel CONSTANT)
    Q_PROPERTY(NetworkTrafficTower* networkTrafficTower READ networkTrafficTower CONSTANT)
    Q_PROPERTY(NetworkStatusModel* networkStatusModel READ networkStatusModel CONSTANT)
    Q_PROPERTY(PeerListModel* peerTableModel READ peerTableModel CONSTANT)
    Q_PROPERTY(PeerListSortProxy* peerListModelProxy READ peerListModelProxy CONSTANT)
    Q_PROPERTY(BanListModel* banListModel READ banListModel CONSTANT)
    Q_PROPERTY(DebugLogModel* debugLogModel READ debugLogModel CONSTANT)
    Q_PROPERTY(RpcConsoleModel* rpcConsoleModel READ rpcConsoleModel CONSTANT)
    Q_PROPERTY(WalletQmlController* walletController READ walletController CONSTANT)
    Q_PROPERTY(WalletListModel* walletListModel READ walletListModel CONSTANT)
    Q_PROPERTY(DesktopWindowBehaviorModel* desktopWindowBehaviorModel READ desktopWindowBehaviorModel CONSTANT)
    Q_PROPERTY(DesktopTrayIconController* desktopTrayIconController READ desktopTrayIconController CONSTANT)
    Q_PROPERTY(OnboardingOptionsModel* onboardingOptionsModel READ onboardingOptionsModel CONSTANT)
    Q_PROPERTY(bool testAutomationEnabled READ testAutomationEnabled CONSTANT)
    Q_PROPERTY(QString startupError READ startupError CONSTANT)

public:
    explicit ApplicationModels(QQmlEngine* engine) : QObject(engine), m_context(engine->rootContext()) {}

    NodeModel* nodeModel() const { return m_context ? m_context->contextProperty(QStringLiteral("nodeModel")).value<NodeModel*>() : nullptr; }
    ChainModel* chainModel() const { return m_context ? m_context->contextProperty(QStringLiteral("chainModel")).value<ChainModel*>() : nullptr; }
    BlockClockModel* blockClockModel() const { return m_context ? m_context->contextProperty(QStringLiteral("blockClockModel")).value<BlockClockModel*>() : nullptr; }
    OptionsQmlModel* optionsModel() const { return m_context ? m_context->contextProperty(QStringLiteral("optionsModel")).value<OptionsQmlModel*>() : nullptr; }
    NetworkTrafficTower* networkTrafficTower() const { return m_context ? m_context->contextProperty(QStringLiteral("networkTrafficTower")).value<NetworkTrafficTower*>() : nullptr; }
    NetworkStatusModel* networkStatusModel() const { return m_context ? m_context->contextProperty(QStringLiteral("networkStatusModel")).value<NetworkStatusModel*>() : nullptr; }
    PeerListModel* peerTableModel() const { return m_context ? m_context->contextProperty(QStringLiteral("peerTableModel")).value<PeerListModel*>() : nullptr; }
    PeerListSortProxy* peerListModelProxy() const { return m_context ? m_context->contextProperty(QStringLiteral("peerListModelProxy")).value<PeerListSortProxy*>() : nullptr; }
    BanListModel* banListModel() const { return m_context ? m_context->contextProperty(QStringLiteral("banListModel")).value<BanListModel*>() : nullptr; }
    DebugLogModel* debugLogModel() const { return m_context ? m_context->contextProperty(QStringLiteral("debugLogModel")).value<DebugLogModel*>() : nullptr; }
    RpcConsoleModel* rpcConsoleModel() const { return m_context ? m_context->contextProperty(QStringLiteral("rpcConsoleModel")).value<RpcConsoleModel*>() : nullptr; }
    WalletQmlController* walletController() const { return m_context ? m_context->contextProperty(QStringLiteral("walletController")).value<WalletQmlController*>() : nullptr; }
    WalletListModel* walletListModel() const { return m_context ? m_context->contextProperty(QStringLiteral("walletListModel")).value<WalletListModel*>() : nullptr; }
    DesktopWindowBehaviorModel* desktopWindowBehaviorModel() const { return m_context ? m_context->contextProperty(QStringLiteral("desktopWindowBehaviorModel")).value<DesktopWindowBehaviorModel*>() : nullptr; }
    DesktopTrayIconController* desktopTrayIconController() const { return m_context ? m_context->contextProperty(QStringLiteral("desktopTrayIconController")).value<DesktopTrayIconController*>() : nullptr; }
    OnboardingOptionsModel* onboardingOptionsModel() const { return m_context ? m_context->contextProperty(QStringLiteral("optionsModel")).value<OnboardingOptionsModel*>() : nullptr; }
    bool testAutomationEnabled() const { return m_context && m_context->contextProperty(QStringLiteral("testAutomationEnabled")).toBool(); }
    QString startupError() const { return m_context ? m_context->contextProperty(QStringLiteral("message")).toString() : QString{}; }

private:
    QPointer<QQmlContext> m_context;
};

#endif // BITCOIN_QML_APPLICATIONMODELS_H
