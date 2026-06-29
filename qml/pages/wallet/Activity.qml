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

            function clearFilters() {
                activityFilterProxy.searchText = ""
                activityFilterProxy.dateFilter = ActivityFilterProxyModel.DateAll
                activityFilterProxy.typeFilter = ActivityFilterProxyModel.TypeAll
                searchField.text = ""
                datePopup.close()
                typePopup.close()
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
                case ActivityFilterProxyModel.LastMonth:
                    return qsTr("Last month")
                case ActivityFilterProxyModel.ThisYear:
                    return qsTr("This year")
                default:
                    return qsTr("All dates")
                }
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
                    return qsTr("Try changing your search, date, or type filters.")
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

            AppSettings {
                id: activitySettings
                property alias searchFiltersVisible: root.filtersVisible
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

            header: Item {
                id: pageHeader
                implicitHeight: 50 + (root.filtersVisible ? filterRow.implicitHeight + 10 : 0)

                RowLayout {
                    id: activityHeader
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.topMargin: 15
                    height: 30
                    spacing: 10

                    CoreText {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignLeft
                        text: qsTr("Activity")
                        font.pixelSize: 21
                        bold: true
                    }

                    IconButton {
                        id: exportButton
                        objectName: "activityExportButton"
                        Layout.preferredWidth: 30
                        Layout.preferredHeight: 30
                        iconSource: "qrc:/icons/file"
                        iconColor: hovered || pressed ? Theme.color.orange : Theme.color.neutral7
                        activeColor: Theme.color.orange
                        size: 30
                        iconSize: 22
                        onClicked: {
                            if (automationExportPathField.text.length > 0) {
                                const exportPath = automationExportPathField.text
                                automationExportPathField.text = ""
                                root.exportActivity(exportPath)
                                return
                            }
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
                        iconSource: "qrc:/icons/search"
                        iconColor: Theme.color.neutral7
                        activeColor: Theme.color.orange
                        size: 30
                        onClicked: root.toggleFilters()
                    }
                }

                RowLayout {
                    id: filterRow
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: activityHeader.bottom
                    anchors.topMargin: 10
                    spacing: 15
                    visible: root.filtersVisible
                    height: visible ? implicitHeight : 0

                    TextField {
                        id: searchField
                        objectName: "activitySearchField"
                        Layout.fillWidth: true
                        Layout.minimumWidth: 160
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
                        required property bool canBump
                        required property string replacedByTxid
                        required property bool isPendingRequest
                        required property string requestId
                        required property int outputIndex

                        HoverHandler {
                            cursorShape: Qt.PointingHandCursor
                        }

                        opacity: (delegate.replacedByTxid !== "" || delegate.status === Transaction.Conflicted) ? 0.4 : 1.0

                        onClicked: {
                            if (delegate.isPendingRequest) {
                                walletController.selectedWallet.loadPaymentRequestDetail(delegate.requestId)
                                stackView.push(paymentRequestDetailPage)
                            } else {
                                var page = stackView.push(detailsPage)
                                page.showTransaction.connect(stackView.navigateToTransaction)
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
                        }

                        contentItem: RowLayout {
                            Icon {
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
                                Layout.alignment: Qt.AlignCenter
                                Layout.preferredWidth: 110
                                Layout.margins: 6
                                wrap: false
                                text: delegate.date
                                font.pixelSize: 15
                                horizontalAlignment: Text.AlignRight
                            }

                            CoreText {
                                Layout.alignment: Qt.AlignCenter
                                Layout.preferredWidth: 140
                                Layout.margins: 6
                                wrap: false
                                text: delegate.amount
                                font.pixelSize: 15
                                horizontalAlignment: Text.AlignRight
                                color: transactionVisuals.amountColor
                            }

                            Component {
                                id: detailsPage
                                ActivityDetails {
                                    txid: delegate.txid
                                    outputIndex: delegate.outputIndex
                                    canBump: delegate.canBump
                                    replacedByTxid: delegate.replacedByTxid
                                    amount: delegate.amount
                                    date: delegate.date
                                    depth: delegate.depth
                                    type: delegate.type
                                    status: delegate.status
                                    address: delegate.address
                                    label: delegate.label
                                    paymentRequests: walletController.selectedWallet
                                        ? walletController.selectedWallet.receiveRequests.matchingEntriesForAddress(delegate.address)
                                        : []
                                }
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
            }

            ContextMenu {
                id: datePopup
                objectName: "activityDateFilterPopup"
                parent: dateFilterButton
                x: datePopup.parent.width - datePopup.width
                y: datePopup.parent.height + 2
                modal: true
                dim: false

                ContextMenuPicker {
                    objectNameRole: "objectName"
                    currentValue: activityFilterProxy.dateFilter
                    model: [
                        { text: qsTr("All"),         value: ActivityFilterProxyModel.DateAll,   objectName: "activityDateAll" },
                        { text: qsTr("Today"),       value: ActivityFilterProxyModel.Today,     objectName: "activityDateToday" },
                        { text: qsTr("This week"),   value: ActivityFilterProxyModel.ThisWeek,  objectName: "activityDateThisWeek" },
                        { text: qsTr("This month"),  value: ActivityFilterProxyModel.ThisMonth, objectName: "activityDateThisMonth" },
                        { text: qsTr("Last month"),  value: ActivityFilterProxyModel.LastMonth, objectName: "activityDateLastMonth" },
                        { text: qsTr("This year"),   value: ActivityFilterProxyModel.ThisYear,  objectName: "activityDateThisYear" }
                    ]
                    onActivated: function(value) {
                        activityFilterProxy.dateFilter = value
                        datePopup.close()
                    }
                }
            }

            ContextMenu {
                id: typePopup
                objectName: "activityTypeFilterPopup"
                parent: typeFilterButton
                x: typePopup.parent.width - typePopup.width
                y: typePopup.parent.height + 2
                modal: true
                dim: false

                ContextMenuPicker {
                    objectNameRole: "objectName"
                    currentValue: activityFilterProxy.typeFilter
                    model: [
                        { text: qsTr("All"),              value: ActivityFilterProxyModel.TypeAll,        objectName: "activityTypeAll" },
                        { text: qsTr("Received"),         value: ActivityFilterProxyModel.Received,       objectName: "activityTypeReceived" },
                        { text: qsTr("Sent"),             value: ActivityFilterProxyModel.Sent,           objectName: "activityTypeSent" },
                        { text: qsTr("Sent to yourself"), value: ActivityFilterProxyModel.SentToSelf,     objectName: "activityTypeSentToSelf" },
                        { text: qsTr("Mined"),            value: ActivityFilterProxyModel.Mined,          objectName: "activityTypeMined" },
                        { text: qsTr("Other"),            value: ActivityFilterProxyModel.Other,          objectName: "activityTypeOther" },
                        { text: qsTr("Payment request"),  value: ActivityFilterProxyModel.PaymentRequest, objectName: "activityTypePaymentRequest" }
                    ]
                    onActivated: function(value) {
                        activityFilterProxy.typeFilter = value
                        typePopup.close()
                    }
                }
            }
        }
    }
}
