// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtTest 1.2
import "../../qml/components"
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
            width: 520
            address: "bcrt1qexampleaddress"
            ellipsesAddress: "bcrt1qexa ... dress"
            label: ""
            category: "single-use"
            currentBalance: "0.00000000"
            displayAmount: "₿ 0.0"
            hasAmount: false
            scriptType: "P2WPKH"
            isUsed: false
            canEditLabel: true
            canCreatePaymentRequest: true
        }
    }

    Component {
        id: detailsComponent
        AddressDetails {
            width: 480
            address: "bcrt1qexampleaddress"
            label: "Pizza"
            amount: "₿ 1.25000000"
            hasAmount: true
            category: "single-use"
            scriptType: "P2WPKH"
            used: false
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

    function test_addressDetails_request_button_edits_existing_request() {
        const details = createTemporaryObject(detailsComponent, host)
        verify(details !== null)
        const button = findChild(details, "addressDetailsRequestButton")
        verify(button !== null)
        compare(button.text, "Create payment request")

        details.hasPaymentRequest = true
        compare(button.text, "Edit payment request")

        // A used address with a saved request keeps the edit action, while
        // a used address without one keeps the button hidden.
        details.used = true
        tryCompare(button, "visible", true)
        details.hasPaymentRequest = false
        tryCompare(button, "visible", false)
    }

    function test_row_menu_action_matches_an_existing_request() {
        const row = createTemporaryObject(rowComponent, this)
        verify(row !== null)

        const action = findChild(row, "addressRowCreatePaymentRequestButton")
        verify(action !== null)
        compare(action.text, "Create payment request")
        verify(row.offersPaymentRequestAction)

        // With a request already saved the row must offer to edit it, the same
        // wording and availability the address details button uses, including
        // once the address has been used.
        row.hasPaymentRequest = true
        compare(action.text, "Edit payment request")
        row.isUsed = true
        verify(row.offersPaymentRequestAction)

        row.hasPaymentRequest = false
        verify(!row.offersPaymentRequestAction)
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

    function test_row_uses_display_amount_and_emits_actions() {
        const row = createTemporaryObject(rowComponent, host)
        verify(row !== null)

        const amountText = findObject(row, "addressRowAmountText")
        verify(amountText !== null)
        compare(amountText.text, "₿ 0.0")

        let editRequested = false
        row.editLabelRequested.connect(function(address, label) {
            editRequested = address === "bcrt1qexampleaddress" && label === ""
        })
        const noteButton = findObject(row, "addressRowNoteButton")
        verify(noteButton !== null)
        mouseClick(noteButton, noteButton.width / 2, noteButton.height / 2)
        verify(editRequested)

        let detailsRequested = false
        row.detailsRequested.connect(function(address, label, amount, hasAmount, category, scriptType, used) {
            detailsRequested = address === "bcrt1qexampleaddress"
                && amount === "₿ 0.0"
                && hasAmount === false
                && category === "single-use"
                && scriptType === "P2WPKH"
                && used === false
        })

        const menuButton = findObject(row, "addressRowMenuButton")
        verify(menuButton !== null)
        mouseClick(menuButton, menuButton.width / 2, menuButton.height / 2)
        tryCompare(row.menu, "opened", true)

        const detailsButton = findObject(row.menu, "addressRowDetailsButton")
        verify(detailsButton !== null)
        mouseClick(detailsButton, detailsButton.width / 2, detailsButton.height / 2)
        verify(detailsRequested)
        tryCompare(row.menu, "opened", false)
    }

    function test_details_has_no_status_row_and_emits_actions() {
        const details = createTemporaryObject(detailsComponent, this)
        verify(details !== null)

        let sawAmount = false
        let sawStatus = false
        function scanText(root) {
            if (!root) {
                return
            }
            if (root.text !== undefined) {
                sawAmount = sawAmount || root.text === "₿ 1.25000000"
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

        let closed = false
        details.closeRequested.connect(function() { closed = true })
        const closeButton = findObject(details, "addressDetailsCloseButton")
        if (closeButton !== null) {
            closeButton.clicked()
            verify(closed)
        }
    }
}
