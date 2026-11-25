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
    objectName: "walletDescriptorDetailPage"

    property var wallet: null
    property string addressType: ""
    property var clipboard: Clipboard
    property var exportItem: null
    property bool extendedKeyFormat: false
    // Previous keys of this type are separate descriptors.
    property bool hasPreviousKeys: false
    // Opened without the Descriptors list, so it also offers what the list does.
    property bool standalone: false
    readonly property bool extendedKeyAvailable: !!exportItem && exportItem.extendedKey !== ""
    readonly property string payload: !exportItem ? ""
        : extendedKeyFormat ? exportItem.extendedKey : exportItem.descriptor

    title: exportItem ? exportItem.label : qsTr("Descriptor")
    backButtonObjectName: "walletDescriptorDetailBackButton"
    maximumContentWidth: 560
    contentSpacing: Theme.space.l

    function refresh() {
        const exports = wallet ? wallet.descriptorExports() : []
        exportItem = exports.find(item => item.id === addressType) || null
        hasPreviousKeys = !!wallet && wallet.inactiveDescriptorExports().some(item => item.id === addressType)
        if (!extendedKeyAvailable) extendedKeyFormat = false
    }

    function setExtendedKeyFormat(enabled) {
        extendedKeyFormat = enabled && extendedKeyAvailable
    }

    function copy() {
        if (payload === "") return
        clipboard.setText(payload)
        copiedTimer.restart()
    }

    Component.onCompleted: refresh()
    onWalletChanged: refresh()
    onAddressTypeChanged: refresh()

    Connections {
        target: root.wallet && root.wallet.descriptorExportsChanged ? root.wallet : null
        function onDescriptorExportsChanged() { root.refresh() }
    }

    Timer {
        id: copiedTimer
        interval: 1000
    }

    DescriptorPrivacyNote {
        objectName: "descriptorsPrivacyNote"
        visible: root.standalone
        Layout.fillWidth: true
    }

    SegmentedPicker {
        objectName: "descriptorsFormatPicker"
        Layout.fillWidth: true
        //: Export formats: an output descriptor, or a bare xpub/ypub/zpub key for wallets that only accept those.
        model: [qsTr("Descriptor"), {text: qsTr("Extended key"), enabled: root.extendedKeyAvailable}]
        currentIndex: root.extendedKeyFormat ? 1 : 0
        onSelected: index => root.setExtendedKeyFormat(index === 1)
    }

    // A bare key has no script type, so an importer can guess another one.
    // SegWit keys carry theirs in the ypub or zpub prefix.
    ToastBanner {
        readonly property string addressType: root.exportItem ? root.exportItem.id : ""
        objectName: "descriptorsScriptTypeWarning"
        Layout.fillWidth: true
        shown: root.extendedKeyFormat && (addressType === "bech32m" || addressType === "legacy")
        tone: ToastBanner.Warning
        textObjectName: "descriptorsScriptTypeWarningText"
        text: addressType === "legacy"
            //: "Bitcoin Core App" is this app's name.
            ? qsTr("This key doesn't record that it is for Legacy addresses. A wallet that imports it as another address type will give out addresses this wallet doesn't watch, and payments to them won't show here. Bitcoin Core App, like many wallets, imports a bare key as SegWit. Use the descriptor unless the other wallet asks for a Legacy key.")
            : qsTr("This key doesn't record that it is for Taproot. A wallet that imports it as another address type will give out addresses this wallet doesn't watch, and payments to them won't show here. Use the descriptor unless the other wallet asks for a Taproot key.")
    }

    Rectangle {
        readonly property real side: Math.min(parent.width, 240)
        Layout.preferredWidth: side
        Layout.preferredHeight: side
        Layout.alignment: Qt.AlignHCenter
        radius: Theme.radius.cardInset
        color: Theme.color.qrBackground

        QRImage {
            objectName: "descriptorsQRImage"
            anchors.fill: parent
            anchors.margins: Theme.space.l
            code: root.payload
            backgroundColor: Theme.color.qrBackground
            foregroundColor: Theme.color.qrForeground
        }
    }

    AddressLabel {
        objectName: "descriptorsKeyText"
        visible: root.extendedKeyFormat
        Layout.fillWidth: true
        address: visible && root.exportItem ? root.exportItem.extendedKey : ""
        textStyle: Theme.text.monoDescription
        clipboard: root.clipboard
        surfaceLevel: 0
    }

    DescriptorText {
        objectName: "descriptorsDetail"
        visible: !root.extendedKeyFormat && !!root.exportItem
        Layout.fillWidth: true
        parts: visible ? root.exportItem.descriptorParts : ({})
        descriptor: visible ? root.exportItem.descriptor : ""
        clipboard: root.clipboard
    }

    FormSection {
        Layout.fillWidth: true

        ValueRow {
            objectName: "descriptorsFingerprint"
            visible: !!root.exportItem && root.exportItem.fingerprint !== ""
            Layout.fillWidth: true
            //: BIP32 fingerprint of the wallet's master key.
            title: qsTr("Master fingerprint")
            value: root.exportItem ? root.exportItem.fingerprint : ""
            valueTextStyle: Theme.text.monoDescription
        }

        ValueRow {
            objectName: "descriptorsDerivationPath"
            visible: !!root.exportItem && root.exportItem.derivationPath !== ""
            Layout.fillWidth: true
            title: qsTr("Derivation path")
            value: root.exportItem ? root.exportItem.derivationPath : ""
            valueTextStyle: Theme.text.monoDescription
            showDivider: !root.extendedKeyFormat
        }

        ValueRow {
            objectName: "descriptorsChains"
            visible: !root.extendedKeyFormat && !!root.exportItem
            Layout.fillWidth: true
            //: Derivation branches the descriptor covers: receiving addresses and change addresses.
            title: qsTr("Chains")
            value: !root.exportItem ? ""
                : root.exportItem.changeChain === "" ? qsTr("Receive %1").arg(root.exportItem.receiveChain)
                : qsTr("Receive %1, change %2").arg(root.exportItem.receiveChain).arg(root.exportItem.changeChain)
            showDivider: false
        }
    }

    CoreText {
        objectName: "descriptorsNote"
        Layout.fillWidth: true
        text: {
            if (!root.exportItem) return ""
            //: "Back up or export wallet" is the wallet settings action that saves a watch-only copy of the whole wallet.
            let note = qsTr("Only includes funds on %1 addresses. For a copy of the whole wallet, use Back up or export wallet.").arg(root.exportItem.label)
            if (root.hasPreviousKeys) {
                //: %1 is an address type. "Copy all for Bitcoin Core" is a button on the Descriptors page.
                note += " " + qsTr("Funds on previous %1 keys are not included. To include them, use Copy all for Bitcoin Core on Descriptors, or save a watch-only copy.").arg(root.exportItem.label)
            }
            return note
        }
        textStyle: Theme.text.caption
        color: Theme.color.neutral7
        horizontalAlignment: Text.AlignLeft
    }

    NeutralButton {
        objectName: "descriptorsCopyButton"
        buttonSize: NeutralButton.Large
        Layout.fillWidth: true
        enabled: root.payload !== ""
        text: copiedTimer.running ? qsTr("Copied")
            : root.extendedKeyFormat ? qsTr("Copy extended key")
            : qsTr("Copy descriptor")
        onClicked: root.copy()
    }

    DescriptorCopyAll {
        visible: root.standalone && !!root.exportItem
        Layout.fillWidth: true
        wallet: root.wallet
        clipboard: root.clipboard
    }
}
