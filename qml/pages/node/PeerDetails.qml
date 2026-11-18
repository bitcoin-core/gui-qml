// Copyright (c) 2024-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0
import "../../controls"
import "../../components"

Page {
    id: root
    objectName: "peerDetails"

    signal back
    signal peerDisconnected(string connectionId)

    property PeerDetailsModel details
    property bool compact: SizeClass.isCompact(width)
    property Item popupParent: null
    property int sectionIndex: 0
    property PeerCommandOperation operation: null
    readonly property bool confirmationVisible: banConfirmation.visible || disconnectConfirmation.visible
    readonly property bool actionPending: operation !== null && operation.pending
    readonly property string operationError: operation === null ? ""
        : operation.queryError.length > 0 ? operation.queryError : operation.errorText
    readonly property string observationText: operation !== null && operation.resultPresent && operation.result.observationAvailable
        ? operation.result.targetConnected ? qsTr("The original connection is still connected.") : qsTr("The original connection is no longer connected.") : ""
    property real maximumContentWidth: 840
    property real contentHorizontalPadding: Theme.space.pagePadding(width)

    readonly property var informationRows: [
        { label: qsTr("Address"), value: available(details ? details.address : ""), mono: true },
        { label: qsTr("Via"), value: available(details ? details.addressLocal : ""), mono: true },
        { label: qsTr("Direction / type"), value: available(details ? details.type : ""), mono: false },
        { label: qsTr("Network / transport"), value: joined([details ? details.network : "", details ? details.transport : ""]), mono: false },
        { label: qsTr("Session ID"), value: available(details ? details.sessionId : ""), mono: true },
        { label: qsTr("Permissions"), value: defaultValue(details ? details.permission : "", qsTr("Default")), mono: false },
        { label: qsTr("Version"), value: available(details ? details.version : ""), mono: true },
        { label: qsTr("User agent"), value: available(details ? details.userAgent : ""), mono: true },
        { label: qsTr("Services"), value: servicesValue(), mono: false },
        { label: qsTr("Transaction relay"), value: yesNo(details && details.transactionRelay), mono: false },
        { label: qsTr("Mapped AS"), value: mappedAsValue(), mono: true }
    ]
    readonly property var blockRelayRows: [
        { label: qsTr("Starting block"), value: available(details ? details.startingHeight : ""), mono: true },
        { label: qsTr("Synced headers"), value: heightValue(details ? details.syncedHeaders : ""), mono: true },
        { label: qsTr("Synced blocks"), value: heightValue(details ? details.syncedBlocks : ""), mono: true },
        { label: qsTr("High bandwidth"), value: yesNo(details && details.highBandwidth), mono: false }
    ]
    readonly property var addressRelayRows: {
        const rows = [
            { label: qsTr("Address relay"), value: yesNo(details && details.addressRelay), mono: false }
        ]
        if (details && details.addressRelay) {
            rows.push({ label: qsTr("Addresses processed"), value: available(details.addressesProcessed), mono: true })
            rows.push({ label: qsTr("Addresses rate-limited"), value: available(details.addressesRateLimited), mono: true })
        }
        return rows
    }
    readonly property var trafficRows: [
        { label: qsTr("Connection time"), value: available(details ? details.connectionDuration : ""), mono: false },
        { label: qsTr("Last send"), value: elapsedValue(details ? details.lastSend : ""), mono: false },
        { label: qsTr("Last receive"), value: elapsedValue(details ? details.lastReceived : ""), mono: false },
        { label: qsTr("Sent"), value: totalValue(details ? details.bytesSent : ""), mono: true },
        { label: qsTr("Received"), value: totalValue(details ? details.bytesReceived : ""), mono: true },
        { label: qsTr("Ping time"), value: available(details ? details.pingTime : ""), mono: false },
        { label: qsTr("Ping wait"), value: available(details ? details.pingWait : ""), mono: false },
        { label: qsTr("Minimum ping"), value: available(details ? details.pingMin : ""), mono: false },
        { label: qsTr("Time offset"), value: available(details ? details.timeOffset : ""), mono: false }
    ]

    background: Rectangle { color: Theme.color.background }

    function unavailable(value) {
        return value === undefined || value === null || String(value).length === 0
    }
    function available(value) { return unavailable(value) ? "—" : value }
    function defaultValue(value, fallback) { return unavailable(value) ? fallback : value }
    function yesNo(value) { return value ? qsTr("Yes") : qsTr("No") }
    function joined(values) {
        const present = []
        for (let i = 0; i < values.length; ++i) if (!unavailable(values[i])) present.push(values[i])
        return present.length > 0 ? present.join(" · ") : "—"
    }
    function servicesValue() {
        if (!details || unavailable(details.services)) return "—"
        return details.services.replace(/\|/g, " · ").replace(/, /g, " · ")
    }
    function mappedAsValue() {
        if (!details || unavailable(details.mappedAS)) return "—"
        return String(details.mappedAS).indexOf("AS") === 0 ? details.mappedAS : "AS" + details.mappedAS
    }
    function heightValue(value) {
        return unavailable(value) || Number(value) < 0 ? "—" : Number(value).toLocaleString(Qt.locale(), "f", 0)
    }
    function elapsedValue(value) { return unavailable(value) ? "—" : qsTr("%1 ago").arg(value) }
    function totalValue(value) { return unavailable(value) ? "—" : qsTr("%1 total").arg(value) }
    function replaceOperation(captured) {
        if (captured === null) return false
        if (confirmationVisible || (operation !== null && (operation.confirmationRequired || operation.pending))) {
            captured.retire()
            return false
        }
        if (operation !== null) operation.retire()
        operation = captured
        return true
    }
    function requestDisconnect() {
        if (!confirmationVisible && details !== null && details.canDisconnect && replaceOperation(details.prepareDisconnect())) {
            disconnectConfirmation.originalOperation = operation
            disconnectConfirmation.open()
        }
    }
    function requestBan(duration, label) {
        if (!confirmationVisible && details !== null && details.canBan && replaceOperation(details.prepareBan(duration))) {
            banConfirmation.originalOperation = operation
            banConfirmation.durationLabel = label
            banConfirmation.open()
        }
    }
    function disconnectPeer() { if (disconnectConfirmation.originalOperation !== null) disconnectConfirmation.originalOperation.confirm() }
    function confirmBan() { if (banConfirmation.originalOperation !== null) banConfirmation.originalOperation.confirm() }
    Connections {
        target: root.operation
        function onChanged() {
            if (root.visible && root.operation.resultPresent && !root.operation.pending && root.operationError.length > 0)
                peerActionError.open()
        }
    }
    Connections {
        target: root.details
        function onDisconnected() {
            if (root.details !== null) root.peerDisconnected(root.details.connectionId)
        }
    }
    Component.onDestruction: if (operation !== null) operation.retire()

    Item {
        objectName: "peerDetailsContentFrame"
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.topMargin: root.compact ? 12 : 28
        anchors.bottomMargin: root.compact ? 16 : 28
        width: Math.max(0, Math.min(
            parent.width - root.contentHorizontalPadding * 2,
            root.maximumContentWidth))

        ColumnLayout {
            anchors.fill: parent
            spacing: 16

            NavButton {
                objectName: "peerDetailsBackButton"
                visible: root.compact
                Layout.alignment: Qt.AlignLeft
                Layout.leftMargin: -10
                Layout.preferredHeight: visible ? implicitHeight : 0
                iconSource: "image://images/caret-left"
                text: qsTr("Peers")
                bold: false
                onClicked: root.back()
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    spacing: 3
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10
                        CoreText {
                            text: root.details ? qsTr("Peer #%1").arg(root.details.nodeIdText) : qsTr("Peer")
                            textStyle: Theme.text.headline
                            color: Theme.color.neutral9
                            horizontalAlignment: Text.AlignLeft
                            wrap: false
                        }
                    }
                    CoreText {
                        Layout.fillWidth: true
                        text: root.details ? root.details.address : ""
                        textStyle: Theme.text.monoDescription
                        color: Theme.color.neutral6
                        horizontalAlignment: Text.AlignLeft
                        elide: Text.ElideMiddle
                        wrap: false
                    }
                }

                OverflowMenuButton {
                    id: actionButton
                    objectName: "peerActionsButton"
                    Layout.alignment: Qt.AlignTop
                    Accessible.name: qsTr("Peer actions")
                    enabled: root.details !== null && (root.details.canBan || root.details.canDisconnect) && !root.actionPending && !root.confirmationVisible
                    checked: actionMenu.opened
                    onClicked: actionMenu.opened ? actionMenu.close() : actionMenu.open()
                    PeerActionsMenu {
                        id: actionMenu
                        objectName: "peerActionsMenu"
                        canBan: root.details !== null && root.details.canBan && !root.actionPending && !root.confirmationVisible
                        canDisconnect: root.details !== null && root.details.canDisconnect && !root.actionPending && !root.confirmationVisible
                        y: actionButton.height + 4
                        x: actionButton.width - width
                        onBanRequested: (duration, label) => root.requestBan(duration, label)
                        onDisconnectRequested: root.requestDisconnect()
                    }
                }
            }

            CoreText {
                objectName: "peerCommandStatus"
                Layout.fillWidth: true
                visible: root.operation !== null && (root.operation.pending || root.operation.resultPresent)
                text: root.operation === null ? "" : root.operation.queryPending ? qsTr("Refreshing the original peer state…")
                    : root.operation.pending ? qsTr("Waiting for the original peer command…")
                    : !root.operation.outcomeKnown ? qsTr("The original command outcome is unknown. Refresh its state before deciding what to do next.")
                    : root.operation.result.outcome === PeerCommandOperation.PartiallyApplied ? qsTr("The original command was only partially applied.") : ""
                font: Theme.text.description.font
                color: Theme.color.neutral7
            }
            CoreText {
                objectName: "peerCommandObservation"
                Layout.fillWidth: true
                visible: root.observationText.length > 0
                text: root.observationText
                font: Theme.text.description.font
                color: Theme.color.neutral7
            }
            TextButton {
                objectName: "peerCommandReconcileButton"
                Layout.alignment: Qt.AlignLeft
                visible: root.operation !== null && root.operation.resultPresent && !root.operation.outcomeKnown
                enabled: root.operation !== null && !root.operation.pending
                text: qsTr("Refresh original peer state")
                onClicked: root.operation.reconcile()
            }

            SegmentedPicker {
                objectName: "peerDetailsSections"
                Accessible.name: qsTr("Peer details")
                Layout.fillWidth: true
                model: [qsTr("Information"), qsTr("Relay data"), qsTr("Network traffic")]
                currentIndex: root.sectionIndex
                onSelected: (index) => root.sectionIndex = index
            }

            ScrollView {
                id: tableScroll
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                contentWidth: availableWidth

                Column {
                    width: tableScroll.availableWidth
                    spacing: 16

                    PeerDetailsTable {
                        compact: root.compact
                        objectName: "peerInformationTable"
                        width: parent.width
                        visible: root.sectionIndex === 0
                        rows: root.informationRows
                    }

                    Column {
                        width: parent.width
                        visible: root.sectionIndex === 1
                        spacing: 16

                        PeerDetailsTable {
                            compact: root.compact
                            objectName: "peerBlocksSection"
                            width: parent.width
                            title: qsTr("Blocks")
                            rows: root.blockRelayRows
                        }
                        PeerDetailsTable {
                            compact: root.compact
                            objectName: "peerAddressesSection"
                            width: parent.width
                            title: qsTr("Addresses")
                            rows: root.addressRelayRows
                        }
                    }

                    PeerDetailsTable {
                        compact: root.compact
                        objectName: "peerNetworkTrafficTable"
                        width: parent.width
                        visible: root.sectionIndex === 2
                        rows: root.trafficRows
                    }
                }
            }
        }
    }

    AlertPopup {
        id: banConfirmation
        objectName: "banConfirmationPopup"
        property PeerCommandOperation originalOperation: null
        property string durationLabel: qsTr("1 hour")
        onClosed: {
            const original = banConfirmation.originalOperation
            if (original !== null && original.confirmationRequired) original.cancel()
        }
        parent: root.popupParent ? root.popupParent : root
        title: qsTr("Ban peer?")
        message: banConfirmation.originalOperation !== null
            ? qsTr("Ban %1 for %2?").arg(banConfirmation.originalOperation.address).arg(banConfirmation.durationLabel)
            : qsTr("Ban this peer for %1?").arg(banConfirmation.durationLabel)
        AlertAction {
            text: qsTr("Cancel")
            role: AlertAction.Cancel
            buttonObjectName: "banCancelButton"
            onTriggered: if (banConfirmation.originalOperation !== null) banConfirmation.originalOperation.cancel()
        }
        AlertAction {
            text: qsTr("Ban")
            role: AlertAction.Destructive
            buttonObjectName: "banConfirmButton"
            enabled: banConfirmation.originalOperation !== null && banConfirmation.originalOperation.confirmationRequired
            // Confirm before closing: closing first would cancel the unconfirmed operation.
            closesPopup: false
            onTriggered: {
                root.confirmBan()
                banConfirmation.close()
            }
        }
    }

    AlertPopup {
        id: disconnectConfirmation
        objectName: "disconnectConfirmationPopup"
        property PeerCommandOperation originalOperation: null
        onClosed: {
            const original = disconnectConfirmation.originalOperation
            if (original !== null && original.confirmationRequired) original.cancel()
        }
        parent: root.popupParent ? root.popupParent : root
        title: qsTr("Disconnect peer?")
        message: disconnectConfirmation.originalOperation !== null
            ? qsTr("Disconnect from %1? The peer may reconnect automatically.").arg(disconnectConfirmation.originalOperation.address)
            : qsTr("Disconnect this peer? The peer may reconnect automatically.")
        AlertAction {
            text: qsTr("Cancel")
            role: AlertAction.Cancel
            buttonObjectName: "disconnectCancelButton"
            onTriggered: if (disconnectConfirmation.originalOperation !== null) disconnectConfirmation.originalOperation.cancel()
        }
        AlertAction {
            text: qsTr("Disconnect")
            role: AlertAction.Destructive
            buttonObjectName: "disconnectConfirmButton"
            enabled: disconnectConfirmation.originalOperation !== null && disconnectConfirmation.originalOperation.confirmationRequired
            // Confirm before closing: closing first would cancel the unconfirmed operation.
            closesPopup: false
            onTriggered: {
                root.disconnectPeer()
                disconnectConfirmation.close()
            }
        }
    }

    AlertPopup {
        id: peerActionError
        objectName: "peerActionErrorPopup"
        parent: root.popupParent ? root.popupParent : root
        title: qsTr("Peer action failed")
        messageObjectName: "actionErrorMessage"
        message: root.operationError
        AlertAction { text: qsTr("OK"); buttonObjectName: "actionErrorCloseButton" }
    }

}
