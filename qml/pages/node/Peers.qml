// Copyright (c) 2023-2026 The Bitcoin Core developers
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
    objectName: "peersList"

    signal back
    signal peerSelected(PeerDetailsModel peerDetails)

    property bool compact: width <= SizeClass.compactWidthMax
    property bool showHeader: false
    property bool showBackButton: true
    property Item popupParent: null
    property int selectedNodeId: -1
    property var contextPeerDetails: null
    property int pendingContextBanDuration: 3600
    property string pendingContextBanLabel: qsTr("1 hour")

    readonly property var directionOptions: [
        { text: qsTr("All"), value: "" },
        { text: qsTr("Inbound"), value: "inbound" },
        { text: qsTr("Outbound"), value: "outbound" }
    ]
    readonly property var connectionTypeOptions: [
        { text: qsTr("All"), value: "" },
        { text: qsTr("Full relay"), value: "full-relay" },
        { text: qsTr("Block relay"), value: "block-relay" },
        { text: qsTr("Manual"), value: "manual" }
    ]
    readonly property var networkOptions: [
        { text: qsTr("All"), value: "" },
        { text: qsTr("IPv4"), value: "ipv4" },
        { text: qsTr("IPv6"), value: "ipv6" },
        { text: qsTr("Onion"), value: "onion" },
        { text: qsTr("I2P"), value: "i2p" }
    ]
    readonly property var transportOptions: [
        { text: qsTr("All"), value: "" },
        { text: qsTr("v1"), value: "v1" },
        { text: qsTr("v2"), value: "v2" }
    ]
    readonly property var sortOptions: [
        { text: qsTr("Peer ID"), value: "nodeId" },
        { text: qsTr("Address"), value: "address" },
        { text: qsTr("Connection type"), value: "connectionType" },
        { text: qsTr("User agent"), value: "subversion" },
        { text: qsTr("Sent"), value: "sent" },
        { text: qsTr("Received"), value: "received" }
    ]

    background: Rectangle { color: Theme.color.neutral0 }

    header: NavigationBar2 {
        visible: root.showHeader
        leftItem: NavButton {
            objectName: "peersBackButton"
            visible: root.showBackButton
            iconSource: "image://images/caret-left"
            text: qsTr("Back")
            onClicked: root.back()
        }
        centerItem: Header {
            headerBold: true
            headerSize: 18
            header: qsTr("Peers")
        }
    }

    AppSettings {
        id: settings
        property string peerListSortBy: "nodeId"
        property bool peerListSortAscending: true
        property string peerDirectionFilters: ""
        property string peerConnectionTypeFilters: ""
        property string peerNetworkFilters: ""
        property string peerTransportFilters: ""
    }

    function decodeFilter(value, options) {
        if (value.length === 0) return []
        const candidate = value.split(",")[0]
        for (let i = 0; i < options.length; ++i) {
            if (options[i].value === candidate) return candidate.length === 0 ? [] : [candidate]
        }
        return []
    }
    function selectedFilter(filters) { return filters.length > 0 ? filters[0] : "" }
    function validSort(value) {
        for (let i = 0; i < sortOptions.length; ++i) {
            if (sortOptions[i].value === value) return value
        }
        return "nodeId"
    }
    function setFilter(group, value) {
        const values = value.length === 0 ? [] : [value]
        if (group === "direction") peerListModelProxy.directionFilters = values
        else if (group === "connectionType") peerListModelProxy.connectionTypeFilters = values
        else if (group === "network") peerListModelProxy.networkFilters = values
        else if (group === "transport") peerListModelProxy.transportFilters = values
        storeFilters()
    }
    function storeFilters() {
        settings.peerDirectionFilters = peerListModelProxy.directionFilters.join(",")
        settings.peerConnectionTypeFilters = peerListModelProxy.connectionTypeFilters.join(",")
        settings.peerNetworkFilters = peerListModelProxy.networkFilters.join(",")
        settings.peerTransportFilters = peerListModelProxy.transportFilters.join(",")
    }
    function filterCount() {
        return peerListModelProxy.directionFilters.length
            + peerListModelProxy.connectionTypeFilters.length
            + peerListModelProxy.networkFilters.length
            + peerListModelProxy.transportFilters.length
    }
    function joinedDetails(values) {
        const present = []
        for (let i = 0; i < values.length; ++i) {
            if (values[i] !== undefined && values[i] !== null && String(values[i]).length > 0) present.push(values[i])
        }
        return present.join(" · ")
    }
    function openPeerActionsMenu(peerDetails, sourceItem, localPosition) {
        if (!peerDetails) return
        contextPeerDetails = peerDetails
        const position = sourceItem.mapToItem(
            root,
            localPosition.x,
            localPosition.y)
        peerRowActionsMenu.x = Math.max(12, Math.min(
            position.x,
            root.width - peerRowActionsMenu.width - 12))
        peerRowActionsMenu.y = Math.max(12, Math.min(
            position.y,
            root.height - peerRowActionsMenu.height - 12))
        peerRowActionsMenu.open()
    }
    function requestContextPeerBan(duration, label) {
        pendingContextBanDuration = duration
        pendingContextBanLabel = label
        peerListBanConfirmation.open()
    }
    function confirmContextPeerBan() {
        if (contextPeerDetails
                && nodeModel.banPeer(contextPeerDetails.rawAddress, pendingContextBanDuration)) {
            peerTableModel.refresh()
            banListModel.refresh()
        } else {
            showPeerListActionError(qsTr("Could not ban peer. The peer may already be disconnected or the node state may have changed."))
        }
    }
    function disconnectContextPeer() {
        if (contextPeerDetails && nodeModel.disconnectPeer(contextPeerDetails.nodeId)) {
            peerTableModel.refresh()
        } else {
            showPeerListActionError(qsTr("Could not disconnect peer. The peer may already be disconnected or the node state may have changed."))
        }
    }
    function requestContextPeerDisconnect() {
        peerListDisconnectConfirmation.open()
    }
    function showPeerListActionError(message) {
        peerListActionError.message = message
        peerListActionError.open()
    }

    Component.onCompleted: {
        peerListModelProxy.searchText = ""
        peerListModelProxy.sortAscending = settings.peerListSortAscending
        peerListModelProxy.sortBy = validSort(settings.peerListSortBy)
        settings.peerListSortBy = peerListModelProxy.sortBy
        peerListModelProxy.directionFilters = decodeFilter(settings.peerDirectionFilters, directionOptions)
        peerListModelProxy.connectionTypeFilters = decodeFilter(settings.peerConnectionTypeFilters, connectionTypeOptions)
        peerListModelProxy.networkFilters = decodeFilter(settings.peerNetworkFilters, networkOptions)
        peerListModelProxy.transportFilters = decodeFilter(settings.peerTransportFilters, transportOptions)
        storeFilters()
    }

    Item {
        objectName: "peersContentFrame"
        anchors.fill: parent
        anchors.leftMargin: root.compact ? 14 : 24
        anchors.rightMargin: root.compact ? 14 : 24
        anchors.topMargin: root.compact ? 16 : 28
        anchors.bottomMargin: root.compact ? 16 : 24

        ColumnLayout {
            anchors.fill: parent
            spacing: 16

            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    CoreText {
                        Layout.fillWidth: true
                        text: qsTr("Peers")
                        font: Theme.text.headline.font
                        lineHeight: Theme.text.headline.lineHeight
                        lineHeightMode: Text.FixedHeight
                        horizontalAlignment: Text.AlignLeft
                        color: Theme.color.neutral9
                    }
                    CoreText {
                        objectName: "peersDescriptionLabel"
                        Layout.fillWidth: true
                        text: qsTr("Peers are nodes you exchange transaction data with.")
                        font: Theme.text.description.font
                        lineHeight: Theme.text.description.lineHeight
                        lineHeightMode: Text.FixedHeight
                        horizontalAlignment: Text.AlignLeft
                        color: Theme.color.neutral6
                        wrapMode: Text.WordWrap
                    }
                }
            }

            InfoBanner {
                objectName: "peersOfflineBanner"
                Layout.fillWidth: true
                visible: typeof networkStatusModel !== "undefined" && networkStatusModel.networkOffline
                iconSource: "image://images/network-light"
                title: qsTr("No network connection")
                message: qsTr("Peer connections will resume when your device is back online.")
                contentMargin: 16
                bannerLayout: InfoBanner.Layout.Horizontal
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                TextField {
                    id: searchField
                    objectName: "peerSearchField"
                    Layout.fillWidth: true
                    implicitHeight: 40
                    leftPadding: 42
                    rightPadding: 14
                    placeholderText: qsTr("Search peers")
                    placeholderTextColor: Theme.color.neutral5
                    color: Theme.color.neutral9
                    font: Theme.text.description.font
                    selectByMouse: true
                    activeFocusOnTab: true
                    KeyNavigation.tab: filterButton
                    KeyNavigation.priority: KeyNavigation.BeforeItem
                    onTextChanged: peerListModelProxy.searchText = text
                    background: Rectangle {
                        radius: 8
                        color: Theme.color.neutral1
                        border.width: 0
                    }
                    Icon {
                        anchors.left: parent.left
                        anchors.leftMargin: 14
                        anchors.verticalCenter: parent.verticalCenter
                        source: "image://images/search"
                        color: Theme.color.neutral6
                        size: 18
                    }
                }

                Button {
                    id: filterButton
                    objectName: "peerFilterButton"
                    Layout.preferredWidth: 40
                    Layout.preferredHeight: 40
                    hoverEnabled: AppMode.isDesktop
                    focusPolicy: Qt.StrongFocus
                    KeyNavigation.tab: sortButton
                    KeyNavigation.backtab: searchField
                    KeyNavigation.priority: KeyNavigation.BeforeItem
                    Accessible.name: root.filterCount() > 0
                        ? qsTr("Filter peers, %1 active").arg(root.filterCount()) : qsTr("Filter peers")
                    background: Rectangle {
                        radius: 8
                        color: filterButton.down ? Theme.color.neutral3
                            : filterButton.hovered ? Theme.color.neutral2 : Theme.color.neutral1
                        border.width: 0
                        FocusBorder {
                            objectName: "peerFilterButtonFocusBorder"
                            visible: filterButton.visualFocus
                            borderRadius: 12
                        }
                    }
                    contentItem: Item {
                        Canvas {
                            anchors.centerIn: parent
                            width: 20; height: 18
                            onPaint: {
                                const context = getContext("2d")
                                context.clearRect(0, 0, width, height)
                                context.strokeStyle = Theme.color.neutral8
                                context.lineWidth = 2
                                context.lineCap = "round"
                                context.beginPath()
                                context.moveTo(2, 3); context.lineTo(18, 3)
                                context.moveTo(5, 9); context.lineTo(15, 9)
                                context.moveTo(8, 15); context.lineTo(12, 15)
                                context.stroke()
                            }
                        }
                    }
                    onClicked: filterMenu.open()

                    ContextMenu {
                        id: filterMenu
                        objectName: "peerFilterMenu"
                        y: filterButton.height + 6
                        x: filterButton.width - width
                        minMenuWidth: 260
                        ContextMenuPicker {
                            title: qsTr("Direction")
                            model: root.directionOptions
                            rowHeight: 32
                            currentValue: root.selectedFilter(peerListModelProxy.directionFilters)
                            onActivated: (value) => root.setFilter("direction", value)
                        }
                        ContextMenuDivider { verticalMargin: 3 }
                        ContextMenuPicker {
                            title: qsTr("Connection")
                            model: root.connectionTypeOptions
                            rowHeight: 32
                            currentValue: root.selectedFilter(peerListModelProxy.connectionTypeFilters)
                            onActivated: (value) => root.setFilter("connectionType", value)
                        }
                        ContextMenuDivider { verticalMargin: 3 }
                        ContextMenuPicker {
                            title: qsTr("Network")
                            model: root.networkOptions
                            rowHeight: 32
                            currentValue: root.selectedFilter(peerListModelProxy.networkFilters)
                            onActivated: (value) => root.setFilter("network", value)
                        }
                        ContextMenuDivider { verticalMargin: 3 }
                        ContextMenuPicker {
                            title: qsTr("Transport")
                            model: root.transportOptions
                            rowHeight: 32
                            currentValue: root.selectedFilter(peerListModelProxy.transportFilters)
                            onActivated: (value) => root.setFilter("transport", value)
                        }
                    }
                }

                Button {
                    id: sortButton
                    objectName: "peerSortButton"
                    Layout.preferredWidth: 40
                    Layout.preferredHeight: 40
                    hoverEnabled: AppMode.isDesktop
                    focusPolicy: Qt.StrongFocus
                    KeyNavigation.tab: listView.count > 0 ? listView.itemAtIndex(0) : null
                    KeyNavigation.backtab: filterButton
                    KeyNavigation.priority: KeyNavigation.BeforeItem
                    Accessible.name: qsTr("Sort peers")
                    background: Rectangle {
                        radius: 8
                        color: sortButton.down ? Theme.color.neutral3
                            : sortButton.hovered ? Theme.color.neutral2 : Theme.color.neutral1
                        border.width: 0
                        FocusBorder {
                            objectName: "peerSortButtonFocusBorder"
                            visible: sortButton.visualFocus
                            borderRadius: 12
                        }
                    }
                    contentItem: Icon {
                        source: "image://images/flip-vertical"
                        color: Theme.color.neutral8
                        size: 20
                    }
                    onClicked: sortMenu.open()
                    ContextMenu {
                        id: sortMenu
                        objectName: "peerSortMenu"
                        y: sortButton.height + 6
                        x: sortButton.width - width
                        minMenuWidth: 250
                        ContextMenuPicker {
                            title: qsTr("Sort by")
                            model: root.sortOptions
                            currentValue: peerListModelProxy.sortBy
                            onActivated: (value) => {
                                peerListModelProxy.sortBy = value
                                settings.peerListSortBy = value
                            }
                        }
                        ContextMenuDivider { }
                        ContextMenuPicker {
                            model: [
                                { text: qsTr("Ascending"), value: true },
                                { text: qsTr("Descending"), value: false }
                            ]
                            currentValue: peerListModelProxy.sortAscending
                            onActivated: (value) => {
                                peerListModelProxy.sortAscending = value
                                settings.peerListSortAscending = value
                            }
                        }
                    }
                }
            }

            Rectangle {
                objectName: "peerListCard"
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: 120
                color: Theme.color.neutral1
                radius: 12
                border.width: 0
                clip: true

                ListView {
                    id: listView
                    objectName: "peerListView"
                    anchors.fill: parent
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    model: peerListModelProxy
                    delegate: ItemDelegate {
                        id: delegate
                        required property int index
                        objectName: "peerListItem_" + nodeId
                        required property int nodeId
                        required property string address
                        required property string subversion
                        required property string direction
                        required property string connectionType
                        required property string network
                        required property string transport
                        required property string sent
                        required property string received
                        width: listView.width
                        height: 112
                        leftPadding: 12; rightPadding: 12; topPadding: 12; bottomPadding: 12
                        hoverEnabled: AppMode.isDesktop
                        focusPolicy: Qt.StrongFocus
                        KeyNavigation.backtab: delegate.index === 0
                            ? sortButton : listView.itemAtIndex(delegate.index - 1)
                        KeyNavigation.priority: KeyNavigation.BeforeItem
                        onClicked: root.peerSelected(peerListModelProxy.peerDetailsAt(index))
                        TapHandler {
                            acceptedButtons: Qt.RightButton
                            onTapped: (eventPoint) => root.openPeerActionsMenu(
                                peerListModelProxy.peerDetailsAt(delegate.index),
                                delegate,
                                eventPoint.position)
                        }
                        background: Item {
                            Rectangle {
                                anchors.fill: parent
                                anchors.margins: 4
                                radius: 16
                                color: delegate.down ? Theme.color.neutral3
                                    : ((!root.compact && root.selectedNodeId === delegate.nodeId) || delegate.hovered)
                                        ? Theme.color.neutral2 : "transparent"
                            }
                            Rectangle {
                                anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                                anchors.leftMargin: 12; anchors.rightMargin: 12
                                height: 1
                                visible: delegate.index < peerListModelProxy.count - 1
                                color: Theme.color.neutral2
                            }
                            FocusBorder {
                                objectName: delegate.objectName + "FocusBorder"
                                visible: delegate.visualFocus
                                borderRadius: 18
                                topMargin: 2
                                bottomMargin: 2
                                leftMargin: 2
                                rightMargin: 2
                            }
                        }
                        contentItem: RowLayout {
                            spacing: 12
                            Rectangle {
                                Layout.alignment: Qt.AlignTop
                                Layout.preferredWidth: Math.max(42, idText.implicitWidth + 16)
                                Layout.preferredHeight: 32
                                radius: 8
                                color: Theme.color.neutral2
                                CoreText {
                                    id: idText
                                    anchors.centerIn: parent
                                    text: "#" + delegate.nodeId
                                    font: Theme.text.description.font
                                    lineHeight: Theme.text.description.lineHeight
                                    lineHeightMode: Text.FixedHeight
                                    color: Theme.color.neutral8
                                    wrap: false
                                }
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                spacing: 2
                                CoreText {
                                    Layout.fillWidth: true
                                    text: delegate.address
                                    font: Theme.text.monoDescription.font
                                    lineHeight: Theme.text.monoDescription.lineHeight
                                    lineHeightMode: Text.FixedHeight
                                    horizontalAlignment: Text.AlignLeft
                                    color: Theme.color.neutral9
                                    elide: Text.ElideMiddle
                                    wrap: false
                                }
                                CoreText {
                                    Layout.fillWidth: true
                                    text: root.joinedDetails([delegate.direction,
                                        delegate.connectionType === delegate.direction ? "" : delegate.connectionType,
                                        delegate.network, delegate.transport])
                                    font: Theme.text.description.font
                                    lineHeight: Theme.text.description.lineHeight
                                    lineHeightMode: Text.FixedHeight
                                    horizontalAlignment: Text.AlignLeft
                                    color: Theme.color.neutral7
                                    elide: Text.ElideRight
                                    wrap: false
                                }
                                CoreText {
                                    Layout.fillWidth: true
                                    text: delegate.subversion
                                    font: Theme.text.monoCaption.font
                                    lineHeight: Theme.text.monoCaption.lineHeight
                                    lineHeightMode: Text.FixedHeight
                                    horizontalAlignment: Text.AlignLeft
                                    color: Theme.color.neutral6
                                    elide: Text.ElideRight
                                    wrap: false
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 12

                                    CoreText {
                                        objectName: delegate.objectName + "Sent"
                                        text: "↑ " + delegate.sent
                                        font: Theme.text.monoCaption.font
                                        lineHeight: Theme.text.monoCaption.lineHeight
                                        lineHeightMode: Text.FixedHeight
                                        color: Theme.color.purple
                                        wrap: false
                                    }
                                    CoreText {
                                        objectName: delegate.objectName + "Received"
                                        text: "↓ " + delegate.received
                                        font: Theme.text.monoCaption.font
                                        lineHeight: Theme.text.monoCaption.lineHeight
                                        lineHeightMode: Text.FixedHeight
                                        color: Theme.color.blue
                                        wrap: false
                                    }
                                    Item {
                                        Layout.fillWidth: true
                                    }
                                }
                            }
                            CaretRightIcon {
                                Layout.alignment: Qt.AlignVCenter
                                color: Theme.color.neutral6
                                size: 12
                            }
                        }
                    }
                }
                CoreText {
                    anchors.centerIn: parent
                    visible: peerListModelProxy.count === 0
                    text: searchField.text.length > 0 || root.filterCount() > 0
                        ? qsTr("No peers match your filters") : qsTr("No peers connected")
                    font: Theme.text.description.font
                    lineHeight: Theme.text.description.lineHeight
                    lineHeightMode: Text.FixedHeight
                    color: Theme.color.neutral6
                }
            }

            Column {
                Layout.fillWidth: true
                spacing: 4

                CoreText {
                    objectName: "connectedPeersLabel"
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: qsTr("%1 connected").arg(nodeModel.numPeers)
                    font: Theme.text.caption.font
                    lineHeight: Theme.text.caption.lineHeight
                    lineHeightMode: Text.FixedHeight
                    horizontalAlignment: Text.AlignHCenter
                    color: Theme.color.neutral6
                }

                TextButton {
                    objectName: "viewBannedPeersButton"
                    anchors.horizontalCenter: parent.horizontalCenter
                    visible: banListModel.count > 0
                    text: qsTr("View %1 banned %2").arg(banListModel.count).arg(
                        banListModel.count === 1 ? qsTr("peer") : qsTr("peers"))
                    textSize: 13
                    bold: false
                    onClicked: bannedPeersPopup.open()
                }
            }
        }
    }

    PeerActionsMenu {
        id: peerRowActionsMenu
        objectName: "peerRowActionsMenu"
        onBanRequested: (duration, label) => root.requestContextPeerBan(duration, label)
        onDisconnectRequested: root.requestContextPeerDisconnect()
    }

    AlertPopup {
        id: peerListBanConfirmation
        objectName: "peerListBanConfirmationPopup"
        parent: root.popupParent ? root.popupParent : root
        title: qsTr("Ban peer?")
        message: root.contextPeerDetails
            ? qsTr("Ban %1 for %2?").arg(root.contextPeerDetails.address).arg(root.pendingContextBanLabel)
            : qsTr("Ban this peer for %1?").arg(root.pendingContextBanLabel)
        AlertAction {
            text: qsTr("Cancel")
            role: AlertAction.Cancel
            buttonObjectName: "peerListBanCancelButton"
        }
        AlertAction {
            text: qsTr("Ban")
            role: AlertAction.Destructive
            buttonObjectName: "peerListBanConfirmButton"
            onTriggered: root.confirmContextPeerBan()
        }
    }

    AlertPopup {
        id: peerListActionError
        objectName: "peerListActionErrorPopup"
        parent: root.popupParent ? root.popupParent : root
        title: qsTr("Peer action failed")
        messageObjectName: "peerListActionErrorMessage"
        AlertAction {
            text: qsTr("OK")
            buttonObjectName: "peerListActionErrorCloseButton"
        }
    }

    AlertPopup {
        id: peerListDisconnectConfirmation
        objectName: "peerListDisconnectConfirmationPopup"
        parent: root.popupParent ? root.popupParent : root
        title: qsTr("Disconnect peer?")
        message: root.contextPeerDetails
            ? qsTr("Disconnect from %1? The peer may reconnect automatically.").arg(root.contextPeerDetails.address)
            : qsTr("Disconnect this peer? The peer may reconnect automatically.")
        AlertAction {
            text: qsTr("Cancel")
            role: AlertAction.Cancel
            buttonObjectName: "peerListDisconnectCancelButton"
        }
        AlertAction {
            text: qsTr("Disconnect")
            role: AlertAction.Destructive
            buttonObjectName: "peerListDisconnectConfirmButton"
            onTriggered: root.disconnectContextPeer()
        }
    }

    BannedPeersPopup {
        id: bannedPeersPopup
        parent: root.popupParent ? root.popupParent : Overlay.overlay
    }
}
