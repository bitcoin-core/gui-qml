// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtTest 1.2
import "../../qml/pages/wallet"

TestCase {
    name: "WalletDescriptors"
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
        descriptor: "wpkh([621f5481/84h/1h/0h]tpubDD7VgqKs35ZQein21QPXtb5o8tUngVN1FK5L/<0;1>/*)#hv6zap77",
        descriptorParts: {
            opening: "wpkh(",
            origin: "[621f5481/84h/1h/0h]",
            key: "tpubDD7VgqKs35ZQein21QPXtb5o8tUngVN1FK5L",
            path: "/<0;1>/*",
            closing: ")",
            checksum: "hv6zap77"
        },
        extendedKey: "vpubSegwit",
        fingerprint: "621f5481",
        derivationPath: "m/84h/1h/0h",
        receiveChain: "0",
        changeChain: "1"
    })
    readonly property var legacy: ({
        id: "legacy",
        label: "Base58 (Legacy)",
        description: "",
        scriptName: "pkh",
        xpub: "tpubDDLegacyReceive0000000000000000000000",
        descriptor: "pkh([621f5481/44h/1h/0h]tpubDDLegacyReceive0000000000000000000000/0/*)#aaaaaaaa\npkh([621f5481/45h/1h/0h]tpubDDLegacyChange00000000000000000000000/1/*)#bbbbbbbb",
        descriptorParts: {},
        extendedKey: "",
        fingerprint: "621f5481",
        derivationPath: "m/44h/1h/0h",
        receiveChain: "0",
        changeChain: "1"
    })
    readonly property var previousSegwit: ({
        id: "bech32",
        label: "Bech32 (SegWit)",
        scriptName: "wpkh",
        xpub: "tpubDCPreviousSegwit00000000000000000000",
        descriptor: "wpkh([0badf00d/84h/1h/0h]tpubDCPreviousSegwit00000000000000000000/<0;1>/*)#cccccccc",
        fingerprint: "0badf00d"
    })

    Component {
        id: listComponent
        WalletDescriptors {}
    }

    Component {
        id: detailComponent
        WalletDescriptorDetail {}
    }

    Component {
        id: spyComponent
        SignalSpy {}
    }

    function makeWallet(exports, inactive) {
        return {
            name: "watch/wallet",
            descriptorExports: () => exports,
            inactiveDescriptorExports: () => inactive || [],
            descriptorImportJson: () => JSON.stringify(exports.concat(inactive || []).map(item => ({desc: item.descriptor}))),
        }
    }

    function createList(exports, inactive, clipboard) {
        const page = createTemporaryObject(listComponent, this, {
            wallet: makeWallet(exports, inactive),
            clipboard: clipboard || { setText: () => {} },
            width: 720,
            height: 1100,
        })
        verify(page !== null)
        return page
    }

    function createDetail(exports, addressType, clipboard) {
        const page = createTemporaryObject(detailComponent, this, {
            wallet: makeWallet(exports),
            addressType: addressType,
            clipboard: clipboard || { setText: () => {} },
            width: 720,
            height: 1100,
        })
        verify(page !== null)
        return page
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

    function test_list_shows_each_descriptor() {
        let copied = ""
        const page = createList([taproot, segwit], [], { setText: value => copied = value })
        const requested = createTemporaryObject(spyComponent, this, {target: page, signalName: "descriptorRequested"})
        compare(page.title, "Descriptors")
        compare(findItem(page, "descriptorsType_bech32m").description, "tr(tpubDCVX...if4Vs6Bw)")
        compare(findItem(page, "descriptorsType_bech32").title, segwit.label)
        verify(findItem(page, "descriptorsType_bech32").showsDisclosureIndicator)
        compare(page.shortDescriptor({scriptName: "sh(wpkh)", xpub: segwit.xpub}), "sh(wpkh(tpubDD7V...gVN1FK5L))")
        compare(findChild(page, "descriptorsTypeList").footerText,
                "Fingerprint 621f5481. Each descriptor covers receive and change addresses.")

        findItem(page, "descriptorsTypeCopy_bech32").copyRequested()
        compare(copied, segwit.descriptor)

        findItem(page, "descriptorsType_bech32m").clicked()
        compare(requested.count, 1)
        compare(requested.signalArguments[0][0], "bech32m")
    }

    function test_list_includes_previous_keys() {
        let copied = ""
        const page = createList([taproot, segwit], [previousSegwit], { setText: value => copied = value })
        const requested = createTemporaryObject(spyComponent, this, {target: page, signalName: "descriptorRequested"})
        compare(findChild(page, "descriptorsTypeList").footerText,
                "Each descriptor covers receive and change addresses.")

        const row = findItem(page, "descriptorsType_inactive0")
        verify(row !== null)
        compare(row.title, previousSegwit.label)
        compare(row.description, "Previous key wpkh(tpubDCPr...00000000)")
        verify(!row.showsDisclosureIndicator)
        verify(findItem(page, "descriptorsType_bech32").showDivider)
        verify(!row.showDivider)
        row.clicked()
        compare(requested.count, 0)
        findItem(page, "descriptorsTypeCopy_inactive0").copyRequested()
        compare(copied, previousSegwit.descriptor)
    }

    function test_copy_all_copies_the_import_json() {
        let copied = ""
        const page = createList([taproot, segwit], [previousSegwit], { setText: value => copied = value })
        const button = findChild(page, "descriptorsCopyAll")
        verify(button.visible)
        compare(button.text, "Copy all for Bitcoin Core")
        button.clicked()
        compare(JSON.parse(copied).map(item => item.desc), [taproot.descriptor, segwit.descriptor, previousSegwit.descriptor])
        compare(button.text, "Copied")
        tryCompare(button, "text", "Copy all for Bitcoin Core")
    }

    function test_copy_all_is_hidden_without_descriptors() {
        const page = createList([])
        verify(!findChild(page, "descriptorsCopyAll").visible)
        verify(!findChild(page, "descriptorsCopyAllNote").visible)
    }

    function test_list_follows_the_wallet() {
        const page = createList([taproot, segwit])
        verify(findItem(page, "descriptorsType_bech32m") !== null)
        page.wallet = makeWallet([legacy])
        tryVerify(() => findItem(page, "descriptorsType_bech32m") === null)
        verify(findItem(page, "descriptorsType_legacy") !== null)
    }

    function test_descriptor_is_laid_out_by_part() {
        const page = createDetail([taproot, segwit], "bech32m")
        compare(page.title, taproot.label)
        compare(page.payload, taproot.descriptor)
        compare(findChild(page, "descriptorsQRImage").code, taproot.descriptor)
        verify(findChild(page, "descriptorsDetail").visible)
        compare(findChild(page, "descriptorsDetailOpening").text, "tr(")
        compare(findChild(page, "descriptorsDetailOrigin").text, "[621f5481/86h/1h/0h]")
        verify(findChild(page, "descriptorsDetailKey").text.indexOf(">tpub<") >= 0)
        compare(findChild(page, "descriptorsDetailPath").text, "/<0;1>/*")
        verify(findChild(page, "descriptorsDetailClosing").text.indexOf("#f3u880ju") >= 0)
        compare(findChild(page, "descriptorsFingerprint").value, "621f5481")
        compare(findChild(page, "descriptorsDerivationPath").value, "m/86h/1h/0h")
        compare(findChild(page, "descriptorsChains").value, "Receive 0, change 1")
        compare(findChild(page, "descriptorsCopyButton").text, "Copy descriptor")
    }

    function test_unjoined_descriptor_falls_back_to_text() {
        const page = createDetail([taproot, legacy], "legacy")
        verify(findChild(page, "descriptorsDetail").visible)
        verify(!findChild(page, "descriptorsDetailOpening").visible)
        verify(findChild(page, "descriptorsDetailPlain").visible)
        compare(findChild(page, "descriptorsDetailPlain").text, legacy.descriptor)
        compare(findChild(page, "descriptorsNote").text,
                "Only includes funds on Base58 (Legacy) addresses. For a copy of the whole wallet, use Back up or export wallet.")
    }

    function test_clicking_descriptor_copies_it() {
        let copied = ""
        const page = createDetail([taproot, segwit], "bech32m", { setText: value => copied = value })
        const descriptor = findChild(page, "descriptorsDetail")
        waitForRendering(descriptor)
        mouseClick(descriptor)
        compare(copied, taproot.descriptor)
        verify(descriptor.showCopiedStatus)
        tryVerify(() => !descriptor.showCopiedStatus)
    }

    function test_extended_key_format() {
        const page = createDetail([taproot, segwit], "bech32")
        findChild(page, "descriptorsFormatPicker").selected(1, "Extended key")
        verify(page.extendedKeyFormat)
        compare(page.payload, "vpubSegwit")
        compare(findChild(page, "descriptorsQRImage").code, "vpubSegwit")
        compare(findChild(page, "descriptorsKeyText").address, "vpubSegwit")
        verify(!findChild(page, "descriptorsDetail").visible)
        verify(!findChild(page, "descriptorsChains").visible)
        compare(findChild(page, "descriptorsCopyButton").text, "Copy extended key")

        findChild(page, "descriptorsFormatPicker").selected(0, "Descriptor")
        compare(page.payload, segwit.descriptor)
    }

    function test_extended_key_is_disabled_without_a_covering_key() {
        const page = createDetail([taproot, legacy], "legacy")
        verify(!page.extendedKeyAvailable)
        verify(!findItem(page, "descriptorsFormatPickerOption_1").enabled)
        verify(findItem(page, "descriptorsFormatPickerOption_0").enabled)
        findChild(page, "descriptorsFormatPicker").selected(1, "Extended key")
        verify(!page.extendedKeyFormat)
        compare(page.payload, legacy.descriptor)
    }

    function test_taproot_extended_key_recommends_descriptor() {
        const page = createDetail([taproot, segwit], "bech32m")
        findChild(page, "descriptorsFormatPicker").selected(1, "Extended key")
        compare(page.payload, taproot.extendedKey)
        verify(findChild(page, "descriptorsNote").text.indexOf("descriptor format") >= 0)
    }

    function test_copy_button_copies_payload() {
        let copied = ""
        const page = createDetail([taproot], "bech32m", { setText: value => copied = value })
        const copyButton = findChild(page, "descriptorsCopyButton")
        compare(copyButton.text, "Copy descriptor")
        waitForRendering(copyButton)
        mouseClick(copyButton)
        compare(copied, taproot.descriptor)
        compare(copyButton.text, "Copied")
        tryCompare(copyButton, "text", "Copy descriptor")
    }

    function test_detail_follows_a_replaced_descriptor() {
        const page = createDetail([segwit], "bech32")
        findChild(page, "descriptorsFormatPicker").selected(1, "Extended key")
        const replaced = Object.assign({}, segwit, {descriptor: "wpkh(new)#aaaaaaaa", descriptorParts: {}, extendedKey: ""})
        page.wallet = makeWallet([replaced])
        compare(page.payload, replaced.descriptor)
        verify(!page.extendedKeyFormat)

        page.wallet = makeWallet([taproot])
        compare(page.payload, "")
        verify(!findChild(page, "descriptorsCopyButton").enabled)
    }
}
