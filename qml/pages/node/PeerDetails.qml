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
    signal peerDisconnected(int nodeId)

    property PeerDetailsModel details
    property bool compact: width <= SizeClass.compactWidthMax
    property Item popupParent: null
    property int sectionIndex: 0
    property int pendingBanDuration: 3600
    property string pendingBanLabel: qsTr("1 hour")
    property real maximumContentWidth: 840
    property real contentHorizontalPadding: width >= 900 ? 56 : width >= 640 ? 40 : 24

    readonly property var informationRows: [
        { label: qsTr("Address"), value: available(details ? details.address : ""), mono: true },
        { label: qsTr("Via"), value: available(details ? details.addressLocal : ""), mono: true },
        { label: qsTr("Direction / type"), value: directionAndType(), mono: false },
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

    background: Rectangle { color: Theme.color.neutral0 }

    function unavailable(value) {
        return value === undefined || value === null || String(value).length === 0 || value === "N/A"
    }
    function available(value) { return unavailable(value) ? "—" : value }
    function defaultValue(value, fallback) { return unavailable(value) ? fallback : value }
    function yesNo(value) { return value ? qsTr("Yes") : qsTr("No") }
    function joined(values) {
        const present = []
        for (let i = 0; i < values.length; ++i) if (!unavailable(values[i])) present.push(values[i])
        return present.length > 0 ? present.join(" · ") : "—"
    }
    function directionAndType() {
        if (!details) return "—"
        let type = details.type
        if (type.indexOf(details.direction) === 0) type = type.slice(details.direction.length).trim()
        return joined([details.direction, type])
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
    function showActionError(message) {
        peerActionError.message = message
        peerActionError.open()
    }
    function disconnectPeer() {
        if (details && nodeModel.disconnectPeer(details.nodeId)) {
            peerTableModel.refresh()
        } else {
            showActionError(qsTr("Could not disconnect peer. The peer may already be disconnected or the node state may have changed."))
        }
    }
    function requestDisconnect() {
        disconnectConfirmation.open()
    }
    function requestBan(duration, label) {
        pendingBanDuration = duration
        pendingBanLabel = label
        banConfirmation.open()
    }
    function confirmBan() {
        if (details && nodeModel.banPeer(details.rawAddress, pendingBanDuration)) {
            peerTableModel.refresh()
            banListModel.refresh()
        } else {
            showActionError(qsTr("Could not ban peer. The peer may already be disconnected or the node state may have changed."))
        }
    }

    Connections {
        target: details
        function onDisconnected() {
            const disconnectedId = root.details ? root.details.nodeId : -1
            root.peerDisconnected(disconnectedId)
        }
    }

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
                            text: details ? qsTr("Peer #%1").arg(details.nodeId) : qsTr("Peer")
                            font: Theme.text.headline.font
                            lineHeight: Theme.text.headline.lineHeight
                            lineHeightMode: Text.FixedHeight
                            color: Theme.color.neutral9
                            horizontalAlignment: Text.AlignLeft
                            wrap: false
                        }
                    }
                    CoreText {
                        Layout.fillWidth: true
                        text: details ? details.address : ""
                        font: Theme.text.monoDescription.font
                        lineHeight: Theme.text.monoDescription.lineHeight
                        lineHeightMode: Text.FixedHeight
                        color: Theme.color.neutral6
                        horizontalAlignment: Text.AlignLeft
                        elide: Text.ElideMiddle
                        wrap: false
                    }
                }

                IconButton {
                    id: actionButton
                    objectName: "peerActionsButton"
                    Layout.alignment: Qt.AlignTop
                    size: 40
                    iconSize: 20
                    focusPolicy: Qt.StrongFocus
                    iconSource: "image://images/ellipsis"
                    iconColor: Theme.color.neutral8
                    onClicked: actionMenu.open()
                    FocusBorder {
                        objectName: "peerActionsButtonFocusBorder"
                        visible: actionButton.visualFocus
                        borderRadius: 12
                        z: 1
                    }
                    PeerActionsMenu {
                        id: actionMenu
                        objectName: "peerActionsMenu"
                        y: actionButton.height + 4
                        x: actionButton.width - width
                        onBanRequested: (duration, label) => root.requestBan(duration, label)
                        onDisconnectRequested: root.requestDisconnect()
                    }
                }
            }

            SegmentedPicker {
                objectName: "peerDetailsSections"
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

                    PeerTable {
                        objectName: "peerInformationTable"
                        width: parent.width
                        visible: root.sectionIndex === 0
                        rows: root.informationRows
                    }

                    Column {
                        width: parent.width
                        visible: root.sectionIndex === 1
                        spacing: 16

                        PeerSection {
                            objectName: "peerBlocksSection"
                            width: parent.width
                            title: qsTr("Blocks")
                            rows: root.blockRelayRows
                        }
                        PeerSection {
                            objectName: "peerAddressesSection"
                            width: parent.width
                            title: qsTr("Addresses")
                            rows: root.addressRelayRows
                        }
                    }

                    PeerTable {
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
        parent: root.popupParent ? root.popupParent : root
        title: qsTr("Ban peer?")
        message: details
            ? qsTr("Ban %1 for %2?").arg(details.address).arg(root.pendingBanLabel)
            : qsTr("Ban this peer for %1?").arg(root.pendingBanLabel)
        AlertAction {
            text: qsTr("Cancel")
            role: AlertAction.Cancel
            buttonObjectName: "banCancelButton"
        }
        AlertAction {
            text: qsTr("Ban")
            role: AlertAction.Destructive
            buttonObjectName: "banConfirmButton"
            onTriggered: root.confirmBan()
        }
    }

    AlertPopup {
        id: disconnectConfirmation
        objectName: "disconnectConfirmationPopup"
        parent: root.popupParent ? root.popupParent : root
        title: qsTr("Disconnect peer?")
        message: details
            ? qsTr("Disconnect from %1? The peer may reconnect automatically.").arg(details.address)
            : qsTr("Disconnect this peer? The peer may reconnect automatically.")
        AlertAction {
            text: qsTr("Cancel")
            role: AlertAction.Cancel
            buttonObjectName: "disconnectCancelButton"
        }
        AlertAction {
            text: qsTr("Disconnect")
            role: AlertAction.Destructive
            buttonObjectName: "disconnectConfirmButton"
            onTriggered: root.disconnectPeer()
        }
    }

    AlertPopup {
        id: peerActionError
        objectName: "peerActionErrorPopup"
        parent: root.popupParent ? root.popupParent : root
        title: qsTr("Peer action failed")
        messageObjectName: "actionErrorMessage"
        AlertAction { text: qsTr("OK"); buttonObjectName: "actionErrorCloseButton" }
    }

    component PeerTable: Rectangle {
        id: table
        required property var rows
        implicitHeight: tableColumn.implicitHeight
        color: Theme.color.neutral1
        radius: 12
        border.width: 0
        clip: true
        ColumnLayout {
            id: tableColumn
            width: parent.width
            spacing: 0
            Repeater {
                model: table.rows
                delegate: ColumnLayout {
                    id: rowDelegate
                    required property int index
                    required property var modelData
                    Layout.fillWidth: true
                    spacing: 0
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: root.compact ? 14 : 20
                        Layout.rightMargin: root.compact ? 14 : 20
                        Layout.minimumHeight: root.compact ? 50 : 48
                        spacing: 16
                        CoreText {
                            Layout.preferredWidth: root.compact ? 112 : 150
                            text: modelData.label
                            font: Theme.text.description.font
                            lineHeight: Theme.text.description.lineHeight
                            lineHeightMode: Text.FixedHeight
                            color: Theme.color.neutral6
                            horizontalAlignment: Text.AlignLeft
                            wrap: false
                        }
                        CoreText {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            text: modelData.value
                            font: modelData.mono ? Theme.text.monoDescription.font : Theme.text.description.font
                            lineHeight: modelData.mono ? Theme.text.monoDescription.lineHeight : Theme.text.description.lineHeight
                            lineHeightMode: Text.FixedHeight
                            color: Theme.color.neutral8
                            horizontalAlignment: Text.AlignLeft
                            elide: Text.ElideMiddle
                            wrap: false
                        }
                    }
                    Separator {
                        Layout.fillWidth: true
                        Layout.leftMargin: root.compact ? 14 : 20
                        Layout.rightMargin: root.compact ? 14 : 20
                        visible: rowDelegate.index < table.rows.length - 1
                        color: Theme.color.neutral2
                    }
                }
            }
        }
    }

    component PeerSection: Column {
        required property string title
        required property var rows
        spacing: 8

        CoreText {
            objectName: parent.objectName + "Title"
            width: parent.width
            leftPadding: root.compact ? 4 : 8
            text: parent.title
            font: Theme.text.subheading.font
            lineHeight: Theme.text.subheading.lineHeight
            lineHeightMode: Text.FixedHeight
            color: Theme.color.neutral8
            horizontalAlignment: Text.AlignLeft
        }
        PeerTable {
            width: parent.width
            rows: parent.rows
        }
    }
}
