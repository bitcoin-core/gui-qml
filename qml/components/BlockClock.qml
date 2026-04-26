// Copyright (c) 2023 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import Qt.labs.settings 1.0

import org.bitcoincore.qt 1.0

import "../controls"
import "../controls/utils.js" as Utils

Item {
    id: root
    objectName: "blockClock"
    property real parentWidth: 600
    property real parentHeight: 600
    property bool showNetworkIndicator: true
    property var nodeModelRef: typeof nodeModel !== "undefined" ? nodeModel : null
    property var chainModelRef: typeof chainModel !== "undefined" ? chainModel : null

    width: dial.width
    height: dial.height + (networkIndicator.visible ? networkIndicator.height + networkIndicator.anchors.topMargin : 0)

    property alias header: mainText.text
    property alias headerSize: mainText.font.pixelSize
    property alias subText: subText.text
    property bool connected: root.nodeModelRef !== null && root.nodeModelRef.numOutboundPeers > 0
    property bool synced: root.nodeModelRef !== null && root.nodeModelRef.verificationProgress > 0.999
    property string syncProgress: formatProgressPercentage((root.nodeModelRef !== null ? root.nodeModelRef.verificationProgress : 0) * 100)
    property bool paused: root.nodeModelRef !== null && root.nodeModelRef.pause
    property var syncState: Utils.formatRemainingSyncTime(root.nodeModelRef !== null ? root.nodeModelRef.remainingSyncTime : 0)
    property string syncTime: syncState.text
    property bool estimating: syncState.estimating
    property bool faulted: root.nodeModelRef !== null && root.nodeModelRef.faulted

    activeFocusOnTab: true

    Settings {
        id: settings
        property alias blockclocksize: dial.scale
    }

    BlockClockDial {
        id: dial
        anchors.horizontalCenter: root.horizontalCenter
        scale: Theme.blockclocksize
        width: {Math.max(Math.min(200, Math.min(root.parentWidth - 30, root.parentHeight - 30)), 
                Math.min((root.parentWidth * dial.scale), (root.parentHeight * dial.scale)))}
        height: dial.width
        penWidth: dial.width / 50
        timeRatioList: root.chainModelRef !== null ? root.chainModelRef.timeRatioList : []
        verificationProgress: root.nodeModelRef !== null ? root.nodeModelRef.verificationProgress : 0
        paused: root.paused || root.faulted
        connected: root.connected
        synced: root.synced
        backgroundColor: Theme.color.neutral2
        timeTickColor: Theme.color.neutral5
        confirmationColors: Theme.color.confirmationColors

        Behavior on backgroundColor {
            ColorAnimation { duration: 150 }
        }

        Behavior on timeTickColor {
            ColorAnimation { duration: 150 }
        }

        Behavior on confirmationColors {
            ColorAnimation { duration: 150 }
        }
    }

    Icon {
        id: bitcoinIcon
        source: "image://images/bitcoin-circle"
        color: Theme.color.neutral9
        size: Math.max(dial.width / 5, 1)
        anchors.bottom: mainText.top
        anchors.horizontalCenter: root.horizontalCenter
    }

    Label {
        id: mainText
        anchors.centerIn: dial
        font.family: "BitcoinCoreSans"
        font.styleName: "Semi Bold"
        font.pixelSize: dial.width * (4/25)
        color: Theme.color.neutral9

        Behavior on color {
            ColorAnimation { duration: 150 }
        }
    }

    Label {
        id: subText
        anchors.top: mainText.bottom
        property bool estimating: root.estimating
        anchors.horizontalCenter: root.horizontalCenter
        font.family: "BitcoinCoreSans"
        font.styleName: "Semi Bold"
        font.pixelSize: dial.width * (9/100)
        color: Theme.color.neutral4

        Component.onCompleted: {
            colorChanged.connect(function() {
                if (!subText.estimating) {
                    themeChange.restart();
                }
            });

            estimatingChanged.connect(function() {
                if (subText.estimating) {
                    estimatingTime.start();
                } else {
                    estimatingTime.stop();
                }
            });

            subText.estimatingChanged();
        }

        ColorAnimation on color{
            id: themeChange
            target: subText
            duration: 150
        }

        SequentialAnimation {
            id: estimatingTime
            loops: Animation.Infinite
            ColorAnimation { target: subText; property: "color"; from: subText.color; to: Theme.color.neutral6; duration: 1000 }
            ColorAnimation { target: subText; property: "color"; from: Theme.color.neutral6; to: subText.color; duration: 1000 }
        }

    }

    PeersIndicator {
        anchors.top: subText.bottom
        anchors.topMargin: dial.width / 10
        anchors.horizontalCenter: root.horizontalCenter
        numOutboundPeers: root.nodeModelRef !== null ? root.nodeModelRef.numOutboundPeers : 0
        maxNumOutboundPeers: root.nodeModelRef !== null ? Math.max(root.nodeModelRef.maxNumOutboundPeers, 1) : 1
        indicatorDimensions: dial.width * (3/200)
        indicatorSpacing: dial.width / 40
        paused: root.paused || root.faulted
    }

    NetworkIndicator {
        id: networkIndicator
        objectName: "blockClockNetworkIndicator"
        show: root.showNetworkIndicator
        chainModelRef: root.chainModelRef
        anchors.top: dial.bottom
        anchors.topMargin: networkIndicator.visible ? 30 : 0
        anchors.horizontalCenter: root.horizontalCenter
    }

    MouseArea {
        objectName: "blockClockToggleArea"
        anchors.fill: dial
        cursorShape: root.faulted ? Qt.ArrowCursor : Qt.PointingHandCursor
        enabled: !root.faulted
        function click() {
            root.togglePause()
        }
        onClicked: click()
        FocusBorder {
            visible: root.activeFocus
        }
    }

    states: [
        State {
            name: "IBD"; when: !faulted && !synced && !paused && connected
            PropertyChanges {
                target: root
                header: root.syncProgress
                subText: root.syncTime
            }
        },

        State {
            name: "BLOCKCLOCK"; when: !faulted && synced && !paused && connected
            PropertyChanges {
                target: root
                header: Number(root.nodeModelRef !== null ? root.nodeModelRef.blockTipHeight : 0).toLocaleString(Qt.locale(), 'f', 0)
                subText: "Blocktime"
                estimating: false
            }
        },

        State {
            name: "PAUSE"; when: paused && !faulted
            PropertyChanges {
                target: root
                header: "Paused"
                headerSize: dial.width * (3/25)
                subText: "Tap to resume"
                estimating: false
            }
            PropertyChanges {
                target: bitcoinIcon
                anchors.bottomMargin: dial.width / 40
            }
            PropertyChanges {
                target: subText
                anchors.topMargin: dial.width / 50
            }
        },

        State {
            name: "ERROR"; when: faulted
            PropertyChanges {
                target: root
                header: "Error"
                headerSize: dial.width * (3/25)
            }
            PropertyChanges {
                target: bitcoinIcon
                anchors.bottomMargin: dial.width / 40
                icon.source: "image://images/error"
            }
        },

        State {
            name: "CONNECTING"; when: !faulted && !paused && !connected
            PropertyChanges {
                target: root
                header: qsTr("Connecting")
                headerSize: dial.width * (3/25)
                subText: qsTr("Please wait")
                estimating: false
            }
            PropertyChanges {
                target: bitcoinIcon
                anchors.bottomMargin: dial.width / 40
            }
            PropertyChanges {
                target: subText
                anchors.topMargin: dial.width / 50
            }
        }
    ]


    function formatProgressPercentage(progress) {
        if (progress >= 1) {
            return Math.round(progress) + "%"
        } else if (progress >= 0.1) {
            return progress.toFixed(1) + "%"
        } else if (progress >= 0.01) {
            return progress.toFixed(2) + "%"
        } else {
            return "0%"
        }
    }

    function togglePause() {
        if (!root.faulted && root.nodeModelRef !== null) {
            root.nodeModelRef.pause = !root.paused
        }
    }
}
