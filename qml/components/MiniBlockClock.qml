// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Window 2.15

import org.bitcoincore.qt 1.0

import "../controls"

Item {
    id: root
    objectName: "miniBlockClock"

    property real iconSize: 18
    property bool pageSelected: false
    property bool renderingActive: true
    property var nodeModelRef: typeof nodeModel !== "undefined" ? nodeModel : null
    readonly property bool connected: root.nodeModelRef !== null && root.nodeModelRef.numPeers > 0
    readonly property bool synced: root.nodeModelRef !== null && root.nodeModelRef.initialSyncComplete
    property bool paused: root.nodeModelRef !== null && root.nodeModelRef.pause
    property bool faulted: root.nodeModelRef !== null && root.nodeModelRef.faulted
    property var networkStatusModelRef: typeof networkStatusModel !== "undefined" ? networkStatusModel : null
    property var blockClockModelRef: typeof blockClockModel !== "undefined" ? blockClockModel : null
    property bool offline: networkStatusModelRef !== null && networkStatusModelRef.networkOffline
    readonly property bool windowVisible: root.Window.window !== null &&
        root.Window.window.visible &&
        root.Window.window.visibility !== Window.Hidden &&
        root.Window.window.visibility !== Window.Minimized
    readonly property bool presentationActive: root.renderingActive && root.visible && root.windowVisible

    readonly property bool showOfflineState: !root.faulted && root.offline
    readonly property bool showPausedState: root.paused && !root.faulted && !root.offline
    readonly property bool showConnectingState: !root.paused && !root.faulted && !root.offline && !root.connected
    readonly property bool showIbdState: !root.paused && !root.faulted && !root.offline && root.connected && !root.synced
    readonly property bool showClockState: !root.paused && !root.faulted && !root.offline && root.connected && root.synced
    readonly property bool showDialState: dial.visible
    readonly property real strokeWidth: Math.max(1, root.width / 12)
    readonly property real pausedStrokeWidth: Math.max(1, Math.round(root.strokeWidth))
    readonly property real pausedBarWidth: Math.max(root.pausedStrokeWidth, Math.round(root.width * 0.11))
    readonly property real pausedBarHeight: Math.max(root.pausedStrokeWidth, Math.round(root.height * 0.42))
    readonly property real pausedBarSpacing: Math.max(root.pausedStrokeWidth, Math.round(root.width * 0.12))
    readonly property color dialGlyphColor: root.pageSelected ? Theme.color.confirmationColors[5] : Theme.color.neutral9
    readonly property color pausedGlyphColor: root.pageSelected ? Theme.color.confirmationColors[5] : Theme.color.neutral6

    implicitWidth: iconSize
    implicitHeight: iconSize
    width: implicitWidth
    height: implicitHeight

    BlockClockDial {
        id: dial
        objectName: "miniBlockClockDial"
        visible: root.showConnectingState || root.showIbdState || root.showClockState
        anchors.fill: parent
        penWidth: root.strokeWidth
        connectingAnimationDelayMs: 0
        currentTimeFraction: root.pageSelected ? 1.0 : (root.blockClockModelRef !== null ? root.blockClockModelRef.currentTimeFraction : 0)
        blockTimeFractions: []
        syncProgress: root.pageSelected ? 1.0 : (root.nodeModelRef !== null ? root.nodeModelRef.verificationProgress : 0)
        connected: root.pageSelected || root.connected
        synced: root.pageSelected || root.synced
        paused: false
        animateDial: root.showConnectingState && !root.pageSelected
        showTimeTicks: false
        showBlockSegments: false
        useGradientArcWhenSynced: !root.pageSelected
        backgroundColor: Theme.color.neutral3
        timeTickColor: "transparent"
        confirmationColors: Theme.color.confirmationColors
        renderingActive: root.presentationActive
    }

    Item {
        visible: root.showDialState
        anchors.centerIn: parent
        width: parent.width * (8 / 18)
        height: width

        Rectangle {
            anchors.top: parent.top
            anchors.horizontalCenter: parent.horizontalCenter
            width: Math.max(1, parent.width * 0.25)
            height: width
            radius: width / 2
            color: root.dialGlyphColor
        }

        Rectangle {
            anchors.top: parent.top
            anchors.topMargin: parent.height * (3.5 / 8)
            anchors.horizontalCenter: parent.horizontalCenter
            width: parent.width
            height: Math.max(1, parent.height * (1.5 / 8))
            radius: height / 2
            color: root.dialGlyphColor
        }

        Rectangle {
            anchors.top: parent.top
            anchors.topMargin: parent.height * (6.5 / 8)
            anchors.horizontalCenter: parent.horizontalCenter
            width: parent.width * 0.5
            height: Math.max(1, parent.height * (1.5 / 8))
            radius: height / 2
            color: root.dialGlyphColor
        }
    }

    Icon {
        id: offlineIcon
        objectName: "miniBlockClockOfflineIcon"
        visible: root.showOfflineState
        anchors.centerIn: parent
        source: "image://images/network-light"
        color: root.pageSelected ? Theme.color.confirmationColors[5] : Theme.color.red
        size: root.width
    }

    Item {
        id: pausedIcon
        objectName: "miniBlockClockPausedIcon"
        visible: root.showPausedState
        width: parent.width
        height: parent.height
        anchors.centerIn: parent

        Rectangle {
            id: pausedRing
            objectName: "miniBlockClockPausedRing"
            anchors.centerIn: parent
            width: Math.round(parent.width - root.pausedStrokeWidth)
            height: width
            radius: width / 2
            color: "transparent"
            border.width: root.pausedStrokeWidth
            border.color: root.pausedGlyphColor
        }

        Row {
            id: pausedBars
            objectName: "miniBlockClockPausedBars"
            anchors.centerIn: pausedRing
            width: implicitWidth
            height: implicitHeight
            spacing: root.pausedBarSpacing

            Rectangle {
                width: root.pausedBarWidth
                height: root.pausedBarHeight
                radius: width / 2
                color: root.pausedGlyphColor
            }

            Rectangle {
                width: root.pausedBarWidth
                height: root.pausedBarHeight
                radius: width / 2
                color: root.pausedGlyphColor
            }
        }
    }

    Item {
        visible: root.faulted
        anchors.fill: parent

        Rectangle {
            anchors.centerIn: parent
            width: parent.width - root.strokeWidth
            height: width
            radius: width / 2
            color: "transparent"
            border.width: root.strokeWidth
            border.color: Theme.color.red
        }

        Rectangle {
            anchors.centerIn: parent
            anchors.verticalCenterOffset: -parent.height * 0.08
            width: Math.max(root.strokeWidth, parent.width * 0.12)
            height: parent.height * 0.34
            radius: width / 2
            color: Theme.color.red
        }

        Rectangle {
            anchors.centerIn: parent
            anchors.verticalCenterOffset: parent.height * 0.22
            width: Math.max(root.strokeWidth, parent.width * 0.12)
            height: width
            radius: width / 2
            color: Theme.color.red
        }
    }
}
