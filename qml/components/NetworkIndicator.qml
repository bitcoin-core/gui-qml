// Copyright (c) 2023 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import "../controls"
import "../components"

import org.bitcoincore.qt 1.0

// Outlined chip naming the network when it is not mainnet.
AbstractButton {
    id: root
    property color chainColor: Theme.color.neutral4
    property bool show: true
    property var chainModelRef: typeof chainModel !== "undefined" ? chainModel : null
    property int textSize: 13
    implicitHeight: 28
    implicitWidth: contentItem.implicitWidth + leftPadding + rightPadding
    leftPadding: 10
    rightPadding: 10
    hoverEnabled: AppMode.isDesktop
    focusPolicy: Qt.NoFocus
    Accessible.name: text
    Accessible.description: tooltip.text
    state: show && root.chainModelRef !== null ? root.chainModelRef.networkName : "MAIN"
    contentItem: Row {
        spacing: 6
        Rectangle {
            objectName: "networkIndicatorDot"
            anchors.verticalCenter: parent.verticalCenter
            width: 8
            height: 8
            radius: 4
            color: root.chainColor
        }
        CoreText {
            anchors.verticalCenter: parent.verticalCenter
            text: root.text
            bold: true
            wrap: false
            font.pixelSize: root.textSize
        }
    }
    background: Rectangle {
        color: "transparent"
        radius: height / 2
        border.width: 1
        border.color: root.chainColor

        Behavior on border.color {
            ColorAnimation { duration: 150 }
        }
    }

    Tooltip {
        id: tooltip
        anchors.top: root.bottom
        anchors.topMargin: 8
        anchors.horizontalCenter: root.horizontalCenter
        shown: root.hovered
        text: qsTr("This node is not on the main Bitcoin network")
    }

    states: [
        State {
            name: "MAIN"
            PropertyChanges {
                target: root
                visible: false
            }
        },
        State {
            name: "TEST"
            PropertyChanges {
                target: root
                visible: true
                text: qsTr("Testnet")
                chainColor: Theme.color.green
            }
        },
        State {
            name: "TESTNET4"
            PropertyChanges {
                target: root
                visible: true
                text: qsTr("Testnet4")
                chainColor: Theme.color.green
            }
        },
        State {
            name: "SIGNET"
            PropertyChanges {
                target: root
                visible: true
                text: qsTr("Signet")
                chainColor: Theme.color.purple
            }
        },
        State {
            name: "REGTEST"
            PropertyChanges {
                target: root
                visible: true
                text: qsTr("Regtest")
                chainColor: Theme.color.blue
            }
        }
    ]
}
