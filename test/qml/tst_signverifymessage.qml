// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Window 2.15
import QtTest 1.2
import org.bitcoincore.qt 1.0

import "../../qml/controls"
import "../../qml/pages/wallet"

TestCase {
    function init() {
        testSetup.failOnWarnings()
        previousClipboard = Clipboard.currentText
        mockSignVerifyModel.resetForTest()
    }

    id: testCase
    name: "SignVerifyMessage"
    when: windowShown
    width: 820
    height: 900
    property var pages: []
    property var controlledWindows: []
    property string previousClipboard: ""
    property var walletModelsToClear: []

    function cleanup() {
        for (let i = 0; i < pages.length; ++i) {
            const receipt = testSetup.watchDestruction(pages[i])
            pages[i].destroy()
            tryCompare(receipt, "pending", 0)
            testSetup.releaseDestructionWatch(receipt)
        }
        pages = []
        for (let i = 0; i < controlledWindows.length; ++i) {
            const receipt = testSetup.watchDestruction(controlledWindows[i])
            controlledWindows[i].close()
            controlledWindows[i].destroy()
            tryCompare(receipt, "pending", 0)
            testSetup.releaseDestructionWatch(receipt)
        }
        controlledWindows = []
        for (let i = 0; i < walletModelsToClear.length; ++i) walletModelsToClear[i].clear()
        walletModelsToClear = []
        mockSignVerifyModel.resetForTest()
        Clipboard.currentText = previousClipboard
    }

    Item {
        id: host
        anchors.fill: parent
    }

    SignVerifyMessageModel { id: mockSignVerifyModel }

    Component {
        id: pageComponent

        SignVerifyMessage {
            width: 820
            height: 900
            wallet: null
            signVerifyModel: mockSignVerifyModel
        }
    }

    Component {
        id: walletBoundPageComponent
        SignVerifyMessage { width: 820; height: 900 }
    }

    function createPage(initialProperties, component) {
        const page = createTemporaryObject(component || pageComponent, host, initialProperties || {})
        verify(page !== null)
        pages.push(page)
        return page
    }

    Component {
        id: controlledWindowComponent
        Window { width: 820; height: 900; visible: false }
    }

    function createControlledPage(initialProperties, component) {
        const properties = {visible: false}
        if (initialProperties) {
            for (const key in initialProperties) properties[key] = initialProperties[key]
        }
        // Keep the original nonvisual fixture unchanged. New physical-input
        // cases get a complete native viewport without resizing QtTest's one.
        const page = createPage(properties, component)
        const window = createTemporaryObject(controlledWindowComponent, testCase)
        verify(window !== null)
        controlledWindows.push(window)
        page.parent = window.contentItem
        page.visible = true
        window.show()
        verify(waitForRendering(page))
        compare(page.visible, true)
        return page
    }

    function clickVisible(page, objectName) {
        const button = findChild(page, objectName)
        verify(button !== null)
        verify(button.visible && button.enabled)
        const window = button.Window.window
        verify(window !== null)
        window.update()
        verify(waitForRendering(button))
        // Newly visible fields join nested layouts over several polish turns.
        // Scroll the real control and require its center inside every clip.
        // A section switch resizes the scroll content; settle layouts before scrolling.
        for (let ancestor = button.parent; ancestor; ancestor = ancestor.parent) waitForItemPolished(ancestor)
        tryVerify(function() {
            for (let parent = button.parent; parent; parent = parent.parent) {
                const flickable = parent as Flickable
                // Skip flickables that do not scroll vertically, such as a tab bar's list.
                if (!flickable || flickable.contentHeight <= flickable.height) continue
                const point = button.mapToItem(flickable.contentItem, button.width / 2, button.height / 2)
                flickable.contentY = Math.max(0, Math.min(flickable.contentHeight - flickable.height, point.y - flickable.height / 2))
                break
            }
            window.update()
            if (button.width <= 0 || button.height <= 0) return false
            for (let parent = button.parent; parent; parent = parent.parent) {
                if (!parent.clip) continue
                const center = button.mapToItem(parent, button.width / 2, button.height / 2)
                if (center.x < 0 || center.x >= parent.width || center.y < 0 || center.y >= parent.height) return false
            }
            const center = button.mapToItem(window.contentItem, button.width / 2, button.height / 2)
            return center.x >= 0 && center.x < window.contentItem.width && center.y >= 0 && center.y < window.contentItem.height
        })
        mouseClick(button, button.width / 2, button.height / 2)
    }

    function fillSigning(page) {
        findChild(page, "signMessageAddressField").text = "1ValidLegacyAddress"
        findChild(page, "signMessageMessageField").text = "Signed statement"
        tryCompare(findChild(page, "signMessageButton"), "enabled", true)
        compare(mockSignVerifyModel.signingAddress, "1ValidLegacyAddress")
        compare(mockSignVerifyModel.signingMessage, "Signed statement")
    }

    function fillVerification(page, signature, expectEnabled) {
        clickVisible(page, "verifyMessageTab")
        compare(page.selectedMode, SignVerifyMessage.VerifyTab)
        findChild(page, "verifyMessageAddressField").text = "1ValidLegacyAddress"
        findChild(page, "verifyMessageMessageField").text = "Signed statement"
        findChild(page, "verifyMessageSignatureField").text = signature
        tryCompare(findChild(page, "verifyMessageButton"), "enabled", expectEnabled === undefined ? true : expectEnabled)
    }

    function openUnlockPrompt(page) {
        fillSigning(page)
        clickVisible(page, "signMessageButton")
        compare(mockSignVerifyModel.signCalls, 1)
        verify(mockSignVerifyModel.completeSigningForTest("unlock", "Wallet password required."))
        const popup = findChild(page, "signMessagePassphrasePopup")
        tryCompare(popup, "opened", true)
        compare(popup.contextValid, true)
        compare(popup.context, mockSignVerifyModel.signingPrompt)
        compare(mockSignVerifyModel.lastSigningAddress, "1ValidLegacyAddress")
        compare(mockSignVerifyModel.lastSigningMessage, "Signed statement")
        compare(findChild(page, "signMessageErrorText").visible, false)
        compare(findChild(popup, "signMessagePassphraseErrorText").text, "Wallet password required.")
        return popup
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
        compare(address.background.color, Theme.color.surface2)
        compare(address.background.radius, 10)
        compare(message.background.color, Theme.color.surface2)
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
        compare(signClearButton.buttonSize, NeutralButton.Large)
        compare(signClearButton.backgroundColor, Theme.color.surface2)
        compare(signButton.enabled, false)
        compare(signButton.background.color, Theme.color.accent)
        compare(signButton.opacity, 0.4)
        compare(page.signingComplete, false)
        compare(signAddressEntry.readOnly, false)
        compare(signAddressEntry.showCopyButton, false)
        compare(signMessageEntry.readOnly, false)
        compare(signMessageEntry.showCopyButton, false)
        compare(signatureOutput.readOnly, true)
        compare(signatureOutput.wrapMode, TextEdit.WrapAnywhere)
        compare(signatureOutput.fieldBackgroundColor, Theme.color.surface2)
        compare(signatureOutput.showCopyButton, true)
        compare(signatureOutput.copyButtonEnabled, true)
        compare(copySignatureButton.text, "Copy")
        compare(signMessageEntry.showCopyButton, false)
        signAddress.text = "1ValidLegacyAddress"
        signMessage.text = "Signed statement"
        compare(signAddress.text, "1ValidLegacyAddress")
        compare(signMessage.text, "Signed statement")
        tryCompare(signButton, "enabled", true)
        compare(signButton.background.color, Theme.color.accent)
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
        compare(verifyClearButton.buttonSize, NeutralButton.Large)
        compare(verifyClearButton.backgroundColor, Theme.color.surface2)
        compare(verifyButton.enabled, false)
        compare(verifyButton.background.color, Theme.color.accent)
        compare(verifyButton.opacity, 0.4)
        verifyAddress.text = "1ValidLegacyAddress"
        verifyMessage.text = "Signed statement"
        verifySignature.text = "test-signature"
        tryCompare(verifyButton, "enabled", true)
        compare(verifyButton.background.color, Theme.color.accent)
        compare(verifyButton.opacity, 1.0)
        page.submitVerify()
        tryCompare(page, "verifyResultSuccess", true)
        tryCompare(resultText, "text", "Message verified successfully.")
        compare(resultBanner.radius, Theme.radius.card)
        compare(resultBanner.implicitHeight, 40)
        compare(resultIcon.width, 24)
        compare(resultIcon.height, 24)
        const iconCenter = resultIcon.mapToItem(
            resultBanner,
            resultIcon.width / 2,
            resultIcon.height / 2)
        verify(Math.abs(iconCenter.y - resultBanner.height / 2) < 0.5)
        compare(resultBanner.tintColor, Theme.color.success)
        compare(resultBanner.backgroundColor, Theme.color.successTint)
        compare(resultBanner.iconColor, Theme.color.success)
        compare(resultBanner.textColor, Theme.color.successText)
        tryCompare(resultText, "color", Theme.color.successText)

        verifyMessage.text = "Changed statement"
        page.submitVerify()
        tryCompare(resultText, "text", "Message verification failed.")
        compare(resultBanner.tintColor, Theme.color.danger)
        compare(resultBanner.backgroundColor, Theme.color.dangerTint)
        compare(resultBanner.iconColor, Theme.color.danger)
        compare(resultBanner.textColor, Theme.color.dangerText)
        tryCompare(resultText, "color", Theme.color.dangerText)
    }

    function test_default_model_binding_follows_explicit_wallet_data() {
        return [{tag: "wallet-replaced", initiallyBound: true},
                {tag: "wallet-attached", initiallyBound: false}]
    }
    function test_default_model_binding_follows_explicit_wallet(data) {
        const firstModel = testWalletModel.signVerifyMessageModel
        const secondModel = testSecondWalletModel.signVerifyMessageModel
        walletModelsToClear = [firstModel, secondModel]
        firstModel.clear()
        secondModel.clear()
        const page = createControlledPage(data.initiallyBound ? {wallet: testWalletModel} : {}, walletBoundPageComponent)
        compare(page.signVerifyModel, data.initiallyBound ? firstModel : null)
        if (data.initiallyBound) {
            const firstTicket = firstModel.signingId
            findChild(page, "signMessageAddressField").text = "1ValidLegacyAddress"
            findChild(page, "signMessageMessageField").text = "First wallet statement"
            tryCompare(findChild(page, "signMessageButton"), "enabled", true)
            clickVisible(page, "signMessageButton")
            tryCompare(firstModel, "signature", "test-signature")
            compare(firstModel.signingId, firstTicket + 1)
            compare(page.signingComplete, true)
            page.wallet = testSecondWalletModel
            compare(page.signVerifyModel, secondModel)
            compare(page.signingComplete, false)
        } else {
            compare(page.verifyResultText, "")
            compare(findChild(page, "signMessageButton").enabled, false)
            page.wallet = testSecondWalletModel
            compare(page.signVerifyModel, secondModel)
        }
        const originalTicket = firstModel.signingId
        const secondTicket = secondModel.signingId
        findChild(page, "signMessageAddressField").text = "1ValidLegacyAddress"
        findChild(page, "signMessageMessageField").text = "Second wallet statement"
        tryCompare(findChild(page, "signMessageButton"), "enabled", true)
        clickVisible(page, "signMessageButton")
        tryCompare(secondModel, "signature", "test-signature")
        compare(secondModel.signingId, secondTicket + 1)
        compare(firstModel.signingId, originalTicket)
        compare(secondModel.signingAddress, "1ValidLegacyAddress")
        compare(secondModel.signingMessage, "Second wallet statement")
        compare(page.signingComplete, true)
    }

    function test_missing_model_rejects_intents_and_clears_local_fields() {
        const page = createControlledPage({signVerifyModel: null})
        findChild(page, "signMessageAddressField").text = "1ValidLegacyAddress"
        findChild(page, "signMessageMessageField").text = "Local signing draft"
        compare(findChild(page, "signMessageButton").enabled, false)
        page.submitSign()
        page.submitSign("unused credential")
        compare(mockSignVerifyModel.signCalls, 0)
        compare(mockSignVerifyModel.passphraseCalls, 0)
        clickVisible(page, "signMessageClearButton")
        compare(findChild(page, "signMessageAddressField").text, "")
        compare(findChild(page, "signMessageMessageField").text, "")
        compare(page.signingComplete, false)
        compare(findChild(page, "signMessageErrorText").text, "")
        fillVerification(page, "test-signature", false)
        compare(findChild(page, "verifyMessageButton").enabled, false)
        page.submitVerify()
        compare(mockSignVerifyModel.verifyCalls, 0)
        clickVisible(page, "verifyMessageClearButton")
        compare(findChild(page, "verifyMessageAddressField").text, "")
        compare(findChild(page, "verifyMessageMessageField").text, "")
        compare(findChild(page, "verifyMessageSignatureField").text, "")
        compare(page.verifyResultText, "")
        compare(page.verifyResultSuccess, false)
        compare(findChild(page, "verifyMessageResultBanner").visible, false)
    }

    function test_held_signing_rejects_duplicate_intents() {
        mockSignVerifyModel.manualCompletion = true
        const page = createControlledPage()
        fillSigning(page)
        const ticket = mockSignVerifyModel.signingId
        clickVisible(page, "signMessageButton")
        compare(mockSignVerifyModel.signCalls, 1)
        compare(mockSignVerifyModel.signingId, ticket + 1)
        compare(mockSignVerifyModel.signingBusy, true)
        compare(page.signingComplete, false)
        compare(findChild(page, "signMessageButton").enabled, false)
        page.submitSign()
        page.submitSign("duplicate credential")
        compare(mockSignVerifyModel.signCalls, 1)
        compare(mockSignVerifyModel.passphraseCalls, 0)
        compare(mockSignVerifyModel.signingId, ticket + 1)
        verify(mockSignVerifyModel.completeSigningForTest("success", ""))
        compare(page.signingComplete, true)
        compare(findChild(page, "signMessageSignatureText").text, "test-signature")
        compare(findChild(page, "signMessagePassphrasePopup").visible, false)
    }

    function test_explicit_passphrase_intent_uses_passphrase_api() {
        mockSignVerifyModel.manualCompletion = true
        const page = createControlledPage()
        fillSigning(page)
        page.submitSign("explicit credential")
        compare(mockSignVerifyModel.signCalls, 0)
        compare(mockSignVerifyModel.passphraseCalls, 1)
        compare(mockSignVerifyModel.lastSigningAddress, "1ValidLegacyAddress")
        compare(mockSignVerifyModel.lastSigningMessage, "Signed statement")
        compare(mockSignVerifyModel.lastPassphrase, "explicit credential")
        verify(mockSignVerifyModel.completeSigningForTest("success", ""))
        compare(page.signingComplete, true)
        compare(mockSignVerifyModel.signature, "test-signature")
    }

    function test_completed_signing_copy_actions_data() {
        return [
            {tag: "address", button: "signMessageCopyAddressButton", expected: "1ValidLegacyAddress"},
            {tag: "message", button: "signMessageCopyMessageButton", expected: "Signed statement"},
            {tag: "signature", button: "signMessageCopySignatureButton", expected: "test-signature"}
        ]
    }
    function test_completed_signing_copy_actions(data) {
        mockSignVerifyModel.manualCompletion = true
        const page = createControlledPage()
        fillSigning(page)
        clickVisible(page, "signMessageButton")
        verify(mockSignVerifyModel.completeSigningForTest("success", ""))
        compare(page.signingComplete, true)
        Clipboard.currentText = "Previous clipboard value"
        const revision = Clipboard.revision
        clickVisible(page, data.button)
        compare(Clipboard.currentText, data.expected)
        compare(Clipboard.revision, revision + 1)
        compare(mockSignVerifyModel.signCalls, 1)
        compare(mockSignVerifyModel.passphraseCalls, 0)
        compare(mockSignVerifyModel.verifyCalls, 0)
    }

    function test_signing_clear_button_releases_completed_fields() {
        mockSignVerifyModel.manualCompletion = true
        const page = createControlledPage()
        fillSigning(page)
        clickVisible(page, "signMessageButton")
        verify(mockSignVerifyModel.completeSigningForTest("success", ""))
        compare(findChild(page, "signMessageAddressEntry").readOnly, true)
        clickVisible(page, "signMessageClearButton")
        compare(page.signingComplete, false)
        compare(mockSignVerifyModel.signature, "")
        compare(mockSignVerifyModel.signingAddress, "")
        compare(mockSignVerifyModel.signingMessage, "")
        compare(findChild(page, "signMessageAddressField").text, "")
        compare(findChild(page, "signMessageMessageField").text, "")
        compare(findChild(page, "signMessageAddressEntry").readOnly, false)
        compare(findChild(page, "signMessageMessageEntry").readOnly, false)
        compare(mockSignVerifyModel.signCalls, 1)
        compare(mockSignVerifyModel.passphraseCalls, 0)
    }

    function test_verification_clear_button_retires_result_data() {
        return [{tag: "success", signature: "test-signature", success: true},
                {tag: "failure", signature: "wrong-signature", success: false}]
    }
    function test_verification_clear_button_retires_result(data) {
        mockSignVerifyModel.manualCompletion = true
        const page = createControlledPage()
        fillVerification(page, data.signature)
        clickVisible(page, "verifyMessageButton")
        compare(mockSignVerifyModel.verifyCalls, 1)
        verify(mockSignVerifyModel.completeVerificationForTest())
        compare(page.verifyResultSuccess, data.success)
        compare(page.verifyResultText, data.success ? "Message verified successfully." : "Message verification failed.")
        compare(findChild(page, "verifyMessageResultBanner").visible, true)
        clickVisible(page, "verifyMessageClearButton")
        compare(findChild(page, "verifyMessageAddressField").text, "")
        compare(findChild(page, "verifyMessageMessageField").text, "")
        compare(findChild(page, "verifyMessageSignatureField").text, "")
        compare(mockSignVerifyModel.verificationAddress, "")
        compare(mockSignVerifyModel.verificationMessage, "")
        compare(mockSignVerifyModel.verificationSignature, "")
        compare(page.verifyResultText, "")
        compare(page.verifyResultSuccess, false)
        compare(findChild(page, "verifyMessageResultBanner").visible, false)
        compare(findChild(page, "verifyMessageButton").enabled, false)
        compare(mockSignVerifyModel.verifyCalls, 1)
    }

    function test_signing_error_is_visible_without_unlock_prompt() {
        mockSignVerifyModel.manualCompletion = true
        const page = createControlledPage()
        fillSigning(page)
        clickVisible(page, "signMessageButton")
        verify(mockSignVerifyModel.completeSigningForTest("error", "Signing was refused by the wallet."))
        compare(mockSignVerifyModel.signCalls, 1)
        compare(page.signingComplete, false)
        compare(findChild(page, "signMessageErrorText").visible, true)
        compare(findChild(page, "signMessageErrorText").text, "Signing was refused by the wallet.")
        compare(findChild(page, "signMessagePassphrasePopup").visible, false)
        compare(findChild(page, "signMessageMessageField").text, "Signed statement")
        compare(findChild(page, "signMessageAddressEntry").readOnly, false)
        compare(findChild(page, "signMessageButton").enabled, true)
    }

    function test_passphrase_retry_owns_exact_signing_context_data() {
        return [{tag: "success", outcome: "success", error: ""},
                {tag: "password-rejected", outcome: "unlock", error: "Incorrect wallet password."}]
    }
    function test_passphrase_retry_owns_exact_signing_context(data) {
        mockSignVerifyModel.manualCompletion = true
        const page = createControlledPage()
        const popup = openUnlockPrompt(page)
        const field = findChild(popup, "signMessagePassphraseField")
        field.text = "retry credential"
        clickVisible(popup.contentItem, "signMessagePassphraseConfirmButton")
        compare(mockSignVerifyModel.signCalls, 1)
        compare(mockSignVerifyModel.passphraseCalls, 1)
        compare(mockSignVerifyModel.lastSigningAddress, "1ValidLegacyAddress")
        compare(mockSignVerifyModel.lastSigningMessage, "Signed statement")
        compare(mockSignVerifyModel.lastPassphrase, "retry credential")
        compare(field.text, "")
        compare(popup.busy, true)
        compare(findChild(popup.contentItem, "signMessagePassphraseConfirmButton").enabled, false)
        compare(findChild(popup.contentItem, "signMessagePassphraseCancelButton").enabled, false)
        popup.submitted("late duplicate")
        compare(mockSignVerifyModel.passphraseCalls, 1)
        verify(mockSignVerifyModel.completeSigningForTest(data.outcome, data.error))
        compare(popup.busy, false)
        if (data.outcome === "success") {
            tryCompare(popup, "visible", false)
            compare(page.signingComplete, true)
            compare(mockSignVerifyModel.signature, "test-signature")
            compare(popup.context, null)
        } else {
            tryCompare(popup, "opened", true)
            compare(page.signingComplete, false)
            compare(popup.contextValid, true)
            compare(findChild(popup, "signMessagePassphraseErrorText").text, data.error)
            field.text = "clear on cancel"
            clickVisible(popup.contentItem, "signMessagePassphraseCancelButton")
            tryCompare(popup, "visible", false)
            compare(popup.context, null)
        }
        compare(field.text, "")
        compare(mockSignVerifyModel.passphraseCalls, 1)
    }

    function test_passphrase_context_invalidation_rejects_stale_intent_data() {
        return [{tag: "address-edited", change: "address"},
                {tag: "message-edited", change: "message"},
                {tag: "model-replaced", change: "model"},
                {tag: "view-hidden", change: "hidden"}]
    }
    function test_passphrase_context_invalidation_rejects_stale_intent(data) {
        mockSignVerifyModel.manualCompletion = true
        const page = createControlledPage()
        const popup = openUnlockPrompt(page)
        const field = findChild(popup, "signMessagePassphraseField")
        field.text = "must be wiped"
        if (data.change === "address") findChild(page, "signMessageAddressField").text = "1ChangedLegacyAddress"
        else if (data.change === "message") findChild(page, "signMessageMessageField").text = "Changed statement"
        else if (data.change === "model") page.signVerifyModel = null
        else page.visible = false
        tryCompare(popup, "visible", false)
        compare(popup.context, null)
        compare(popup.contextValid, false)
        compare(field.text, "")
        popup.submitted("stale queued credential")
        compare(mockSignVerifyModel.signCalls, 1)
        compare(mockSignVerifyModel.passphraseCalls, 0)
        compare(mockSignVerifyModel.signingBusy, false)
        compare(mockSignVerifyModel.signature, "")
        if (data.change === "hidden") {
            // A later public outcome cannot reopen a prompt over another page.
            mockSignVerifyModel.signingStateChanged()
            compare(popup.visible, false)
            page.visible = true
            compare(popup.visible, false)
        }
    }

    function test_native_message_entry_and_copy_preserve_exact_text_data() {
        return [{tag: "nbsp", value: "Audit\u00a0statement"},
                {tag: "space-control", value: "Audit statement"},
                {tag: "unicode-tab-newline", value: "\u00e9\tFirst\nSecond"},
                {tag: "literal-markup", value: "<b>literal</b> & text"}]
    }
    function test_native_message_entry_and_copy_preserve_exact_text(data) {
        mockSignVerifyModel.manualCompletion = true
        const page = createControlledPage()
        const address = findChild(page, "signMessageAddressField")
        const message = findChild(page, "signMessageMessageField")
        verify(testSetup.replaceText(address, "1ValidLegacyAddress"))
        verify(testSetup.replaceText(message, data.value))
        tryCompare(mockSignVerifyModel.message, "text", data.value)
        clickVisible(page, "signMessageButton")
        compare(mockSignVerifyModel.signCalls, 1)
        compare(mockSignVerifyModel.lastSigningMessage, data.value)
        verify(mockSignVerifyModel.completeSigningForTest("success", ""))
        tryCompare(page, "signingComplete", true)
        Clipboard.currentText = "prior clipboard"
        clickVisible(page, "signMessageCopyMessageButton")
        compare(Clipboard.currentText, data.value)
        compare(mockSignVerifyModel.lastSigningMessage, data.value)
    }

    function test_exact_native_text_budget_blocks_an_effect_until_corrected_data() {
        return [{tag: "signing-message", verifyMode: false},
                {tag: "verification-signature", verifyMode: true}]
    }
    function test_exact_native_text_budget_blocks_an_effect_until_corrected(data) {
        mockSignVerifyModel.manualCompletion = true
        const page = createControlledPage()
        if (data.verifyMode) fillVerification(page, "test-signature")
        else fillSigning(page)
        const field = findChild(page, data.verifyMode ? "verifyMessageSignatureField" : "signMessageMessageField")
        const entry = findChild(page, data.verifyMode ? "verifyMessageSignatureEntry" : "signMessageMessageEntry")
        verify(testSetup.replaceText(field, "x".repeat(32769)))
        tryCompare(entry, "exactTextValid", false)
        compare(findChild(page, data.verifyMode ? "verifyMessageButton" : "signMessageButton").enabled, false)
        if (data.verifyMode) page.submitVerify()
        else page.submitSign()
        compare(mockSignVerifyModel.signCalls, 0)
        compare(mockSignVerifyModel.verifyCalls, 0)
        verify(testSetup.replaceText(field, data.verifyMode ? "test-signature" : "Corrected message"))
        tryCompare(entry, "exactTextValid", true)
        clickVisible(page, data.verifyMode ? "verifyMessageButton" : "signMessageButton")
        compare(mockSignVerifyModel.signCalls, data.verifyMode ? 0 : 1)
        compare(mockSignVerifyModel.verifyCalls, data.verifyMode ? 1 : 0)
        if (data.verifyMode) {
            verify(mockSignVerifyModel.completeVerificationForTest())
            compare(page.verifyResultSuccess, true)
        } else {
            compare(mockSignVerifyModel.lastSigningMessage, "Corrected message")
            verify(mockSignVerifyModel.completeSigningForTest("success", ""))
            compare(page.signingComplete, true)
        }
    }

    function test_held_verification_discards_edited_input_outcome() {
        mockSignVerifyModel.manualCompletion = true
        const page = createControlledPage()
        fillVerification(page, "test-signature")
        const ticket = mockSignVerifyModel.verificationId
        clickVisible(page, "verifyMessageButton")
        compare(mockSignVerifyModel.verifyCalls, 1)
        compare(mockSignVerifyModel.verificationId, ticket + 1)
        compare(mockSignVerifyModel.verificationBusy, true)
        compare(findChild(page, "verifyMessageButton").enabled, false)
        page.submitVerify()
        compare(mockSignVerifyModel.verifyCalls, 1)
        findChild(page, "verifyMessageSignatureField").text = "changed-signature"
        verify(mockSignVerifyModel.completeVerificationForTest())
        compare(mockSignVerifyModel.verificationBusy, false)
        compare(page.verifyResultText, "")
        compare(findChild(page, "verifyMessageResultBanner").visible, false)
        clickVisible(page, "verifyMessageButton")
        compare(mockSignVerifyModel.verifyCalls, 2)
        compare(mockSignVerifyModel.verificationId, ticket + 2)
        verify(mockSignVerifyModel.completeVerificationForTest())
        compare(page.verifyResultSuccess, false)
        compare(page.verifyResultText, "Message verification failed.")
    }
}
