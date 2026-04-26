// Copyright (c) 2024-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Dialogs
import QtQuick.Layouts 1.15
import Qt.labs.settings 1.0
import org.bitcoincore.qt 1.0

import "../../controls"
import "../../components"

PageStack {
    id: stackView

    function navigateToTransaction(txid) {
        if (!walletController.selectedWallet)
            return

        var details = walletController.selectedWallet.activityListModel.transactionDetails(txid)
        if (Object.keys(details).length === 0)
            return

        var page = stackView.push("ActivityDetails.qml", details)
        page.showTransaction.connect(stackView.navigateToTransaction)
    }

    Connections {
        target: walletController
        function onSelectedWalletChanged() {
            stackView.pop()
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
            Layout.maximumWidth: 600
            Layout.margins: 20

            property bool filtersVisible: false
            property bool exportSucceeded: true
            readonly property int activityListTopMargin: 10
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
                case ActivityFilterProxyModel.PaymentRequest:
                    return qsTr("Payment request")
                default:
                    return qsTr("All types")
                }
            }

            function emptyActivityTitle() {
                if (activitySourceEmpty) {
                    if (nodeModel.verificationProgress < 0.9999) {
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
                    if (nodeModel.verificationProgress < 0.9999) {
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
            }

            Settings {
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

            header: ColumnLayout {
                id: pageHeader
                spacing: 10

                RowLayout {
                    id: activityHeader
                    Layout.fillWidth: true
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

                    Button {
                        id: searchToggle
                        objectName: "activitySearchToggle"
                        Layout.preferredWidth: 30
                        Layout.preferredHeight: 30
                        padding: 0
                        checkable: true
                        checked: root.filtersVisible
                        hoverEnabled: AppMode.isDesktop
                        onClicked: root.toggleFilters()

                        HoverHandler {
                            cursorShape: Qt.PointingHandCursor
                        }

                        background: Rectangle {
                            radius: 5
                            color: searchToggle.hovered || searchToggle.pressed ? Theme.color.neutral2 : Theme.color.background
                        }

                        contentItem: Canvas {
                            id: searchIcon
                            property color strokeColor: searchToggle.checked || searchToggle.pressed ? Theme.color.orange : Theme.color.neutral7
                            anchors.fill: parent
                            onStrokeColorChanged: requestPaint()
                            onPaint: {
                                const ctx = getContext("2d")
                                ctx.clearRect(0, 0, width, height)
                                ctx.strokeStyle = strokeColor
                                ctx.lineWidth = 2
                                ctx.lineCap = "round"
                                ctx.beginPath()
                                ctx.arc(width / 2 - 2, height / 2 - 2, 5.5, 0, Math.PI * 2)
                                ctx.stroke()
                                ctx.beginPath()
                                ctx.moveTo(width / 2 + 2.5, height / 2 + 2.5)
                                ctx.lineTo(width / 2 + 8, height / 2 + 8)
                                ctx.stroke()
                            }
                        }
                    }
                }

                RowLayout {
                    id: filterRow
                    Layout.fillWidth: true
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
                        font.family: "Inter"
                        font.pixelSize: 15
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

                    Button {
                        id: dateFilterButton
                        objectName: "activityDateFilterButton"
                        Layout.preferredHeight: 30
                        leftPadding: 10
                        rightPadding: 4
                        topPadding: 2
                        bottomPadding: 2
                        hoverEnabled: AppMode.isDesktop
                        onClicked: datePopup.opened ? datePopup.close() : datePopup.open()

                        HoverHandler {
                            cursorShape: Qt.PointingHandCursor
                        }

                        background: Rectangle {
                            radius: 6
                            color: dateFilterButton.hovered || dateFilterButton.pressed || datePopup.opened ? Theme.color.neutral2 : Theme.color.background
                        }

                        contentItem: RowLayout {
                            spacing: 5
                            CoreText {
                                text: root.dateFilterText()
                                color: Theme.color.neutral7
                                font.pixelSize: 15
                                horizontalAlignment: Text.AlignHCenter
                            }
                            Icon {
                                source: "image://images/caret-down-medium-filled"
                                color: Theme.color.orange
                                size: 20
                            }
                        }
                    }

                    Button {
                        id: typeFilterButton
                        objectName: "activityTypeFilterButton"
                        Layout.preferredHeight: 30
                        leftPadding: 10
                        rightPadding: 4
                        topPadding: 2
                        bottomPadding: 2
                        hoverEnabled: AppMode.isDesktop
                        onClicked: typePopup.opened ? typePopup.close() : typePopup.open()

                        HoverHandler {
                            cursorShape: Qt.PointingHandCursor
                        }

                        background: Rectangle {
                            radius: 6
                            color: typeFilterButton.hovered || typeFilterButton.pressed || typePopup.opened ? Theme.color.neutral2 : Theme.color.background
                        }

                        contentItem: RowLayout {
                            spacing: 5
                            CoreText {
                                text: root.typeFilterText()
                                color: Theme.color.neutral7
                                font.pixelSize: 15
                                horizontalAlignment: Text.AlignHCenter
                            }
                            Icon {
                                source: "image://images/caret-down-medium-filled"
                                color: Theme.color.orange
                                size: 20
                            }
                        }
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
                        objectName: delegate.txid !== "" ? "activityItem_" + delegate.txid : "activityItem_pending_" + index
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

                        HoverHandler {
                            cursorShape: Qt.PointingHandCursor
                        }

                        opacity: (delegate.replacedByTxid !== "" || delegate.status === Transaction.Conflicted) ? 0.4 : 1.0

                        onClicked: {
                            var page = stackView.push(detailsPage)
                            page.showTransaction.connect(stackView.navigateToTransaction)
                        }

                        width: ListView.view.width

                        background: Item {
                            Separator {
                                anchors.bottom: parent.bottom
                                width: parent.width
                            }
                        }

                        contentItem: RowLayout {
                            Icon {
                                Layout.alignment: Qt.AlignCenter
                                Layout.margins: 6
                                source: {
                                    if (delegate.type == Transaction.RecvWithAddress
                                        || delegate.type == Transaction.RecvFromOther) {
                                        "qrc:/icons/triangle-down"
                                    } else if (delegate.type == Transaction.Generated) {
                                        "qrc:/icons/coinbase"
                                    } else {
                                        "qrc:/icons/triangle-up"
                                    }
                                }
                                color: {
                                    if (delegate.status == Transaction.Confirmed) {
                                        if (delegate.type == Transaction.RecvWithAddress ||
                                            delegate.type == Transaction.RecvFromOther ||
                                            delegate.type == Transaction.Generated) {
                                            Theme.color.green
                                        } else {
                                            Theme.color.orange
                                        }
                                    } else {
                                        Theme.color.blue
                                    }
                                }
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
                                text: delegate.label !== "" ? delegate.label : delegate.address
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
                                color: {
                                    if (delegate.type == Transaction.RecvWithAddress
                                        || delegate.type == Transaction.RecvFromOther
                                        || delegate.type == Transaction.Generated) {
                                        Theme.color.green
                                    } else {
                                        Theme.color.neutral9
                                    }
                                }
                            }

                            Component {
                                id: detailsPage
                                ActivityDetails {
                                    txid: delegate.txid
                                    canBump: delegate.canBump
                                    replacedByTxid: delegate.replacedByTxid
                                    amount: delegate.amount
                                    date: delegate.date
                                    depth: delegate.depth
                                    type: delegate.type
                                    status: delegate.status
                                    address: delegate.address
                                    label: delegate.label
                                }
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

            Popup {
                id: datePopup
                objectName: "activityDateFilterPopup"
                parent: dateFilterButton
                x: datePopup.parent.width - datePopup.width
                y: datePopup.parent.height + 2
                modal: true
                dim: false
                width: 250
                height: 5 * 36 + 10
                padding: 5
                closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

                background: Rectangle {
                    color: Theme.color.background
                    border.color: Theme.color.neutral4
                    radius: 6
                }

                contentItem: ColumnLayout {
                    spacing: 0

                    FilterMenuItem {
                        objectName: "activityDateAll"
                        text: qsTr("All")
                        selected: activityFilterProxy.dateFilter === ActivityFilterProxyModel.DateAll
                        onClicked: {
                            activityFilterProxy.dateFilter = ActivityFilterProxyModel.DateAll
                            datePopup.close()
                        }
                    }
                    FilterMenuItem {
                        objectName: "activityDateToday"
                        text: qsTr("Today")
                        selected: activityFilterProxy.dateFilter === ActivityFilterProxyModel.Today
                        onClicked: {
                            activityFilterProxy.dateFilter = ActivityFilterProxyModel.Today
                            datePopup.close()
                        }
                    }
                    FilterMenuItem {
                        objectName: "activityDateThisWeek"
                        text: qsTr("This week")
                        selected: activityFilterProxy.dateFilter === ActivityFilterProxyModel.ThisWeek
                        onClicked: {
                            activityFilterProxy.dateFilter = ActivityFilterProxyModel.ThisWeek
                            datePopup.close()
                        }
                    }
                    FilterMenuItem {
                        objectName: "activityDateThisMonth"
                        text: qsTr("This month")
                        selected: activityFilterProxy.dateFilter === ActivityFilterProxyModel.ThisMonth
                        onClicked: {
                            activityFilterProxy.dateFilter = ActivityFilterProxyModel.ThisMonth
                            datePopup.close()
                        }
                    }
                    FilterMenuItem {
                        objectName: "activityDateThisYear"
                        text: qsTr("This year")
                        selected: activityFilterProxy.dateFilter === ActivityFilterProxyModel.ThisYear
                        onClicked: {
                            activityFilterProxy.dateFilter = ActivityFilterProxyModel.ThisYear
                            datePopup.close()
                        }
                    }
                }
            }

            Popup {
                id: typePopup
                objectName: "activityTypeFilterPopup"
                parent: typeFilterButton
                x: typePopup.parent.width - typePopup.width
                y: typePopup.parent.height + 2
                modal: true
                dim: false
                width: 250
                height: 6 * 36 + 10
                padding: 5
                closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

                background: Rectangle {
                    color: Theme.color.background
                    border.color: Theme.color.neutral4
                    radius: 6
                }

                contentItem: ColumnLayout {
                    spacing: 0

                    FilterMenuItem {
                        objectName: "activityTypeAll"
                        text: qsTr("All")
                        selected: activityFilterProxy.typeFilter === ActivityFilterProxyModel.TypeAll
                        onClicked: {
                            activityFilterProxy.typeFilter = ActivityFilterProxyModel.TypeAll
                            typePopup.close()
                        }
                    }
                    FilterMenuItem {
                        objectName: "activityTypeReceived"
                        text: qsTr("Received")
                        selected: activityFilterProxy.typeFilter === ActivityFilterProxyModel.Received
                        onClicked: {
                            activityFilterProxy.typeFilter = ActivityFilterProxyModel.Received
                            typePopup.close()
                        }
                    }
                    FilterMenuItem {
                        objectName: "activityTypeSent"
                        text: qsTr("Sent")
                        selected: activityFilterProxy.typeFilter === ActivityFilterProxyModel.Sent
                        onClicked: {
                            activityFilterProxy.typeFilter = ActivityFilterProxyModel.Sent
                            typePopup.close()
                        }
                    }
                    FilterMenuItem {
                        objectName: "activityTypeSentToSelf"
                        text: qsTr("Sent to yourself")
                        selected: activityFilterProxy.typeFilter === ActivityFilterProxyModel.SentToSelf
                        onClicked: {
                            activityFilterProxy.typeFilter = ActivityFilterProxyModel.SentToSelf
                            typePopup.close()
                        }
                    }
                    FilterMenuItem {
                        objectName: "activityTypeMined"
                        text: qsTr("Mined")
                        selected: activityFilterProxy.typeFilter === ActivityFilterProxyModel.Mined
                        onClicked: {
                            activityFilterProxy.typeFilter = ActivityFilterProxyModel.Mined
                            typePopup.close()
                        }
                    }
                    FilterMenuItem {
                        objectName: "activityTypePaymentRequest"
                        text: qsTr("Payment request")
                        selected: activityFilterProxy.typeFilter === ActivityFilterProxyModel.PaymentRequest
                        onClicked: {
                            activityFilterProxy.typeFilter = ActivityFilterProxyModel.PaymentRequest
                            typePopup.close()
                        }
                    }
                }
            }

            component FilterMenuItem: ItemDelegate {
                id: menuItem
                property bool selected: false
                Layout.fillWidth: true
                Layout.preferredHeight: 36
                leftPadding: 10
                rightPadding: 4
                topPadding: 2
                bottomPadding: 2
                hoverEnabled: AppMode.isDesktop

                HoverHandler {
                    cursorShape: Qt.PointingHandCursor
                }

                background: Item {
                    Rectangle {
                        anchors.fill: parent
                        radius: 6
                        color: Theme.color.neutral2
                        visible: menuItem.hovered || menuItem.pressed
                    }
                }

                contentItem: RowLayout {
                    spacing: 5
                    CoreText {
                        Layout.fillWidth: true
                        text: menuItem.text
                        color: Theme.color.neutral9
                        font.pixelSize: 15
                        horizontalAlignment: Text.AlignLeft
                        elide: Text.ElideRight
                    }
                    Icon {
                        visible: menuItem.selected
                        source: "image://images/check"
                        color: Theme.color.orange
                        size: 20
                    }
                }
            }
        }
    }
}
