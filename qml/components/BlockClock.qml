// Copyright (c) 2023 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Window 2.15

import org.bitcoincore.qt 1.0

import "../controls"
import "../controls/utils.js" as Utils

Item {
    id: root
    objectName: "blockClock"
    property real parentWidth: 600
    property real parentHeight: 600
    property bool showNetworkIndicator: true
    // Backing models remain current while false; only presentation work stops.
    property bool renderingActive: true
    property var nodeModelRef: typeof nodeModel !== "undefined" ? nodeModel : null
    property var chainModelRef: typeof chainModel !== "undefined" ? chainModel : null
    property var blockClockModelRef: typeof blockClockModel !== "undefined" ? blockClockModel : null
    property var networkStatusModelRef: typeof networkStatusModel !== "undefined" ? networkStatusModel : null
    readonly property bool windowVisible: root.Window.window !== null &&
        root.Window.window.visible &&
        root.Window.window.visibility !== Window.Hidden &&
        root.Window.window.visibility !== Window.Minimized
    readonly property bool presentationActive: root.renderingActive && root.visible && root.windowVisible

    width: Math.max(dial.width, statusLine.implicitWidth)
    height: root.showDetailsLink
        ? detailsLink.y + detailsLink.height
        : (networkIndicator.visible
            ? networkIndicator.y + networkIndicator.height
            : statusLine.y + statusLine.height)

    signal detailsRequested()
    signal warningsRequested()

    property alias header: mainText.text
    property alias subText: subText.text
    readonly property alias status: nodeStatus
    readonly property string statusLabel: nodeStatus.label
    readonly property color statusTone: nodeStatus.tone
    // Words such as "Connecting" use a smaller headline than numbers.
    property bool headerIsWord: true
    property bool showDetailsLink: false
    readonly property bool connected: root.nodeModelRef !== null && root.nodeModelRef.numPeers > 0
    // Completion is latched after Core leaves IBD and the active chain catches
    // the best known header, and resets if the chain falls substantially behind.
    // verificationProgress only renders ongoing sync.
    readonly property bool synced: root.nodeModelRef !== null && root.nodeModelRef.initialSyncComplete
    readonly property bool headerSyncActive: root.nodeModelRef !== null && root.nodeModelRef.headerSyncActive
    readonly property real syncProgressFraction: root.headerSyncActive
        ? root.nodeModelRef.headerSyncProgress
        : (root.nodeModelRef !== null ? root.nodeModelRef.verificationProgress : 0)
    // A node holding only the genesis block has not synced anything yet, even
    // though Core reports a tiny verification progress for it.
    readonly property bool hasSyncedBlocks: root.synced || (root.nodeModelRef !== null && root.nodeModelRef.blockTipHeight > 0)
    readonly property real headerProgressFraction: root.headerSyncActive
        ? root.nodeModelRef.headerSyncProgress
        : (root.hasSyncedBlocks ? 1 : 0)
    readonly property real blockProgressFraction: root.headerSyncActive || !root.hasSyncedBlocks
        ? 0
        : root.nodeModelRef.verificationProgress
    readonly property string syncProgress: root.headerSyncActive
        ? Math.floor(root.syncProgressFraction * 100) + "%"
        : formatProgressPercentage(root.syncProgressFraction * 100, root.synced)
    property bool paused: root.nodeModelRef !== null && root.nodeModelRef.pause
    property var syncState: Utils.formatRemainingSyncTime(root.nodeModelRef !== null ? root.nodeModelRef.remainingSyncTime : 0)
    property string syncTime: syncState.text
    property bool estimating: syncState.estimating
    property bool faulted: root.nodeModelRef !== null && root.nodeModelRef.faulted
    property bool offline: root.networkStatusModelRef !== null && root.networkStatusModelRef.networkOffline
    readonly property string peersText: nodeStatus.peersText

    activeFocusOnTab: true

    NodeStatus {
        id: nodeStatus
        nodeModelRef: root.nodeModelRef
        chainModelRef: root.chainModelRef
        blockClockModelRef: root.blockClockModelRef
        networkStatusModelRef: root.networkStatusModelRef
    }

    AppSettings {
        id: settings
        property alias blockclocksize: dial.scale
    }

    BlockClockDial {
        id: dial
        objectName: "blockClockDial"
        anchors.top: root.top
        anchors.horizontalCenter: root.horizontalCenter
        scale: Theme.blockclocksize
        width: {Math.max(Math.min(200, Math.min(root.parentWidth - 30, root.parentHeight - 30)),
                Math.min((root.parentWidth * dial.scale), (root.parentHeight * dial.scale)))}
        height: dial.width
        layeredRings: true
        currentTimeFraction: root.blockClockModelRef !== null ? root.blockClockModelRef.currentTimeFraction : 0
        blockTimeFractions: root.blockClockModelRef !== null ? root.blockClockModelRef.blockTimeFractions : []
        syncProgress: root.syncProgressFraction
        headerProgress: root.headerProgressFraction
        blockProgress: root.blockProgressFraction
        paused: root.paused || root.faulted || root.offline
        connected: root.connected && !root.offline
        synced: root.synced
        dimmed: root.paused || root.offline || !root.connected
        faulted: root.faulted
        backgroundColor: root.faulted ? Theme.color.red : Theme.color.neutral2
        timeTickColor: Theme.color.neutral5
        dimmedColor: Theme.color.neutral4
        headerProgressColor: Theme.color.blue
        progressColor: Theme.color.orange
        handColor: Theme.color.neutral9
        confirmationColors: Theme.color.confirmationColors
        renderingActive: root.presentationActive

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

    Column {
        anchors.centerIn: dial
        spacing: 2

        Label {
            id: mainText
            objectName: "blockClockHeader"
            anchors.horizontalCenter: parent.horizontalCenter
            font.family: "BitcoinCoreSans"
            font.styleName: "Semi Bold"
            // Hero proportions, never below the design's compact widget sizes.
            readonly property int headerPixelSize: root.headerIsWord
                ? Math.max(20, Math.round(dial.width * 0.093))
                : Math.max(30, Math.round(dial.width * 0.133))
            font.pixelSize: headerPixelSize
            font.letterSpacing: -0.02 * headerPixelSize
            color: Theme.color.neutral9

            Behavior on color {
                ColorAnimation { duration: 150 }
            }
        }

        TextMetrics {
            id: subTextMetrics
            font.family: "BitcoinCoreSans"
            font.styleName: "Regular"
            font.pixelSize: Math.max(13, Math.round(dial.width * 0.05))
            text: subText.text
        }

        Label {
            id: subText
            objectName: "blockClockSubText"
            property bool estimating: root.estimating
            anchors.horizontalCenter: parent.horizontalCenter
            width: dial.width * 0.7
            horizontalAlignment: Text.AlignHCenter
            font.family: "BitcoinCoreSans"
            font.styleName: "Regular"
            readonly property int desiredPixelSize: subTextMetrics.font.pixelSize
            readonly property real desiredTextWidth: Math.max(subTextMetrics.width, subTextMetrics.advanceWidth)
            font.pixelSize: subText.desiredTextWidth > subText.width
                ? Math.max(1, Math.floor(subText.desiredPixelSize * (subText.width - 2) / subText.desiredTextWidth))
                : subText.desiredPixelSize
            elide: Text.ElideRight
            color: Theme.color.neutral7

            Behavior on color {
                enabled: !subText.estimating
                ColorAnimation { duration: 150 }
            }

            SequentialAnimation {
                id: estimatingTime
                objectName: "blockClockEstimatingAnimation"
                running: root.presentationActive && subText.estimating
                loops: Animation.Infinite
                ColorAnimation { target: subText; property: "color"; from: Theme.color.neutral7; to: Theme.color.neutral5; duration: 1000 }
                ColorAnimation { target: subText; property: "color"; from: Theme.color.neutral5; to: Theme.color.neutral7; duration: 1000 }
            }
        }
    }

    Row {
        id: statusLine
        objectName: "blockClockStatusLine"
        anchors.top: dial.bottom
        anchors.topMargin: 18
        anchors.horizontalCenter: root.horizontalCenter
        spacing: 8

        Rectangle {
            objectName: "blockClockStatusDot"
            anchors.verticalCenter: parent.verticalCenter
            width: 8
            height: 8
            radius: 4
            color: root.statusTone

            Behavior on color {
                ColorAnimation { duration: 150 }
            }
        }
        CoreText {
            objectName: "blockClockStatusLabel"
            text: root.statusLabel
            font.pixelSize: 14
            wrap: false
        }
        WarningsLink {
            objectName: "blockClockWarnings"
            anchors.verticalCenter: parent.verticalCenter
            status: nodeStatus
            onClicked: root.warningsRequested()
        }
        CoreText {
            text: "·"
            font.pixelSize: 14
            wrap: false
            color: Theme.color.neutral5
        }
        CoreText {
            objectName: "blockClockPeersText"
            text: root.peersText
            font.pixelSize: 14
            wrap: false
            color: Theme.color.neutral7
        }
    }

    NetworkIndicator {
        id: networkIndicator
        objectName: "blockClockNetworkIndicator"
        show: root.showNetworkIndicator
        chainModelRef: root.chainModelRef
        anchors.top: statusLine.bottom
        anchors.topMargin: networkIndicator.visible ? 18 : 0
        anchors.horizontalCenter: root.horizontalCenter
    }

    LinkButton {
        id: detailsLink
        objectName: "blockClockDetailsLink"
        visible: root.showDetailsLink
        anchors.top: networkIndicator.visible ? networkIndicator.bottom : statusLine.bottom
        anchors.topMargin: 12
        anchors.horizontalCenter: root.horizontalCenter
        text: qsTr("Node details")
        onClicked: root.detailsRequested()
    }

    // The whole clock opens Node details. Pausing has its own button.
    MouseArea {
        objectName: "blockClockDetailsArea"
        anchors.fill: dial
        cursorShape: Qt.PointingHandCursor
        onClicked: root.detailsRequested()
        FocusBorder {
            visible: root.activeFocus
        }
    }

    Keys.onReturnPressed: root.detailsRequested()
    Keys.onSpacePressed: root.detailsRequested()
    Accessible.role: Accessible.Button
    Accessible.name: qsTr("Block clock")
    Accessible.description: root.statusLabel + ", " + root.header + ", " + root.subText

    states: [
        State {
            name: "IBD"; when: !faulted && !offline && !synced && !paused && connected
            PropertyChanges {
                target: root
                header: root.syncProgress
                headerIsWord: false
                subText: root.headerSyncActive
                    ? (root.nodeModelRef.headerPresync ? qsTr("Pre-syncing headers") : qsTr("Syncing headers"))
                    : root.syncTime
            }
        },

        State {
            name: "BLOCKCLOCK"; when: !faulted && !offline && synced && !paused && connected
            PropertyChanges {
                target: root
                header: Number(root.nodeModelRef !== null ? root.nodeModelRef.blockTipHeight : 0).toLocaleString(Qt.locale(), 'f', 0)
                headerIsWord: false
                subText: nodeStatus.formatLastBlock(nodeStatus.lastBlockAge)
                estimating: false
            }
        },

        State {
            name: "PAUSE"; when: paused && !faulted && !offline
            PropertyChanges {
                target: root
                header: qsTr("Paused")
                subText: qsTr("Network off")
                estimating: false
            }
        },

        State {
            name: "ERROR"; when: faulted
            PropertyChanges {
                target: root
                header: qsTr("Error")
                subText: qsTr("Node stopped")
                estimating: false
            }
        },

        State {
            name: "OFFLINE"; when: !faulted && offline
            PropertyChanges {
                target: root
                header: qsTr("Offline")
                subText: qsTr("Check connection")
                estimating: false
            }
        },

        State {
            name: "CONNECTING"; when: !faulted && !paused && !connected
            PropertyChanges {
                target: root
                header: qsTr("Connecting")
                subText: qsTr("Finding peers")
                estimating: false
            }
        }
    ]

    function formatProgressPercentage(progress, complete) {
        if (complete === true) {
            return "100%"
        }
        // Never present an estimate as complete while initial sync is pending.
        if (progress >= 99.9) {
            return "99.9%"
        }
        if (progress >= 10) {
            return progress.toFixed(1) + "%"
        }
        return Math.max(0, progress).toFixed(2) + "%"
    }
}
