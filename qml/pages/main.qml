// Copyright (c) 2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import Qt.labs.settings 1.0
import org.bitcoincore.qt 1.0
import "../components"
import "../controls"
import "./onboarding"
import "./node"
import "./wallet"

ApplicationWindow {
    id: appWindow
    title: "Bitcoin Core App"
    minimumWidth: 640
    minimumHeight: 665
    color: Theme.color.background
    visible: true

    Settings {
        property alias x: appWindow.x
        property alias y: appWindow.y
        property alias width: appWindow.width
        property alias height: appWindow.height
    }

    Behavior on color {
        ColorAnimation { duration: 150 }
    }

    PageStack {
        id: main
        initialItem: {
            if (needOnboarding) {
                onboardingWizard
            } else {
                if (AppMode.walletEnabled && AppMode.isDesktop) {
                    desktopWallets
                } else {
                    node
                }
            }
        }
        anchors.fill: parent
        focus: true
        Keys.onReleased: {
            if (event.key == Qt.Key_Back) {
                nodeModel.requestShutdown()
                event.accepted = true
            }
        }
    }

    Connections {
        target: nodeModel
        function onRequestedShutdown() {
            main.clear()
            main.push(shutdown)
        }
    }

    Component {
        id: onboardingWizard
        OnboardingWizard {
            onFinished: {
                optionsModel.onboard()
                if (AppMode.walletEnabled && AppMode.isDesktop) {
                    main.push(desktopWallets)
                    main.push(createWalletWizard)
                } else {
                    main.push(node)
                }
            }
        }
    }

    Component {
        id: desktopWallets
        DesktopWallets {}
    }

    Component {
        id: createWalletWizard
        CreateWalletWizard {
            onFinished: {
                main.pop()
            }
        }
    }

    Component {
        id: shutdown
        Shutdown {}
    }

    Component {
        id: node
        PageStack {
            id: nodeStack
            vertical: true
            initialItem: node
            Component {
                id: node
                NodeRunner {
                    onSettingsClicked: {
                        nodeStack.push(nodeSettings)
                    }
                }
            }
            Component {
                id: nodeSettings
                 NodeSettings {
                    onDoneClicked: {
                        nodeStack.pop()
                    }
                }
            }
        }
    }
}
