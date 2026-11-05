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
    property color bgColor
    property color textColor: Theme.color.accentForeground
    property bool shorten: false
    property bool show: true
    property var chainModelRef: typeof chainModel !== "undefined" ? chainModel : null
    property var textStyle: Theme.text.description
    topPadding: Theme.space.xxs
    bottomPadding: Theme.space.xxs
    leftPadding: Theme.space.m
    rightPadding: Theme.space.m
    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)
    Accessible.role: Accessible.StaticText
    Accessible.name: text
    state: show && root.chainModelRef !== null ? root.chainModelRef.networkName : "MAIN"
    contentItem: CoreText {
        text: root.text
        textStyle: root.textStyle
        color: root.textColor
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        Accessible.ignored: true
    }
    background: Rectangle {
        id: bg
        color: root.bgColor
        radius: 2

        Behavior on color {
            enabled: Theme.motion.animated
            ColorAnimation { duration: Theme.motion.normal }
        }
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
                text: shorten ? qsTr("Testnet") : qsTr("Test network")
                bgColor: Theme.color.success
            }
        },
        State {
            name: "TESTNET4"
            PropertyChanges {
                target: root
                visible: true
                text: shorten ? qsTr("Testnet4") : qsTr("Test network (v4)")
                bgColor: Theme.color.success
            }
        },
        State {
            name: "SIGNET"
            PropertyChanges {
                target: root
                visible: true
                text: shorten ? qsTr("Signet") : qsTr("Signet network")
                bgColor: Theme.color.warning
                textColor: Theme.color.warningForeground
            }
        },
        State {
            name: "REGTEST"
            PropertyChanges {
                target: root
                visible: true
                text: shorten ? qsTr("Regtest") : qsTr("Regtest mode")
                bgColor: Theme.color.info
            }
        }
    ]
}
