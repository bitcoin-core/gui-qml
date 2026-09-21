// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Dialogs
import org.bitcoincore.qt 1.0
import "../controls"

Pane {
    id: root
    property var wallet
    property var clipboard: Clipboard
    property var request: wallet ? wallet.receivingAddress : null
    property string errorText: ""
    property bool pendingNext: false
    property string pendingType: ""
    readonly property bool ready: !!request && request.address !== "" && !request.paymentReceived
    padding: 24
    implicitHeight: contentItem.implicitHeight + topPadding + bottomPadding
    background: Rectangle { color: Theme.color.neutral1; radius: 16 }

    function ensureAddress(next, type) {
        if (!wallet) return
        pendingNext = !!next
        pendingType = type || ""
        if (wallet.ensureReceivingAddress(pendingNext, pendingType)) errorText = ""
        else if (request.needsUnlock) passphrasePopup.open()
        else errorText = qsTr("A receiving address could not be generated. Please try again.")
    }
    function captureQR(callback) {
        if (!ready || qrImage.status !== Image.Ready) {
            errorText = qsTr("The QR code is not ready. Please try again.")
            return false
        }
        const address = request.address
        return qrPanel.grabToImage(function(result) {
            if (root.ready && root.request.address === address) callback(result)
        }, Qt.size(784, 784))
    }
    function copyQR() {
        return captureQR(function(result) {
            if (!root.clipboard.setImage(result.image)) root.errorText = qsTr("The QR code could not be copied. Please try again.")
        })
    }
    function saveQRToFile(fileUrl) {
        return captureQR(function(result) {
            if (!result.saveToFile(decodeURIComponent(fileUrl.toString().replace(/^file:\/\//, ""))))
                root.errorText = qsTr("The QR code could not be saved. Please try again.")
        })
    }
    Connections {
        target: root.request
        function onAddressChanged() { qrMenu.close(); saveDialog.close() }
        function onPaymentReceivedChanged() {
            if (root.request.paymentReceived) {
                qrMenu.close()
                saveDialog.close()
                Qt.callLater(function() { root.ensureAddress(false, "") })
            }
        }
    }
    contentItem: ColumnLayout {
        spacing: 24
        CoreText {
            Layout.fillWidth: true
            text: qsTr("Receiving address")
            font: Theme.text.title.font
        }
        Rectangle {
            Layout.alignment: Qt.AlignHCenter
            implicitWidth: readyLabel.implicitWidth + 24
            implicitHeight: 30
            radius: 15
            color: root.ready ? Qt.rgba(Theme.color.green.r, Theme.color.green.g, Theme.color.green.b, 0.16)
                : Theme.color.neutral2
            CoreText {
                id: readyLabel
                anchors.centerIn: parent
                text: root.ready ? qsTr("Ready to receive") : qsTr("Preparing address")
                font: Theme.text.captionStrong.font
                color: root.ready ? Theme.color.green : Theme.color.neutral6
            }
        }
        Rectangle {
            id: qrPanel
            visible: root.ready
            Layout.preferredWidth: 196
            Layout.preferredHeight: 196
            Layout.alignment: Qt.AlignHCenter
            color: "white"
            radius: 10
            QRImage {
                id: qrImage
                objectName: "receivingAddressQRImage"
                anchors.fill: parent
                anchors.margins: 16
                code: root.ready ? root.request.address : ""
                backgroundColor: "white"
                foregroundColor: "black"
            }
            MouseArea {
                objectName: "receivingAddressQRContextArea"
                anchors.fill: parent
                acceptedButtons: Qt.RightButton
                enabled: root.ready
                onClicked: function(mouse) {
                    qrMenu.x = mouse.x
                    qrMenu.y = mouse.y
                    qrMenu.open()
                }
            }
            ContextMenu {
                id: qrMenu
                objectName: "receivingAddressQRContextMenu"
                ContextMenuButton {
                    objectName: "receivingAddressQRContextCopy"
                    text: qsTr("Copy QR code")
                    enabled: root.ready
                    onTriggered: root.copyQR()
                }
                ContextMenuButton {
                    objectName: "receivingAddressQRContextSave"
                    text: qsTr("Save QR code")
                    enabled: root.ready
                    onTriggered: if (root.ready) saveDialog.open()
                }
            }
        }
        AddressLabel {
            objectName: "receivingAddressText"
            Layout.fillWidth: true
            address: root.ready ? root.request.address : ""
            textStyle: Theme.text.monoDescription
            embedded: true
            interactive: root.ready
        }
        FormRow {
            Layout.fillWidth: true
            title: qsTr("Address type")
            showDivider: false
            leftPadding: 0
            rightPadding: 0
            trailingItem: PopupPicker {
                objectName: "requestPaymentAddressTypeDropdown"
                model: root.wallet ? root.wallet.availableReceiveAddressTypes() : []
                textRole: "label"
                valueRole: "id"
                subtitleRole: "description"
                embedded: true
                currentValue: root.request ? root.request.addressType.toLowerCase() : "bech32m"
                onActivated: function(value) { root.ensureAddress(false, value) }
            }
        }
        CoreText {
            visible: root.errorText !== ""
            Layout.fillWidth: true
            text: root.errorText
            color: Theme.color.red
            font: Theme.text.caption.font
            wrap: true
        }
        TextButton {
            visible: !root.ready
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("Try again")
            onClicked: root.ensureAddress(root.pendingNext, root.pendingType)
        }
    }
    FileDialog {
        id: saveDialog
        objectName: "receivingAddressSaveQRDialog"
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("PNG files (*.png)")]
        defaultSuffix: "png"
        onAccepted: root.saveQRToFile(selectedFile)
    }
    WalletPassphrasePopup {
        id: passphrasePopup
        parent: Overlay.overlay
        width: Math.min(420, parent ? parent.width - 32 : 420)
        titleText: qsTr("Enter wallet password")
        descriptionText: qsTr("Enter your wallet password to create a receiving address.")
        confirmText: qsTr("Unlock and create address")
        onSubmitted: function(passphrase) {
            if (root.wallet.ensureReceivingAddressWithPassphrase(passphrase, root.pendingNext, root.pendingType)) {
                root.errorText = ""
                close()
            } else errorText = root.request.unlockError || qsTr("The receiving address could not be created.")
        }
    }
}
