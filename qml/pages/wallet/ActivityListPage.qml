// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Window 2.15
import org.bitcoincore.qt 1.0
import "../../controls"
import "../../components"

Page {
    id: root
    objectName: "activityPage"
    property WalletQmlModel wallet: null
    property bool exportSucceeded: true
    readonly property ActivityExportOperation csvExport: activityFilterProxy.exportOperation
    property bool ready: false
    readonly property bool activitySourceEmpty: !wallet || wallet.transactionActivityModel.count === 0
    readonly property bool typeFilterActive: activityFilterProxy.typeFilters.length > 0
    readonly property bool dateFilterActive: activityFilterProxy.dateFilter !== ActivityFilterProxyModel.DateAll
    readonly property bool amountFilterActive: activityFilterProxy.minAmount >= 0 || activityFilterProxy.maxAmount >= 0
    readonly property int activeFilterCount: Number(typeFilterActive) + Number(dateFilterActive) + Number(amountFilterActive)
    readonly property real pageSidePadding: SizeClass.isCompact(width) ? Theme.space.pageCompact : Theme.space.pageRegular
    readonly property real activityContentWidth: Math.max(0, Math.min(1100, width - pageSidePadding * 2))
    readonly property real activitySideInset: (width - activityContentWidth) / 2

    signal transactionRequested(string txid)
    signal paymentRequestRequested(string requestId)

    background: Rectangle { color: Theme.color.background }
    padding: 0
    Component.onCompleted: ready = true
    onVisibleChanged: if (!visible && ready) { activityRowMenu.close(); cancelExportSelection() }
    onWalletChanged: if (ready) {
        cancelExportSelection()
        activityRowMenu.close()
        activityRequestMenu.close()
        clearFilters()
        activityFilterProxy.searchText = ""
        searchField.text = ""
    }

    function clearFilters() {
        activityFilterProxy.dateFilter = ActivityFilterProxyModel.DateAll
        activityFilterProxy.typeFilters = []
        activityFilterProxy.setAmountRange(-1, -1)
        datePopup.close()
        typePopup.close()
        amountPopup.close()
        clearFiltersMenu.close()
    }

    function countText() {
        const transactions = activityFilterProxy.transactionCount
        const requests = activityFilterProxy.requestCount
        const parts = []
        if (transactions > 0 || requests === 0)
            parts.push(transactions === 1 ? qsTr("1 transaction") : qsTr("%1 transactions").arg(transactions))
        if (requests > 0)
            parts.push(requests === 1 ? qsTr("1 payment request") : qsTr("%1 payment requests").arg(requests))
        return parts.join(" · ")
    }

    function emptyActivityTitle() {
        if (activityFilterProxy.error) return qsTr("Activity could not be loaded")
        if (activityFilterProxy.busy) return qsTr("Loading wallet activity…")
        if (activitySourceEmpty && wallet && wallet.transactionActivityModel.loading) return qsTr("Loading wallet activity…")
        if (activitySourceEmpty)
            return ApplicationModels.nodeModel.blockSyncActive ? qsTr("Syncing wallet activity…") : qsTr("No activity yet")
        return qsTr("No activity matches your filters.")
    }
    function emptyActivityDescription() {
        if (activityFilterProxy.error) return activityFilterProxy.error
        if (activityFilterProxy.busy) return ""
        if (activitySourceEmpty && wallet && wallet.transactionActivityModel.loading) return ""
        if (activitySourceEmpty)
            return ApplicationModels.nodeModel.blockSyncActive ? qsTr("Transactions may appear as your wallet catches up.")
                : qsTr("Your transactions and payment requests will appear here.")
        return qsTr("Try changing your search, date, type, or amount filters.")
    }
    function exportResultTitle() { return exportSucceeded ? qsTr("Export complete") : qsTr("Export failed") }
    function exportResultDescription() {
        if (activityFilterProxy.exportRejected) return activityFilterProxy.exportError
        if (activityFilterProxy.exportResult.present && !activityFilterProxy.exportResult.outcomeKnown) return qsTr("The export outcome is unknown. Check the original destination before trying again: %1").arg(activityFilterProxy.exportResult.finalPath)
        return exportSucceeded ? qsTr("Your Activity CSV has been saved.")
            : activityFilterProxy.exportResult.present ? activityFilterProxy.exportResult.errorText : activityFilterProxy.exportError || qsTr("The Activity CSV could not be saved. Check the file path and try again.")
    }

    function cancelExportSelection() {
        activityFilterProxy.cancelExport(exportDialog.intentToken)
        exportDialog.intentToken = ""
        exportDialog.close()
    }
    function beginExportSelection() {
        const token = activityFilterProxy.captureExport()
        if (!token.length) return
        exportDialog.intentToken = token
        exportDialog.open()
    }
    function completeExportSelection(token, path) {
        if (activityFilterProxy.requestCapturedExportCsv(token, String(path)) === 0) {
            exportSucceeded = false
            exportResultPopup.open()
        }
    }
    function exportActivity(path) {
        if (activityFilterProxy.requestExportCsv(String(path)) === 0) {
            exportSucceeded = false
            exportResultPopup.open()
        }
    }

    Connections {
        target: activityFilterProxy
        function onExportFinished(ticket, saved, path, error) {
            if (activityFilterProxy.exportResult.cancelled) return
            root.exportSucceeded = saved
            exportResultPopup.open()
        }
    }

    // A filter popup right-aligns to its button, but a wide popup on a
    // button near the left edge (the date preset menu, sized for the
    // calendar) would spill past the page. Clamp x so the popup stays
    // within the page horizontally instead of overflowing the list area.
    // Callers assign this from onAboutToShow rather than binding x to
    // it: mapToItem is not dependency-tracked, so a binding would keep
    // a clamp computed from stale geometry.
    function filterPopupX(button, popup) {
        const anchorX = button.mapToItem(root, 0, 0).x
        const rightAligned = button.width - popup.width
        const minX = -anchorX
        const maxX = root.width - popup.width - anchorX
        return Math.max(minX, Math.min(rightAligned, maxX))
    }

    // A QDate read back from the model reaches QML as a JS Date at UTC
    // midnight; rebuild it at local noon on the same calendar day (local
    // midnight is skipped in some zones on a clock change) so
    // seeding the calendar does not read it a day early west of UTC (the
    // read-side mirror of the Apply path, which hands the model ISO
    // strings for the same reason).
    function modelDateToLocal(date) {
        return (date && !isNaN(date.getTime()))
            ? new Date(date.getUTCFullYear(), date.getUTCMonth(), date.getUTCDate(), 12)
            : null
    }

    function toggleActivityType(value) {
        const selected = Array.from(activityFilterProxy.typeFilters)
        const index = selected.indexOf(value)
        if (index < 0) selected.push(value)
        else selected.splice(index, 1)
        activityFilterProxy.typeFilters = selected
    }

    function applyAmountRange() {
        const lower = Math.round(amountSlider.lowerValue)
        const upper = Math.round(amountSlider.upperValue)
        if (lower === 0 && upper === activityFilterProxy.availableMaxAmount)
            activityFilterProxy.setAmountRange(-1, -1)
        else activityFilterProxy.setAmountRange(lower, upper)
    }

    ActivityFilterProxyModel {
        id: activityFilterProxy
        objectName: "activityFilterProxyModel"
        sourceModel: root.wallet ? root.wallet.transactionActivityModel : null
        displayUnit: ApplicationModels.optionsModel.displayUnit
        groupBy: activitySettings.activityGrouping === "day" ? ActivityFilterProxyModel.Day : ActivityFilterProxyModel.Month
    }
    BitcoinAmount {
        id: lowerAmountFormatter
        unit: ApplicationModels.optionsModel.displayUnit
        satoshi: Math.round(amountSlider.lowerValue)
    }
    BitcoinAmount {
        id: upperAmountFormatter
        unit: ApplicationModels.optionsModel.displayUnit
        satoshi: Math.round(amountSlider.upperValue)
    }
    AppPreferences { id: activitySettings }
    AppFileDialog {
        id: exportDialog
        objectName: "activityExportDialog"
        defaultSuffix: "csv"
        confirmOverwrite: false
        fileMode: AppFileDialog.SaveFile
        nameFilters: [qsTr("Comma separated file (*.csv)")]
        property string intentToken: ""
        onAccepted: {
            const original = intentToken
            intentToken = ""
            root.completeExportSelection(original, exportDialog.selectedFile.toString())
        }
        onRejected: {
            activityFilterProxy.cancelExport(intentToken)
            intentToken = ""
        }
    }
    TextField {
        id: automationExportPathField
        objectName: "activityExportPathField"
        visible: false
    }
    Shortcut {
        sequences: [StandardKey.Find]
        enabled: root.visible
        onActivated: searchField.focusSearch()
    }

    ColumnLayout {
        id: content
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.topMargin: 28
        anchors.bottomMargin: 16
        anchors.left: parent.left
        anchors.right: parent.right
        spacing: 0

        InfoBanner {
            objectName: "outgoingNoteStatus"
            visible: root.wallet !== null && root.wallet.recipientLabelsError.length > 0
            Layout.fillWidth: true
            Layout.leftMargin: root.activitySideInset
            Layout.rightMargin: root.activitySideInset
            Layout.bottomMargin: visible ? Theme.space.l : 0
            title: qsTr("Outgoing notes")
            message: root.wallet ? root.wallet.recipientLabelsError : ""
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: root.activitySideInset
            Layout.rightMargin: root.activitySideInset
            Layout.bottomMargin: 28
            spacing: 16
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 6
                CoreText {
                    Layout.fillWidth: true
                    text: qsTr("Activity")
                    textStyle: Theme.text.headline
                    horizontalAlignment: Text.AlignLeft
                }
                CoreText {
                    Layout.fillWidth: true
                    text: qsTr("Your wallet’s payments and transactions")
                    textStyle: Theme.text.description
                    color: Theme.color.neutral7
                    horizontalAlignment: Text.AlignLeft
                }
                CoreText {
                    visible: SizeClass.isCompact(root.width)
                    Layout.fillWidth: true
                    text: root.countText()
                    textStyle: Theme.text.caption
                    color: Theme.color.neutral7
                    horizontalAlignment: Text.AlignLeft
                }
            }
            CoreText {
                objectName: "activityCount"
                visible: !SizeClass.isCompact(root.width)
                Layout.alignment: Qt.AlignVCenter
                text: root.countText()
                textStyle: Theme.text.caption
                color: Theme.color.neutral7
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: root.activitySideInset
            Layout.rightMargin: root.activitySideInset
            Layout.bottomMargin: 12
            spacing: 12
            SearchBar {
                id: searchField
                objectName: "activitySearchBar"
                fieldObjectName: "activitySearchField"
                clearButtonObjectName: "activityClearSearchButton"
                searchIconObjectName: "activitySearchIcon"
                Layout.fillWidth: true
                placeholderText: root.width < 560 ? qsTr("Search activity") : qsTr("Search notes, addresses or transaction IDs")
                accessibleName: qsTr("Search activity")
                text: activityFilterProxy.searchText
                onTextChanged: activityFilterProxy.searchText = text
            }
            OverflowMenuButton {
                id: moreButton
                objectName: "activityMoreButton"
                checked: moreMenu.opened
                onClicked: moreMenu.opened ? moreMenu.close() : moreMenu.open()
            }
        }

        Flow {
            id: filters
            objectName: "activityFilters"
            Layout.fillWidth: true
            Layout.leftMargin: root.activitySideInset
            Layout.rightMargin: root.activitySideInset
            Layout.preferredHeight: childrenRect.height
            Layout.bottomMargin: 24
            spacing: 12
            Item {
                id: activeFiltersSlot
                property real reveal: root.activeFilterCount > 0 ? 1 : 0
                property int displayedCount: root.activeFilterCount
                // Retain the last count while the button animates out.
                Connections {
                    target: root
                    function onActiveFilterCountChanged() {
                        if (root.activeFilterCount > 0) activeFiltersSlot.displayedCount = root.activeFilterCount
                    }
                }
                visible: reveal > 0
                width: Math.max(0, (activeFiltersButton.implicitWidth + filters.spacing) * reveal - filters.spacing)
                height: activeFiltersButton.height
                Behavior on reveal {
                    enabled: !Theme.motion.reduced
                    NumberAnimation { duration: Theme.motion.moderate; easing.type: Theme.motion.easing.move }
                }
                FilterButton {
                    id: activeFiltersButton
                    objectName: "activityActiveFiltersButton"
                    enabled: root.activeFilterCount > 0
                    opacity: activeFiltersSlot.reveal
                    scale: 0.8 + 0.2 * activeFiltersSlot.reveal
                    transformOrigin: Item.Left
                    active: true
                    count: activeFiltersSlot.displayedCount
                    size: typeFilterButton.height
                    iconSize: 20
                    text: count === 1 ? qsTr("1 active filter") : qsTr("%1 active filters").arg(count)
                    onClicked: clearFiltersMenu.opened ? clearFiltersMenu.close() : clearFiltersMenu.open()
                }
            }
            DropdownButton {
                id: typeFilterButton
                objectName: "activityTypeFilterButton"
                text: qsTr("Activity")
                active: root.typeFilterActive
                opened: typePopup.visible
                onClicked: typePopup.opened ? typePopup.close() : typePopup.open()
            }
            DropdownButton {
                id: dateFilterButton
                objectName: "activityDateFilterButton"
                text: qsTr("Date")
                active: root.dateFilterActive
                opened: datePopup.visible
                onClicked: datePopup.opened ? datePopup.close() : datePopup.open()
            }
            DropdownButton {
                id: amountFilterButton
                objectName: "activityAmountFilterButton"
                text: qsTr("Amount")
                active: root.amountFilterActive
                opened: amountPopup.visible
                onClicked: amountPopup.opened ? amountPopup.close() : amountPopup.open()
            }

        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 0

            ListView {
                id: listView
                objectName: "activityListView"
                function ensureFocusedItemVisible() {
                    heldContentY = NaN // Keyboard navigation is the user's own scrolling.
                    const window = root.Window.window
                    const item = window ? window.activeFocusItem : null
                    let ancestor = item
                    while (ancestor && ancestor !== listView.contentItem) ancestor = ancestor.parent
                    if (!ancestor) return
                    const position = item.mapToItem(listView, 0, 0)
                    const margin = Theme.space.m
                    let y = listView.contentY
                    if (position.y < margin) y += position.y - margin
                    else if (position.y + item.height > listView.height - margin)
                        y += position.y + item.height - listView.height + margin
                    listView.contentY = Math.max(listView.originY,
                        Math.min(y, listView.originY + listView.contentHeight - listView.height))
                }
                // A confirmation re-groups the list: rows move out of Pending into
                // their dated groups. Qt keeps each visible row on screen as it moves,
                // so the view follows the row up the list and jumps. The list stays
                // where the user left it instead, at the top or further down. Only the
                // position is held while the move is applied; the rows still move.
                // The user always wins: a drag, a flick, the scroll bar or the wheel
                // ends the hold.
                property real heldContentY: NaN
                readonly property bool userScrolling: moving || dragging || (ScrollBar.vertical && ScrollBar.vertical.pressed)
                function holdPosition() {
                    if (isNaN(heldContentY) && !userScrolling) heldContentY = contentY
                    if (!isNaN(heldContentY)) holdPositionTimer.restart()
                }
                // While the list lays out the moved rows its content height is only an
                // estimate, and Qt clamps the position to it and does not come back.
                // So hold the exact position meanwhile and clamp only once settled.
                function restorePosition(settled) {
                    const held = heldContentY
                    if (isNaN(held)) return
                    if (userScrolling) { heldContentY = NaN; return }
                    const highest = Math.max(originY, originY + contentHeight - height)
                    const target = settled === true ? Math.max(originY, Math.min(held, highest)) : held
                    if (contentY !== target) contentY = target
                }
                onContentYChanged: restorePosition(false)
                onUserScrollingChanged: if (userScrolling) heldContentY = NaN
                Timer {
                    id: holdPositionTimer
                    interval: 400
                    onTriggered: { listView.restorePosition(true); listView.heldContentY = NaN }
                }
                Connections {
                    target: activityFilterProxy
                    function onRowsAboutToBeMoved() { listView.holdPosition() }
                    function onModelAboutToBeReset() { listView.holdPosition() }
                    function onLayoutAboutToBeChanged() { listView.holdPosition() }
                    function onRowsMoved() { listView.restorePosition(false); listView.holdPosition() }
                    function onModelReset() { listView.restorePosition(false); listView.holdPosition() }
                    function onLayoutChanged() { listView.restorePosition(false); listView.holdPosition() }
                    function onDataChanged() { if (!isNaN(listView.heldContentY)) listView.holdPosition() }
                }
                Timer {
                    id: focusScrollTimer
                    interval: 0
                    onTriggered: listView.ensureFocusedItemVisible()
                }
                Connections {
                    target: root.Window.window
                    function onActiveFocusItemChanged() { focusScrollTimer.restart() }
                }
                anchors.fill: parent
                anchors.topMargin: activityRefreshError.visible ? activityRefreshError.height + 8 : 0
                clip: true
                model: root.visible ? activityFilterProxy : null
                reuseItems: false
                boundsBehavior: Flickable.StopAtBounds
                // Nothing on this page uses the current item. Left at its default it
                // is the first row and is pushed down by every row inserted above
                // it; when a confirmation re-groups the list, the view then jumps
                // to that row instead of staying where the user left it.
                currentIndex: -1
                highlightFollowsCurrentItem: false
                ScrollBar.vertical: ScrollBar {}
                section.property: root.visible ? "sectionLabel" : ""
                section.criteria: ViewSection.FullString
                section.delegate: Item {
                    id: sectionHeader
                    required property string section
                    readonly property bool isPending: section === qsTranslate("ActivityFilterProxyModel", "Pending")
                    width: listView.width
                    height: 56
                    RowLayout {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.leftMargin: root.activitySideInset + Theme.space.xs
                        anchors.rightMargin: root.activitySideInset + Theme.space.xs
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: 12
                        spacing: 12
                        CoreText {
                            objectName: "activitySectionTitle"
                            Layout.fillWidth: true
                            text: sectionHeader.section
                            textStyle: Theme.text.subheading
                            horizontalAlignment: Text.AlignLeft
                        }
                        BitcoinAmountDisplayLabel {
                            objectName: sectionHeader.isPending ? "activityPendingBalance" : ""
                            hidesAmount: true
                            visible: sectionHeader.isPending && activityFilterProxy.pendingBalanceSat !== 0
                            text: visible ? activityFilterProxy.pendingBalance : ""
                            textStyle: Theme.text.amountDescription
                            color: !visible ? Theme.color.neutral7 : activityFilterProxy.pendingBalanceSat > 0 ? Theme.color.txIncoming
                                : activityFilterProxy.pendingBalanceSat < 0 ? Theme.color.neutral9 : Theme.color.neutral7
                            horizontalAlignment: Text.AlignRight
                        }
                    }
                }
                delegate: Item {
                    id: activityDelegate
                    required property var model
                    required property int index
                    width: listView.width
                    height: activityRow.implicitHeight + (activityPreviewNotice.visible ? activityPreviewNotice.implicitHeight + Theme.space.m : 0)
                    CoreText {
                        id: activityPreviewNotice
                        anchors.top: activityRow.bottom
                        anchors.topMargin: Theme.space.xs
                        x: root.activitySideInset
                        width: root.activityContentWidth
                        visible: activityDelegate.model.presentationLimited === true
                        text: qsTr("A preview is shown. Open the transaction for complete details.")
                        textStyle: Theme.text.description
                        color: Theme.color.neutral7
                    }
                    ActivityRow {
                        id: activityRow
                        isCompact: activitySettings.activityDisplayDensity === "compact"
                        x: root.activitySideInset
                        width: root.activityContentWidth
                        activityId: activityDelegate.model.activityId
                        txid: activityDelegate.model.txid
                        requestId: activityDelegate.model.requestId
                        label: activityDelegate.model.label
                        address: activityDelegate.model.address
                        amount: activityDelegate.model.amount
                        netAmountSat: activityDelegate.model.netAmountSat
                        dateTimeLabel: activityDelegate.model.dateTimeLabel
                        activityType: activityDelegate.model.activityType
                        status: activityDelegate.model.status
                        depth: activityDelegate.model.depth
                        blocksToMaturity: activityDelegate.model.blocksToMaturity
                        statusKnown: activityDelegate.model.statusKnown
                        isInactive: activityDelegate.model.isInactive
                        isPendingRequest: activityDelegate.model.isPendingRequest
                        replacedByTxid: activityDelegate.model.replacedByTxid
                        hasPaymentRequest: activityDelegate.model.hasPaymentRequest
                        actions: activityDelegate.model.actions
                        firstInSection: activityDelegate.ListView.section !== activityDelegate.ListView.previousSection
                        lastInSection: activityDelegate.ListView.section !== activityDelegate.ListView.nextSection
                        onContextMenuRequested: function(x, y) {
                            const menu = activityRow.isPendingRequest ? activityRequestMenu : activityRowMenu
                            const point = activityRow.mapToItem(menu.parent, x, y)
                            if (activityRow.isPendingRequest) {
                                activityRequestMenu.requestId = activityRow.requestId
                            } else {
                                // The token keeps a delayed raw copy bound to this row.
                                activityRowMenu.contextToken = activityFilterProxy.captureRowContext(activityDelegate.index)
                                if (activityRowMenu.contextToken === 0) return
                                activityRowMenu.txid = activityRow.txid
                            }
                            menu.x = point.x
                            menu.y = point.y
                            menu.open()
                        }
                        onActivated: function(txid, requestId, isRequest) {
                            if (isRequest) root.paymentRequestRequested(requestId)
                            else root.transactionRequested(txid)
                        }
                    }
                }
            }

            InfoBanner {
                id: activityRefreshError
                objectName: "activityRefreshError"
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.leftMargin: root.activitySideInset
                anchors.rightMargin: root.activitySideInset
                visible: !!root.wallet && activityFilterProxy.count > 0 && !!root.wallet.transactionActivityModel.loadError
                bannerLayout: SizeClass.isCompact(width) ? InfoBanner.Layout.Vertical : InfoBanner.Layout.Horizontal
                message: root.wallet ? root.wallet.transactionActivityModel.loadError : ""
                messageObjectName: "activityRefreshErrorText"
                primaryButtonText: qsTr("Try again")
                primaryButtonOutlined: true
                onPrimaryClicked: if (root.wallet) root.wallet.transactionActivityModel.reload()
            }

            ColumnLayout {
                objectName: "activityEmptyState"
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.top: parent.top
                anchors.topMargin: 64
                width: Math.min(root.activityContentWidth, 440)
                visible: ApplicationModels.walletController.initialized && activityFilterProxy.count === 0
                spacing: 10
                CoreText {
                    objectName: "activityEmptyStateTitle"
                    Layout.fillWidth: true
                    text: root.emptyActivityTitle()
                    textStyle: Theme.text.heading
                }
                CoreText {
                    objectName: "activityEmptyStateDescription"
                    Layout.fillWidth: true
                    text: root.emptyActivityDescription()
                    textStyle: Theme.text.description
                    color: Theme.color.neutral7
                }
                NeutralButton {
                    objectName: "activityRetryButton"
                    surfaceLevel: 0
                    Layout.alignment: Qt.AlignHCenter
                    visible: root.wallet && !!root.wallet.transactionActivityModel.loadError
                    text: qsTr("Try again")
                    onClicked: root.wallet.transactionActivityModel.reload()
                }
            }

            Column {
                visible: !ApplicationModels.walletController.initialized
                anchors.top: parent.top
                anchors.topMargin: 28
                x: root.activitySideInset
                width: root.activityContentWidth
                spacing: 12
                Repeater {
                    model: 3
                    Skeleton { width: parent.width; height: 88 }
                }
            }
        }
    }

    TransactionActionsMenu {
        id: activityRowMenu
        objectName: "activityRowContextMenu"
        backgroundColor: Theme.color.surface2
        parent: Overlay.overlay
        itemObjectNamePrefix: "activity"
        property double contextToken: 0
        onClosed: activityFilterProxy.releaseRowContext(contextToken)
        showsCopyRawTransaction: true
        onCopyRawTransactionRequested: activityFilterProxy.requestCopyRawTransaction(activityRowMenu.contextToken)
        onExplorerLinkRequested: function(url) { explorerLinkPopup.openLink(url) }
    }

    PaymentRequestActionsMenu {
        id: activityRequestMenu
        objectName: "activityRequestContextMenu"
        property string requestId: ""
        // Typed request sharing owns its copy receipt; the menu stays open
        // until the copy is performed or reports its error.
        readonly property RequestSharingModel requestSharing: root.wallet && root.wallet.receive && requestId.length > 0
            ? root.wallet.receive.sharingRequest(requestId, activityRequestMenu) : null
        Binding { target: activityRequestMenu.requestSharing; property: "active"; value: root.visible && activityRequestMenu.opened; when: activityRequestMenu.requestSharing !== null }
        backgroundColor: Theme.color.surface2
        parent: Overlay.overlay
        showsCopyRequest: true
        copyRequestEnabled: requestSharing
            ? requestSharing.canCopyUri && (!requestSharing.lastOperation || !requestSharing.lastOperation.busy)
            : !!root.wallet && !root.wallet.receiveRequestReconciliationPending
        copyRequestClosesMenu: requestSharing === null
        copyRequestErrorText: requestSharing && requestSharing.lastOperation ? requestSharing.lastOperation.errorText : ""
        copyRequestObjectName: "activityCopyPaymentRequest"
        deleteObjectName: "activityDeletePaymentRequest"
        onCopyRequestRequested: {
            if (requestSharing) {
                requestSharing.copyUri()
                return
            }
            const uri = root.wallet ? root.wallet.transactionActivityModel.paymentRequestUri(activityRequestMenu.requestId) : ""
            if (uri.length === 0) return
            Clipboard.setText(uri)
        }
        Connections {
            target: activityRequestMenu.requestSharing ? activityRequestMenu.requestSharing.lastOperation : null
            function onChanged() {
                const operation = activityRequestMenu.requestSharing ? activityRequestMenu.requestSharing.lastOperation : null
                if (operation && operation.result.present && operation.result.performed) activityRequestMenu.close()
            }
        }
        onDeleteRequested: deleteRequestConfirmation.confirmDelete(root.wallet, activityRequestMenu.requestId)
    }

    // The explorer receives the transaction ID, so opening it is confirmed.
    ExternalPopup {
        id: explorerLinkPopup
        objectName: "activityExplorerLinkPopup"
    }

    PaymentRequestDeleteConfirmation {
        id: deleteRequestConfirmation
        objectName: "activityDeletePaymentRequestConfirmation"
        onDeleted: function(requestId) {
            for (const request of [root.wallet.currentPaymentRequest, root.wallet.detailPaymentRequest]) {
                if (request && request.id === requestId) request.clear()
            }
        }
    }

    ContextMenu {
        id: moreMenu
        objectName: "activityMoreMenu"
        parent: moreButton
        y: moreButton.height + 2
        minMenuWidth: 232
        title: qsTr("Group by")
        modal: true
        dim: false
        onAboutToShow: x = root.filterPopupX(moreButton, moreMenu)
        ContextMenuPicker {
            objectNameRole: "objectName"
            currentValue: activitySettings.activityGrouping
            model: [
                { text: qsTr("Month"), value: "month", objectName: "activityGroupMonth" },
                { text: qsTr("Day"), value: "day", objectName: "activityGroupDay" }
            ]
            onActivated: function(value) {
                activitySettings.activityGrouping = value
                moreMenu.close()
                listView.heldContentY = NaN
                listView.positionViewAtBeginning()
            }
        }
        ContextMenuDivider {}
        ContextMenuPicker {
            title: qsTr("Display")
            objectName: "activityDisplayPicker"
            objectNameRole: "objectName"
            currentValue: activitySettings.activityDisplayDensity
            model: [
                { text: qsTr("Comfortable"), value: "comfortable", objectName: "activityDisplayComfortable" },
                { text: qsTr("Compact"), value: "compact", objectName: "activityDisplayCompact" }
            ]
            onActivated: function(value) {
                activitySettings.activityDisplayDensity = value
                moreMenu.close()
            }
        }
        ContextMenuDivider {}
        ContextMenuButton {
            objectName: "activityExportButton"
            enabled: !activityFilterProxy.exportBusy && !activityFilterProxy.busy
            text: qsTr("Export CSV…")
            iconSource: "qrc:/icons/export"
            onTriggered: {
                if (automationExportPathField.text.length > 0) {
                    const path = automationExportPathField.text
                    automationExportPathField.text = ""
                    root.exportActivity(path)
                } else root.beginExportSelection()
            }
        }
    }

    Instantiator {
        model: root.csvExport ? [root.csvExport] : []
        delegate: SaveConfirmationPopup {
            required property ActivityExportOperation modelData
            objectName: "activityCsvDestinationConfirmation"
            parent: root.Overlay.overlay
            visible: modelData.confirmationRequired
            replacing: modelData.replacing
            finalPath: modelData.finalPath
            saveTitle: qsTr("Save Activity CSV?")
            replaceTitle: qsTr("Replace Activity CSV?")
            pathObjectName: "activityCsvFinalDestination"
            approveObjectName: "activityCsvApproveDestination"
            refuseObjectName: "activityCsvRefuseDestination"
            onApproved: modelData.approve()
            onRefused: modelData.refuse()
        }
    }

    AlertPopup {
        id: exportResultPopup
        objectName: "activityExportResultPopup"
        parent: Overlay.overlay
        tone: root.exportSucceeded ? AlertPopup.Success : AlertPopup.Error
        iconSource: root.exportSucceeded ? "qrc:/icons/check" : "qrc:/icons/cross"
        title: root.exportResultTitle()
        titleObjectName: "activityExportResultTitle"
        message: root.exportResultDescription()
        messageObjectName: "activityExportResultDescription"

        AlertAction {
            text: qsTr("Close")
            buttonObjectName: "activityExportResultCloseButton"
        }
    }

    ContextMenu {
        id: datePopup
        objectName: "activityDateFilterPopup"
        parent: dateFilterButton
        y: datePopup.parent.height + 2
        minMenuWidth: 280
        modal: true
        dim: false

        property bool dateCustomExpanded: false

        // Seed before the popup becomes visible, not from onOpened:
        // onOpened only fires once the enter transition finishes, so a
        // click landing during the animation (a fast user, or the test
        // bridge) would be undone by the late re-seed.
        onAboutToShow: {
            x = root.filterPopupX(dateFilterButton, datePopup)
            calendar.refreshToday()
            var applied = activityFilterProxy.dateFilter === ActivityFilterProxyModel.CustomRange
            datePopup.dateCustomExpanded = applied
            // Only seed the draft from an applied range; otherwise start
            // clean, so a reset or a discarded selection does not reappear.
            if (applied) {
                calendar.seed(root.modelDateToLocal(activityFilterProxy.rangeStart),
                              root.modelDateToLocal(activityFilterProxy.rangeEnd))
            } else {
                calendar.seed(null, null)
            }
        }

        onClosed: {
            // Restore the pane to match the applied filter on close, so a
            // reopen shows the right pane from the first frame instead of
            // flashing the calendar before it collapses back to presets.
            datePopup.dateCustomExpanded =
                activityFilterProxy.dateFilter === ActivityFilterProxyModel.CustomRange
        }

        ContextMenuPicker {
            // Hidden while picking a custom range so the popup shows the
            // calendar instead of presets plus calendar (which overflows).
            visible: !datePopup.dateCustomExpanded
            objectNameRole: "objectName"
            currentValue: activityFilterProxy.dateFilter
            model: [
                { text: qsTr("Today"),        value: ActivityFilterProxyModel.Today,       objectName: "activityDateToday" },
                { text: qsTr("This week"),    value: ActivityFilterProxyModel.ThisWeek,    objectName: "activityDateThisWeek" },
                { text: qsTr("This month"),   value: ActivityFilterProxyModel.ThisMonth,   objectName: "activityDateThisMonth" },
                { text: qsTr("This year"),    value: ActivityFilterProxyModel.ThisYear,    objectName: "activityDateThisYear" }
            ]
            onActivated: function(value) {
                activityFilterProxy.dateFilter = value
                datePopup.dateCustomExpanded = false
                datePopup.close()
            }
        }

        ContextMenuDivider { visible: !datePopup.dateCustomExpanded }
        ContextMenuPicker {
            visible: !datePopup.dateCustomExpanded
            objectNameRole: "objectName"
            currentValue: activityFilterProxy.dateFilter
            model: [{ text: qsTr("Custom range"), value: ActivityFilterProxyModel.CustomRange, objectName: "activityDateCustomRange" }]
            onActivated: datePopup.dateCustomExpanded = true
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            Layout.bottomMargin: 6
            spacing: 8
            visible: datePopup.dateCustomExpanded

            // Return to the preset list.
            ItemDelegate {
                id: presetsBackButton
                objectName: "activityDatePresetsBack"
                Layout.fillWidth: true
                implicitHeight: 30
                padding: 0
                focusPolicy: Qt.StrongFocus
                background: Item {
                    FocusBorder {
                        visible: presetsBackButton.visualFocus
                        topMargin: 0
                        bottomMargin: 0
                        leftMargin: -4
                        rightMargin: -4
                        borderRadius: 5
                    }
                }
                //: Accessibility label for the control that leaves the custom date range calendar and returns to the date presets.
                Accessible.name: qsTr("Back to date presets")
                Accessible.role: Accessible.Button
                contentItem: RowLayout {
                    spacing: 4
                    Icon {
                        source: "image://images/caret-left"
                        color: Theme.color.accentText
                        size: 14
                    }
                    CoreText {
                        Layout.fillWidth: true
                        //: Activity custom date range: link back to the list of date presets.
                        text: qsTr("Presets")
                        color: Theme.color.accentText
                        textStyle: Theme.text.captionStrong
                        horizontalAlignment: Text.AlignLeft
                        verticalAlignment: Text.AlignVCenter
                    }
                }
                onClicked: datePopup.dateCustomExpanded = false
            }

            Separator { Layout.fillWidth: true }

            ActivityCalendar {
                id: calendar
                objectName: "activityCalendar"
                Layout.fillWidth: true
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 2

                LinkButton {
                    objectName: "activityDateRangeReset"
                    //: Button that clears the Activity custom date range.
                    text: qsTr("Reset")
                    textStyle: Theme.text.menuItemStrong
                    // Nothing to reset until a date has been picked.
                    enabled: calendar.hasSelection
                        || activityFilterProxy.dateFilter !== ActivityFilterProxyModel.DateAll
                    textColor: !enabled ? Theme.color.disabledContent
                        : down || hovered ? Theme.color.neutral9 : Theme.color.neutral7
                    // Clear the picked dates and drop the applied filter,
                    // but stay in the calendar so a new range can be picked.
                    onClicked: {
                        calendar.reset()
                        activityFilterProxy.clearDateFilter()
                    }
                }

                Item { Layout.fillWidth: true }

                LinkButton {
                    objectName: "activityDateRangeApply"
                    //: Button that applies the picked Activity custom date range.
                    text: qsTr("Apply")
                    textStyle: Theme.text.menuItemStrong
                    enabled: calendar.valid
                    // Pass ISO strings rather than the Dates: a JS Date
                    // converted straight to QDate can shift a day across
                    // time zones. applyCustomRange selects the custom
                    // date filter along with the range.
                    onClicked: {
                        activityFilterProxy.applyCustomRange(calendar.startIso, calendar.endIso)
                        datePopup.close()
                    }
                }
            }
        }
    }

    ContextMenu {
        id: typePopup
        objectName: "activityTypeFilterPopup"
        parent: typeFilterButton
        y: typePopup.parent.height + 2
        modal: true
        dim: false

        onAboutToShow: x = root.filterPopupX(typeFilterButton, typePopup)

        ContextMenuPicker {
            objectNameRole: "objectName"
            currentValue: root.typeFilterActive ? -1 : ActivityFilterProxyModel.TypeAll
            model: [{ text: qsTr("All activity"), value: ActivityFilterProxyModel.TypeAll, objectName: "activityTypeAll" }]
            onActivated: activityFilterProxy.typeFilters = []
        }
        ContextMenuDivider {}
        ContextMenuPicker {
            objectNameRole: "objectName"
            multiSelect: true
            selectedValues: activityFilterProxy.typeFilters
            model: [
                { text: qsTr("Received"),         value: ActivityFilterProxyModel.Received,       objectName: "activityTypeReceived" },
                { text: qsTr("Sent"),             value: ActivityFilterProxyModel.Sent,           objectName: "activityTypeSent" },
                { text: qsTr("Sent to yourself"), value: ActivityFilterProxyModel.SentToSelf,     objectName: "activityTypeSentToSelf" },
                { text: qsTr("Multiple actions"), value: ActivityFilterProxyModel.Multiple, objectName: "activityTypeMultiple" },
                { text: qsTr("Payment request"),  value: ActivityFilterProxyModel.PaymentRequest, objectName: "activityTypePaymentRequest" },
                { text: qsTr("Mined"),            value: ActivityFilterProxyModel.Mined,          objectName: "activityTypeMined" }
            ]
            onActivated: function(value) { root.toggleActivityType(value) }
        }
    }

    ContextMenu {
        id: amountPopup
        objectName: "activityAmountFilterPopup"
        parent: amountFilterButton
        y: amountFilterButton.height + 2
        implicitWidth: Math.min(320, root.width)
        modal: true
        dim: false

        onAboutToShow: x = root.filterPopupX(amountFilterButton, amountPopup)

        ColumnLayout {
            Layout.fillWidth: true
            Layout.margins: 12
            spacing: 8

            BitcoinAmountDisplayLabel {
                hidesAmount: true
                objectName: "activityAmountRangeLabel"
                Layout.fillWidth: true
                text: qsTr("%1 to %2").arg(lowerAmountFormatter.display).arg(upperAmountFormatter.displayWithUnit)
                textStyle: Theme.text.description
                horizontalAlignment: Text.AlignLeft
            }
            RangeSlider {
                id: amountSlider
                objectName: "activityAmountRangeSlider"
                Layout.fillWidth: true
                valuesHidden: ApplicationModels.optionsModel.hideAmounts
                minValue: 0
                maxValue: Math.max(activityFilterProxy.availableMaxAmount,
                                   activityFilterProxy.minAmount, activityFilterProxy.maxAmount)
                selectedLower: Math.max(0, activityFilterProxy.minAmount)
                selectedUpper: activityFilterProxy.maxAmount < 0 ? Math.max(activityFilterProxy.availableMaxAmount, activityFilterProxy.minAmount) : activityFilterProxy.maxAmount
                Accessible.name: qsTr("Activity amount range")
                first.onMoved: root.applyAmountRange()
                second.onMoved: root.applyAmountRange()
            }

        }
    }

    ContextMenu {
        id: clearFiltersMenu
        objectName: "activityClearFiltersMenu"
        parent: activeFiltersButton
        y: activeFiltersButton.height + 2
        minMenuWidth: 180
        modal: true
        dim: false
        onAboutToShow: x = root.filterPopupX(activeFiltersButton, clearFiltersMenu)
        ContextMenuButton {
            objectName: "activityClearFiltersAction"
            text: qsTr("Clear filters")
            onTriggered: root.clearFilters()
        }
    }
}
