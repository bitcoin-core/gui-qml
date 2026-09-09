// Copyright (c) 2024-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Dialogs
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0

import "../../controls"
import "../../components"

PageStack {
    id: stackView
    objectName: "activityStack"

    function navigateToTransaction(txid, outputIndex) {
        if (!walletController.selectedWallet)
            return

        const model = walletController.selectedWallet.activityListModel
        var details = outputIndex === undefined || outputIndex < 0
            ? model.firstTransactionDetails(txid)
            : model.transactionDetails(txid, outputIndex)
        if (Object.keys(details).length === 0)
            return

        // Show the transaction on top of the Activity list rather than on top of
        // whatever detail page is still open. The wallet tabs are a StackLayout
        // that keeps this stack alive, so a detail page opened before the user
        // went to Send is still here when that send's "View Transaction" arrives:
        // without the reset, Back walks into that stale page, which by then is
        // bound to rows the accompanying reload() has already invalidated.
        stackView.pop(null)
        var page = stackView.push("ActivityDetails.qml", details)
        page.showTransaction.connect(stackView.navigateToTransaction)
    }

    Connections {
        target: walletController
        function onSelectedWalletChanged() {
            stackView.pop(null)
        }
        function onClosePaymentRequestDetailRequested() {
            stackView.pop(null)
        }
    }

    Binding {
        target: walletController.selectedWallet
        property: "displayUnit"
        value: optionsModel.displayUnit
        when: walletController.selectedWallet !== null
    }

    initialItem: RowLayout {
        Page {
            id: root
            objectName: "activityPage"

            Layout.alignment: Qt.AlignCenter
            Layout.fillHeight: true
            Layout.fillWidth: true
            Layout.maximumWidth: 520

            property bool filtersVisible: false
            property bool exportSucceeded: true
            readonly property int activityListTopMargin: 10
            readonly property int activityListBottomMargin: 10
            readonly property bool activitySourceEmpty: walletController.selectedWallet.activityListModel.count === 0
            readonly property bool activityFiltersActive: activityFilterProxy.searchText.trim().length > 0
                || activityFilterProxy.dateFilter !== ActivityFilterProxyModel.DateAll
                || activityFilterProxy.typeFilter !== ActivityFilterProxyModel.TypeAll
                || activityFilterProxy.minAmount >= 0

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

            function toggleFilters() {
                filtersVisible = !filtersVisible
                if (!filtersVisible) {
                    clearFilters()
                    return
                }
                searchField.forceActiveFocus()
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

            function defaultExportFileUrl() {
                const stamp = Qt.formatDateTime(new Date(), "yyyy-MM-dd-HHmm")
                return "file://" + walletController.homePath() + "/activity-" + stamp + ".csv"
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
                return qsTr("Any amount")
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
                case ActivityFilterProxyModel.Other:
                    //: Activity type filter option for transactions that are not received, sent, sent to yourself, or mined.
                    return qsTr("Other")
                case ActivityFilterProxyModel.PaymentRequest:
                    return qsTr("Payment request")
                default:
                    return qsTr("All types")
                }
            }

            function shortenedAddress(address) {
                var value = String(address)
                if (value.length <= 12) {
                    return value
                }
                return value.substring(0, 6)
                    + " ... "
                    + value.substring(value.length - 6)
            }

            function emptyActivityTitle() {
                if (activitySourceEmpty) {
                    if (nodeModel.blockSyncActive) {
                        return qsTr("Syncing wallet activity...")
                    }
                    return qsTr("No activity yet")
                }
                if (activityFiltersActive) {
                    return qsTr("No activity matches your filters.")
                }
                return qsTr("No activity")
            }

            function emptyActivityDescription() {
                if (activitySourceEmpty) {
                    if (nodeModel.blockSyncActive) {
                        return qsTr("Transactions may appear as your wallet catches up.")
                    }
                    return qsTr("Once you send or receive bitcoin, your transactions will appear here.")
                }
                if (activityFiltersActive) {
                    //: Empty state hint when the active search, date, type, or amount filters match no transactions.
                    return qsTr("Try changing your search, date, type, or amount filters.")
                }
                return ""
            }

            function exportResultTitle() {
                return exportSucceeded ? qsTr("Export complete") : qsTr("Export failed")
            }

            function exportResultDescription() {
                return exportSucceeded
                    ? qsTr("Your Activity CSV has been saved.")
                    : qsTr("The Activity CSV could not be saved. Check the file path and try again.")
            }

            background: null

            ActivityFilterProxyModel {
                id: activityFilterProxy
                objectName: "activityFilterProxyModel"
                sourceModel: walletController.selectedWallet.activityListModel
                displayUnit: optionsModel.displayUnit
            }

            // Helpers for the amount filter: amountFormatter tracks the active
            // minimum for display; amountParser parses typed input on Apply.
            BitcoinAmount {
                id: amountFormatter
                unit: optionsModel.displayUnit
                satoshi: Math.max(0, activityFilterProxy.minAmount)
            }
            BitcoinAmount { id: amountParser }

            AppSettings {
                id: activitySettings
                property alias searchFiltersVisible: root.filtersVisible
            }

            FileDialog {
                id: exportDialog
                //: Shown as the tooltip of the file icon button in the Activity page header, and as the title of the save dialog that button opens.
                //: The action writes the list of transactions currently shown in Activity to a CSV file.
                title: qsTr("Export activity to CSV")
                defaultSuffix: "csv"
                fileMode: FileDialog.SaveFile
                currentFolder: "file://" + walletController.homePath()
                currentFile: root.defaultExportFileUrl()
                nameFilters: [qsTr("Comma separated file (*.csv)")]
                onAccepted: root.exportActivity(exportDialog.selectedFile.toString())
            }

            TextField {
                id: automationExportPathField
                objectName: "activityExportPathField"
                visible: false
            }

            header: Item {
                id: pageHeader
                objectName: "activityPageHeader"
                // Mirror the Send/Receive title block: 36 above the title and
                // 36 below it, with the ListView's own top margin counted
                // toward the lower gap.
                implicitHeight: 36 + activityHeader.height + 36 - root.activityListTopMargin
                    + (root.filtersVisible ? filterRow.implicitHeight + 10 : 0)
                // Let the header's hover tooltips draw over the activity list.
                z: 1

                RowLayout {
                    id: activityHeader
                    objectName: "activityHeaderRow"
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.topMargin: 36
                    // Let this row's hover tooltips draw over the filter row,
                    // which follows it and would otherwise paint on top.
                    z: 1
                    height: activityTitle.implicitHeight
                    spacing: 10

                    CoreText {
                        id: activityTitle
                        objectName: "activityTitle"
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignLeft
                        text: qsTr("Activity")
                        font: Theme.text.subtitle.font
                        lineHeight: Theme.text.subtitle.lineHeight
                        lineHeightMode: Text.FixedHeight
                        color: Theme.color.neutral9
                    }

                    IconButton {
                        id: exportButton
                        objectName: "activityExportButton"
                        Layout.preferredWidth: 30
                        Layout.preferredHeight: 30
                        //: Accessibility label for the control that exports the Activity list to a CSV file.
                        Accessible.name: qsTr("Export activity")
                        iconSource: "qrc:/icons/file"
                        iconColor: hovered || pressed ? Theme.color.orange : Theme.color.neutral7
                        activeColor: Theme.color.orange
                        size: 30
                        iconSize: 22
                        //: Shown as the tooltip of the file icon button in the Activity page header, and as the title of the save dialog that button opens.
                        //: The action writes the list of transactions currently shown in Activity to a CSV file.
                        tooltipText: qsTr("Export activity to CSV")
                        onClicked: {
                            if (automationExportPathField.text.length > 0) {
                                const exportPath = automationExportPathField.text
                                automationExportPathField.text = ""
                                root.exportActivity(exportPath)
                                return
                            }
                            exportDialog.currentFile = root.defaultExportFileUrl()
                            exportDialog.open()
                        }
                    }

                    IconButton {
                        id: searchToggle
                        objectName: "activitySearchToggle"
                        Layout.preferredWidth: 30
                        Layout.preferredHeight: 30
                        checkable: true
                        checked: root.filtersVisible
                        //: Accessibility label for the control that shows or hides the Activity search and filter row.
                        Accessible.name: qsTr("Search and filter activity")
                        iconSource: "qrc:/icons/search"
                        iconColor: Theme.color.neutral7
                        activeColor: Theme.color.orange
                        size: 30
                        iconSize: 24
                        tooltipText: root.filtersVisible
                            //: Tooltip for the search icon button in the Activity page header, shown while the search and filter row is open.
                            //: Pressing the button hides that row again.
                            ? qsTr("Hide search and filters")
                            //: Tooltip for the search icon button in the Activity page header, shown while the search and filter row is hidden.
                            //: Pressing the button reveals the row, which searches and filters the list of transactions.
                            : qsTr("Search and filter activity")
                        onClicked: root.toggleFilters()
                    }
                }

                ColumnLayout {
                    id: filterRow
                    objectName: "activityFilterRow"
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: activityHeader.bottom
                    anchors.topMargin: 10
                    spacing: 10
                    visible: root.filtersVisible
                    height: visible ? implicitHeight : 0

                    TextField {
                        id: searchField
                        objectName: "activitySearchField"
                        Layout.fillWidth: true
                        implicitHeight: 37
                        leftPadding: 15
                        rightPadding: clearSearchButton.visible ? 36 : 10
                        text: activityFilterProxy.searchText
                        placeholderText: qsTr("Search")
                        placeholderTextColor: Theme.color.neutral7
                        color: Theme.color.neutral9
                        font.family: Theme.text.family
                        font.pixelSize: 15
                        verticalAlignment: TextInput.AlignVCenter
                        topPadding: 0
                        bottomPadding: 0
                        selectByMouse: true
                        onTextChanged: activityFilterProxy.searchText = text

                        background: Rectangle {
                            color: Theme.color.neutral2
                            radius: 5
                        }

                        IconButton {
                            id: clearSearchButton
                            anchors.right: parent.right
                            anchors.rightMargin: 4
                            anchors.verticalCenter: parent.verticalCenter
                            visible: searchField.text.length > 0
                            iconSource: "qrc:/icons/cross"
                            iconColor: Theme.color.neutral7
                            size: 30
                            onClicked: searchField.clear()
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 15

                        DropdownButton {
                            id: dateFilterButton
                            objectName: "activityDateFilterButton"
                            text: root.dateFilterText()
                            textColor: Theme.color.neutral7
                            textAlignment: Text.AlignHCenter
                            opened: datePopup.visible
                            onClicked: datePopup.opened ? datePopup.close() : datePopup.open()
                        }

                        DropdownButton {
                            id: typeFilterButton
                            objectName: "activityTypeFilterButton"
                            text: root.typeFilterText()
                            textColor: Theme.color.neutral7
                            textAlignment: Text.AlignHCenter
                            opened: typePopup.visible
                            onClicked: typePopup.opened ? typePopup.close() : typePopup.open()
                        }

                        DropdownButton {
                            id: amountFilterButton
                            objectName: "activityAmountFilterButton"
                            text: root.amountFilterText()
                            textColor: Theme.color.neutral7
                            textAlignment: Text.AlignHCenter
                            opened: amountPopup.visible
                            onClicked: amountPopup.opened ? amountPopup.close() : amountPopup.open()
                        }

                        Item { Layout.fillWidth: true }
                    }
                }
            }

            contentItem: Item {
                Loader {
                    id: skeletonOverlay

                    width: Math.min(parent.width, 600)
                    anchors.top: parent.top
                    anchors.topMargin: root.activityListTopMargin
                    active: !walletController.initialized
                    z: 2

                    sourceComponent: Column {
                        spacing: 0
                        Repeater {
                            model: 5
                            delegate: ItemDelegate {
                                height: 51
                                width: skeletonOverlay.width
                                contentItem: RowLayout {
                                    spacing: 12
                                    Skeleton {
                                        Layout.leftMargin: 6
                                        width: 15
                                        height: 15
                                    }
                                    Skeleton {
                                        height: 15
                                        Layout.fillWidth: true
                                    }
                                    Skeleton {
                                        width: 75
                                        height: 15
                                    }
                                    Skeleton {
                                        width: 120
                                        height: 15
                                    }
                                }
                                background: Item {
                                    Separator {
                                        anchors.bottom: parent.bottom
                                        width: parent.width
                                    }
                                }
                            }
                        }
                    }
                }

                ListView {
                    id: listView
                    objectName: "activityListView"
                    anchors.fill: parent
                    anchors.topMargin: root.activityListTopMargin
                    anchors.bottomMargin: root.activityListBottomMargin
                    clip: true
                    model: activityFilterProxy

                    header: Item {
                        objectName: "activityEmptyState"
                        width: listView.width
                        height: visible ? emptyStateContent.implicitHeight + 36 : 0
                        visible: walletController.initialized && activityFilterProxy.count === 0

                        ColumnLayout {
                            id: emptyStateContent
                            anchors.horizontalCenter: parent.horizontalCenter
                            anchors.top: parent.top
                            anchors.topMargin: 18
                            width: Math.min(parent.width - 40, 420)
                            spacing: 8

                            CoreText {
                                objectName: "activityEmptyStateTitle"
                                Layout.fillWidth: true
                                text: root.emptyActivityTitle()
                                color: Theme.color.neutral7
                                font.pixelSize: 15
                                horizontalAlignment: Text.AlignHCenter
                            }

                            CoreText {
                                objectName: "activityEmptyStateDescription"
                                Layout.fillWidth: true
                                visible: text.length > 0
                                text: root.emptyActivityDescription()
                                color: Theme.color.neutral6
                                font.pixelSize: 13
                                horizontalAlignment: Text.AlignHCenter
                            }
                        }
                    }

                    delegate: ItemDelegate {
                        id: delegate
                        objectName: delegate.txid !== "" ? "activityItem_" + delegate.txid : "activityItem_pending_" + delegate.index
                        required property int index
                        required property string address
                        required property string amount
                        required property string date
                        required property int depth
                        required property string label
                        required property int status
                        required property int type
                        required property string txid
                        required property string replacedByTxid
                        required property bool isPendingRequest
                        required property bool isUsedAddressRequest
                        required property string requestId
                        required property int outputIndex
                        required property bool countsForBalance
                        required property double netAmountSat

                        HoverHandler {
                            cursorShape: Qt.PointingHandCursor
                        }

                        opacity: (delegate.replacedByTxid !== "" || delegate.status === Transaction.Conflicted) ? 0.4 : 1.0

                        onClicked: {
                            if (delegate.isPendingRequest) {
                                walletController.selectedWallet.loadPaymentRequestDetail(delegate.requestId)
                                stackView.push(paymentRequestDetailPage)
                            } else {
                                // Details come from the model's invokable, which
                                // reads bump eligibility once on open; a row role
                                // would query the wallet on every delegate paint.
                                stackView.navigateToTransaction(delegate.txid, delegate.outputIndex)
                            }
                        }

                        width: ListView.view.width

                        background: Item {
                            Separator {
                                anchors.bottom: parent.bottom
                                width: parent.width
                            }
                        }

                        ActivityTransactionVisuals {
                            id: transactionVisuals
                            transactionType: delegate.type
                            transactionStatus: delegate.status
                            isPendingRequest: delegate.isPendingRequest
                            countsForBalance: delegate.countsForBalance
                        }

                        contentItem: RowLayout {
                            Icon {
                                objectName: delegate.txid !== "" ? "activityItemIcon_" + delegate.txid : "activityItemIcon_pending_" + delegate.index
                                Layout.alignment: Qt.AlignCenter
                                Layout.margins: 6
                                source: transactionVisuals.iconSource
                                color: transactionVisuals.iconColor
                                size: 14
                            }
                            CoreText {
                                Layout.alignment: Qt.AlignCenter
                                Layout.fillWidth: true
                                Layout.preferredWidth: 0
                                Layout.margins: 6
                                wrap: false
                                color: delegate.hovered ? Theme.color.orange : Theme.color.neutral9
                                elide: Text.ElideMiddle
                                text: delegate.label !== "" ? delegate.label : root.shortenedAddress(delegate.address)
                                font.pixelSize: 15
                                horizontalAlignment: Text.AlignLeft
                                clip: true
                            }

                            CoreText {
                                objectName: delegate.txid !== "" ? "activityItemDate_" + delegate.txid : "activityItemDate_pending_" + delegate.index
                                Layout.alignment: Qt.AlignCenter
                                Layout.preferredWidth: 110
                                Layout.margins: 6
                                wrap: false
                                //: Shown in place of the date for a transaction that has no confirmations yet
                                text: transactionVisuals.zeroConf ? qsTr("Pending") : delegate.date
                                font.pixelSize: 15
                                horizontalAlignment: Text.AlignRight
                            }

                            CoreText {
                                objectName: delegate.txid !== "" ? "activityItemAmount_" + delegate.txid : "activityItemAmount_pending_" + delegate.index
                                Layout.alignment: Qt.AlignCenter
                                Layout.preferredWidth: 140
                                Layout.margins: 6
                                wrap: false
                                // A request created without an amount has no number to
                                // show; a zero would assert an amount that was never
                                // requested, so use the same dash the editor shows for
                                // its empty locked fields.
                                text: delegate.isPendingRequest && delegate.netAmountSat === 0
                                    ? "—"
                                    : delegate.amount
                                font.pixelSize: 15
                                horizontalAlignment: Text.AlignRight
                                color: transactionVisuals.amountColor
                            }

                            Component {
                                id: paymentRequestDetailPage
                                PaymentRequestDetail {}
                            }
                        }
                    }
                }

                Popup {
                    id: exportResultPopup
                    objectName: "activityExportResultPopup"
                    modal: true
                    // Center in the window overlay, not the declaring item,
                    // which sits below the page header and skews the popup
                    // off the vertical center (the QR popup does the same).
                    anchors.centerIn: Overlay.overlay
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
                        { text: qsTr("Payment request"),  value: ActivityFilterProxyModel.PaymentRequest, objectName: "activityTypePaymentRequest" },
                        { text: qsTr("Mined"),            value: ActivityFilterProxyModel.Mined,          objectName: "activityTypeMined" },
                        //: Activity type filter menu entry for other transaction types.
                        { text: qsTr("Other"),            value: ActivityFilterProxyModel.Other,          objectName: "activityTypeOther" }
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
                implicitHeight: 37
                leftPadding: 12
                rightPadding: 12
                topPadding: 0
                bottomPadding: 0
                color: Theme.color.neutral9
                placeholderTextColor: Theme.color.neutral7
                font.family: Theme.text.family
                font.pixelSize: 15
                verticalAlignment: TextInput.AlignVCenter
                selectByMouse: true
                background: Rectangle {
                    color: Theme.color.neutral2
                    radius: 5
                }
            }

        }
    }
}
