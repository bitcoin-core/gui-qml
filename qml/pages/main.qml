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

    StackView {
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
        SwipeView {
            id: swipeView
            property bool finished: false
            interactive: false

            OnboardingCover {}
            OnboardingStrengthen {}
            OnboardingBlockclock {}
            OnboardingStorageLocation {}
            OnboardingStorageAmount {}
            OnboardingConnection {}

            onFinishedChanged: {
                optionsModel.onboard()
                if (AppMode.walletEnabled && AppMode.isDesktop) {
                    main.push(desktopWallets)
                    main.push(addWallet)
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
        id: addWallet
        AddWallet {
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
        SwipeView {
            id: node_swipe
            interactive: false
            orientation: Qt.Vertical
            NodeRunner {
                onSettingsClicked: {
                    node_swipe.incrementCurrentIndex()
                }
            }
            NodeSettings {
                onDoneClicked: {
                    node_swipe.decrementCurrentIndex()
                }
            }
        }
    }
}
