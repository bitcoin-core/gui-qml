// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

import "../controls"

RowLayout {
    id: root
    objectName: "nodeStatusBar"

    property NodeStatus status: NodeStatus {}
    property bool showStatus: true
    readonly property alias pauseConfirmation: pauseConfirmation
    signal routeRequested(string action)
    signal warningsRequested()

    spacing: Theme.space.m

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
        spacing: Theme.space.ml
        Layout.alignment: Qt.AlignVCenter
        Layout.rightMargin: Theme.space.m

        Rectangle {
            Layout.alignment: Qt.AlignVCenter
            implicitWidth: Theme.space.m
            implicitHeight: Theme.space.m
            radius: height / 2
            color: root.status.tone
        }
        CoreText {
            objectName: "nodeStatusBarLabel"
            text: root.status.label
            textStyle: Theme.text.caption
            wrap: false
        }
        WarningsLink {
            objectName: "nodeStatusBarWarnings"
            status: root.status
            onClicked: root.warningsRequested()
        }
        CoreText {
            text: "·"
            textStyle: Theme.text.caption
            wrap: false
            color: Theme.color.neutral6
            Accessible.ignored: true
        }
        CoreText {
            text: root.status.peersText
            textStyle: Theme.text.caption
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

    AlertPopup {
        id: pauseConfirmation
        objectName: "pauseNetworkConfirmation"
        parent: Overlay.overlay
        title: qsTr("Pause network activity?")
        message: root.status.pauseText
        messageObjectName: "pauseNetworkConfirmationText"

        AlertAction {
            text: qsTr("Cancel")
            role: AlertAction.Cancel
            buttonObjectName: "pauseNetworkCancelButton"
        }
        AlertAction {
            text: qsTr("Pause")
            buttonObjectName: "pauseNetworkConfirmButton"
            closesPopup: false
            onTriggered: root.confirmPause()
        }
    }
}
