// Copyright (c) 2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0
import "../components"
import "../controls"
import "./onboarding"
import "./node"

ApplicationWindow {
    id: appWindow
    title: "Bitcoin Core App"
    minimumWidth: 640
    minimumHeight: 665
    color: Theme.color.background
    visible: true

    StackView {
        id: main
        initialItem: needOnboarding ? onboardingWizard : node
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

            onFinishedChanged: main.push(node)
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
                navRightDetail: NavButton {
                    iconSource: "image://images/gear"
                    iconHeight: 24
                    iconWidth: 24
                    onClicked: node_swipe.incrementCurrentIndex()
                }
            }
            NodeSettings {
                navMiddleDetail: Header {
                    headerBold: true
                    headerSize: 18
                    header: "Settings"
                }
                navRightDetail: NavButton {
                    text: qsTr("Done")
                    onClicked: node_swipe.decrementCurrentIndex()
                }
            }
        }
    }
}
