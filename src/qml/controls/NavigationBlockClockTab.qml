// Copyright (c) 2024 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import org.bitcoincore.qt 1.0

Button {
    property color bgActiveColor: Theme.color.orange
    property color textColor: Theme.color.neutral7
    property color textHoverColor: Theme.color.neutral9
    property color textActiveColor: Theme.color.orange
    property color iconColor: "transparent"
    property string iconSource: ""
    property bool connected: nodeModel.numOutboundPeers > 0
    property bool synced: nodeModel.verificationProgress > 0.999
    property bool paused: false

    id: root
    checkable: true
    hoverEnabled: AppMode.isDesktop
    implicitHeight: 60
    implicitWidth: 80
    bottomPadding: 0
    topPadding: 0

    contentItem: BlockClockDial {
        anchors.fill: parent
        penWidth: 1
        timeRatioList: chainModel.timeRatioList
        verificationProgress: nodeModel.verificationProgress
        paused: root.paused
        connected: root.connected
        synced: nodeModel.verificationProgress > 0.999
        backgroundColor: Theme.color.neutral2
        timeTickColor: Theme.color.neutral5
        confirmationColors: Theme.color.confirmationColors
    }

    background: Item {
        Rectangle {
            id: bg
            height: parent.height - 5
            width: parent.width
            radius: 5
            color: Theme.color.neutral3
            visible: root.hovered

            FocusBorder {
                visible: root.visualFocus
            }

            Behavior on color {
                ColorAnimation { duration: 150 }
            }
        }
        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 3
            visible: root.checked
            color: root.bgActiveColor
        }
    }
}
