// Copyright (c) 2023 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BitcoinApp.Controls
import org.bitcoincore.qt

Item {
    id: root

    Layout.alignment: Qt.AlignCenter
    implicitWidth: 200
    implicitHeight: 200

    property alias header: mainText.text
    property alias headerSize: mainText.font.pixelSize
    property alias subText: subText.text
    property bool synced: nodeModel.verificationProgress > 0.999
    property bool paused: false
    property bool conns: true

    BlockClockDial {
        id: dial
        anchors.fill: parent
        timeRatioList: chainModel.timeRatioList
        verificationProgress: nodeModel.verificationProgress
        paused: root.paused
        synced: nodeModel.verificationProgress > 0.999
        backgroundColor: Theme.color.neutral2
        timeTickColor: Theme.color.neutral5
    }

    Button {
        id: bitcoinIcon
        background: null
        icon.source: "image://images/bitcoin-circle"
        icon.color: Theme.color.neutral9
        icon.width: 40
        icon.height: 40
        anchors.bottom: mainText.top
        anchors.horizontalCenter: root.horizontalCenter
    }

    Label {
        id: mainText
        anchors.centerIn: parent
        font.family: "Inter"
        font.styleName: "Semi Bold"
        font.pixelSize: 32
        color: Theme.color.neutral9
    }

    Label {
        id: subText
        anchors.top: mainText.bottom
        anchors.horizontalCenter: root.horizontalCenter
        font.family: "Inter"
        font.styleName: "Semi Bold"
        font.pixelSize: 18
        color: Theme.color.neutral4
    }

    RowLayout {
        id: peersIndicator
        anchors.top: subText.bottom
        anchors.topMargin: 20
        anchors.horizontalCenter: root.horizontalCenter
        spacing: 5
        Repeater {
            model: 5
            Rectangle {
                width: 3
                height: width
                radius: width/2
                color: Theme.color.neutral9
            }
        }
    }

    MouseArea {
        anchors.fill: dial
        onClicked: {
            root.paused = !root.paused
            nodeModel.pause = root.paused
        }
    }

    states: [
        State {
            name: "intialBlockDownload"; when: !synced && !paused && conns
            PropertyChanges {
                target: root
                header: Math.round(nodeModel.verificationProgress * 100) + "%"
                subText: Math.round(nodeModel.remainingSyncTime/60000) > 0 ? Math.round(nodeModel.remainingSyncTime/60000) + "mins" : Math.round(nodeModel.remainingSyncTime/1000) + "secs"
            }
        },

        State {
            name: "blockClock"; when: synced && !paused && conns
            PropertyChanges {
                target: root
                header: Number(nodeModel.blockTipHeight).toLocaleString(Qt.locale(), 'f', 0)
                subText: "Blocktime"
            }
        },

        State {
            name: "Manual Pause"; when: paused
            PropertyChanges {
                target: root
                header: "Paused"
                headerSize: 24
                subText: "Tap to resume"
            }
            PropertyChanges {
                target: bitcoinIcon
                anchors.bottomMargin: 5
            }
            PropertyChanges {
                target: subText
                anchors.topMargin: 4
            }
        },

        State {
            name: "Connecting"; when: !paused && !conns
            PropertyChanges {
                target: root
                header: "Connecting"
                headerSize: 24
                subText: "Please Wait"
            }
            PropertyChanges {
                target: bitcoinIcon
                anchors.bottomMargin: 5
            }
            PropertyChanges {
                target: subText
                anchors.topMargin: 4
            }
        }
    ]
}
