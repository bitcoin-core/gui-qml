// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

import "../controls"

// Node status with the action for the current state. Pausing asks first.
RowLayout {
    id: root
    objectName: "nodeStatusBar"

    property NodeStatus status: NodeStatus {}
    property bool showStatus: true
    readonly property alias pauseConfirmation: pauseConfirmation
    // Actions the page routes, such as "networkSettings" and "debugLog".
    signal routeRequested(string action)
    signal warningsRequested()

    spacing: 8

    function trigger(action) {
        const network = action === "pause" || action === "resume"
        if (network && (!root.status.networkActionReady || root.status.networkActionPending)) return
        if (action === "pause") {
            pauseConfirmation.open()
        } else if (action === "resume") {
            if (root.status.nodeModelRef !== null) root.status.nodeModelRef.pause = false
        } else if (action === "shutdown") {
            if (root.status.nodeModelRef !== null) root.status.nodeModelRef.requestShutdown()
        } else if (action === "warnings") {
            root.warningsRequested()
        } else if (action.length > 0) {
            root.routeRequested(action)
        }
    }

    function confirmPause() {
        pauseConfirmation.close()
        // Readiness can change while the confirmation is open.
        if (!root.status.networkActionReady || root.status.networkActionPending) return
        if (root.status.nodeModelRef !== null) root.status.nodeModelRef.pause = true
    }

    Connections {
        target: root.status.nodeModelRef
        ignoreUnknownSignals: true
        function onNetworkActionErrorChanged() {
            if (root.visible && root.status.networkActionError.length > 0) networkError.open()
        }
    }

    AlertPopup {
        id: networkError
        objectName: "nodeStatusNetworkError"
        parent: Overlay.overlay
        title: qsTr("Network action failed")
        message: root.status.networkActionError
    }

    RowLayout {
        visible: root.showStatus
        spacing: 10
        Layout.alignment: Qt.AlignVCenter
        Layout.rightMargin: 8

        Rectangle {
            Layout.alignment: Qt.AlignVCenter
            implicitWidth: 8
            implicitHeight: 8
            radius: 4
            color: root.status.tone
        }
        CoreText {
            objectName: "nodeStatusBarLabel"
            text: root.status.label
            font.pixelSize: 14
            wrap: false
        }
        WarningsLink {
            objectName: "nodeStatusBarWarnings"
            status: root.status
            onClicked: root.warningsRequested()
        }
        CoreText {
            text: "·"
            font.pixelSize: 14
            wrap: false
            color: Theme.color.neutral5
        }
        CoreText {
            text: root.status.peersText
            font.pixelSize: 14
            wrap: false
            color: Theme.color.neutral7
        }
    }

    NodeActionButton {
        objectName: "nodeStatusSecondaryAction"
        visible: root.status.secondaryAction.length > 0
        text: root.status.secondaryActionText
        onClicked: root.trigger(root.status.secondaryAction)
    }

    NodeActionButton {
        objectName: "nodeStatusPrimaryAction"
        text: root.status.primaryActionText
        accent: root.status.primaryActionAccent
        enabled: root.status.primaryActionEnabled
        busy: root.status.primaryActionIsNetwork && root.status.networkActionPending
        onClicked: root.trigger(root.status.primaryAction)
    }

    Popup {
        id: pauseConfirmation
        objectName: "pauseNetworkConfirmation"
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        width: 400
        padding: 24
        background: Rectangle {
            color: Theme.color.neutral1
            radius: 12
            border.width: 1
            border.color: Theme.color.neutral3
        }

        contentItem: ColumnLayout {
            spacing: 12

            CoreText {
                Layout.fillWidth: true
                text: qsTr("Pause network activity?")
                bold: true
                font.pixelSize: 18
                horizontalAlignment: Text.AlignLeft
            }
            CoreText {
                objectName: "pauseNetworkConfirmationText"
                Layout.fillWidth: true
                text: root.status.pauseText
                font.pixelSize: 14
                lineHeight: 1.5
                color: Theme.color.neutral7
                horizontalAlignment: Text.AlignLeft
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 8
                spacing: 8
                Item { Layout.fillWidth: true }
                NodeActionButton {
                    objectName: "pauseNetworkCancelButton"
                    implicitHeight: 40
                    text: qsTr("Cancel")
                    onClicked: pauseConfirmation.close()
                }
                NodeActionButton {
                    objectName: "pauseNetworkConfirmButton"
                    implicitHeight: 40
                    accent: true
                    text: qsTr("Pause")
                    onClicked: root.confirmPause()
                }
            }
        }
    }
}
