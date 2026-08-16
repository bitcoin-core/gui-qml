// Copyright (c) 2026 The Bitcoin Core developers
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
    objectName: "addressListPage"
    background: null

    property WalletQmlModel wallet: walletController.selectedWallet
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

    signal back
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

    header: SettingsHeader {
        title: qsTr("Addresses")
        backButtonObjectName: "addressListBackButton"
        onBack: root.back()
        rightItem: IconButton {
            objectName: "addressesMenuButton"
            iconSource: "image://images/ellipsis"
            iconColor: Theme.color.neutral9
            size: 28
            onClicked: {
                root.openMenuAt(pageMenu, this);
            }
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
        id: labelPopup
        objectName: "addressLabelPopup"
        anchors.centerIn: Overlay.overlay
        width: Math.min(420, root.width - 40)
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
        contentItem: ColumnLayout {
            spacing: 16

            Header {
                Layout.fillWidth: true
                header: qsTr("Note to self")
                headerBold: true
                center: false
            }
            CoreTextField {
                id: labelInput
                objectName: "addressLabelInput"
                Layout.fillWidth: true
                placeholderText: qsTr("Add note...")
            }
            ContinueButton {
                objectName: "addressLabelSaveButton"
                Layout.fillWidth: true
                text: qsTr("Save")
                onClicked: {
                    root.errorText = "";
                    if (addressModel.setAddressLabel(root.selectedAddress, labelInput.text)) {
                        labelPopup.close();
                    } else {
                        labelPopup.close();
                        addressModel.refresh();
                        root.errorText = qsTr("This address is no longer available.");
                    }
                }
            }
        }
    }

    Popup {
        id: detailsPopup
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
            objectName: "addressDetailsView"
            address: root.selectedAddress
            label: root.selectedLabel
            amount: root.selectedAmount
            hasAmount: root.selectedHasAmount
            category: root.selectedCategory
            scriptType: root.selectedScriptType
            used: root.selectedUsed
            hasPaymentRequest: root.selectedHasPaymentRequest
            onCloseRequested: detailsPopup.close()
            onCopyAddressRequested: Clipboard.setText(root.selectedAddress)
            onCreatePaymentRequestRequested: {
                root.createPaymentRequestFromSelected(function() { detailsPopup.close(); });
            }
        }
    }

    ScrollView {
        anchors.fill: parent
        clip: true
        contentWidth: width

        ColumnLayout {
            width: Math.min(520, parent.width)
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 26

            SegmentedPicker {
                Layout.fillWidth: true
                Layout.topMargin: 28
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
                font: Theme.text.body.font
                horizontalAlignment: Text.AlignLeft
            }

            ListView {
                id: addressList
                objectName: "addressListView"
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(contentHeight, root.height - 230)
                clip: true
                model: addressModel
                spacing: 0

                delegate: AddressRow {
                    width: addressList.width
                    onEditLabelRequested: (address, label) => {
                        root.selectedAddress = address;
                        root.selectedLabel = label;
                        labelInput.text = label;
                        labelPopup.open();
                    }
                    onCreatePaymentRequestRequested: (address) => {
                        root.selectedAddress = address;
                        root.createPaymentRequestFromSelected(undefined);
                    }
                    onDetailsRequested: (address, label, amount, hasAmount, category, scriptType, used) => {
                        root.selectedAddress = address;
                        root.selectedLabel = label;
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
                        detailsPopup.open();
                    }
                }
            }
        }
    }

}
