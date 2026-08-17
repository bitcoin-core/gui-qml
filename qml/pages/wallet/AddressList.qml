pragma ComponentBehavior: Bound

// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0

import "../../controls"
import "../../components"

SettingsPage {
    id: root
    objectName: "addressListPage"
    title: qsTr("Addresses")
    backButtonObjectName: "addressListBackButton"
    maximumContentWidth: 840
    contentSpacing: 16

    property WalletQmlModel wallet: walletController.selectedWallet
    property AddressListModel addressModel: wallet.addressListModel
    property string selectedAddress: ""
    property string selectedLabel: ""
    property string selectedAmount: ""
    property bool selectedHasAmount: false
    property string selectedCategory: ""
    property string selectedScriptType: ""
    property bool selectedUsed: false
    property string errorText: ""
    property var pendingNotesByAddress: ({})

    signal receiveRequested

    function openMenuAt(menu, item) {
        const pos = item.mapToItem(Overlay.overlay, item.width, item.height);
        menu.x = Math.max(12, Math.min(pos.x - menu.width, Overlay.overlay.width - menu.width - 12));
        menu.y = Math.max(12, Math.min(pos.y, Overlay.overlay.height - menu.height - 12));
        menu.open();
    }

    function createPaymentRequestFromSelected(closeAction) {
        root.errorText = "";
        if (wallet && wallet.setCurrentPaymentRequestAddress(root.selectedAddress)) {
            if (closeAction) {
                closeAction();
            }
            root.receiveRequested();
            return;
        }
        if (closeAction) {
            closeAction();
        }
        addressModel.refresh();
        root.errorText = qsTr("This address is no longer available.");
    }

    function pendingNote(address, fallbackLabel) {
        const note = root.pendingNotesByAddress[address]
        return note === undefined ? fallbackLabel : note
    }

    function updatePendingNote(address, note) {
        const notes = {}
        for (const key in root.pendingNotesByAddress) {
            notes[key] = root.pendingNotesByAddress[key]
        }
        notes[address] = note
        root.pendingNotesByAddress = notes
    }

    function clearPendingNote(address) {
        const notes = {}
        for (const key in root.pendingNotesByAddress) {
            if (key !== address) notes[key] = root.pendingNotesByAddress[key]
        }
        root.pendingNotesByAddress = notes
    }

    function updateAddressLabel(address, label) {
        root.errorText = ""
        if (root.addressModel.setAddressLabel(address, label)) {
            if (root.selectedAddress === address) root.selectedLabel = label
            root.clearPendingNote(address)
            return true
        }
        root.clearPendingNote(address)
        root.addressModel.refresh()
        root.errorText = qsTr("This address is no longer available.")
        return false
    }

    rightItem: IconButton {
        objectName: "addressesMenuButton"
        iconSource: "image://images/ellipsis"
        iconColor: Theme.color.neutral9
        size: 28
        onClicked: {
            root.openMenuAt(pageMenu, this);
        }
    }

    Component.onCompleted: addressModel.refresh()

    ContextMenu {
        id: pageMenu
        parent: Overlay.overlay
        modal: true
        dim: false
        focus: true

        ContextMenuToggle {
            checkable: false
            text: qsTr("Show used addresses")
            checked: addressModel.showUsed
            onClicked: addressModel.showUsed = !addressModel.showUsed
        }
    }

    Popup {
        id: detailsPopup
        objectName: "addressDetailsPopup"
        anchors.centerIn: Overlay.overlay
        width: Math.min(560, root.width - 40)
        modal: true
        focus: true
        leftPadding: 40
        rightPadding: 40
        topPadding: 30
        bottomPadding: 30
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle {
            color: Theme.color.neutral0
            border.color: Theme.color.neutral4
            radius: 10
        }
        contentItem: AddressDetails {
            address: root.selectedAddress
            label: root.selectedLabel
            amount: root.selectedAmount
            hasAmount: root.selectedHasAmount
            category: root.selectedCategory
            scriptType: root.selectedScriptType
            used: root.selectedUsed
            onCloseRequested: detailsPopup.close()
            onCopyAddressRequested: Clipboard.setText(root.selectedAddress)
            onCreatePaymentRequestRequested: {
                root.createPaymentRequestFromSelected(function() { detailsPopup.close(); });
            }
        }
    }

    SegmentedPicker {
        Layout.fillWidth: true
        Layout.maximumWidth: 360
        Layout.alignment: Qt.AlignHCenter
        implicitHeight: 36
        model: addressModel.categoryOptions
        currentIndex: Math.max(0, addressModel.categoryOptions.findIndex(option => option.value === addressModel.category))
        onSelected: (index, option) => {
            root.errorText = "";
            addressModel.category = option.value;
        }
    }

    CoreText {
        Layout.fillWidth: true
        visible: root.errorText.length > 0
        text: root.errorText
        color: Theme.color.red
        font: Theme.text.description.font
        horizontalAlignment: Text.AlignLeft
    }

    CoreText {
        Layout.fillWidth: true
        visible: addressModel.count === 0
        text: {
            if (addressModel.category === AddressListModel.Change) {
                return qsTr("No current change addresses.");
            }
            return addressModel.showUsed ? qsTr("No single-use addresses.") : qsTr("No unused single-use addresses.");
        }
        color: Theme.color.neutral6
        font: Theme.text.description.font
        horizontalAlignment: Text.AlignLeft
    }

    FormSection {
        objectName: "addressListSection"
        visible: addressModel.count > 0
        Layout.fillWidth: true

        ListView {
            id: addressList
            objectName: "addressListView"
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(contentHeight, Math.max(76, root.height - 230))
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            reuseItems: true
            model: addressModel
            spacing: 0

            delegate: AddressRow {
                width: ListView.view.width
                pendingLabel: root.pendingNote(address, label)
                showDivider: index < addressList.count - 1
                onNoteDraftEdited: (address, label) => root.updatePendingNote(address, label)
                onNoteDraftDiscarded: (address) => root.clearPendingNote(address)
                onEditLabelRequested: (address, label) => root.updateAddressLabel(address, label)
                onDetailsRequested: (address, label, amount, hasAmount, category, scriptType, used) => {
                    root.selectedAddress = address;
                    root.selectedLabel = root.pendingNote(address, label);
                    root.selectedAmount = amount;
                    root.selectedHasAmount = hasAmount;
                    root.selectedCategory = category;
                    root.selectedScriptType = scriptType;
                    root.selectedUsed = used;
                    detailsPopup.open();
                }
            }
        }
    }
}
