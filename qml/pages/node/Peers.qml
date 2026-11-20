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

    property bool compact: SizeClass.isCompact(width)
    property bool showHeader: false
    property bool showBackButton: true
    property Item popupParent: null
    property string selectedConnectionId: ""
    property PeerDetailsModel contextPeerDetails: null
    property PeerCommandOperation operation: null
    readonly property bool confirmationVisible: peerListBanConfirmation.visible || peerListDisconnectConfirmation.visible
    readonly property bool actionPending: operation !== null && operation.pending
    readonly property string operationError: operation === null ? ""
        : operation.queryError.length > 0 ? operation.queryError : operation.errorText
    readonly property string observationText: operation !== null && operation.resultPresent && operation.result.observationAvailable
        ? operation.result.targetConnected ? qsTr("The original connection is still connected.") : qsTr("The original connection is no longer connected.") : ""

    readonly property var directionOptions: [
        { text: qsTr("Inbound"), value: "inbound" },
        { text: qsTr("Outbound"), value: "outbound" }
    ]
    readonly property var connectionTypeOptions: [
        { text: qsTr("Full relay"), value: "full-relay" },
        { text: qsTr("Block relay"), value: "block-relay" },
        { text: qsTr("Manual"), value: "manual" }
    ]
    readonly property var networkOptions: [
        { text: qsTr("IPv4"), value: "ipv4" },
        { text: qsTr("IPv6"), value: "ipv6" },
        { text: qsTr("Onion"), value: "onion" },
        { text: qsTr("I2P"), value: "i2p" },
        { text: qsTr("CJDNS"), value: "cjdns" },
        //: Peer network filter choice for peers on a local or private network, such as a LAN node or regtest.
        { text: qsTr("Local network"), value: "unroutable" }
    ]
    readonly property var transportOptions: [
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

    background: Rectangle { color: Theme.color.background }

    header: NavigationBar2 {
        visible: root.showHeader
        showBackButton: root.showBackButton
        backButtonObjectName: "peersBackButton"
        onBackClicked: root.back()
        title: qsTr("Peers")
    }

    AppPreferences { id: settings; objectName: "peerFilterSettings" }
    Binding { target: ApplicationModels.peerListModelProxy; property: "sortBy"; value: settings.peerListSortBy }
    Binding { target: ApplicationModels.peerListModelProxy; property: "sortAscending"; value: settings.peerListSortAscending }
    Binding { target: ApplicationModels.peerListModelProxy; property: "directionFilters"; value: settings.peerDirectionFilters }
    Binding { target: ApplicationModels.peerListModelProxy; property: "connectionTypeFilters"; value: settings.peerConnectionTypeFilters }
    Binding { target: ApplicationModels.peerListModelProxy; property: "networkFilters"; value: settings.peerNetworkFilters }
    Binding { target: ApplicationModels.peerListModelProxy; property: "transportFilters"; value: settings.peerTransportFilters }

    function clearFilter(group) { settings.clearPeerFilter(group) }
    function toggleFilter(group, value, options) { settings.togglePeerFilter(group, value) }
    function clearFilters() { settings.clearPeerFilters() }
    function filterPopupX(button, popup) {
        const anchorX = button.mapToItem(root, 0, 0).x
        return Math.max(12 - anchorX, Math.min(button.width - popup.width,
            root.width - popup.width - anchorX - 12))
    }
    function filterCount() {
        return (ApplicationModels.peerListModelProxy.directionFilters.length > 0 ? 1 : 0)
            + (ApplicationModels.peerListModelProxy.connectionTypeFilters.length > 0 ? 1 : 0)
            + (ApplicationModels.peerListModelProxy.networkFilters.length > 0 ? 1 : 0)
            + (ApplicationModels.peerListModelProxy.transportFilters.length > 0 ? 1 : 0)
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
    function requestContextPeerBan(duration, label) {
        if (!confirmationVisible && contextPeerDetails !== null && contextPeerDetails.canBan && replaceOperation(contextPeerDetails.prepareBan(duration))) {
            peerListBanConfirmation.originalOperation = operation
            peerListBanConfirmation.durationLabel = label
            peerListBanConfirmation.open()
        }
    }
    function confirmContextPeerBan() { if (peerListBanConfirmation.originalOperation !== null) peerListBanConfirmation.originalOperation.confirm() }
    function disconnectContextPeer() { if (peerListDisconnectConfirmation.originalOperation !== null) peerListDisconnectConfirmation.originalOperation.confirm() }
    function requestContextPeerDisconnect() {
        if (!confirmationVisible && contextPeerDetails !== null && contextPeerDetails.canDisconnect && replaceOperation(contextPeerDetails.prepareDisconnect())) {
            peerListDisconnectConfirmation.originalOperation = operation
            peerListDisconnectConfirmation.open()
        }
    }
    Connections {
        target: root.operation
        function onChanged() {
            if (root.visible && root.operation.resultPresent && !root.operation.pending && root.operationError.length > 0)
                peerListActionError.open()
        }
    }
    Component.onDestruction: if (operation !== null) operation.retire()

    Component.onCompleted: ApplicationModels.peerListModelProxy.searchText = ""


    Item {
        objectName: "peersContentFrame"
        anchors.fill: parent
        anchors.leftMargin: root.compact ? Theme.space.pageCompact : Theme.space.xxl
        anchors.rightMargin: root.compact ? Theme.space.pageCompact : Theme.space.xxl
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
                        textStyle: Theme.text.headline
                        horizontalAlignment: Text.AlignLeft
                        color: Theme.color.neutral9
                    }
                    CoreText {
                        objectName: "peersDescriptionLabel"
                        Layout.fillWidth: true
                        text: qsTr("Peers are nodes you exchange transaction data with.")
                        textStyle: Theme.text.description
                        horizontalAlignment: Text.AlignLeft
                        color: Theme.color.neutral6
                        wrapMode: Text.WordWrap
                    }
                }
            }

            InfoBanner {
                objectName: "peersOfflineBanner"
                Layout.fillWidth: true
                visible: typeof networkStatusModel !== "undefined" && ApplicationModels.networkStatusModel.networkOffline
                iconSource: "image://images/network-light"
                title: qsTr("No network connection")
                message: qsTr("Peer connections will resume when your device is back online.")
                contentMargin: 16
                bannerLayout: InfoBanner.Layout.Horizontal
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                SearchBar {
                    id: searchField
                    objectName: "peerSearchBar"
                    fieldObjectName: "peerSearchField"
                    Layout.fillWidth: true
                    placeholderText: qsTr("Search peers")
                    accessibleName: qsTr("Search peers")
                    nextTabItem: sortButton
                    onTextChanged: ApplicationModels.peerListModelProxy.searchText = text
                }

                IconButton {
                    id: sortButton
                    objectName: "peerSortButton"
                    toolbar: true
                    iconSource: "image://images/flip-vertical"
                    accessibleName: qsTr("Sort peers")
                    Layout.preferredWidth: size
                    Layout.preferredHeight: size
                    KeyNavigation.tab: filterButton.visible ? filterButton : directionFilterButton
                    KeyNavigation.backtab: searchField.inputField
                    KeyNavigation.priority: KeyNavigation.BeforeItem
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
                            currentValue: ApplicationModels.peerListModelProxy.sortBy
                            onActivated: (value) => {
                                settings.peerListSortBy = value
                            }
                        }
                        ContextMenuDivider { }
                        ContextMenuPicker {
                            model: [
                                { text: qsTr("Ascending"), value: true },
                                { text: qsTr("Descending"), value: false }
                            ]
                            currentValue: ApplicationModels.peerListModelProxy.sortAscending
                            onActivated: (value) => {
                                settings.peerListSortAscending = value
                            }
                        }
                    }
                }
            }

            Flow {
                objectName: "peerFilters"
                Layout.fillWidth: true
                Layout.preferredHeight: childrenRect.height
                spacing: 12
                FilterButton {
                    id: filterButton
                    objectName: "peerFilterButton"
                    visible: count > 0
                    active: true
                    count: root.filterCount()
                    size: directionFilterButton.height
                    iconSize: 20
                    text: qsTr("Filter peers, %1 active").arg(count)
                    KeyNavigation.tab: directionFilterButton
                    KeyNavigation.backtab: sortButton
                    onClicked: clearFiltersMenu.opened ? clearFiltersMenu.close() : clearFiltersMenu.open()
                }
                DropdownButton {
                    id: directionFilterButton
                    objectName: "peerDirectionFilterButton"
                    text: qsTr("Direction")
                    active: ApplicationModels.peerListModelProxy.directionFilters.length > 0
                    opened: directionFilterMenu.visible
                    defaultBgColor: Theme.color.surface1
                    KeyNavigation.tab: connectionTypeFilterButton
                    KeyNavigation.backtab: filterButton.visible ? filterButton : sortButton
                    onClicked: directionFilterMenu.opened ? directionFilterMenu.close() : directionFilterMenu.open()
                }
                DropdownButton {
                    id: connectionTypeFilterButton
                    objectName: "peerConnectionFilterButton"
                    text: qsTr("Connection")
                    active: ApplicationModels.peerListModelProxy.connectionTypeFilters.length > 0
                    opened: connectionTypeFilterMenu.visible
                    defaultBgColor: Theme.color.surface1
                    KeyNavigation.tab: networkFilterButton
                    KeyNavigation.backtab: directionFilterButton
                    onClicked: connectionTypeFilterMenu.opened ? connectionTypeFilterMenu.close() : connectionTypeFilterMenu.open()
                }
                DropdownButton {
                    id: networkFilterButton
                    objectName: "peerNetworkFilterButton"
                    text: qsTr("Network")
                    active: ApplicationModels.peerListModelProxy.networkFilters.length > 0
                    opened: networkFilterMenu.visible
                    defaultBgColor: Theme.color.surface1
                    KeyNavigation.tab: transportFilterButton
                    KeyNavigation.backtab: connectionTypeFilterButton
                    onClicked: networkFilterMenu.opened ? networkFilterMenu.close() : networkFilterMenu.open()
                }
                DropdownButton {
                    id: transportFilterButton
                    objectName: "peerTransportFilterButton"
                    text: qsTr("Transport")
                    active: ApplicationModels.peerListModelProxy.transportFilters.length > 0
                    opened: transportFilterMenu.visible
                    defaultBgColor: Theme.color.surface1
                    KeyNavigation.backtab: networkFilterButton
                    // Resolve the first row on each press: itemAtIndex() is null while
                    // that row is scrolled out and does not notify when it returns.
                    Keys.onTabPressed: (event) => {
                        if (listView.count === 0) {
                            event.accepted = false
                            return
                        }
                        listView.positionViewAtBeginning()
                        const first = listView.itemAtIndex(0)
                        if (first) first.forceActiveFocus(Qt.TabFocusReason)
                    }
                    onClicked: transportFilterMenu.opened ? transportFilterMenu.close() : transportFilterMenu.open()
                }
                add: Transition {
                    NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.motion.normal; easing.type: Theme.motion.easing.enter }
                    NumberAnimation { property: "scale"; from: Theme.motion.reduced ? 1 : 0.8; to: 1; duration: Theme.motion.travel(Theme.motion.normal); easing.type: Theme.motion.easing.enter }
                }
            }

            Rectangle {
                objectName: "peerListCard"
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: 120
                color: Theme.color.surface1
                radius: Theme.radius.card
                border.width: 0
                clip: true

                ListView {
                    id: listView
                    objectName: "peerListView"
                    anchors.fill: parent
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    model: ApplicationModels.peerListModelProxy
                    delegate: ItemDelegate {
                        id: delegate
                        required property int index
                        objectName: "peerListItem_" + nodeIdText
                        required property string nodeIdText
                        required property string connectionId
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
                            ? transportFilterButton : listView.itemAtIndex(delegate.index - 1)
                        KeyNavigation.priority: KeyNavigation.BeforeItem
                        Accessible.name: delegate.address
                        Accessible.description: root.joinedDetails([delegate.direction,
                            delegate.connectionType === delegate.direction ? "" : delegate.connectionType,
                            delegate.network, delegate.transport, delegate.subversion])
                        onClicked: root.peerSelected(ApplicationModels.peerListModelProxy.peerDetailsAt(index))
                        MouseArea {
                            anchors.fill: parent
                            acceptedButtons: Qt.RightButton
                            onClicked: (mouse) => root.openPeerActionsMenu(
                                ApplicationModels.peerListModelProxy.peerDetailsAt(delegate.index),
                                delegate,
                                Qt.point(mouse.x, mouse.y))
                        }
                        background: Item {
                            StateBackground {
                                anchors.fill: parent
                                anchors.margins: 4
                                radius: Theme.radius.listRow
                                stateColor: delegate.down ? Theme.color.surfacePressed
                                    : !root.compact && root.selectedConnectionId === delegate.connectionId ? Theme.color.surfaceSelected
                                    : delegate.hovered ? Theme.color.surfaceHover : "transparent"
                            }
                            Separator {
                                anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                                anchors.leftMargin: 12; anchors.rightMargin: 12
                                visible: delegate.index < ApplicationModels.peerListModelProxy.count - 1
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
                                color: Theme.color.surface2
                                CoreText {
                                    id: idText
                                    anchors.centerIn: parent
                                    text: "#" + delegate.nodeIdText
                                    textStyle: Theme.text.description
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
                                    textStyle: Theme.text.monoDescription
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
                                    textStyle: Theme.text.description
                                    horizontalAlignment: Text.AlignLeft
                                    color: Theme.color.neutral7
                                    elide: Text.ElideRight
                                    wrap: false
                                }
                                CoreText {
                                    Layout.fillWidth: true
                                    text: delegate.subversion
                                    textStyle: Theme.text.monoCaption
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
                                        textStyle: Theme.text.monoCaption
                                        color: Theme.color.trafficSent
                                        wrap: false
                                    }
                                    CoreText {
                                        objectName: delegate.objectName + "Received"
                                        text: "↓ " + delegate.received
                                        textStyle: Theme.text.monoCaption
                                        color: Theme.color.trafficReceived
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
                    visible: ApplicationModels.peerListModelProxy.count === 0
                    text: searchField.text.length > 0 || root.filterCount() > 0
                        ? qsTr("No peers match your filters") : qsTr("No peers connected")
                    textStyle: Theme.text.description
                    color: Theme.color.neutral6
                }
            }

            Column {
                Layout.fillWidth: true
                spacing: 4

                CoreText {
                    objectName: "connectedPeersLabel"
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: qsTr("%1 connected").arg(ApplicationModels.nodeModel.numPeers)
                    textStyle: Theme.text.caption
                    horizontalAlignment: Text.AlignHCenter
                    color: Theme.color.neutral6
                }

                LinkButton {
                    objectName: "viewBannedPeersButton"
                    anchors.horizontalCenter: parent.horizontalCenter
                    visible: ApplicationModels.banListModel.count > 0
                    text: ApplicationModels.banListModel.count === 1 ? qsTr("View 1 banned peer…")
                        : qsTr("View %1 banned peers…").arg(ApplicationModels.banListModel.count.toLocaleString(Qt.locale(), "f", 0))
                    onClicked: bannedPeersPopup.open()
                }
            }
        }
    }

    PeerActionsMenu {
        id: peerRowActionsMenu
        objectName: "peerRowActionsMenu"
        canBan: root.contextPeerDetails !== null && root.contextPeerDetails.canBan && !root.actionPending && !root.confirmationVisible
        canDisconnect: root.contextPeerDetails !== null && root.contextPeerDetails.canDisconnect && !root.actionPending && !root.confirmationVisible
        onBanRequested: (duration, label) => root.requestContextPeerBan(duration, label)
        onDisconnectRequested: root.requestContextPeerDisconnect()
    }

    AlertPopup {
        id: peerListBanConfirmation
        objectName: "peerListBanConfirmationPopup"
        property PeerCommandOperation originalOperation: null
        property string durationLabel: qsTr("1 hour")
        onClosed: {
            const original = peerListBanConfirmation.originalOperation
            if (original !== null && original.confirmationRequired) original.cancel()
        }
        parent: root.popupParent ? root.popupParent : root
        title: qsTr("Ban peer?")
        message: peerListBanConfirmation.originalOperation !== null
            ? qsTr("Ban %1 for %2?").arg(peerListBanConfirmation.originalOperation.address).arg(peerListBanConfirmation.durationLabel)
            : qsTr("Ban this peer for %1?").arg(peerListBanConfirmation.durationLabel)
        AlertAction {
            text: qsTr("Cancel")
            role: AlertAction.Cancel
            buttonObjectName: "peerListBanCancelButton"
            onTriggered: if (peerListBanConfirmation.originalOperation !== null) peerListBanConfirmation.originalOperation.cancel()
        }
        AlertAction {
            text: qsTr("Ban")
            role: AlertAction.Destructive
            buttonObjectName: "peerListBanConfirmButton"
            enabled: peerListBanConfirmation.originalOperation !== null && peerListBanConfirmation.originalOperation.confirmationRequired
            // Confirm before closing: closing first would cancel the unconfirmed operation.
            closesPopup: false
            onTriggered: {
                root.confirmContextPeerBan()
                peerListBanConfirmation.close()
            }
        }
    }

    AlertPopup {
        id: peerListActionError
        objectName: "peerListActionErrorPopup"
        parent: root.popupParent ? root.popupParent : root
        title: qsTr("Peer action failed")
        messageObjectName: "peerListActionErrorMessage"
        message: root.operationError + (root.observationText.length > 0 ? "\n" + root.observationText : "")
        AlertAction {
            text: qsTr("Refresh original peer state")
            buttonObjectName: "peerListCommandReconcileButton"
            closesPopup: false
            enabled: root.operation !== null && root.operation.resultPresent && !root.operation.outcomeKnown && !root.operation.pending
            onTriggered: root.operation.reconcile()
        }
        AlertAction {
            text: qsTr("OK")
            buttonObjectName: "peerListActionErrorCloseButton"
        }
    }

    AlertPopup {
        id: peerListDisconnectConfirmation
        objectName: "peerListDisconnectConfirmationPopup"
        property PeerCommandOperation originalOperation: null
        onClosed: {
            const original = peerListDisconnectConfirmation.originalOperation
            if (original !== null && original.confirmationRequired) original.cancel()
        }
        parent: root.popupParent ? root.popupParent : root
        title: qsTr("Disconnect peer?")
        message: peerListDisconnectConfirmation.originalOperation !== null
            ? qsTr("Disconnect from %1? The peer may reconnect automatically.").arg(peerListDisconnectConfirmation.originalOperation.address)
            : qsTr("Disconnect this peer? The peer may reconnect automatically.")
        AlertAction {
            text: qsTr("Cancel")
            role: AlertAction.Cancel
            buttonObjectName: "peerListDisconnectCancelButton"
            onTriggered: if (peerListDisconnectConfirmation.originalOperation !== null) peerListDisconnectConfirmation.originalOperation.cancel()
        }
        AlertAction {
            text: qsTr("Disconnect")
            role: AlertAction.Destructive
            buttonObjectName: "peerListDisconnectConfirmButton"
            enabled: peerListDisconnectConfirmation.originalOperation !== null && peerListDisconnectConfirmation.originalOperation.confirmationRequired
            // Confirm before closing: closing first would cancel the unconfirmed operation.
            closesPopup: false
            onTriggered: {
                root.disconnectContextPeer()
                peerListDisconnectConfirmation.close()
            }
        }
    }

    BannedPeersPopup {
        id: bannedPeersPopup
        parent: root.popupParent ? root.popupParent : Overlay.overlay
    }

    ContextMenu {
        id: directionFilterMenu
        objectName: "peerDirectionFilterMenu"
        parent: directionFilterButton
        y: directionFilterButton.height + 2
        minMenuWidth: 200
        modal: true
        dim: false
        onAboutToShow: x = root.filterPopupX(directionFilterButton, directionFilterMenu)
        ContextMenuPicker {
            objectName: "peerDirectionFilterPicker"
            model: root.directionOptions
            currentValue: ApplicationModels.peerListModelProxy.directionFilters.length > 0 ? ApplicationModels.peerListModelProxy.directionFilters[0] : undefined
            onActivated: (value) => root.toggleFilter("direction", value, root.directionOptions)
        }
    }

    ContextMenu {
        id: connectionTypeFilterMenu
        objectName: "peerConnectionFilterMenu"
        parent: connectionTypeFilterButton
        y: connectionTypeFilterButton.height + 2
        minMenuWidth: 200
        modal: true
        dim: false
        onAboutToShow: x = root.filterPopupX(connectionTypeFilterButton, connectionTypeFilterMenu)
        ContextMenuPicker {
            objectName: "peerConnectionAllPicker"
            model: [{ text: qsTr("All"), value: "" }]
            currentValue: ApplicationModels.peerListModelProxy.connectionTypeFilters.length === 0 ? "" : undefined
            onActivated: root.clearFilter("connectionType")
        }
        ContextMenuDivider { }
        ContextMenuPicker {
            objectName: "peerConnectionFilterPicker"
            model: root.connectionTypeOptions
            multiSelect: true
            selectedValues: ApplicationModels.peerListModelProxy.connectionTypeFilters
            onActivated: (value) => root.toggleFilter("connectionType", value, root.connectionTypeOptions)
        }
    }

    ContextMenu {
        id: networkFilterMenu
        objectName: "peerNetworkFilterMenu"
        parent: networkFilterButton
        y: networkFilterButton.height + 2
        minMenuWidth: 200
        modal: true
        dim: false
        onAboutToShow: x = root.filterPopupX(networkFilterButton, networkFilterMenu)
        ContextMenuPicker {
            objectName: "peerNetworkAllPicker"
            model: [{ text: qsTr("All"), value: "" }]
            currentValue: ApplicationModels.peerListModelProxy.networkFilters.length === 0 ? "" : undefined
            onActivated: root.clearFilter("network")
        }
        ContextMenuDivider { }
        ContextMenuPicker {
            objectName: "peerNetworkFilterPicker"
            model: root.networkOptions
            multiSelect: true
            selectedValues: ApplicationModels.peerListModelProxy.networkFilters
            onActivated: (value) => root.toggleFilter("network", value, root.networkOptions)
        }
    }

    ContextMenu {
        id: transportFilterMenu
        objectName: "peerTransportFilterMenu"
        parent: transportFilterButton
        y: transportFilterButton.height + 2
        minMenuWidth: 200
        modal: true
        dim: false
        onAboutToShow: x = root.filterPopupX(transportFilterButton, transportFilterMenu)
        ContextMenuPicker {
            objectName: "peerTransportAllPicker"
            model: [{ text: qsTr("All"), value: "" }]
            currentValue: ApplicationModels.peerListModelProxy.transportFilters.length === 0 ? "" : undefined
            onActivated: root.clearFilter("transport")
        }
        ContextMenuDivider { }
        ContextMenuPicker {
            objectName: "peerTransportFilterPicker"
            model: root.transportOptions
            multiSelect: true
            selectedValues: ApplicationModels.peerListModelProxy.transportFilters
            onActivated: (value) => root.toggleFilter("transport", value, root.transportOptions)
        }
    }

    ContextMenu {
        id: clearFiltersMenu
        objectName: "peerClearFiltersMenu"
        parent: filterButton
        y: filterButton.height + 2
        minMenuWidth: 180
        modal: true
        dim: false
        onAboutToShow: x = root.filterPopupX(filterButton, clearFiltersMenu)
        ContextMenuButton {
            objectName: "peerClearFiltersAction"
            text: qsTr("Clear filters")
            onTriggered: root.clearFilters()
        }
    }
}
