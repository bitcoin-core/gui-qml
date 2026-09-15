// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Dialogs
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0
import "../../controls"
import "../../components"

Page {
    id: root
    objectName: "activityPage"
    property var wallet: walletController.selectedWallet
    property bool exportSucceeded: true
    property bool ready: false
    readonly property bool activitySourceEmpty: !wallet || wallet.transactionActivityModel.count === 0
    readonly property bool activityFiltersActive: activityFilterProxy.searchText.trim().length > 0
        || activityFilterProxy.dateFilter !== ActivityFilterProxyModel.DateAll
        || activityFilterProxy.typeFilter !== ActivityFilterProxyModel.TypeAll
        || activityFilterProxy.minAmount >= 0
    readonly property real pageSidePadding: width < 640 ? 16 : 40
    readonly property real activityContentWidth: Math.max(0, Math.min(1100, width - pageSidePadding * 2))
    readonly property real activitySideInset: (width - activityContentWidth) / 2

    signal transactionRequested(string txid)
    signal paymentRequestRequested(string requestId)

    background: Rectangle { color: Theme.color.neutral0 }
    padding: 0
    Component.onCompleted: ready = true
    onWalletChanged: if (ready) { activityRowMenu.close(); clearFilters() }

    function clearFilters() {
        activityFilterProxy.searchText = ""
        activityFilterProxy.dateFilter = ActivityFilterProxyModel.DateAll
        activityFilterProxy.typeFilter = ActivityFilterProxyModel.TypeAll
        activityFilterProxy.minAmount = -1
        searchField.text = ""
        datePopup.close()
        typePopup.close()
        amountPopup.close()
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
        if (activitySourceEmpty)
            return nodeModel.blockSyncActive ? qsTr("Syncing wallet activity…") : qsTr("No activity yet")
        return qsTr("No activity matches your filters.")
    }
    function emptyActivityDescription() {
        if (activitySourceEmpty)
            return nodeModel.blockSyncActive ? qsTr("Transactions may appear as your wallet catches up.")
                : qsTr("Your transactions and payment requests will appear here.")
        return qsTr("Try changing your search, date, type, or amount filters.")
    }
    function exportResultTitle() { return exportSucceeded ? qsTr("Export complete") : qsTr("Export failed") }
    function exportResultDescription() {
        return exportSucceeded ? qsTr("Your Activity CSV has been saved.")
            : qsTr("The Activity CSV could not be saved. Check the file path and try again.")
    }

    function normalizeLocalPath(path) {
        var value = String(path)
        if (value.indexOf("file:///") === 0) {
            if (Qt.platform.os === "windows") {
                return decodeURIComponent(value.substring(8))
            }
            return decodeURIComponent(value.substring(7))
        }
        if (value.indexOf("file://") === 0) {
            return decodeURIComponent(value.substring(7))
        }
        return value
    }

    function exportActivity(path) {
        const ok = activityFilterProxy.exportCsv(normalizeLocalPath(path))
        exportSucceeded = ok
        exportResultPopup.open()
    }

    function dateFilterText() {
        switch (activityFilterProxy.dateFilter) {
        case ActivityFilterProxyModel.Today:
            return qsTr("Today")
        case ActivityFilterProxyModel.ThisWeek:
            return qsTr("This week")
        case ActivityFilterProxyModel.ThisMonth:
            return qsTr("This month")
        case ActivityFilterProxyModel.ThisYear:
            return qsTr("This year")
        case ActivityFilterProxyModel.CustomRange:
            //: Activity date filter option for a user-picked start and end date.
            return qsTr("Custom range")
        default:
            return qsTr("All dates")
        }
    }

    function amountFilterText() {
        if (activityFilterProxy.minAmount >= 0) {
            //: Activity amount filter button: shows the active minimum with its unit, e.g. "≥ 0.50000000 ₿".
            return qsTr("≥ %1").arg(amountFormatter.displayWithUnit)
        }
        //: Activity amount filter button default: no minimum amount is set.
        return qsTr("Amount")
    }

    // Placeholder and input mask for the minimum amount field, per
    // display unit. The integer digits keep any accepted value below
    // MAX_MONEY (2.1e15 sat) and within the JS exact integer range for
    // the display round trip.
    //
    // The unit is a BitcoinAmount.Unit value, compared by number rather
    // than by name: QML only exposes enum values whose name begins with
    // a capital letter, so BitcoinAmount.mBTC and BitcoinAmount.uBTC
    // read as undefined here and every comparison against them is false.
    function minAmountPlaceholder(unit) {
        switch (unit) {
        case 3: return "0"           // SAT
        case 2: return "0.00"        // uBTC (bits)
        case 1: return "0.00000"     // mBTC
        default: return "0.00000000" // BTC
        }
    }

    function minAmountPattern(unit) {
        switch (unit) {
        case 3: return /^[0-9]{0,15}$/                    // SAT
        case 2: return /^[0-9]{0,13}(\.[0-9]{0,2})?$/     // uBTC (bits)
        case 1: return /^[0-9]{0,10}(\.[0-9]{0,5})?$/     // mBTC
        default: return /^[0-9]{0,8}(\.[0-9]{0,8})?$/     // BTC
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
    // midnight; rebuild it at local midnight on the same calendar day so
    // seeding the calendar does not read it a day early west of UTC (the
    // read-side mirror of the Apply path, which hands the model ISO
    // strings for the same reason).
    function modelDateToLocal(date) {
        return (date && !isNaN(date.getTime()))
            ? new Date(date.getUTCFullYear(), date.getUTCMonth(), date.getUTCDate())
            : null
    }

    function applyMinAmount() {
        var trimmed = minAmountField.text.trim()
        if (trimmed.length === 0) {
            activityFilterProxy.minAmount = -1
            amountPopup.close()
            return
        }
        amountParser.unit = optionsModel.displayUnit
        amountParser.display = trimmed
        var sats = amountParser.satoshi
        activityFilterProxy.minAmount = sats > 0 ? sats : -1
        amountPopup.close()
    }

    function typeFilterText() {
        switch (activityFilterProxy.typeFilter) {
        case ActivityFilterProxyModel.Received:
            return qsTr("Received")
        case ActivityFilterProxyModel.Sent:
            return qsTr("Sent")
        case ActivityFilterProxyModel.SentToSelf:
            return qsTr("Sent to yourself")
        case ActivityFilterProxyModel.Mined:
            return qsTr("Mined")
        case ActivityFilterProxyModel.Multiple: return qsTr("Multiple actions")
        case ActivityFilterProxyModel.Consolidation: return qsTr("Consolidation")
        case ActivityFilterProxyModel.Split: return qsTr("Split")
        case ActivityFilterProxyModel.Other:
            //: Activity type filter option for transactions that are not received, sent, sent to yourself, or mined.
            return qsTr("Other")
        case ActivityFilterProxyModel.PaymentRequest:
            return qsTr("Payment request")
        default:
            return qsTr("All activity")
        }
    }


    ActivityFilterProxyModel {
        id: activityFilterProxy
        objectName: "activityFilterProxyModel"
        sourceModel: root.wallet ? root.wallet.transactionActivityModel : null
        displayUnit: optionsModel.displayUnit
        groupBy: activitySettings.activityGrouping === "day" ? ActivityFilterProxyModel.Day : ActivityFilterProxyModel.Month
    }
    BitcoinAmount {
        id: amountFormatter
        unit: optionsModel.displayUnit
        satoshi: Math.max(0, activityFilterProxy.minAmount)
    }
    BitcoinAmount { id: amountParser }
    AppSettings {
        id: activitySettings
        property string activityGrouping: "month"
        property string activityDisplayDensity: "comfortable"
    }
    FileDialog {
        id: exportDialog
        defaultSuffix: "csv"
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("Comma separated file (*.csv)")]
        onAccepted: root.exportActivity(exportDialog.selectedFile.toString())
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
                    font: Theme.text.headline.font
                    lineHeight: Theme.text.headline.lineHeight
                    lineHeightMode: Text.FixedHeight
                    horizontalAlignment: Text.AlignLeft
                }
                CoreText {
                    Layout.fillWidth: true
                    text: qsTr("Your wallet’s payments and transactions")
                    font: Theme.text.description.font
                    color: Theme.color.neutral7
                    horizontalAlignment: Text.AlignLeft
                }
                CoreText {
                    visible: root.width < 640
                    Layout.fillWidth: true
                    text: root.countText()
                    font: Theme.text.caption.font
                    color: Theme.color.neutral7
                    horizontalAlignment: Text.AlignLeft
                }
            }
            CoreText {
                objectName: "activityCount"
                visible: root.width >= 640
                Layout.alignment: Qt.AlignVCenter
                text: root.countText()
                font: Theme.text.caption.font
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
                iconColor: Theme.color.neutral7
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
            DropdownButton {
                id: typeFilterButton
                objectName: "activityTypeFilterButton"
                text: root.typeFilterText()
                opened: typePopup.visible
                onClicked: typePopup.opened ? typePopup.close() : typePopup.open()
            }
            DropdownButton {
                id: dateFilterButton
                objectName: "activityDateFilterButton"
                text: root.dateFilterText()
                opened: datePopup.visible
                onClicked: datePopup.opened ? datePopup.close() : datePopup.open()
            }
            DropdownButton {
                id: amountFilterButton
                objectName: "activityAmountFilterButton"
                text: root.amountFilterText()
                opened: amountPopup.visible
                onClicked: amountPopup.opened ? amountPopup.close() : amountPopup.open()
            }
            TextButton {
                objectName: "activityClearFiltersButton"
                visible: root.activityFiltersActive
                height: typeFilterButton.height
                topPadding: 0
                bottomPadding: 0
                text: qsTr("Clear filters")
                textSize: 13
                textColor: Theme.color.neutral7
                onClicked: root.clearFilters()
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 0

            ListView {
                id: listView
                objectName: "activityListView"
                anchors.fill: parent
                clip: true
                model: activityFilterProxy
                reuseItems: true
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {}
                section.property: "sectionLabel"
                section.criteria: ViewSection.FullString
                section.delegate: Item {
                    required property string section
                    width: listView.width
                    height: 56
                    CoreText {
                        objectName: "activitySectionTitle"
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.leftMargin: root.activitySideInset
                        anchors.rightMargin: root.activitySideInset
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: 12
                        text: parent.section
                        font: Theme.text.subheading.font
                        horizontalAlignment: Text.AlignLeft
                    }
                }
                delegate: Item {
                    id: activityDelegate
                    required property var model
                    width: listView.width
                    height: activityRow.implicitHeight
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
                            const point = activityRow.mapToItem(activityRowMenu.parent, x, y)
                            activityRowMenu.txid = activityRow.txid
                            activityRowMenu.requestId = activityRow.requestId
                            activityRowMenu.isRequest = activityRow.isPendingRequest
                            activityRowMenu.x = point.x
                            activityRowMenu.y = point.y
                            activityRowMenu.open()
                        }
                        onActivated: function(txid, requestId, isRequest) {
                            if (isRequest) root.paymentRequestRequested(requestId)
                            else root.transactionRequested(txid)
                        }
                    }
                }
            }

            ColumnLayout {
                objectName: "activityEmptyState"
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.top: parent.top
                anchors.topMargin: 64
                width: Math.min(root.activityContentWidth, 440)
                visible: walletController.initialized && activityFilterProxy.count === 0
                spacing: 10
                CoreText {
                    objectName: "activityEmptyStateTitle"
                    Layout.fillWidth: true
                    text: root.emptyActivityTitle()
                    font: Theme.text.heading.font
                }
                CoreText {
                    objectName: "activityEmptyStateDescription"
                    Layout.fillWidth: true
                    text: root.emptyActivityDescription()
                    font: Theme.text.description.font
                    color: Theme.color.neutral7
                }
                TextButton {
                    visible: root.activityFiltersActive
                    Layout.alignment: Qt.AlignHCenter
                    text: qsTr("Clear filters")
                    onClicked: root.clearFilters()
                }
            }

            Column {
                visible: !walletController.initialized
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

    ContextMenu {
        id: activityRowMenu
        objectName: "activityRowContextMenu"
        backgroundColor: Theme.color.neutral2
        parent: Overlay.overlay
        margins: 8
        property string txid: ""
        property string requestId: ""
        property bool isRequest: false
        ContextMenuButton {
            objectName: "activityCopyTransactionId"
            visible: !activityRowMenu.isRequest
            text: qsTr("Copy transaction ID")
            onTriggered: Clipboard.setText(activityRowMenu.txid)
        }
        ContextMenuButton {
            objectName: "activityCopyRawTransaction"
            visible: !activityRowMenu.isRequest
            text: qsTr("Copy raw transaction hex")
            onTriggered: {
                const hex = root.wallet ? root.wallet.transactionActivityModel.rawTransaction(activityRowMenu.txid) : ""
                if (hex.length > 0) Clipboard.setText(hex)
            }
        }
        ContextMenuButton {
            objectName: "activityCopyPaymentRequest"
            visible: activityRowMenu.isRequest
            text: qsTr("Copy payment request")
            onTriggered: {
                const uri = root.wallet ? root.wallet.transactionActivityModel.paymentRequestUri(activityRowMenu.requestId) : ""
                if (uri.length > 0) Clipboard.setText(uri)
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
            text: qsTr("Export CSV")
            iconSource: "qrc:/icons/export"
            onTriggered: {
                if (automationExportPathField.text.length > 0) {
                    const path = automationExportPathField.text
                    automationExportPathField.text = ""
                    root.exportActivity(path)
                } else exportDialog.open()
            }
        }
    }

    Popup {
        id: exportResultPopup
        objectName: "activityExportResultPopup"
        modal: true
        anchors.centerIn: parent
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        padding: 20
        width: Math.min(420, root.width - 40)
        height: exportResultLayout.implicitHeight + 40

        background: Rectangle {
            color: Theme.color.neutral0
            border.color: Theme.color.neutral4
            border.width: 1
            radius: 5
        }

        contentItem: ColumnLayout {
            id: exportResultLayout
            spacing: 20

            Item {
                Layout.alignment: Qt.AlignHCenter
                width: 60
                height: 60

                Rectangle {
                    anchors.fill: parent
                    radius: 30
                    color: root.exportSucceeded ? Theme.color.green : Theme.color.red
                    opacity: 0.2
                }

                Icon {
                    anchors.centerIn: parent
                    source: root.exportSucceeded ? "qrc:/icons/check" : "qrc:/icons/cross"
                    color: root.exportSucceeded ? Theme.color.green : Theme.color.red
                    size: 30
                }
            }

            CoreText {
                objectName: "activityExportResultTitle"
                Layout.alignment: Qt.AlignHCenter
                Layout.fillWidth: true
                text: root.exportResultTitle()
                font.pixelSize: 28
                bold: true
                horizontalAlignment: Text.AlignHCenter
            }

            CoreText {
                objectName: "activityExportResultDescription"
                Layout.alignment: Qt.AlignHCenter
                Layout.fillWidth: true
                color: Theme.color.neutral7
                text: root.exportResultDescription()
                font.pixelSize: 18
                wrap: true
                horizontalAlignment: Text.AlignHCenter
            }

            ContinueButton {
                objectName: "activityExportResultCloseButton"
                Layout.preferredWidth: Math.min(200, parent.width - 40)
                Layout.alignment: Qt.AlignCenter
                text: qsTr("Close window")
                borderColor: Theme.color.neutral6
                borderHoverColor: Theme.color.neutral9
                borderPressedColor: Theme.color.neutral9
                textColor: Theme.color.neutral9
                backgroundColor: "transparent"
                backgroundHoverColor: "transparent"
                backgroundPressedColor: "transparent"
                onClicked: exportResultPopup.close()
            }
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
                { text: qsTr("All"),          value: ActivityFilterProxyModel.DateAll,     objectName: "activityDateAll" },
                { text: qsTr("Today"),        value: ActivityFilterProxyModel.Today,       objectName: "activityDateToday" },
                { text: qsTr("This week"),    value: ActivityFilterProxyModel.ThisWeek,    objectName: "activityDateThisWeek" },
                { text: qsTr("This month"),   value: ActivityFilterProxyModel.ThisMonth,   objectName: "activityDateThisMonth" },
                { text: qsTr("This year"),    value: ActivityFilterProxyModel.ThisYear,    objectName: "activityDateThisYear" },
                //: Activity date filter menu entry that opens the custom start/end date calendar.
                { text: qsTr("Custom range"), value: ActivityFilterProxyModel.CustomRange, objectName: "activityDateCustomRange" }
            ]
            onActivated: function(value) {
                if (value === ActivityFilterProxyModel.CustomRange) {
                    datePopup.dateCustomExpanded = true
                    return
                }
                activityFilterProxy.dateFilter = value
                datePopup.dateCustomExpanded = false
                datePopup.close()
            }
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
                objectName: "activityDatePresetsBack"
                Layout.fillWidth: true
                implicitHeight: 30
                padding: 0
                //: Accessibility label for the control that leaves the custom date range calendar and returns to the date presets.
                Accessible.name: qsTr("Back to date presets")
                Accessible.role: Accessible.Button
                contentItem: RowLayout {
                    spacing: 4
                    Icon {
                        source: "image://images/caret-left"
                        color: Theme.color.orange
                        size: 14
                    }
                    CoreText {
                        Layout.fillWidth: true
                        //: Activity custom date range: link back to the list of date presets.
                        text: qsTr("Presets")
                        color: Theme.color.orange
                        font.pixelSize: 14
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

                TextButton {
                    objectName: "activityDateRangeReset"
                    //: Button that clears the Activity custom date range.
                    text: qsTr("Reset")
                    textSize: 15
                    // Nothing to reset until a date has been picked.
                    enabled: calendar.hasSelection
                    textColor: enabled ? Theme.color.neutral7 : Theme.color.neutral5
                    // A disabled control still receives hover, which would
                    // trigger the button's hover recolor; gate it on enabled.
                    hoverEnabled: enabled && AppMode.isDesktop
                    // Clear the picked dates and drop the applied filter,
                    // but stay in the calendar so a new range can be picked.
                    onClicked: {
                        calendar.reset()
                        activityFilterProxy.dateFilter = ActivityFilterProxyModel.DateAll
                    }
                }

                Item { Layout.fillWidth: true }

                TextButton {
                    objectName: "activityDateRangeApply"
                    //: Button that applies the picked Activity custom date range.
                    text: qsTr("Apply")
                    textSize: 15
                    enabled: calendar.valid
                    textColor: enabled ? Theme.color.orange : Theme.color.neutral5
                    hoverEnabled: enabled && AppMode.isDesktop
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
            currentValue: activityFilterProxy.typeFilter
            model: [
                { text: qsTr("All"),              value: ActivityFilterProxyModel.TypeAll,        objectName: "activityTypeAll" },
                { text: qsTr("Received"),         value: ActivityFilterProxyModel.Received,       objectName: "activityTypeReceived" },
                { text: qsTr("Sent"),             value: ActivityFilterProxyModel.Sent,           objectName: "activityTypeSent" },
                { text: qsTr("Sent to yourself"), value: ActivityFilterProxyModel.SentToSelf,     objectName: "activityTypeSentToSelf" },
                { text: qsTr("Multiple actions"), value: ActivityFilterProxyModel.Multiple, objectName: "activityTypeMultiple" },
                { text: qsTr("Payment request"),  value: ActivityFilterProxyModel.PaymentRequest, objectName: "activityTypePaymentRequest" },
                { text: qsTr("Mined"),            value: ActivityFilterProxyModel.Mined,          objectName: "activityTypeMined" }
            ]
            onActivated: function(value) {
                activityFilterProxy.typeFilter = value
                typePopup.close()
            }
        }
    }

    ContextMenu {
        id: amountPopup
        objectName: "activityAmountFilterPopup"
        parent: amountFilterButton
        y: amountFilterButton.height + 2
        minMenuWidth: 280
        modal: true
        dim: false

        onAboutToShow: {
            x = root.filterPopupX(amountFilterButton, amountPopup)
            minAmountField.text = activityFilterProxy.minAmount >= 0 ? amountFormatter.display : ""
        }

        onOpened: {
            minAmountField.forceActiveFocus()
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.margins: 6
            spacing: 6

            CoreText {
                //: Heading of the Activity amount filter popup.
                text: qsTr("Minimum amount")
                color: Theme.color.neutral7
                font.pixelSize: 13
                horizontalAlignment: Text.AlignLeft
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                ActivityFilterInput {
                    id: minAmountField
                    objectName: "activityMinAmountField"
                    //: Accessibility label for the Activity minimum amount filter input.
                    Accessible.name: qsTr("Minimum amount")
                    Layout.fillWidth: true
                    inputMethodHints: Qt.ImhFormattedNumbersOnly
                    placeholderText: root.minAmountPlaceholder(amountFormatter.unit)
                    validator: RegularExpressionValidator {
                        regularExpression: root.minAmountPattern(amountFormatter.unit)
                    }
                    onAccepted: root.applyMinAmount()
                }

                CoreText {
                    text: amountFormatter.unitLabel
                    color: Theme.color.neutral7
                    font.pixelSize: 15
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 2

                TextButton {
                    objectName: "activityMinAmountReset"
                    //: Button that clears the Activity minimum amount filter.
                    text: qsTr("Reset")
                    textSize: 15
                    textColor: Theme.color.neutral7
                    onClicked: {
                        minAmountField.text = ""
                        activityFilterProxy.minAmount = -1
                        amountPopup.close()
                    }
                }

                Item { Layout.fillWidth: true }

                TextButton {
                    objectName: "activityMinAmountApply"
                    //: Button that applies the Activity minimum amount filter.
                    text: qsTr("Apply")
                    textSize: 15
                    onClicked: root.applyMinAmount()
                }
            }
        }
    }


    component ActivityFilterInput: TextField {
        implicitHeight: 40
        leftPadding: 12
        rightPadding: 12
        topPadding: 0
        bottomPadding: 0
        color: Theme.color.neutral9
        placeholderTextColor: Theme.color.neutral7
        font: Theme.text.description.font
        verticalAlignment: TextInput.AlignVCenter
        selectByMouse: true
        background: Rectangle { color: Theme.color.neutral2; radius: 8 }
    }
}
