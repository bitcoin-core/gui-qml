// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtTest 1.2
import "../../qml/components"
import "../../qml/controls"
import "../../qml/pages/wallet"

TestCase {
    name: "AddressComponents"
    when: windowShown
    width: 720
    height: 480
    visible: true

    Item {
        id: host
        width: parent.width
        height: parent.height
    }

    function findObject(root, objectName) {
        if (!root) {
            return null
        }
        if (root.objectName === objectName) {
            return root
        }
        const extraRoots = []
        if (root.contentItem) extraRoots.push(root.contentItem)
        if (root.background) extraRoots.push(root.background)
        for (let i = 0; i < extraRoots.length; ++i) {
            const extraMatch = findObject(extraRoots[i], objectName)
            if (extraMatch) {
                return extraMatch
            }
        }
        const objectChildren = root.children || []
        for (let i = 0; i < objectChildren.length; ++i) {
            const childMatch = findObject(objectChildren[i], objectName)
            if (childMatch) {
                return childMatch
            }
        }
        const visualChildren = root.childItems || []
        for (let j = 0; j < visualChildren.length; ++j) {
            const visualMatch = findObject(visualChildren[j], objectName)
            if (visualMatch) {
                return visualMatch
            }
        }
        return null
    }

    Component {
        id: rowComponent
        AddressRow {
            index: 0
            width: 520
            address: "bcrt1qexampleaddress"
            label: ""
            category: "single-use"
            displayAmount: "₿ 0"
            hasAmount: false
            scriptType: "P2WPKH"
            isUsed: false
            canEditLabel: true
        }
    }

    Component {
        id: detailsComponent
        AddressDetails {
            width: 480
            address: "bcrt1qabcdefghijklmnopqrstuvwxyz0123456789abcdefghijklmnopqrstuv"
            label: "Pizza"
            amount: "₿ 1.25"
            hasAmount: true
            category: "single-use"
            scriptType: "P2WPKH"
            used: false
        }
    }

    Component {
        id: addressListComponent

        AddressList {
            width: 720
            height: 480
            wallet: testWalletModel
        }
    }

    Component {
        id: addressLabelComponent

        AddressLabel {
            objectName: "referenceAddressLabel"
            width: 120
            address: "abcdefghijklmnopqrst"
        }
    }

    Component {
        id: addressDisplayFieldComponent

        BitcoinAddressDisplayField {
            objectName: "toggleAddressField"
            width: 320
            text: "abcd efgh ... mnop qrst"
            fullText: "abcd efgh ijkl mnop qrst"
        }
    }

    Component {
        id: multipleRecipientsSummaryComponent

        MultipleRecipientsSummary {
            width: 450
            wallet: null
            transaction: null
            recipients: ListModel {
                ListElement {
                    address: "abcd efgh ... uvwx yz12"
                    label: ""
                    amount: "1000"
                    formattedAddress: "abcd efgh ijkl mnop qrst uvwx yz12"
                    amountUnitLabel: "sats"
                }
                ListElement {
                    address: "2345 6789 ... uvwx yz12"
                    label: "Alice"
                    amount: "2000"
                    formattedAddress: "2345 6789 abcd efgh ijkl mnop qrst uvwx yz12"
                    amountUnitLabel: "sats"
                }
            }
        }
    }

    function test_addressLabel_alternates_color_every_four_characters() {
        const label = createTemporaryObject(addressLabelComponent, host)
        verify(label !== null)

        const primary = label.primaryColor.toString()
        const secondary = label.secondaryColor.toString()
        verify(label.formattedText.indexOf("<font color=\"" + primary + "\">abcd</font>") !== -1)
        verify(label.formattedText.indexOf("<font color=\"" + secondary + "\">efgh</font>") !== -1)
        verify(label.formattedText.indexOf("<font color=\"" + primary + "\">ijkl</font>") !== -1)
        verify(label.formattedText.indexOf("<font color=\"" + secondary + "\">mnop</font>") !== -1)
        verify(label.formattedText.indexOf("<font color=\"" + primary + "\">qrst</font>") !== -1)
    }

    function test_addressDisplayField_toggles_same_monospace_text() {
        const field = createTemporaryObject(addressDisplayFieldComponent, host)
        const referenceLabel = createTemporaryObject(addressLabelComponent, host)
        verify(field !== null)
        verify(referenceLabel !== null)

        const addressText = findObject(field, "toggleAddressFieldText")
        const referenceText = findObject(referenceLabel, "referenceAddressLabelValue")
        verify(addressText !== null)
        verify(referenceText !== null)
        compare(addressText.font.family, referenceText.font.family)
        compare(addressText.font.styleName, referenceText.font.styleName)
        compare(addressText.font.pixelSize, referenceText.font.pixelSize)
        compare(addressText.text, "abcd efgh ... mnop qrst")
        compare(field.expanded, false)

        field.click()
        compare(field.expanded, true)
        compare(addressText.text, "abcd efgh ijkl mnop qrst")

        field.click()
        compare(field.expanded, false)
        compare(addressText.text, "abcd efgh ... mnop qrst")
        compare(findObject(field, "toggleAddressFieldText"), addressText)
    }

    function test_multipleRecipients_toggle_each_address_in_place() {
        const summary = createTemporaryObject(multipleRecipientsSummaryComponent, host)
        const referenceLabel = createTemporaryObject(addressLabelComponent, host)
        verify(summary !== null)
        verify(referenceLabel !== null)

        tryVerify(function() {
            return findObject(summary, "multipleSendReviewRecipient0AddressText") !== null
                && findObject(summary, "multipleSendReviewRecipient1AddressText") !== null
        })
        const firstAddress = findObject(summary, "multipleSendReviewRecipient0AddressText")
        const secondAddress = findObject(summary, "multipleSendReviewRecipient1AddressText")
        const secondLabel = findObject(summary, "multipleSendReviewRecipient1PrimaryText")
        const referenceText = findObject(referenceLabel, "referenceAddressLabelValue")
        verify(secondLabel !== null)
        verify(referenceText !== null)
        compare(firstAddress.font.family, referenceText.font.family)
        compare(secondAddress.font.family, referenceText.font.family)
        compare(firstAddress.text, "abcd efgh ... uvwx yz12")
        compare(secondLabel.text, "Alice")
        compare(secondAddress.text, "2345 6789 ... uvwx yz12")

        mouseClick(firstAddress, firstAddress.width / 2, firstAddress.height / 2)
        compare(firstAddress.text, "abcd efgh ijkl mnop qrst uvwx yz12")
        mouseClick(firstAddress, firstAddress.width / 2, firstAddress.height / 2)
        compare(firstAddress.text, "abcd efgh ... uvwx yz12")

        mouseClick(secondAddress, secondAddress.width / 2, secondAddress.height / 2)
        compare(secondLabel.text, "Alice")
        compare(secondAddress.text, "2345 6789 abcd efgh ijkl mnop qrst uvwx yz12")
        mouseClick(secondAddress, secondAddress.width / 2, secondAddress.height / 2)
        compare(secondAddress.text, "2345 6789 ... uvwx yz12")
    }

    function test_addressLabel_truncatesVisuallyButRetainsFullAddress() {
        const label = createTemporaryObject(addressLabelComponent, host)
        verify(label !== null)

        compare(label.displayAddress, label.address)
        compare(label.isTruncated, false)
        compare(label.embedded, false)
        compare(label.textAlignment, Text.AlignLeft)
        const valueText = findObject(label, "referenceAddressLabelValue")
        const copiedStatusText = findObject(label, "referenceAddressLabelCopiedStatusText")
        verify(valueText !== null)
        verify(copiedStatusText !== null)
        compare(copiedStatusText.font.pixelSize, label.textStyle.font.pixelSize)
        label.address = "bc1qabcdefghijklmnopqrstuvwxyz0123456789abcdefghijklmnopqrstuv"
        tryVerify(function() { return valueText.paintedHeight > label.textStyle.lineHeight })
        compare(valueText.wrapMode, Text.WordWrap)
        label.textAlignment = Text.AlignRight
        compare(valueText.horizontalAlignment, Text.AlignRight)
        label.address = "abcdefghijklmnopqrst"
        label.truncateWhenNeeded = true
        tryCompare(label, "isTruncated", true)
        compare(label.displayAddress, "abcdefgh…mnopqrst")
        compare(label.address, "abcdefghijklmnopqrst")
        verify(label.formattedText.indexOf(">…</font>") !== -1)
        verify(label.formattedText.indexOf(">mnop</font>") !== -1)
        verify(label.formattedText.indexOf(">qrst</font>") !== -1)

        label.width = 1000
        tryCompare(label, "isTruncated", false)
        compare(label.displayAddress, label.address)

        const background = findObject(label, "referenceAddressLabelBackground")
        verify(background !== null)
        label.embedded = true
        compare(background.opacity, 0)
        mouseMove(label, label.width / 2, label.height / 2)
        tryCompare(background, "color", Theme.color.neutral3)
        tryCompare(background, "opacity", 1)
    }

    function test_row_uses_display_amount_and_emits_actions() {
        const row = createTemporaryObject(rowComponent, host)
        verify(row !== null)

        const addressLabel = findObject(row, "addressRowAddressText")
        const noteField = findObject(row, "addressRowNoteField")
        const noteFocusBorder = findObject(row, "addressRowNoteFocusBorder")
        const amountText = findObject(row, "addressRowAmountText")
        const divider = findObject(row, "addressRowDivider")
        verify(addressLabel !== null)
        verify(noteField !== null)
        verify(noteFocusBorder !== null)
        verify(amountText !== null)
        verify(divider !== null)
        compare(noteField.text, "")
        compare(noteField.placeholderText, "Add a note to self")
        compare(noteField.readOnly, false)
        compare(noteField.activeFocusOnPress, true)
        compare(noteField.activeFocusOnTab, true)
        compare(noteFocusBorder.border.color, Theme.color.orange)
        compare(noteField.font.pixelSize, Theme.text.description.font.pixelSize)
        compare(addressLabel.address, "bcrt1qexampleaddress")
        compare(addressLabel.truncated, false)
        compare(addressLabel.truncateWhenNeeded, true)
        compare(addressLabel.isTruncated, false)
        compare(addressLabel.textStyle.font.pixelSize, Theme.text.monoCaption.font.pixelSize)
        verify(addressLabel.y < noteField.y)
        compare(amountText.text, "₿ 0")
        compare(amountText.font.pixelSize, Theme.text.description.font.pixelSize)
        compare(divider.color, Theme.color.neutral3)
        const disclosureIndicator = findObject(row, "addressRowDisclosureIndicator")
        verify(disclosureIndicator !== null)
        compare(disclosureIndicator.size, 14)
        compare(disclosureIndicator.width, 14)
        compare(disclosureIndicator.height, 14)

        mouseMove(row, row.width / 2, row.height / 2)
        tryCompare(row.background, "color", Qt.rgba(0, 0, 0, 0))

        let editRequestCount = 0
        let requestedLabel = ""
        let draftLabel = ""
        let discardedDraft = false
        row.noteDraftEdited.connect(function(address, label) {
            compare(address, "bcrt1qexampleaddress")
            draftLabel = label
        })
        row.noteDraftDiscarded.connect(function(address) {
            compare(address, "bcrt1qexampleaddress")
            discardedDraft = true
        })
        row.editLabelRequested.connect(function(address, label) {
            compare(address, "bcrt1qexampleaddress")
            editRequestCount += 1
            requestedLabel = label
        })
        noteField.forceActiveFocus()
        verify(noteField.activeFocus)
        noteField.text = "Pizza"
        noteField.textEdited()
        compare(draftLabel, "Pizza")
        keyClick(Qt.Key_Return)
        tryCompare(noteField, "activeFocus", false)
        compare(editRequestCount, 1)
        compare(requestedLabel, "Pizza")

        let detailsRequested = false
        let detailsLabel = ""
        row.detailsRequested.connect(function(address, label, amount, hasAmount, category, scriptType, used) {
            detailsRequested = address === "bcrt1qexampleaddress"
                && amount === "₿ 0"
                && hasAmount === false
                && category === "single-use"
                && scriptType === "P2WPKH"
                && used === false
            detailsLabel = label
        })

        const detailsButton = findObject(row, "addressRowDetailsButton")
        verify(detailsButton !== null)
        mouseClick(detailsButton, detailsButton.width / 2, detailsButton.height / 2)
        verify(detailsRequested)
        compare(detailsLabel, "Pizza")

        noteField.forceActiveFocus()
        noteField.text = "Discard this"
        keyClick(Qt.Key_Escape)
        tryCompare(noteField, "activeFocus", false)
        compare(noteField.text, row.label)
        compare(editRequestCount, 1)
        verify(discardedDraft)
        verify(findObject(row, "addressRowMenuButton") === null)

        row.canEditLabel = false
        compare(noteField.readOnly, true)
        compare(noteField.activeFocusOnPress, false)
        compare(noteField.activeFocusOnTab, false)
    }

    function test_addressList_preservesPendingNotesAcrossRefresh() {
        const page = createTemporaryObject(addressListComponent, host)
        verify(page !== null)

        const list = findObject(page, "addressListView")
        verify(list !== null)
        compare(list.clip, true)

        page.updatePendingNote("bcrt1qdraftaddress", "Draft note")
        compare(page.pendingNote("bcrt1qdraftaddress", "Saved note"), "Draft note")
        page.addressModel.refresh()
        compare(page.pendingNote("bcrt1qdraftaddress", "Saved note"), "Draft note")

        page.selectedAddress = "bcrt1qdraftaddress"
        page.selectedLabel = "Saved note"
        page.addressModel.setAddressLabelSucceeds = true
        verify(page.updateAddressLabel("bcrt1qdraftaddress", "Draft note"))
        compare(page.selectedLabel, "Draft note")
        compare(page.pendingNote("bcrt1qdraftaddress", "Saved note"), "Saved note")
        page.addressModel.setAddressLabelSucceeds = false
    }

    function test_details_has_no_status_row_and_emits_actions() {
        const details = createTemporaryObject(detailsComponent, this)
        verify(details !== null)

        const section = findObject(details, "addressDetailsSection")
        const sectionCard = findObject(details, "addressDetailsSectionCard")
        const addressRow = findObject(details, "addressDetailsAddressRow")
        const addressLabel = findObject(details, "addressDetailsAddressLabel")
        const amountRow = findObject(details, "addressDetailsAmountRow")
        const noteRow = findObject(details, "addressDetailsNoteRow")
        const noteField = findObject(details, "addressDetailsNoteField")
        const typeRow = findObject(details, "addressDetailsTypeRow")
        const addressTypeRow = findObject(details, "addressDetailsAddressTypeRow")
        verify(section !== null)
        verify(sectionCard !== null)
        verify(addressRow !== null)
        verify(addressLabel !== null)
        verify(amountRow !== null)
        verify(noteRow !== null)
        verify(noteField !== null)
        verify(typeRow !== null)
        verify(addressTypeRow !== null)
        compare(sectionCard.color, Theme.color.neutral2)
        compare(addressRow.title, "Address")
        compare(addressRow.minimumRowHeight, 76)
        compare(addressRow.dividerColor, Theme.color.neutral3)
        compare(addressLabel.address, "bcrt1qabcdefghijklmnopqrstuvwxyz0123456789abcdefghijklmnopqrstuv")
        compare(addressLabel.embedded, true)
        compare(addressLabel.truncateWhenNeeded, false)
        compare(addressLabel.textAlignment, Text.AlignRight)
        compare(addressLabel.isTruncated, false)
        const addressValueText = findObject(details, "addressDetailsAddressLabelValue")
        verify(addressValueText !== null)
        tryVerify(function() { return addressValueText.paintedHeight > addressLabel.textStyle.lineHeight })
        compare(amountRow.title, "Amount")
        compare(amountRow.value, "₿ 1.25")
        compare(amountRow.dividerColor, Theme.color.neutral3)
        compare(noteRow.text, "Pizza")
        compare(noteRow.placeholderText, "Add a note to self")
        compare(noteField.text, "Pizza")
        compare(noteField.readOnly, false)
        compare(noteField.background.border.color, Theme.color.orange)
        compare(noteRow.dividerColor, Theme.color.neutral3)
        compare(typeRow.value, "Single-use (Receive)")
        compare(typeRow.dividerColor, Theme.color.neutral3)
        compare(addressTypeRow.value, "P2WPKH")
        compare(addressTypeRow.showDivider, false)
        verify(findObject(details, "addressDetailsCopyAddressButton") === null)

        let editedAddress = ""
        let editedLabel = ""
        details.editLabelRequested.connect(function(address, label) {
            editedAddress = address
            editedLabel = label
        })
        noteField.forceActiveFocus()
        verify(noteField.activeFocus)
        noteField.text = "Updated note"
        keyClick(Qt.Key_Return)
        tryCompare(noteField, "activeFocus", false)
        compare(editedAddress, details.address)
        compare(editedLabel, "Updated note")
        details.resetNoteEditor()
        compare(noteField.text, "Pizza")
        details.label = "Current note"
        compare(noteField.text, "Current note")

        let sawAmount = false
        let sawStatus = false
        function scanText(root) {
            if (!root) {
                return
            }
            if (root.text !== undefined) {
                sawAmount = sawAmount || root.text === "₿ 1.25"
                sawStatus = sawStatus || root.text === "Status"
            }
            const objectChildren = root.children || []
            for (let i = 0; i < objectChildren.length; ++i) {
                scanText(objectChildren[i])
            }
            const visualChildren = root.childItems || []
            for (let j = 0; j < visualChildren.length; ++j) {
                scanText(visualChildren[j])
            }
        }
        scanText(details)
        verify(sawAmount)
        verify(!sawStatus)
        const paymentRequestButton = findObject(details, "addressDetailsCreatePaymentRequestButton")
        verify(paymentRequestButton !== null)
        compare(paymentRequestButton.visible, true)

        let closed = false
        details.closeRequested.connect(function() { closed = true })
        const closeButton = findObject(details, "addressDetailsCloseButton")
        if (closeButton !== null) {
            closeButton.clicked()
            verify(closed)
        }
    }

    function test_details_disables_note_for_change_addresses() {
        const details = createTemporaryObject(detailsComponent, this, {
            category: "change"
        })
        verify(details !== null)

        const noteRow = findObject(details, "addressDetailsNoteRow")
        const noteField = findObject(details, "addressDetailsNoteField")
        verify(noteRow !== null)
        verify(noteField !== null)
        compare(details.canEditLabel, false)
        compare(noteRow.enabled, false)
        compare(noteField.enabled, false)
        compare(noteField.readOnly, true)

        noteField.forceActiveFocus(Qt.TabFocusReason)
        verify(!noteField.activeFocus)
    }
}
