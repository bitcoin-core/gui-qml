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
    readonly property bool extendedKeyAvailable: !!exportItem && exportItem.extendedKey !== ""
    readonly property string payload: !exportItem ? ""
        : extendedKeyFormat ? exportItem.extendedKey : exportItem.descriptor

    title: exportItem ? exportItem.label : qsTr("Descriptor")
    backButtonObjectName: "walletDescriptorDetailBackButton"
    maximumContentWidth: 560
    contentSpacing: 16

    function refresh() {
        const exports = wallet ? wallet.descriptorExports() : []
        exportItem = exports.find(item => item.id === addressType) || null
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

    SegmentedPicker {
        objectName: "descriptorsFormatPicker"
        Layout.fillWidth: true
        implicitHeight: 36
        //: Export formats: an output descriptor, or a bare xpub/ypub/zpub key for wallets that only accept those.
        model: [qsTr("Descriptor"), {text: qsTr("Extended key"), enabled: root.extendedKeyAvailable}]
        currentIndex: root.extendedKeyFormat ? 1 : 0
        onSelected: index => root.setExtendedKeyFormat(index === 1)
    }

    Rectangle {
        readonly property real side: Math.min(parent.width, 240)
        Layout.preferredWidth: side
        Layout.preferredHeight: side
        Layout.alignment: Qt.AlignHCenter
        radius: 10
        color: "white"

        QRImage {
            objectName: "descriptorsQRImage"
            anchors.fill: parent
            anchors.margins: 14
            code: root.payload
            backgroundColor: "white"
            foregroundColor: "black"
        }
    }

    AddressLabel {
        objectName: "descriptorsKeyText"
        visible: root.extendedKeyFormat
        Layout.fillWidth: true
        address: visible && root.exportItem ? root.exportItem.extendedKey : ""
        textStyle: Theme.text.monoDescription
        clipboard: root.clipboard
        embedded: true
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
            if (root.extendedKeyFormat && root.exportItem.id === "bech32m") {
                note += " " + qsTr("Most wallets need the descriptor format to import Taproot keys.")
            }
            return note
        }
        font: Theme.text.caption.font
        lineHeight: Theme.text.caption.lineHeight
        lineHeightMode: Text.FixedHeight
        color: Theme.color.neutral7
        horizontalAlignment: Text.AlignLeft
        wrap: true
    }

    OutlineButton {
        objectName: "descriptorsCopyButton"
        Layout.fillWidth: true
        enabled: root.payload !== ""
        text: copiedTimer.running ? qsTr("Copied")
            : root.extendedKeyFormat ? qsTr("Copy extended key")
            : qsTr("Copy descriptor")
        onClicked: root.copy()
    }
}
