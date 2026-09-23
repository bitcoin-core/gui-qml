// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0
import "../controls"

ColumnLayout {
    id: root
    property var wallet
    property var coins: wallet ? wallet.coinsListModel : null
    property bool selectionMode: true
    property bool walletSelectionActive: false
    property var markedIds: []
    property string errorText: ""
    property string pendingLockedCoinId: ""
    property string contextCoinId: ""
    property bool contextCoinLocked: false
    property real fallbackTarget: 0
    readonly property real target: feeReady ? wallet.sendTotalSatoshi : fallbackTarget
    onTargetChanged: if (feeReady) fallbackTarget = target
    readonly property real selectedAmount: coins ? coins.totalSelectedSatoshi : 0
    readonly property bool covered: selectedAmount >= target && target > 0
    readonly property bool feeReady: wallet && !wallet.feeEstimatePending && wallet.estimatedFeeSatoshi >= 0
    readonly property real remainingAmount: Math.max(0, target - selectedAmount)
    readonly property real changeAmount: Math.max(0, selectedAmount - target)
    readonly property int activeFilterCount: !coins ? 0 : (coins.filter !== "all" ? 1 : 0) + (coins.minAmount >= 0 || coins.maxAmount >= 0 ? 1 : 0)
    spacing: 16
    onWalletChanged: { markedIds = []; walletSelectionActive = false; pendingLockedCoinId = ""; errorText = "" }

    BitcoinAmount { id: targetAmount; unit: optionsModel.displayUnit; satoshi: root.target }
    BitcoinAmount { id: selectedAmountText; unit: optionsModel.displayUnit; satoshi: root.selectedAmount }
    BitcoinAmount { id: remainingAmountText; unit: optionsModel.displayUnit; satoshi: root.remainingAmount }
    BitcoinAmount { id: changeAmountText; unit: optionsModel.displayUnit; satoshi: root.changeAmount }
    BitcoinAmount { id: totalAmountText; unit: optionsModel.displayUnit; satoshi: root.coins ? root.coins.totalSatoshi : 0 }
    BitcoinAmount { id: spendableAmountText; unit: optionsModel.displayUnit; satoshi: root.coins ? root.coins.totalSatoshi - root.coins.lockedSatoshi : 0 }
    BitcoinAmount { id: lockedAmountText; unit: optionsModel.displayUnit; satoshi: root.coins ? root.coins.lockedSatoshi : 0 }
    BitcoinAmount { id: lowerAmountText; unit: optionsModel.displayUnit; satoshi: amountSlider.lowerValue }
    BitcoinAmount { id: upperAmountText; unit: optionsModel.displayUnit; satoshi: amountSlider.upperValue }

    function toggleRow(row, coinId, locked, marked) {
        if (selectionMode && locked && !marked) {
            pendingLockedCoinId = coinId
            lockedCoinAlert.open()
        } else if (selectionMode) coins.toggleCoinSelection(row)
        else if (walletSelectionActive) {
            const ids = markedIds.slice()
            const i = ids.indexOf(coinId)
            if (i < 0) ids.push(coinId); else ids.splice(i, 1)
            markedIds = ids
        }
    }
    function lockMarked(locked) {
        errorText = coins.setCoinsLocked(markedIds, locked) ? "" : qsTr("Some coins could not be updated. Try again.")
        markedIds = []
    }
    function setCoinLocked(coinId, locked) {
        errorText = coins.setCoinsLocked([coinId], locked) ? "" : qsTr("This coin could not be updated. Try again.")
    }
    function openMenu(menu, button) {
        const p = button.mapToItem(Overlay.overlay, button.width, button.height + 6)
        menu.x = Math.max(12, Math.min(p.x - menu.width, Overlay.overlay.width - menu.width - 12))
        menu.y = Math.max(12, Math.min(p.y, Overlay.overlay.height - menu.height - 12))
        menu.open()
    }
    function clearFilters() {
        if (!coins) return
        coins.filter = "all"
        coins.minAmount = -1
        coins.maxAmount = -1
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 10
        SearchBar {
            objectName: "coinBrowserSearch"
            fieldObjectName: "coinBrowserSearchField"
            Layout.fillWidth: true
            placeholderText: root.width < 600 ? qsTr("Search coins") : qsTr("Search notes, addresses or transaction IDs")
            accessibleName: qsTr("Search coins")
            text: root.coins ? root.coins.searchText : ""
            onTextChanged: if (root.coins) root.coins.searchText = text
        }
        NeutralButton {
            id: sortButton
            objectName: "coinBrowserSortButton"
            implicitWidth: 40; implicitHeight: 40
            leftPadding: 0; rightPadding: 0
            iconSize: 16
            iconSource: "qrc:/icons/arrow-up-down.svg"
            text: ""
            Accessible.name: qsTr("Sort coins")
            onClicked: root.openMenu(sortMenu, sortButton)
        }
        NeutralButton {
            id: groupButton
            objectName: "coinBrowserGroupButton"
            implicitWidth: 40; implicitHeight: 40
            leftPadding: 0; rightPadding: 0
            iconSize: 16
            iconSource: "qrc:/icons/group-by.svg"
            text: ""
            Accessible.name: qsTr("Group coins")
            onClicked: root.openMenu(groupMenu, groupButton)
        }
        NeutralButton {
            id: selectionButton
            objectName: "walletCoinsSelectionButton"
            visible: !root.selectionMode
            implicitWidth: visible ? 40 : 0; implicitHeight: 40
            leftPadding: 0; rightPadding: 0
            iconSize: 16
            iconSource: "qrc:/icons/checklist.svg"
            text: ""
            backgroundColor: root.walletSelectionActive ? Theme.color.orange : Theme.color.neutral2
            hoverBackgroundColor: root.walletSelectionActive ? Theme.color.orangeLight1 : Theme.color.neutral3
            textColor: root.walletSelectionActive ? Theme.color.white : Theme.color.neutral9
            Accessible.name: qsTr("Select coins")
            Accessible.checkable: true
            Accessible.checked: root.walletSelectionActive
            onClicked: {
                root.walletSelectionActive = !root.walletSelectionActive
                if (!root.walletSelectionActive) root.markedIds = []
            }
        }
    }

    Flow {
        Layout.fillWidth: true
        Layout.preferredHeight: childrenRect.height
        spacing: 10
        FilterButton {
            id: activeFiltersButton
            objectName: "coinBrowserActiveFiltersButton"
            visible: root.activeFilterCount > 0
            active: true
            count: root.activeFilterCount
            size: coinTypeButton.height
            iconSize: 20
            text: count === 1 ? qsTr("1 active filter") : qsTr("%1 active filters").arg(count)
            onClicked: root.openMenu(clearFiltersMenu, activeFiltersButton)
        }
        DropdownButton {
            id: coinTypeButton
            objectName: "coinBrowserFilterButton"
            implicitHeight: 40; isOnSurface: true
            text: !root.coins || root.coins.filter === "all" ? qsTr("Coin type") : root.coins.filter === "locked" ? qsTr("Locked") : qsTr("Spendable")
            active: root.coins && root.coins.filter !== "all"
            opened: filterMenu.opened
            onClicked: root.openMenu(filterMenu, coinTypeButton)
        }
        DropdownButton {
            id: amountFilterButton
            objectName: "coinBrowserAmountFilterButton"
            implicitHeight: 40; isOnSurface: true
            text: qsTr("Amount")
            active: root.coins && (root.coins.minAmount >= 0 || root.coins.maxAmount >= 0)
            opened: amountPopup.opened
            onClicked: root.openMenu(amountPopup, amountFilterButton)
        }
    }

    ListView {
        id: list
        objectName: "coinSelectionListView"
        Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 120
        clip: true; model: root.coins; reuseItems: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}
        section.property: "groupTitle"
        section.criteria: ViewSection.FullString
        section.delegate: Item {
            required property string section
            width: list.width; height: 44
            CoreText {
                anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.bottomMargin: 8
                text: parent.section; horizontalAlignment: Text.AlignLeft; wrap: false; elide: Text.ElideMiddle
                font: root.coins && root.coins.groupBy === "address" ? Theme.text.monoCaption.font : Theme.text.captionStrong.font
                color: Theme.color.neutral7
            }
        }
        delegate: ItemDelegate {
            id: coinRow
            required property int index
            required property string coinId
            required property string address
            required property string label
            required property var amountSatoshi
            required property var date
            required property bool locked
            required property bool selected
            required property bool groupFirst
            required property bool groupLast
            property bool isOnSurface: root.selectionMode
            property bool suppressNextRowClick: false
            objectName: "coinSelectionItem_" + index
            readonly property bool marked: root.selectionMode ? selected : root.markedIds.indexOf(coinId) >= 0
            width: list.width
            height: root.coins && root.coins.groupBy === "address"
                ? (label.length > 0 ? 72 : 52)
                : (label.length > 0 ? 92 : 72)
            leftPadding: 12; rightPadding: 12
            onClicked: {
                if (suppressNextRowClick) {
                    suppressNextRowClick = false
                    return
                }
                root.toggleRow(index, coinId, locked, marked)
            }
            MouseArea {
                anchors.fill: parent
                z: 2
                acceptedButtons: Qt.RightButton
                enabled: !root.selectionMode
                onClicked: function(mouse) {
                    const point = coinRow.mapToItem(Overlay.overlay, mouse.x, mouse.y)
                    root.contextCoinId = coinRow.coinId
                    root.contextCoinLocked = coinRow.locked
                    coinContextMenu.x = Math.max(12, Math.min(point.x, Overlay.overlay.width - coinContextMenu.width - 12))
                    coinContextMenu.y = Math.max(12, Math.min(point.y, Overlay.overlay.height - coinContextMenu.height - 12))
                    coinContextMenu.open()
                }
            }
            Accessible.name: (label.length ? label + " " : "") + address + " " + rowAmount.displayWithUnit
            Accessible.checkable: root.selectionMode || root.walletSelectionActive
            Accessible.checked: marked
            background: Rectangle {
                id: rowBackground
                radius: coinRow.groupFirst || coinRow.groupLast ? 10 : 0
                color: coinRow.marked || coinRow.hovered
                    ? (coinRow.isOnSurface ? Theme.color.neutral3 : Theme.color.neutral2)
                    : (coinRow.isOnSurface ? Theme.color.neutral2 : Theme.color.neutral1)
                border.color: coinRow.visualFocus ? Theme.color.orange : "transparent"
                Rectangle {
                    visible: coinRow.groupFirst && !coinRow.groupLast
                    anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                    height: parent.height / 2
                    color: rowBackground.color
                }
                Rectangle {
                    visible: coinRow.groupLast && !coinRow.groupFirst
                    anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                    height: parent.height / 2
                    color: rowBackground.color
                }
                Rectangle {
                    visible: !coinRow.groupLast
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.leftMargin: 50
                    anchors.rightMargin: 12
                    height: 1
                    color: Theme.color.neutral3
                }
            }
            BitcoinAmount { id: rowAmount; satoshi: coinRow.amountSatoshi; unit: optionsModel.displayUnit }
            contentItem: RowLayout {
                spacing: 0
                Item {
                    id: checkboxSlot
                    readonly property bool shown: root.selectionMode || root.walletSelectionActive
                    Layout.preferredWidth: shown ? 38 : 0
                    Layout.preferredHeight: 28
                    Layout.alignment: Qt.AlignVCenter
                    clip: true

                    Behavior on Layout.preferredWidth {
                        enabled: !root.selectionMode
                        NumberAnimation { duration: 180; easing.type: Easing.OutCubic }
                    }

                    CheckBox {
                        objectName: "coinSelectionCheckbox"
                        width: 24
                        height: 28
                        anchors.verticalCenter: parent.verticalCenter
                        x: checkboxSlot.shown ? 0 : -width
                        opacity: checkboxSlot.shown ? 1 : 0
                        checkable: false
                        enabled: checkboxSlot.shown && coinRow.enabled
                        checked: coinRow.marked
                        Accessible.name: coinRow.Accessible.name
                        Behavior on x { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
                        Behavior on opacity { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }
                        onClicked: {
                            coinRow.suppressNextRowClick = true
                            root.toggleRow(coinRow.index, coinRow.coinId, coinRow.locked, coinRow.marked)
                            Qt.callLater(function() { coinRow.suppressNextRowClick = false })
                        }
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true; Layout.minimumWidth: 60; Layout.alignment: Qt.AlignVCenter; spacing: 3
                    RowLayout {
                        visible: coinRow.label.length > 0
                        Layout.fillWidth: true
                        CoreText { visible: coinRow.label.length > 0; Layout.fillWidth: true; text: coinRow.label; horizontalAlignment: Text.AlignLeft; font: Theme.text.subheading.font; wrap: false; elide: Text.ElideRight }
                    }
                    CoreText { visible: !root.coins || root.coins.groupBy !== "address"; Layout.fillWidth: true; text: coinRow.address; horizontalAlignment: Text.AlignLeft; font: Theme.text.monoCaption.font; color: Theme.color.neutral7; wrap: false; elide: Text.ElideMiddle }
                    CoreText { Layout.fillWidth: true; text: Qt.formatDateTime(coinRow.date, "MMM d, h:mm AP"); horizontalAlignment: Text.AlignLeft; font: Theme.text.caption.font; color: Theme.color.neutral7; wrap: false }
                }
                RowLayout {
                    spacing: 6
                    Layout.alignment: Qt.AlignVCenter
                    Item {
                        visible: coinRow.locked
                        Layout.preferredWidth: visible ? 20 : 0
                        Layout.preferredHeight: visible ? 20 : 0
                        Icon { anchors.centerIn: parent; source: "qrc:/icons/lock.fill.svg"; size: 16; color: Theme.color.neutral6 }
                        HoverHandler { id: lockHover }
                        ToolTip {
                            visible: lockHover.hovered
                            delay: 400
                            text: qsTr("Locked from automatic coin selection")
                        }
                    }
                    CoreText {
                        Layout.preferredWidth: Math.min(190, implicitWidth); Layout.alignment: Qt.AlignVCenter
                        text: rowAmount.displayWithUnit; horizontalAlignment: Text.AlignRight
                        font: Theme.text.monoDescription.font; wrap: false
                    }
                }
            }
        }
        CoreText {
            anchors.centerIn: parent; width: parent.width - 32
            visible: list.count === 0
            text: root.coins && root.coins.coinCount > 0 ? qsTr("No coins match your search or filters.") : qsTr("No coins in this wallet yet.")
            font: Theme.text.description.font; color: Theme.color.neutral7
        }
    }

    FormSection {
        objectName: "coinBrowserSummary"
        visible: root.selectionMode || !root.walletSelectionActive
        Layout.fillWidth: true
        isOnSurface: root.selectionMode
        showGradientBorder: false
        rowSpacing: 0
        ValueRow { Layout.fillWidth: true; title: root.selectionMode ? qsTr("Target amount") : qsTr("Total in coins"); value: root.selectionMode ? (root.target > 0 ? targetAmount.displayWithUnit : "—") : totalAmountText.displayWithUnit; dividerColor: root.selectionMode ? Theme.color.neutral3 : Theme.color.neutral2 }
        ValueRow { Layout.fillWidth: true; title: root.selectionMode ? qsTr("Amount selected") : qsTr("Spendable"); value: root.selectionMode ? selectedAmountText.displayWithUnit : spendableAmountText.displayWithUnit; showDivider: !root.selectionMode || root.remainingAmount > 0 || root.changeAmount > 0; dividerColor: root.selectionMode ? Theme.color.neutral3 : Theme.color.neutral2 }
        ValueRow { Layout.fillWidth: true; visible: root.selectionMode && root.remainingAmount > 0; title: qsTr("Remaining to select"); value: remainingAmountText.displayWithUnit; showDivider: root.changeAmount > 0; dividerColor: Theme.color.neutral3 }
        ValueRow { Layout.fillWidth: true; visible: root.selectionMode && root.changeAmount > 0; title: qsTr("Change"); value: changeAmountText.displayWithUnit; showDivider: false }
        ValueRow { Layout.fillWidth: true; visible: !root.selectionMode; title: qsTr("Locked"); value: lockedAmountText.displayWithUnit; showDivider: false }
    }

    RowLayout {
        visible: !root.selectionMode && root.walletSelectionActive
        Layout.fillWidth: true
        CoreText { text: qsTr("%1 selected").arg(root.markedIds.length); Layout.fillWidth: true; horizontalAlignment: Text.AlignLeft; font: Theme.text.description.font }
        NeutralButton { objectName: "coinsLockButton"; text: qsTr("Lock coins"); enabled: root.markedIds.length > 0; onClicked: root.lockMarked(true) }
        NeutralButton { objectName: "coinsUnlockButton"; text: qsTr("Unlock coins"); enabled: root.markedIds.length > 0; onClicked: root.lockMarked(false) }
    }
    CoreText { Layout.fillWidth: true; visible: root.errorText.length > 0; text: root.errorText; color: Theme.color.red; horizontalAlignment: Text.AlignLeft }

    ContextMenu {
        id: sortMenu; parent: Overlay.overlay; objectName: "coinBrowserSortMenu"
        ContextMenuPicker {
            title: qsTr("Sort by")
            model: [{text: qsTr("Date"), value: "date"}, {text: qsTr("Amount"), value: "amount"}, {text: qsTr("Label / note"), value: "label"}]
            currentValue: root.coins ? root.coins.sortBy : "date"
            onActivated: function(value) { root.coins.sortBy = value; sortMenu.close() }
        }
        ContextMenuDivider {}
        ContextMenuPicker {
            model: [{text: qsTr("Ascending"), value: false}, {text: qsTr("Descending"), value: true}]
            currentValue: root.coins ? root.coins.sortDescending : true
            onActivated: function(value) { root.coins.sortDescending = value; sortMenu.close() }
        }
    }
    ContextMenu {
        id: groupMenu; parent: Overlay.overlay; objectName: "coinBrowserGroupMenu"
        ContextMenuPicker {
            title: qsTr("Group by")
            model: [{text: qsTr("Date"), value: "date"}, {text: qsTr("Month"), value: "month"}, {text: qsTr("Address"), value: "address"}]
            currentValue: root.coins ? root.coins.groupBy : "date"
            onActivated: function(value) { root.coins.groupBy = value; groupMenu.close() }
        }
    }
    ContextMenu {
        id: filterMenu; parent: Overlay.overlay; objectName: "coinBrowserFilterMenu"
        ContextMenuPicker {
            title: qsTr("Coin type")
            model: [{text: qsTr("All"), value: "all"}, {text: qsTr("Spendable"), value: "spendable"}, {text: qsTr("Locked"), value: "locked"}]
            currentValue: root.coins ? root.coins.filter : "all"
            onActivated: function(value) { root.coins.filter = value; filterMenu.close() }
        }
    }
    ContextMenu {
        id: amountPopup; parent: Overlay.overlay; objectName: "coinBrowserAmountFilterPopup"
        implicitWidth: Math.min(320, root.width)
        onAboutToShow: {
            const low = root.coins && root.coins.availableMinAmount !== undefined ? root.coins.availableMinAmount : 0
            const high = root.coins && root.coins.availableMaxAmount !== undefined ? root.coins.availableMaxAmount : 0
            amountSlider.setValues(root.coins && root.coins.minAmount >= 0 ? root.coins.minAmount : low,
                                   root.coins && root.coins.maxAmount >= 0 ? root.coins.maxAmount : high)
        }
        ColumnLayout {
            Layout.fillWidth: true
            Layout.margins: 12
            spacing: 8
            CoreText { Layout.fillWidth: true; text: qsTr("%1 to %2").arg(lowerAmountText.display).arg(upperAmountText.displayWithUnit); font: Theme.text.description.font; horizontalAlignment: Text.AlignLeft }
            RangeSlider {
                id: amountSlider; objectName: "coinBrowserAmountRangeSlider"; Layout.fillWidth: true
                minValue: root.coins && root.coins.availableMinAmount !== undefined ? root.coins.availableMinAmount : 0
                maxValue: root.coins && root.coins.availableMaxAmount !== undefined ? root.coins.availableMaxAmount : 0
                Accessible.name: qsTr("Coin amount range")
                first.onMoved: if (root.coins) root.coins.minAmount = Math.round(lowerValue)
                second.onMoved: if (root.coins) root.coins.maxAmount = Math.round(upperValue)
            }
        }
    }
    ContextMenu {
        id: clearFiltersMenu; parent: Overlay.overlay; objectName: "coinBrowserClearFiltersMenu"
        ContextMenuButton { text: qsTr("Clear filters"); role: ContextMenuButton.Destructive; onTriggered: { root.clearFilters(); clearFiltersMenu.close() } }
    }
    ContextMenu {
        id: coinContextMenu
        parent: Overlay.overlay
        objectName: "walletCoinContextMenu"
        ContextMenuButton {
            objectName: "walletCoinLockToggle"
            text: root.contextCoinLocked ? qsTr("Unlock coin") : qsTr("Lock coin")
            onTriggered: root.setCoinLocked(root.contextCoinId, !root.contextCoinLocked)
        }
    }

    AlertPopup {
        id: lockedCoinAlert
        objectName: "lockedCoinSelectionAlert"
        parent: Overlay.overlay
        title: qsTr("Use locked coin?")
        message: qsTr("This coin is excluded from automatic coin selection. Do you still want to spend it?")
        onClosed: root.pendingLockedCoinId = ""
        AlertAction {
            text: qsTr("Cancel")
            role: AlertAction.Neutral
            buttonObjectName: "lockedCoinSelectionCancelButton"
        }
        AlertAction {
            text: qsTr("Use coin")
            buttonObjectName: "lockedCoinSelectionConfirmButton"
            onTriggered: {
                const coinId = root.pendingLockedCoinId
                if (coinId.length && root.coins && !root.coins.toggleCoinSelectionById(coinId)) {
                    root.errorText = qsTr("This coin is no longer available. Try again.")
                }
            }
        }
    }
}
