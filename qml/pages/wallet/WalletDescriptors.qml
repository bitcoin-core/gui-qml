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
    // Descriptors that importdescriptors can't take without private keys.
    property int omittedCount: 0
    readonly property var completeExports: exports.concat(inactiveExports)
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
        omittedCount = wallet ? wallet.descriptorImportOmittedCount() : 0
    }

    function shortKey(key) {
        return key.length > 17 ? key.substring(0, 8) + "..." + key.substring(key.length - 8) : key
    }

    // "sh(wpkh)" and a key become "sh(wpkh(tpubDDen...WQTz))".
    function shortDescriptor(item) {
        const scripts = item.scriptName.replace(/\)/g, "").split("(")
        return scripts.join("(") + "(" + shortKey(item.xpub) + ")".repeat(scripts.length)
    }

    function copyAll() {
        const json = wallet ? wallet.descriptorImportJson() : ""
        if (json === "") return
        clipboard.setText(json)
        copyAllTimer.restart()
    }

    Component.onCompleted: refresh()
    onWalletChanged: refresh()

    Connections {
        target: root.wallet && root.wallet.descriptorExportsChanged ? root.wallet : null
        function onDescriptorExportsChanged() { root.refresh() }
    }

    Timer {
        id: copyAllTimer
        interval: 1000
    }

    CoreText {
        objectName: "descriptorsListNote"
        Layout.fillWidth: true
        text: qsTr("Anyone with these descriptors can see all of this wallet's addresses and transactions, but cannot spend from it.")
        textStyle: Theme.text.description
        color: Theme.color.neutral7
        horizontalAlignment: Text.AlignLeft
    }

    FormSection {
        objectName: "descriptorsTypeList"
        Layout.fillWidth: true
        title: qsTr("Address types")
        footerText: root.sharedFingerprint !== ""
            //: %1 is the wallet's BIP32 master key fingerprint.
            ? qsTr("Fingerprint %1. Each descriptor covers receive and change addresses.").arg(root.sharedFingerprint)
            : qsTr("Each descriptor covers receive and change addresses.")

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

    NeutralButton {
        objectName: "descriptorsCopyAll"
        buttonSize: NeutralButton.Large
        visible: root.completeExports.length > 0
        enabled: !(root.wallet && root.wallet.watchOnlyWalletExportPending)
        Layout.fillWidth: true
        //: Copies every descriptor as JSON for Bitcoin Core's importdescriptors command.
        text: copyAllTimer.running ? qsTr("Copied") : qsTr("Copy all for Bitcoin Core")
        onClicked: root.copyAll()
    }

    CoreText {
        objectName: "descriptorsCopyAllNote"
        visible: root.completeExports.length > 0
        Layout.fillWidth: true
        //: "importdescriptors" is a Bitcoin Core command and is not translated.
        text: qsTr("Copies every descriptor, with when it was created, as JSON for importdescriptors. Import it into a Bitcoin Core wallet without private keys.")
            + (root.omittedCount === 0 ? ""
               : " " + (root.omittedCount === 1
                   ? qsTr("1 older descriptor needs private keys to find its addresses, so it is left out. To keep it, save a watch-only copy from Back up or export wallet.")
                   //: %1 is a number of descriptors.
                   : qsTr("%1 older descriptors need private keys to find their addresses, so they are left out. To keep them, save a watch-only copy from Back up or export wallet.").arg(root.omittedCount)))
        textStyle: Theme.text.caption
        color: Theme.color.neutral7
        horizontalAlignment: Text.AlignLeft
    }
}
