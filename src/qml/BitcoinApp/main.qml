// Copyright (c) 2021-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BitcoinApp.Components
import BitcoinApp.Controls

ApplicationWindow {
    id: appWindow
    title: "Bitcoin Core App"
    minimumWidth: 640
    minimumHeight: 665
    color: Theme.color.background
    visible: true

    StackView {
        id: main
        initialItem: onboardingWizard
        anchors.fill: parent
    }

    Component {
        id: onboardingWizard
        SwipeView {
            id: swipeView
            property bool finished: false
            anchors.fill: parent
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
        id: node
        SwipeView {
            id: node_swipe
            anchors.fill: parent
            interactive: false
            orientation: Qt.Vertical
            NodeRunner {
                navRightDetail: NavButton {
                    iconSource: "image://images/gear"
                    iconHeight: 24
                    onClicked: node_swipe.incrementCurrentIndex()
                }
            }
            NodeSettings {
                navMiddleDetail: Header {
                    bold: true
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
