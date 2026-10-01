// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../../controls"
import "../../components"
import "../../components/widgets"

Page {
    id: root
    objectName: "nodeRunner"
    background: null
    clip: true

    function openNode() {
        nodeTabButton.checked = true
        nodeNavigationStack.pop(null, StackView.Immediate)
    }
    function openDashboard() { widgetsTabButton.checked = true }
    function openPeers() { nodeTabButton.checked = true; nodeOverview.openPeers() }
    function openSettings(section) {
        if (section) settingsLoader.pendingSection = section
        settingsTabButton.checked = true
        Qt.callLater(settingsLoader.applyPendingSection)
    }
    function openConsole() { root.openSettings("rpc-console") }
    function openNetworkTraffic() { root.openSettings("network-traffic") }

    ButtonGroup { id: navigationTabs }
    header: NavigationBar2 {
        rightItem: RowLayout {
            spacing: 5
            NetworkIndicator {
                Layout.rightMargin: 8
                objectName: "nodeRunnerNetworkIndicator"
                textSize: 11
                shorten: true
                enabled: false
                focusPolicy: Qt.NoFocus
                Accessible.name: text
                Accessible.role: Accessible.StaticText
            }
            NavigationTab {
                id: nodeTabButton
                objectName: "blockClockTabButton"
                checked: true
                property int index: 0
                Layout.preferredWidth: 40
                ButtonGroup.group: navigationTabs
                Accessible.name: qsTr("Node")
                customContent: MiniBlockClock { pageSelected: nodeTabButton.checked }
                Tooltip {
                    anchors.top: nodeTabButton.bottom
                    anchors.topMargin: 8
                    anchors.horizontalCenter: nodeTabButton.horizontalCenter
                    shown: nodeTabButton.hovered
                    text: qsTr("Node")
                }
            }
            NavigationTab {
                id: widgetsTabButton
                objectName: "widgetsTabButton"
                iconSource: "image://images/widgets.svg"
                iconColor: Theme.color.neutral7
                iconSize: 18
                property int index: 1
                Layout.preferredWidth: 40
                ButtonGroup.group: navigationTabs
                Accessible.name: qsTr("Dashboard")
                Tooltip {
                    anchors.top: widgetsTabButton.bottom
                    anchors.topMargin: 8
                    anchors.horizontalCenter: widgetsTabButton.horizontalCenter
                    shown: widgetsTabButton.hovered
                    text: qsTr("Dashboard")
                }
            }
            NavigationTab {
                id: settingsTabButton
                objectName: "nodeSettingsButton"
                iconSource: "image://images/gear-outline"
                iconColor: Theme.color.neutral7
                Layout.preferredWidth: 40
                property int index: 2
                ButtonGroup.group: navigationTabs
                Accessible.name: qsTr("Settings")
                Tooltip {
                    anchors.top: settingsTabButton.bottom
                    anchors.topMargin: 8
                    anchors.horizontalCenter: settingsTabButton.horizontalCenter
                    shown: settingsTabButton.hovered
                    text: qsTr("Settings")
                }
            }
        }
    }
    Component.onCompleted: nodeModel.startNodeInitializionThread()
    StackLayout {
        anchors.fill: parent
        currentIndex: navigationTabs.checkedButton.index
        PageStack {
            id: nodeNavigationStack
            objectName: "nodeNavigationStack"
            initialItem: NodeOverview { id: nodeOverview }
        }
        WidgetDashboard {}
        Item {
            Loader {
                id: settingsLoader
                objectName: "nodeSettingsLoader"
                anchors.fill: parent
                property bool retainItem: false
                property string pendingSection: ""

                function applyPendingSection() {
                    if (!item || pendingSection.length === 0) return
                    item.selectSection(pendingSection)
                    pendingSection = ""
                }

                active: settingsTabButton.checked || retainItem
                onLoaded: {
                    retainItem = true
                    Qt.callLater(applyPendingSection)
                }
                sourceComponent: SettingsView { showDoneButton: false }
            }
        }
    }
}
