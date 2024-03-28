// Copyright (c) 2024 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0
import "../../controls"
import "../../components"
import "../node"

Page {
    id: root
    background: null

    ButtonGroup { id: navigationTabs }

    header: NavigationBar2 {
        id: navBar
        leftItem: RowLayout {
            spacing: 5
            Icon {
                source: "image://images/singlesig-wallet"
                color: Theme.color.neutral8
                Layout.preferredWidth: 30
                Layout.preferredHeight: 30
                Layout.leftMargin: 10
            }
            Column {
                spacing: 2
                CoreText {
                    text: "Singlesig Wallet"
                    color: Theme.color.neutral7
                    bold: true
                }
                CoreText {
                    text: "<font color=\""+Theme.color.white+"\">₿</font> 0.00 <font color=\""+Theme.color.white+"\">167 599</font>"
                    color: Theme.color.neutral7
                }
            }
        }
        centerItem: RowLayout {
            NavigationTab {
                id: activityTabButton
                checked: true
                text: qsTr("Activity")
                property int index: 0
                ButtonGroup.group: navigationTabs
            }
            NavigationTab {
                text: qsTr("Send")
                property int index: 1
                ButtonGroup.group: navigationTabs
            }
            NavigationTab {
                text: qsTr("Receive")
                property int index: 2
                ButtonGroup.group: navigationTabs
            }
        }
        rightItem: RowLayout {
            spacing: 5
            NetworkIndicator {
                textSize: 11
                Layout.rightMargin: 5
                shorten: true
            }
            NavigationTab {
                Layout.preferredWidth: 30
                Layout.rightMargin: 10
                property int index: 3
                ButtonGroup.group: navigationTabs
            }
            NavigationTab {
                iconSource: "image://images/gear-outline"
                iconColor: Theme.color.neutral7
                Layout.preferredWidth: 30
                property int index: 4
                ButtonGroup.group: navigationTabs
            }
        }
        background: Rectangle {
            color: Theme.color.neutral4
            anchors.bottom: navBar.bottom
            anchors.bottomMargin: 4
            height: 1
            width: parent.width
        }
    }

    StackLayout {
        width: parent.width
        height: parent.height
        currentIndex: navigationTabs.checkedButton.index
        Item {
            id: activityTab
            CoreText { text: "Activity" }
        }
        Item {
            id: sendTab
            CoreText { text: "Send" }
        }
        Item {
            id: receiveTab
            CoreText { text: "Receive" }
        }
        Item {
            id: blockClockTab
            anchors.fill: parent
            BlockClock {
                parentWidth: parent.width - 40
                parentHeight: parent.height
                anchors.centerIn: parent
                showNetworkIndicator: false
            }
        }
        NodeSettings {
        }
    }

    Component.onCompleted: nodeModel.startNodeInitializionThread();
}
