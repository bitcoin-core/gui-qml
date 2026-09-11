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
    objectName: "signVerifyMessagePage"
    background: null

    property WalletQmlModel wallet: walletController.selectedWallet
    property var signVerifyModel: wallet ? wallet.signVerifyMessageModel : null
    property string verifyResultText: ""
    property bool verifyResultSuccess: false
    property int initialTab: SignVerifyMessage.SignTab

    enum Tabs { SignTab, VerifyTab }

    signal back

    ButtonGroup {
        id: messageTabs
    }

    function clearSignForm() {
        signAddress.text = "";
        signMessageText.text = "";
        if (root.signVerifyModel) {
            root.signVerifyModel.clear();
        }
    }

    function clearVerifyForm() {
        verifyAddress.text = "";
        verifyMessageText.text = "";
        verifySignature.text = "";
        root.verifyResultText = "";
        root.verifyResultSuccess = false;
    }

    function addressErrorText(address) {
        if (address.length === 0 || (root.signVerifyModel && root.signVerifyModel.isLegacyP2PKHAddress(address))) {
            return "";
        }
        return qsTr("Not a valid P2PKH address.");
    }

    function submitSign(passphrase) {
        if (!root.signVerifyModel) {
            return;
        }
        const signed = passphrase === undefined ? root.signVerifyModel.signMessage(signAddress.text, signMessageText.text) : root.signVerifyModel.signMessageWithPassphrase(signAddress.text, signMessageText.text, passphrase);
        if (signed) {
            signPassphrasePopup.close();
            return;
        }
        if (root.signVerifyModel.signingNeedsUnlock) {
            signPassphrasePopup.errorText = "";
            signPassphrasePopup.open();
        }
    }

    function submitVerify() {
        const verified = root.signVerifyModel && root.signVerifyModel.verifyMessage(verifyAddress.text, verifyMessageText.text, verifySignature.text);
        root.verifyResultSuccess = verified;
        root.verifyResultText = verified ? qsTr("Message verified successfully.") : qsTr("Message verification failed.");
    }

    header: SettingsHeader {
        title: qsTr("Sign or verify message")
        backButtonObjectName: "signVerifyMessageBackButton"
        onBack: root.back()
    }

    ScrollView {
        clip: true
        anchors.fill: parent
        contentWidth: width

        ColumnLayout {
            width: Math.min(parent.width - 40, 706)
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 0

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 18
                Layout.maximumWidth: 508
                Layout.alignment: Qt.AlignHCenter
                spacing: 0

                NavigationTab {
                    id: signTabButton
                    objectName: "signMessageTab"
                    text: qsTr("Sign Message")
                    checked: true
                    Layout.fillWidth: true
                    property int index: 0
                    ButtonGroup.group: messageTabs
                }

                NavigationTab {
                    id: verifyTabButton
                    objectName: "verifyMessageTab"
                    text: qsTr("Verify Message")
                    Layout.fillWidth: true
                    property int index: 1
                    ButtonGroup.group: messageTabs
                }
            }

            StackLayout {
                Layout.fillWidth: true
                currentIndex: messageTabs.checkedButton.index

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0

                    CoreText {
                        Layout.fillWidth: true
                        Layout.topMargin: 36
                        text: qsTr("You can sign messages or agreements with your legacy P2PKH addresses to prove you can receive bitcoin sent to them. Be careful not to sign anything vague or random, as phishing attacks may try to trick you into signing your identity over to them. Only sign fully detailed statements you agree to.")
                        color: Theme.color.neutral7
                        font: Theme.text.description.font
                        wrapMode: Text.WordWrap
                    }

                    LineTextField {
                        id: signAddress
                        objectName: "signMessageAddressField"
                        Layout.fillWidth: true
                        Layout.topMargin: 22
                        placeholderText: qsTr("Enter a bitcoin address")
                    }

                    AddressError {
                        objectName: "signMessageAddressError"
                        Layout.fillWidth: true
                        Layout.topMargin: 8
                        text: root.addressErrorText(signAddress.text)
                    }

                    LineTextArea {
                        id: signMessageText
                        objectName: "signMessageMessageField"
                        Layout.fillWidth: true
                        Layout.topMargin: 22
                        Layout.preferredHeight: 150
                        placeholderText: qsTr("Enter the message you want to sign here")
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.topMargin: 32
                        spacing: 15

                        ContinueButton {
                            objectName: "signMessageButton"
                            Layout.preferredWidth: 206
                            text: qsTr("Sign Message")
                            enabled: root.signVerifyModel && root.signVerifyModel.isLegacyP2PKHAddress(signAddress.text)
                            onClicked: root.submitSign()
                        }

                        OutlineButton {
                            objectName: "signMessageClearButton"
                            Layout.preferredWidth: 140
                            text: qsTr("Clear All")
                            onClicked: root.clearSignForm()
                        }
                    }

                    CoreText {
                        objectName: "signMessageErrorText"
                        Layout.fillWidth: true
                        Layout.topMargin: 28
                        visible: root.signVerifyModel && root.signVerifyModel.signingError.length > 0 && !root.signVerifyModel.signingNeedsUnlock
                        text: root.signVerifyModel ? root.signVerifyModel.signingError : ""
                        color: Theme.color.red
                        font: Theme.text.caption.font
                        wrapMode: Text.WordWrap
                    }

                    ColumnLayout {
                        objectName: "signMessageSignatureOutput"
                        Layout.fillWidth: true
                        Layout.topMargin: 28
                        visible: root.signVerifyModel && root.signVerifyModel.signature.length > 0
                        spacing: 10

                        CoreText {
                            text: qsTr("Signature")
                            color: Theme.color.neutral7
                            font: Theme.text.body.font
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: Math.max(70, signatureText.contentHeight + 34)
                            color: Theme.color.neutral3
                            radius: 5

                            CoreText {
                                id: signatureText
                                objectName: "signMessageSignatureText"
                                anchors.left: parent.left
                                anchors.right: copySignatureButton.left
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.leftMargin: 22
                                anchors.rightMargin: 10
                                text: root.signVerifyModel ? root.signVerifyModel.signature : ""
                                color: Theme.color.neutral9
                                font: Theme.text.body.font
                                wrapMode: Text.WrapAnywhere
                            }

                            IconButton {
                                id: copySignatureButton
                                objectName: "signMessageCopySignatureButton"
                                anchors.right: parent.right
                                anchors.rightMargin: 22
                                anchors.verticalCenter: parent.verticalCenter
                                iconSource: "image://images/copy"
                                iconColor: Theme.color.neutral7
                                hoverColor: Theme.color.orange
                                onClicked: Clipboard.setText(root.signVerifyModel ? root.signVerifyModel.signature : "")
                            }
                        }
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0

                    CoreText {
                        Layout.fillWidth: true
                        Layout.topMargin: 36
                        text: qsTr("Enter the receiver's address, message (ensure you copy line breaks), spaces, tabs, etc. exactly) and signature below to verify the message. Be careful not to read more into the signature than what is in the signed message itself, to avoid being tricked by a man-in-the-middle attack. Note that this only proves the signing party receives with the address, it cannot prove sendership of any transaction.")
                        color: Theme.color.neutral7
                        font: Theme.text.description.font
                        wrapMode: Text.WordWrap
                    }

                    LineTextField {
                        id: verifyAddress
                        objectName: "verifyMessageAddressField"
                        Layout.fillWidth: true
                        Layout.topMargin: 22
                        placeholderText: qsTr("Enter a bitcoin address")
                    }

                    AddressError {
                        objectName: "verifyMessageAddressError"
                        Layout.fillWidth: true
                        Layout.topMargin: 8
                        text: root.addressErrorText(verifyAddress.text)
                    }

                    LineTextArea {
                        id: verifyMessageText
                        objectName: "verifyMessageMessageField"
                        Layout.fillWidth: true
                        Layout.topMargin: 22
                        Layout.preferredHeight: 150
                        placeholderText: qsTr("Enter the message you want to verify here")
                    }

                    CoreText {
                        Layout.fillWidth: true
                        Layout.topMargin: 28
                        text: qsTr("Signature")
                        color: Theme.color.neutral7
                        font: Theme.text.body.font
                        horizontalAlignment: Text.AlignLeft
                    }

                    LineTextArea {
                        id: verifySignature
                        objectName: "verifyMessageSignatureField"
                        Layout.fillWidth: true
                        Layout.topMargin: 12
                        Layout.preferredHeight: 68
                        placeholderText: qsTr("The signature when the message was signed")
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.topMargin: 32
                        spacing: 15

                        ContinueButton {
                            objectName: "verifyMessageButton"
                            Layout.preferredWidth: 220
                            text: qsTr("Verify Message")
                            enabled: root.signVerifyModel && root.signVerifyModel.isLegacyP2PKHAddress(verifyAddress.text) && verifySignature.text.length > 0
                            onClicked: root.submitVerify()
                        }

                        OutlineButton {
                            objectName: "verifyMessageClearButton"
                            Layout.preferredWidth: 140
                            text: qsTr("Clear All")
                            onClicked: root.clearVerifyForm()
                        }
                    }

                    ToastBanner {
                        objectName: "verifyMessageResultBanner"
                        Layout.fillWidth: true
                        Layout.topMargin: 28
                        visible: root.verifyResultText.length > 0
                        backgroundColor: root.verifyResultSuccess ? Theme.color.green : Theme.color.red
                        iconSource: root.verifyResultSuccess ? "image://images/check" : "image://images/info-filled"
                        text: root.verifyResultText
                        textObjectName: "verifyMessageResultText"
                    }
                }
            }
        }
    }

    WalletPassphrasePopup {
        id: signPassphrasePopup
        parent: Overlay.overlay
        width: Math.min(420, root.width - 40)
        popupObjectName: "signMessagePassphrasePopup"
        passphraseFieldObjectName: "signMessagePassphraseField"
        errorTextObjectName: "signMessagePassphraseErrorText"
        cancelButtonObjectName: "signMessagePassphraseCancelButton"
        confirmButtonObjectName: "signMessagePassphraseConfirmButton"
        titleText: qsTr("Enter wallet password")
        descriptionText: qsTr("Enter the wallet password to sign this message.")
        confirmText: qsTr("Unlock and sign")
        busyConfirmText: qsTr("Signing...")
        onSubmitted: passphrase => {
            signPassphrasePopup.busy = true;
            if (root.signVerifyModel.signMessageWithPassphrase(signAddress.text, signMessageText.text, passphrase)) {
                signPassphrasePopup.busy = false;
                signPassphrasePopup.close();
                return;
            }
            signPassphrasePopup.busy = false;
            signPassphrasePopup.errorText = root.signVerifyModel.signingError;
        }
    }

    component LineTextField: TextField {
        id: lineField
        selectByMouse: true
        font: Theme.text.body.font
        color: Theme.color.neutral9
        placeholderTextColor: Theme.color.neutral6
        leftPadding: 0
        rightPadding: 0
        background: Rectangle {
            implicitHeight: 45
            color: "transparent"
            border.color: "transparent"
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: Theme.color.neutral5
            }
        }
    }

    component AddressError: RowLayout {
        property alias text: errorText.text

        visible: errorText.text.length > 0
        spacing: 8

        Icon {
            source: "image://images/alert-filled"
            size: 22
            color: Theme.color.red
        }

        CoreText {
            id: errorText
            color: Theme.color.red
            font: Theme.text.description.font
            horizontalAlignment: Text.AlignLeft
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
        }
    }

    component LineTextArea: TextArea {
        id: lineArea
        selectByMouse: true
        wrapMode: TextEdit.Wrap
        font: Theme.text.body.font
        color: Theme.color.neutral9
        placeholderTextColor: Theme.color.neutral6
        leftPadding: 0
        rightPadding: 0
        topPadding: 0
        bottomPadding: 10
        background: Rectangle {
            color: "transparent"
            border.color: "transparent"
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: Theme.color.neutral5
            }
        }
    }

    Component.onCompleted: {
        if (root.initialTab === SignVerifyMessage.VerifyTab) {
            verifyTabButton.checked = true
        } else {
            signTabButton.checked = true
        }
    }
}
