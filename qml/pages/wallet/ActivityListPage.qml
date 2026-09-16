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
    readonly property bool typeFilterActive: activityFilterProxy.typeFilters.length > 0
    readonly property bool dateFilterActive: activityFilterProxy.dateFilter !== ActivityFilterProxyModel.DateAll
    readonly property bool amountFilterActive: activityFilterProxy.minAmount >= 0 || activityFilterProxy.maxAmount >= 0
    readonly property int activeFilterCount: Number(typeFilterActive) + Number(dateFilterActive) + Number(amountFilterActive)
    readonly property real pageSidePadding: width < 640 ? 16 : 40
    readonly property real activityContentWidth: Math.max(0, Math.min(1100, width - pageSidePadding * 2))
    readonly property real activitySideInset: (width - activityContentWidth) / 2

    signal transactionRequested(string txid)
    signal paymentRequestRequested(string requestId)

    background: Rectangle { color: Theme.color.neutral0 }
    padding: 0
    Component.onCompleted: ready = true
    onWalletChanged: if (ready) {
        activityRowMenu.close()
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
        displayUnit: optionsModel.displayUnit
        groupBy: activitySettings.activityGrouping === "day" ? ActivityFilterProxyModel.Day : ActivityFilterProxyModel.Month
    }
    BitcoinAmount {
        id: lowerAmountFormatter
        unit: optionsModel.displayUnit
        satoshi: Math.round(amountSlider.lowerValue)
    }
    BitcoinAmount {
        id: upperAmountFormatter
        unit: optionsModel.displayUnit
        satoshi: Math.round(amountSlider.upperValue)
    }
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
                    NumberAnimation { duration: 200; easing.type: Easing.InOutCubic }
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
                anchors.fill: parent
                clip: true
                model: activityFilterProxy
                reuseItems: true
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {}
                section.property: "sectionLabel"
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
                        anchors.leftMargin: root.activitySideInset
                        anchors.rightMargin: root.activitySideInset
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: 12
                        spacing: 12
                        CoreText {
                            objectName: "activitySectionTitle"
                            Layout.fillWidth: true
                            text: sectionHeader.section
                            font: Theme.text.subheading.font
                            horizontalAlignment: Text.AlignLeft
                        }
                        CoreText {
                            objectName: sectionHeader.isPending ? "activityPendingBalance" : ""
                            visible: sectionHeader.isPending && activityFilterProxy.pendingBalanceSat !== 0
                            text: visible ? activityFilterProxy.pendingBalance : ""
                            font: Theme.text.subheading.font
                            color: !visible ? Theme.color.neutral7 : activityFilterProxy.pendingBalanceSat > 0 ? Theme.color.green
                                : activityFilterProxy.pendingBalanceSat < 0 ? Theme.color.neutral9 : Theme.color.neutral7
                            horizontalAlignment: Text.AlignRight
                        }
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

        function seedRange() {
            const maximum = activityFilterProxy.availableMaxAmount
            amountSlider.setValues(Math.min(Math.max(0, activityFilterProxy.minAmount), maximum),
                activityFilterProxy.maxAmount < 0 ? maximum : Math.min(activityFilterProxy.maxAmount, maximum))
        }
        onAboutToShow: {
            x = root.filterPopupX(amountFilterButton, amountPopup)
            seedRange()
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.margins: 12
            spacing: 8

            CoreText {
                objectName: "activityAmountRangeLabel"
                Layout.fillWidth: true
                text: qsTr("%1 to %2").arg(lowerAmountFormatter.display).arg(upperAmountFormatter.displayWithUnit)
                font: Theme.text.description.font
                horizontalAlignment: Text.AlignLeft
            }
            RangeSlider {
                id: amountSlider
                objectName: "activityAmountRangeSlider"
                Layout.fillWidth: true
                minValue: 0
                maxValue: activityFilterProxy.availableMaxAmount
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
            role: ContextMenuButton.Destructive
            onTriggered: root.clearFilters()
        }
    }
}
