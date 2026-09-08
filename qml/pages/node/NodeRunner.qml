// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../../controls"
import "../../controls/utils.js" as Utils
import "../../components"

Page {
    signal settingsClicked
    signal peersClicked
    signal consoleClicked
    id: root
    objectName: "nodeRunner"
    background: null
    clip: true
    property bool showNavigationButtons: true
    header: NavigationBar2 {
        rightItem: Item {
            implicitWidth: actionsRow.implicitWidth + 12
            implicitHeight: actionsRow.implicitHeight + 10

            RowLayout {
                id: actionsRow
                anchors.top: parent.top
                anchors.right: parent.right
                anchors.topMargin: 8
                anchors.rightMargin: 12
                spacing: 4

                NodeStatusActions {
                    Layout.alignment: Qt.AlignVCenter
                }
                IconButton {
                    objectName: "peersTabButton"
                    visible: root.showNavigationButtons
                    iconSource: Utils.nodeConnectionIcon(nodeModel.numPeers)
                    iconColor: Theme.color.neutral7
                    hoverColor: Theme.color.neutral9
                    size: 34
                    iconSize: 24
                    Layout.alignment: Qt.AlignVCenter
                    onClicked: root.peersClicked()
                }
                IconButton {
                    objectName: "consoleTabButton"
                    visible: root.showNavigationButtons
                    iconSource: "image://images/console"
                    iconColor: Theme.color.neutral7
                    hoverColor: Theme.color.neutral9
                    size: 34
                    iconSize: 24
                    Layout.alignment: Qt.AlignVCenter
                    onClicked: root.consoleClicked()
                }
                IconButton {
                    objectName: "nodeSettingsButton"
                    visible: root.showNavigationButtons
                    iconSource: "image://images/gear"
                    iconColor: Theme.color.neutral9
                    hoverColor: Theme.color.neutral9
                    size: 34
                    iconSize: 34
                    Layout.alignment: Qt.AlignVCenter
                    onClicked: root.settingsClicked()
                }
            }
        }
    }

    Component.onCompleted: nodeModel.startNodeInitializionThread();

    BlockClock {
        parentWidth: parent.width - 40
        parentHeight: parent.height
        anchors.centerIn: parent
    }
}
