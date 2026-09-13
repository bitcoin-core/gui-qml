// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtTest 1.2

import "../../qml/controls"
import "../../qml/pages/wallet"

TestCase {
    id: testCase
    name: "SignVerifyMessage"
    when: windowShown
    width: 820
    height: 900

    Item {
        id: host
        anchors.fill: parent
    }

    QtObject {
        id: mockSignVerifyModel

        property string signature: ""
        property string signingError: ""
        property bool signingNeedsUnlock: false

        function clear() {
            mockSignVerifyModel.signature = ""
            mockSignVerifyModel.signingError = ""
            mockSignVerifyModel.signingNeedsUnlock = false
        }

        function isLegacyP2PKHAddress(address) {
            return address === "1ValidLegacyAddress"
        }

        function signMessage(address, message) {
            if (!isLegacyP2PKHAddress(address) || message.length === 0) return false
            mockSignVerifyModel.signature = "test-signature"
            return true
        }

        function signMessageWithPassphrase(address, message, passphrase) {
            return signMessage(address, message) && passphrase.length > 0
        }

        function verifyMessage(address, message, candidateSignature) {
            return isLegacyP2PKHAddress(address)
                && message === "Signed statement"
                && candidateSignature === "test-signature"
        }
    }

    Component {
        id: pageComponent

        SignVerifyMessage {
            width: 820
            height: 900
            wallet: null
            signVerifyModel: mockSignVerifyModel
        }
    }

    function createPage(initialProperties) {
        const page = createTemporaryObject(pageComponent, host, initialProperties || {})
        verify(page !== null)
        return page
    }

    function test_reopeningClearsPreviousSigningState() {
        const firstPage = createPage()
        const firstAddress = findChild(firstPage, "signMessageAddressField")
        const firstMessage = findChild(firstPage, "signMessageMessageField")
        verify(firstAddress !== null)
        verify(firstMessage !== null)

        firstAddress.text = "1ValidLegacyAddress"
        firstMessage.text = "Signed statement"
        firstPage.submitSign()
        tryCompare(mockSignVerifyModel, "signature", "test-signature")
        compare(firstPage.signingComplete, true)

        const reopenedPage = createPage()
        const reopenedAddress = findChild(reopenedPage, "signMessageAddressField")
        const reopenedMessage = findChild(reopenedPage, "signMessageMessageField")
        verify(reopenedAddress !== null)
        verify(reopenedMessage !== null)
        compare(mockSignVerifyModel.signature, "")
        compare(reopenedPage.signingComplete, false)
        compare(reopenedAddress.text, "")
        compare(reopenedAddress.readOnly, false)
        compare(reopenedMessage.text, "")
        compare(reopenedMessage.readOnly, false)
    }

    function test_initialTabSelectsRequestedMode() {
        const page = createPage({
            "initialTab": SignVerifyMessage.VerifyTab
        })

        compare(page.initialTab, SignVerifyMessage.VerifyTab)
        compare(page.selectedMode, SignVerifyMessage.VerifyTab)
        const picker = findChild(page, "signVerifyMessageModePicker")
        verify(picker !== null)
        compare(picker.currentIndex, SignVerifyMessage.VerifyTab)
    }

    function test_usesSettingsPageCardsAndSegmentedModePicker() {
        const page = createPage()
        const picker = findChild(page, "signVerifyMessageModePicker")
        const signTab = findChild(page, "signMessageTab")
        const verifyTab = findChild(page, "verifyMessageTab")
        const signSection = findChild(page, "signMessageFormSection")
        const signCard = findChild(page, "signMessageFormSectionCard")
        const address = findChild(page, "signMessageAddressField")
        const message = findChild(page, "signMessageMessageField")

        verify(picker !== null)
        verify(signTab !== null)
        verify(verifyTab !== null)
        verify(signSection !== null)
        verify(signCard !== null)
        verify(address !== null)
        verify(message !== null)
        compare(page.maximumContentWidth, 706)
        compare(page.selectedMode, 0)
        compare(signCard.radius, 16)
        compare(address.background.color, Theme.color.neutral2)
        compare(address.background.radius, 10)
        compare(message.background.color, Theme.color.neutral2)
        compare(address.font.family, Theme.text.monoDescription.family)
        compare(address.font.pixelSize, Theme.text.monoDescription.pixelSize)
        verify(message.height > address.height)

        verifyTab.clicked()
        compare(page.selectedMode, 1)
        const verifySection = findChild(page, "verifyMessageFormSection")
        const verifyCard = findChild(page, "verifyMessageFormSectionCard")
        verify(verifySection !== null)
        verify(verifyCard !== null)
        compare(verifyCard.radius, 16)
    }

    function test_addressFieldsRejectUnsupportedCharacters() {
        const page = createPage()
        const signAddress = findChild(page, "signMessageAddressField")
        const signAddressModel = findChild(page, "signMessageAddressModel")
        const verifyAddress = findChild(page, "verifyMessageAddressField")
        const verifyAddressModel = findChild(page, "verifyMessageAddressModel")

        verify(signAddress !== null)
        verify(signAddressModel !== null)
        verify(verifyAddress !== null)
        verify(verifyAddressModel !== null)

        signAddress.text = "1Va!lid\tLegacy Address"
        tryCompare(signAddressModel, "address", "1ValidLegacyAddress")
        verify(signAddress.text.indexOf("!") === -1)
        verify(signAddress.text.indexOf("\t") === -1)

        verifyAddress.text = "1Va@lid\tLegacy Address"
        tryCompare(verifyAddressModel, "address", "1ValidLegacyAddress")
        verify(verifyAddress.text.indexOf("@") === -1)
        verify(verifyAddress.text.indexOf("\t") === -1)
        compare(verifyAddress.font.family, Theme.text.monoDescription.family)
        compare(verifyAddress.font.pixelSize, Theme.text.monoDescription.pixelSize)
    }

    function test_preservesSigningAndVerificationSelectorsAndFlow() {
        const page = createPage()
        const signAddress = findChild(page, "signMessageAddressField")
        const signAddressEntry = findChild(page, "signMessageAddressEntry")
        const signMessage = findChild(page, "signMessageMessageField")
        const signMessageEntry = findChild(page, "signMessageMessageEntry")
        const signButton = findChild(page, "signMessageButton")
        const signatureOutput = findChild(page, "signMessageSignatureOutput")
        const signatureText = findChild(page, "signMessageSignatureText")
        const copyAddressButton = findChild(page, "signMessageCopyAddressButton")
        const copyMessageButton = findChild(page, "signMessageCopyMessageButton")
        const copySignatureButton = findChild(page, "signMessageCopySignatureButton")
        const signClearButton = findChild(page, "signMessageClearButton")
        const signClearButtonBackground = findChild(page, "signMessageClearButtonBackground")

        verify(signAddressEntry !== null)
        verify(signMessageEntry !== null)
        verify(signatureOutput !== null)
        verify(copyAddressButton !== null)
        verify(copyMessageButton !== null)
        verify(copySignatureButton !== null)
        verify(signClearButton !== null)
        verify(signClearButtonBackground !== null)
        compare(signClearButton.embedded, true)
        compare(signClearButtonBackground.border.width, 0)
        compare(signClearButtonBackground.color, Theme.color.neutral2)
        compare(signButton.enabled, false)
        compare(signButton.background.color, Theme.color.orange)
        compare(signButton.opacity, 0.4)
        compare(page.signingComplete, false)
        compare(signAddressEntry.readOnly, false)
        compare(signAddressEntry.showCopyButton, false)
        compare(signMessageEntry.readOnly, false)
        compare(signMessageEntry.showCopyButton, false)
        compare(signatureOutput.readOnly, true)
        compare(signatureOutput.wrapMode, TextEdit.WrapAnywhere)
        compare(signatureOutput.fieldBackgroundColor, Theme.color.neutral2)
        compare(signatureOutput.showCopyButton, true)
        compare(signatureOutput.copyButtonEnabled, true)
        compare(copySignatureButton.text, "Copy")
        compare(signMessageEntry.showCopyButton, false)
        signAddress.text = "1ValidLegacyAddress"
        signMessage.text = "Signed statement"
        compare(signAddress.text, "1ValidLegacyAddress")
        compare(signMessage.text, "Signed statement")
        tryCompare(signButton, "enabled", true)
        compare(signButton.background.color, Theme.color.orange)
        compare(signButton.opacity, 1.0)
        page.submitSign()
        tryCompare(mockSignVerifyModel, "signature", "test-signature")
        compare(page.signingComplete, true)
        compare(signAddressEntry.readOnly, true)
        compare(signAddressEntry.showCopyButton, true)
        compare(signMessageEntry.readOnly, true)
        compare(signMessageEntry.showCopyButton, true)
        compare(copyAddressButton.text, "Copy")
        compare(copyMessageButton.text, "Copy")
        compare(page.selectedMode, 0)
        tryCompare(signatureText, "text", "test-signature")

        page.clearSignForm()
        compare(page.signingComplete, false)
        compare(signAddressEntry.readOnly, false)
        compare(signAddressEntry.showCopyButton, false)
        compare(signMessageEntry.readOnly, false)
        compare(signMessageEntry.showCopyButton, false)
        compare(signAddress.text, "")
        compare(signMessage.text, "")

        signAddress.text = "1ValidLegacyAddress"
        signMessage.text = "Signed statement"
        page.submitSign()
        tryCompare(mockSignVerifyModel, "signature", "test-signature")

        const verifyTab = findChild(page, "verifyMessageTab")
        verifyTab.clicked()
        const verifyAddress = findChild(page, "verifyMessageAddressField")
        const verifyMessage = findChild(page, "verifyMessageMessageField")
        const verifySignature = findChild(page, "verifyMessageSignatureField")
        const verifyButton = findChild(page, "verifyMessageButton")
        const verifyClearButton = findChild(page, "verifyMessageClearButton")
        const verifyClearButtonBackground = findChild(page, "verifyMessageClearButtonBackground")
        const resultBanner = findChild(page, "verifyMessageResultBanner")
        const resultIcon = findChild(page, "verifyMessageResultBannerIcon")
        const resultText = findChild(page, "verifyMessageResultText")

        verify(resultBanner !== null)
        verify(resultIcon !== null)
        verify(verifyClearButton !== null)
        verify(verifyClearButtonBackground !== null)
        compare(verifyClearButton.embedded, true)
        compare(verifyClearButtonBackground.border.width, 0)
        compare(verifyClearButtonBackground.color, Theme.color.neutral2)
        compare(verifyButton.enabled, false)
        compare(verifyButton.background.color, Theme.color.orange)
        compare(verifyButton.opacity, 0.4)
        verifyAddress.text = "1ValidLegacyAddress"
        verifyMessage.text = "Signed statement"
        verifySignature.text = "test-signature"
        tryCompare(verifyButton, "enabled", true)
        compare(verifyButton.background.color, Theme.color.orange)
        compare(verifyButton.opacity, 1.0)
        page.submitVerify()
        compare(page.verifyResultSuccess, true)
        compare(resultText.text, "Message verified successfully.")
        compare(resultBanner.radius, 15)
        compare(resultBanner.implicitHeight, 40)
        compare(resultIcon.width, 24)
        compare(resultIcon.height, 24)
        const iconCenter = resultIcon.mapToItem(
            resultBanner,
            resultIcon.width / 2,
            resultIcon.height / 2)
        verify(Math.abs(iconCenter.y - resultBanner.height / 2) < 0.5)
        compare(resultBanner.tintColor, Theme.color.green)
        compare(resultBanner.backgroundColor, Qt.rgba(
            Theme.color.green.r,
            Theme.color.green.g,
            Theme.color.green.b,
            0.25))
        compare(resultBanner.iconColor, Theme.color.green)
        compare(resultBanner.textColor, Theme.color.green)
        tryCompare(resultText, "color", Theme.color.green)

        verifyMessage.text = "Changed statement"
        page.submitVerify()
        compare(resultText.text, "Message verification failed.")
        compare(resultBanner.tintColor, Theme.color.red)
        compare(resultBanner.backgroundColor, Qt.rgba(
            Theme.color.red.r,
            Theme.color.red.g,
            Theme.color.red.b,
            0.25))
        compare(resultBanner.iconColor, Theme.color.red)
        compare(resultBanner.textColor, Theme.color.red)
        tryCompare(resultText, "color", Theme.color.red)
    }
}
