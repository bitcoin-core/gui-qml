// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtTest 1.2
import "../../qml/components"

TestCase {
    name: "WatchOnlyExportModal"
    when: windowShown
    width: 720
    height: 1200
    visible: true

    readonly property var taproot: ({
        id: "bech32m",
        label: "Bech32m (Taproot)",
        description: "",
        scriptName: "tr",
        xpub: "tpubDCVXgEFJ2YikWyX17RmAtzPYVfzoif4Vs6Bw",
        descriptor: "tr([621f5481/86h/1h/0h]tpubDCVXgEFJ2YikWyX17RmAtzPYVfzoif4Vs6Bw/<0;1>/*)#f3u880ju",
        descriptorParts: {
            opening: "tr(",
            origin: "[621f5481/86h/1h/0h]",
            key: "tpubDCVXgEFJ2YikWyX17RmAtzPYVfzoif4Vs6Bw",
            path: "/<0;1>/*",
            closing: ")",
            checksum: "f3u880ju"
        },
        extendedKey: "tpubDCVXgEFJ2YikWyX17RmAtzPYVfzoif4Vs6Bw",
        fingerprint: "621f5481",
        derivationPath: "m/86h/1h/0h",
        receiveChain: "0",
        changeChain: "1"
    })
    readonly property var segwit: ({
        id: "bech32",
        label: "Bech32 (SegWit)",
        description: "",
        scriptName: "wpkh",
        xpub: "tpubDD7VgqKs35ZQein21QPXtb5o8tUngVN1FK5L",
        descriptor: "wpkh([621f5481/84h/1h/0h]tpubDD7VgqKs35ZQein21QPXtb5o8tUngVN1FK5L/0/*)#hv6zap77\nwpkh(tpubOther/1/*)#aaaaaaaa",
        descriptorParts: {},
        extendedKey: "vpubSegwit",
        fingerprint: "621f5481",
        derivationPath: "m/84h/1h/0h",
        receiveChain: "0",
        changeChain: "1"
    })

    Component {
        id: modalComponent
        WatchOnlyExportModal {}
    }

    function makeWallet(exports, preferred, saveError) {
        return {
            name: "watch/wallet",
            saved: [],
            watchOnlyExports: () => exports,
            defaultReceiveAddressType: () => preferred,
            saveWatchOnlyExport: function(path, contents) {
                this.saved.push({path: path, contents: contents})
                return saveError || ""
            }
        }
    }

    function openModal(exports, preferred, properties) {
        const modal = createTemporaryObject(modalComponent, this, Object.assign({
            wallet: makeWallet(exports, preferred),
            clipboard: { setText: () => {} }
        }, properties || {}))
        verify(modal !== null)
        verify(modal.openForWallet())
        tryCompare(modal, "opened", true)
        return modal
    }

    function findItem(item, objectName) {
        if (!item) return null
        if (item.objectName === objectName) return item
        for (let i = 0; i < item.children.length; ++i) {
            const match = findItem(item.children[i], objectName)
            if (match) return match
        }
        return null
    }

    function optionIds(modal) {
        return findChild(modal, "watchOnlyExportAddressTypePicker").model.map(option => option.id)
    }

    function test_all_types_lists_each_descriptor() {
        let copied = ""
        const modal = openModal([taproot, segwit], "bech32", {clipboard: { setText: value => copied = value }})
        compare(modal.addressType, "")
        compare(modal.payload, taproot.descriptor + "\n" + segwit.descriptor)
        compare(findChild(modal, "watchOnlyExportQRImage").code, modal.payload)
        verify(findChild(modal, "watchOnlyExportTypeList").visible)
        compare(findItem(modal.contentItem, "watchOnlyExportType_bech32m").description, "tr(tpubDCVX...if4Vs6Bw)")
        compare(findItem(modal.contentItem, "watchOnlyExportType_bech32").title, segwit.label)
        compare(modal.shortDescriptor({scriptName: "sh(wpkh)", xpub: segwit.xpub}), "sh(wpkh(tpubDD7V...gVN1FK5L))")
        verify(!findChild(modal, "watchOnlyExportDescriptor").visible)

        findItem(modal.contentItem, "watchOnlyExportTypeCopy_bech32").copyRequested()
        compare(copied, segwit.descriptor)

        findItem(modal.contentItem, "watchOnlyExportType_bech32m").clicked()
        compare(modal.addressType, "bech32m")
        verify(!findChild(modal, "watchOnlyExportTypeList").visible)
    }

    function test_descriptor_is_laid_out_by_part() {
        const modal = openModal([taproot, segwit], "bech32")
        findChild(modal, "watchOnlyExportAddressTypePicker").activated("bech32m")
        compare(modal.payload, taproot.descriptor)
        verify(findChild(modal, "watchOnlyExportDescriptor").visible)
        compare(findChild(modal, "watchOnlyExportDescriptorOpening").text, "tr(")
        compare(findChild(modal, "watchOnlyExportDescriptorOrigin").text, "[621f5481/86h/1h/0h]")
        verify(findChild(modal, "watchOnlyExportDescriptorKey").text.indexOf(">tpub<") >= 0)
        compare(findChild(modal, "watchOnlyExportDescriptorPath").text, "/<0;1>/*")
        verify(findChild(modal, "watchOnlyExportDescriptorClosing").text.indexOf("#f3u880ju") >= 0)
        compare(findChild(modal, "watchOnlyExportAddressTypeValue").text, taproot.label)
        compare(findChild(modal, "watchOnlyExportFingerprint").value, "621f5481")
        compare(findChild(modal, "watchOnlyExportDerivationPath").value, "m/86h/1h/0h")
        compare(findChild(modal, "watchOnlyExportChains").value, "Receive 0, change 1")
        compare(findChild(modal, "watchOnlyExportCopyButton").text, "Copy descriptor")
        compare(optionIds(modal), ["", "bech32m", "bech32"])
    }

    function test_unjoined_descriptor_falls_back_to_text() {
        const modal = openModal([taproot, segwit], "bech32")
        findChild(modal, "watchOnlyExportAddressTypePicker").activated("bech32")
        verify(findChild(modal, "watchOnlyExportDescriptor").visible)
        verify(!findChild(modal, "watchOnlyExportDescriptorOpening").visible)
        verify(findChild(modal, "watchOnlyExportDescriptorPlain").visible)
        compare(findChild(modal, "watchOnlyExportDescriptorPlain").text, segwit.descriptor)
        compare(findChild(modal, "watchOnlyExportNote").text, "Only includes funds on Bech32 (SegWit) addresses.")
    }

    function test_clicking_descriptor_copies_it() {
        let copied = ""
        const modal = openModal([taproot, segwit], "bech32", {clipboard: { setText: value => copied = value }})
        findChild(modal, "watchOnlyExportAddressTypePicker").activated("bech32m")
        const descriptor = findChild(modal, "watchOnlyExportDescriptor")
        waitForRendering(descriptor)
        mouseClick(descriptor)
        compare(copied, taproot.descriptor)
        verify(descriptor.showCopiedStatus)
        tryVerify(() => !descriptor.showCopiedStatus)
    }

    function test_extended_key_uses_default_receive_type() {
        const modal = openModal([taproot, segwit], "bech32")
        findChild(modal, "watchOnlyExportFormatPicker").selected(1, "Extended key")
        compare(modal.addressType, "bech32")
        compare(modal.payload, "vpubSegwit")
        compare(findChild(modal, "watchOnlyExportKeyText").address, "vpubSegwit")
        verify(!findChild(modal, "watchOnlyExportChains").visible)
        compare(findChild(modal, "watchOnlyExportCopyButton").text, "Copy extended key")
        compare(optionIds(modal), ["bech32m", "bech32"])

        findChild(modal, "watchOnlyExportFormatPicker").selected(0, "Descriptor")
        compare(modal.payload, segwit.descriptor)
    }

    function test_taproot_extended_key_recommends_descriptor() {
        const modal = openModal([taproot, segwit], "bech32m")
        findChild(modal, "watchOnlyExportFormatPicker").selected(1, "Extended key")
        compare(modal.payload, taproot.extendedKey)
        verify(findChild(modal, "watchOnlyExportNote").text.indexOf("descriptor format") >= 0)
    }

    function test_single_export_has_no_type_choice() {
        const modal = openModal([taproot], "bech32")
        compare(modal.addressType, "bech32m")
        verify(!findChild(modal, "watchOnlyExportTypeList").visible)
        verify(!findChild(modal, "watchOnlyExportAddressType").showsDisclosureIndicator)
    }

    function test_copy_button_copies_payload() {
        let copied = ""
        const modal = openModal([taproot, segwit], "bech32", {clipboard: { setText: value => copied = value }})
        const copyButton = findChild(modal, "watchOnlyExportCopyButton")
        compare(copyButton.text, "Copy all descriptors")
        waitForRendering(copyButton)
        mouseClick(copyButton)
        compare(copied, modal.payload)
        compare(copyButton.text, "Copied")
        tryCompare(copyButton, "text", "Copy all descriptors")
    }

    function test_save_writes_payload() {
        const modal = openModal([taproot, segwit], "bech32")
        const dialog = findChild(modal, "watchOnlyExportSaveDialog")
        findChild(modal, "watchOnlyExportSaveMenuButton").triggered()
        compare(dialog.openCalls, 1)
        compare(dialog.currentFile.toString(), "file:///tmp/watch_wallet-watch-only.txt")

        dialog.selectAndAccept("file:///tmp/export.txt")
        compare(modal.wallet.saved.length, 1)
        compare(modal.wallet.saved[0].path, "file:///tmp/export.txt")
        compare(modal.wallet.saved[0].contents, modal.payload)
        verify(!findChild(modal, "watchOnlyExportError").visible)
    }

    function test_save_failure_is_shown() {
        const modal = openModal([taproot], "bech32", {wallet: makeWallet([taproot], "bech32", "Disk full")})
        findChild(modal, "watchOnlyExportSaveDialog").selectAndAccept("file:///tmp/export.txt")
        compare(findChild(modal, "watchOnlyExportError").text, "Disk full")
        verify(findChild(modal, "watchOnlyExportError").visible)
    }

    function test_refuses_to_open_without_exports() {
        const modal = createTemporaryObject(modalComponent, this, {wallet: makeWallet([], "")})
        verify(!modal.openForWallet())
        verify(!modal.opened)
    }

    function test_closes_when_wallet_changes() {
        const modal = openModal([taproot], "bech32m")
        modal.wallet = makeWallet([segwit], "bech32")
        tryCompare(modal, "opened", false)
        tryVerify(() => modal.exports.length === 0)
    }
}
