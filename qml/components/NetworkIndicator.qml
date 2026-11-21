// Copyright (c) 2023 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import "../controls"

import org.bitcoincore.qt 1.0

// A status badge, not a control: it takes no focus or clicks.
Control {
    id: root
    property string text: ""
    property color chainColor: Theme.color.controlBorder
    property bool show: true
    property var chainModelRef: typeof chainModel !== "undefined" ? chainModel : null
    property var textStyle: Theme.text.captionStrong
    topPadding: Theme.space.xs
    bottomPadding: Theme.space.xs
    leftPadding: Theme.space.ml
    rightPadding: Theme.space.ml
    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(Theme.size.controlSmall,
                             implicitContentHeight + topPadding + bottomPadding)
    hoverEnabled: AppMode.isDesktop
    Accessible.role: Accessible.StaticText
    Accessible.name: text
    Accessible.description: tooltip.text
    state: show && root.chainModelRef !== null ? root.chainModelRef.networkName : "MAIN"
    contentItem: Row {
        spacing: Theme.space.s
        Rectangle {
            objectName: "networkIndicatorDot"
            anchors.verticalCenter: parent.verticalCenter
            width: Theme.space.m
            height: Theme.space.m
            radius: height / 2
            color: root.chainColor
        }
        CoreText {
            anchors.verticalCenter: parent.verticalCenter
            text: root.text
            textStyle: root.textStyle
            wrap: false
            Accessible.ignored: true
        }
    }
    background: Rectangle {
        color: "transparent"
        radius: height / 2
        border.width: 1
        border.color: root.chainColor

        Behavior on border.color {
            enabled: Theme.motion.animated
            ColorAnimation { duration: Theme.motion.normal }
        }
    }

    Tooltip {
        id: tooltip
        anchors.top: root.bottom
        anchors.topMargin: Theme.space.m
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
                chainColor: Theme.color.success
            }
        },
        State {
            name: "TESTNET4"
            PropertyChanges {
                target: root
                visible: true
                text: qsTr("Testnet4")
                chainColor: Theme.color.success
            }
        },
        State {
            name: "SIGNET"
            PropertyChanges {
                target: root
                visible: true
                text: qsTr("Signet")
                chainColor: Theme.color.warning
            }
        },
        State {
            name: "REGTEST"
            PropertyChanges {
                target: root
                visible: true
                text: qsTr("Regtest")
                chainColor: Theme.color.info
            }
        }
    ]
}
