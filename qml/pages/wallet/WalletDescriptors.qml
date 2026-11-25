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
    objectName: "walletDescriptorsPage"

    property var wallet: null
    property var clipboard: Clipboard
    property var exports: []
    // Descriptors that no longer give out addresses but may still hold funds.
    property var inactiveExports: []
    readonly property var completeExports: exports.concat(inactiveExports)
    readonly property bool coversChange: completeExports.length > 0 && completeExports.every(item => item.coversChange)
    readonly property string sharedFingerprint: {
        const fingerprints = new Set(completeExports.map(item => item.fingerprint))
        return fingerprints.size === 1 ? completeExports[0].fingerprint : ""
    }

    signal descriptorRequested(string addressType)

    title: qsTr("Descriptors")
    backButtonObjectName: "walletDescriptorsBackButton"
    maximumContentWidth: 706
    contentSpacing: Theme.space.l

    function refresh() {
        exports = wallet ? wallet.descriptorExports() : []
        inactiveExports = wallet ? wallet.inactiveDescriptorExports() : []
    }

    function shortKey(key) {
        return key.length > 17 ? key.substring(0, 8) + "..." + key.substring(key.length - 8) : key
    }

    // "sh(wpkh)" and a key become "sh(wpkh(tpubDDen...WQTz))".
    function shortDescriptor(item) {
        const scripts = item.scriptName.replace(/\)/g, "").split("(")
        return scripts.join("(") + "(" + shortKey(item.xpub) + ")".repeat(scripts.length)
    }

    Component.onCompleted: refresh()
    onWalletChanged: refresh()

    Connections {
        target: root.wallet && root.wallet.descriptorExportsChanged ? root.wallet : null
        function onDescriptorExportsChanged() { root.refresh() }
    }

    DescriptorPrivacyNote {
        objectName: "descriptorsListNote"
        Layout.fillWidth: true
    }

    FormSection {
        objectName: "descriptorsTypeList"
        Layout.fillWidth: true
        title: qsTr("Address types")
        // Imported descriptors can cover receive addresses only.
        footerText: {
            if (root.sharedFingerprint === "") {
                return root.coversChange ? qsTr("Each descriptor covers receive and change addresses.") : ""
            }
            return root.coversChange
                //: %1 is the wallet's BIP32 master key fingerprint.
                ? qsTr("Fingerprint %1. Each descriptor covers receive and change addresses.").arg(root.sharedFingerprint)
                //: %1 is the wallet's BIP32 master key fingerprint.
                : qsTr("Fingerprint %1.").arg(root.sharedFingerprint)
        }

        Repeater {
            model: root.completeExports

            ListRow {
                required property var modelData
                required property int index
                readonly property bool inactive: index >= root.exports.length
                readonly property string rowId: inactive ? "inactive" + (index - root.exports.length) : modelData.id
                objectName: "descriptorsType_" + rowId
                Layout.fillWidth: true
                title: modelData.label
                description: inactive
                    //: Shown under an address type in the descriptors list for keys that no longer give out new addresses, such as keys from before a password was set. %1 is a shortened descriptor.
                    ? qsTr("Previous key %1").arg(root.shortDescriptor(modelData))
                    : root.shortDescriptor(modelData)
                showDivider: index < root.completeExports.length - 1
                showsDisclosureIndicator: !inactive
                onClicked: if (!inactive) root.descriptorRequested(modelData.id)
                trailingItem: CopyButton {
                    objectName: "descriptorsTypeCopy_" + rowId
                    displayMode: CopyButton.IconOnly
                    Accessible.name: qsTr("Copy %1 descriptor").arg(modelData.label)
                    onCopyRequested: root.clipboard.setText(modelData.descriptor)
                }
            }
        }
    }

    DescriptorCopyAll {
        visible: root.completeExports.length > 0
        Layout.fillWidth: true
        wallet: root.wallet
        clipboard: root.clipboard
    }
}
