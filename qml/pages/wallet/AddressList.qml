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

    property WalletQmlModel wallet: null
    property AddressListModel addressModel: wallet.addressListModel
    property string selectedAddress: ""
    property string selectedLabel: ""
    property string selectedAmount: ""
    property bool selectedHasAmount: false
    property string selectedCategory: ""
    property string selectedScriptType: ""
    property bool selectedUsed: false
    property bool selectedHasPaymentRequest: false
    property string errorText: ""
    property var pendingNotesByAddress: ({})

    signal paymentRequestRequested(string requestId)
    signal receiveRequested

    function createPaymentRequestFromSelected(closeAction) {
        root.errorText = "";
        const requests = wallet && wallet.receiveRequests
            ? wallet.receiveRequests.matchingEntriesForAddress(root.selectedAddress) : [];
        if (requests.length > 0) {
            if (closeAction) closeAction();
            root.paymentRequestRequested(requests[0].requestId);
            return;
        }
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
        if (root.walletBusy()) {
            root.errorText = root.walletBusyText();
            return;
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
        if (root.addressModel.labelChangePending) return false
        root.errorText = ""
        if (root.addressModel.setAddressLabel(address, label)) return true
        root.errorText = root.walletBusy() ? root.walletBusyText()
            : qsTr("The address label could not be saved. Please try again.")
        return false
    }

    function walletBusy() { return !!root.wallet && root.wallet.receiveOperationPending }
    function walletBusyText() { return qsTr("The wallet is saving another change. Try again in a moment.") }

    Connections {
        target: root.addressModel
        function onLabelChangeFinished(address, label, success, error) {
            if (success && root.selectedAddress === address) root.selectedLabel = label
            if (success && root.pendingNotesByAddress[address] === label) root.clearPendingNote(address)
            root.errorText = error
            if (root.selectedAddress === address) addressDetails.noteErrorText = error
        }
    }

    rightItem: OverflowMenuButton {
        objectName: "addressesMenuButton"
        checked: pageMenu.opened
        onClicked: {
            pageMenu.openBelow(this, 0);
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

    DialogPopup {
        id: detailsPopup
        objectName: "addressDetailsPopup"
        parent: Overlay.overlay
        title: qsTr("Address details")
        implicitWidth: 640
        contentItem: ScrollView {
            id: detailsScroll
            contentWidth: availableWidth
            clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

            AddressDetails {
                id: addressDetails
                width: detailsScroll.availableWidth
                address: root.selectedAddress
                label: root.selectedLabel
                amount: root.selectedAmount
                hasAmount: root.selectedHasAmount
                category: root.selectedCategory
                scriptType: root.selectedScriptType
                used: root.selectedUsed
                hasPaymentRequest: root.selectedHasPaymentRequest
                labelChangePending: root.addressModel.labelChangePending
                receiveOperationPending: !!root.wallet && root.wallet.receiveOperationPending
                onCloseRequested: detailsPopup.close()
                onCreatePaymentRequestRequested: {
                    root.createPaymentRequestFromSelected(function() { detailsPopup.close(); });
                }
                onEditLabelRequested: (address, label) => {
                    if (root.addressModel.labelChangePending) return
                    if (root.updateAddressLabel(address, label)) {
                        noteErrorText = "";
                    } else {
                        noteErrorText = root.walletBusy() ? root.walletBusyText()
                            : qsTr("This address is no longer available.");
                    }
                }
            }
        }
    }

    PageHeading {
        objectName: "addressListIntroduction"
        Layout.fillWidth: true
        descriptionTextFormat: Text.StyledText
        description: qsTr("View addresses generated by this wallet.") + "<br><br>"
            + qsTr("<b>Single-use</b> addresses are intended for receiving one payment; avoiding address reuse helps protect your privacy.") + "<br>"
            + qsTr("<b>Change</b> addresses are created and managed automatically when you send bitcoin, receiving any remaining amount back into your wallet.")
    }

    SegmentedPicker {
        Accessible.name: qsTr("Addresses")
        Layout.fillWidth: true
        Layout.maximumWidth: 360
        Layout.alignment: Qt.AlignHCenter
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
        color: Theme.color.dangerText
        textStyle: Theme.text.description
        horizontalAlignment: Text.AlignLeft
    }

    CoreText {
        Layout.fillWidth: true
        visible: addressModel.count === 0
        text: {
            if (addressModel.category === AddressListModel.Change) {
                return qsTr("No current change addresses.");
            }
            return addressModel.showUsed ? qsTr("No receive addresses.") : qsTr("No unused receive addresses.");
        }
        color: Theme.color.neutral6
        textStyle: Theme.text.description
        horizontalAlignment: Text.AlignLeft
    }

    CoreText {
        objectName: "addressLabelSavingText"
        Layout.fillWidth: true
        visible: root.addressModel.labelChangePending
        text: qsTr("Saving…")
        color: Theme.color.neutral6
        textStyle: Theme.text.description
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
            reuseItems: addressModel.category === AddressListModel.Change
            model: addressModel
            spacing: 0

            delegate: AddressRow {
                width: ListView.view.width
                pendingLabel: root.pendingNote(address, label)
                labelChangePending: root.addressModel.labelChangePending
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
                    // Resolved when the popup opens: a binding over the
                    // request model would go stale, since it cannot see
                    // requests added while the same address stays selected.
                    root.selectedHasPaymentRequest = root.wallet && root.wallet.receiveRequests
                        ? root.wallet.receiveRequests.matchingEntriesForAddress(address).length > 0
                        : false;
                    addressDetails.resetNoteEditor();
                    detailsPopup.open();
                }
            }
        }
    }
}
