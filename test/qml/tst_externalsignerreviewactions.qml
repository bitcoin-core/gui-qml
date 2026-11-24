// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtTest 1.2
import org.bitcoincore.qt 1.0
import "../../qml/components"

TestCase {
    function init() {
        testSetup.failOnWarnings()
    }

    name: "ExternalSignerReviewActions"
    when: windowShown
    width: 500
    height: 300

    Component {
        id: walletComponent

        QtObject {
            property int approveCalls: 0
            property int signerApprovalState: WalletModel.ApprovalInitial
            property string signerApprovalError: ""
            property bool signerApprovalSignerNotFound: false
            property string currentTransactionReviewMessage: ""

            signal externalSignerApprovalSucceeded()
            signal externalSignerApprovalPartiallySucceeded()
            signal externalSignerApprovalFailed(string message, bool signerNotFound)
            onExternalSignerApprovalSucceeded: signerApprovalState = WalletModel.ApprovalSigned
            onExternalSignerApprovalPartiallySucceeded: signerApprovalState = WalletModel.ApprovalPartiallySigned
            onExternalSignerApprovalFailed: (message, signerNotFound) => {
                signerApprovalSignerNotFound = signerNotFound
                signerApprovalError = message
                signerApprovalState = WalletModel.ApprovalFailed
            }

            function approveExternalSignerTransaction() {
                if (signerApprovalState === WalletModel.ApprovalWaiting) return
                signerApprovalSignerNotFound = false
                signerApprovalState = WalletModel.ApprovalWaiting
                approveCalls += 1
            }
        }
    }

    Component {
        id: reviewComponent

        ExternalSignerReviewActions {
            required property QtObject wallet
            approvalState: wallet ? wallet.signerApprovalState : 0
            errorMessage: wallet ? wallet.signerApprovalError : ""
            signerNotFound: wallet && wallet.signerApprovalSignerNotFound
            reviewMessage: wallet ? wallet.currentTransactionReviewMessage : ""
            onApprovalRequested: wallet.approveExternalSignerTransaction()
            width: 420
            buttonObjectName: "externalSignerApproveButton"
            statusObjectName: "externalSignerStatusText"
        }
    }

    function findObjectByName(root, objectName) {
        if (!root) {
            return null
        }
        if (root.objectName === objectName) {
            return root
        }

        if (root.contentItem) {
            const contentResult = findObjectByName(root.contentItem, objectName)
            if (contentResult) {
                return contentResult
            }
        }

        const children = root.children || []
        for (let i = 0; i < children.length; ++i) {
            const childResult = findObjectByName(children[i], objectName)
            if (childResult) {
                return childResult
            }
        }

        return null
    }

    function waitForApproveCalls(wallet, count) {
        tryCompare(wallet, "approveCalls", count, 1000)
    }

    function test_waiting_state_persists_until_mock_completes() {
        const wallet = createTemporaryObject(walletComponent, this)
        verify(wallet !== null)

        const review = createTemporaryObject(reviewComponent, this, {wallet: wallet})
        verify(review !== null)

        const button = findObjectByName(review, "externalSignerApproveButton")
        verify(button !== null)

        compare(review.reviewState, "initial")
        compare(review.statusText, "Approve on external signer to broadcast this transaction.")
        compare(review.buttonText, "Approve on external signer")
        verify(button.enabled)

        review.beginApproval()

        compare(review.reviewState, "waiting")
        compare(review.statusText, "Waiting for approval on external signer.")
        compare(review.buttonText, "Waiting for approval…")
        verify(!button.enabled)

        waitForApproveCalls(wallet, 1)
        compare(review.reviewState, "waiting")

        wallet.externalSignerApprovalSucceeded()

        compare(review.reviewState, "signed")
        compare(review.statusText, "Signed on external signer. Ready to send.")
        compare(review.buttonText, "Send")
        verify(button.enabled)
    }

    function test_uncertain_outcome_blocks_blind_retry() {
        const wallet = createTemporaryObject(walletComponent, this)
        const review = createTemporaryObject(reviewComponent, this, {wallet: wallet, canSend: true})
        verify(review !== null)
        wallet.signerApprovalError = "Reconcile the PSBT before retrying."
        wallet.signerApprovalState = WalletModel.ApprovalUncertain
        compare(review.reviewState, "uncertain")
        compare(review.statusText, wallet.signerApprovalError)
        const button = findObjectByName(review, "externalSignerApproveButton")
        verify(button !== null)
        verify(!button.enabled)
        compare(wallet.approveCalls, 0)
    }

    function test_unverified_result_blocks_approval_and_remains_visible() {
        const wallet = createTemporaryObject(walletComponent, this)
        wallet.signerApprovalState = WalletModel.ApprovalUnverified
        wallet.signerApprovalError = "Export unverified result to reconcile it."
        const review = createTemporaryObject(reviewComponent, this, {wallet: wallet, canSend: false})
        compare(review.reviewState, "unverified")
        compare(review.statusText, wallet.signerApprovalError)
        const button = findObjectByName(review, "externalSignerApproveButton")
        verify(!button.enabled)
        mouseClick(button)
        compare(wallet.approveCalls, 0)
    }

    function test_partial_success_shows_more_signatures_message() {
        const wallet = createTemporaryObject(walletComponent, this)
        verify(wallet !== null)
        wallet.currentTransactionReviewMessage = "Signed on external signer. More signatures are required."

        const review = createTemporaryObject(reviewComponent, this, {wallet: wallet, canSend: false})
        verify(review !== null)

        const button = findObjectByName(review, "externalSignerApproveButton")
        verify(button !== null)

        wallet.externalSignerApprovalPartiallySucceeded()

        compare(review.reviewState, "partiallySigned")
        compare(review.statusText, "Signed on external signer. More signatures are required.")
        compare(review.buttonText, "More signatures required")
        verify(!button.enabled)
    }

    function test_error_state_retries_approval() {
        const wallet = createTemporaryObject(walletComponent, this)
        verify(wallet !== null)

        const review = createTemporaryObject(reviewComponent, this, {wallet: wallet})
        verify(review !== null)

        const button = findObjectByName(review, "externalSignerApproveButton")
        verify(button !== null)

        review.beginApproval()
        waitForApproveCalls(wallet, 1)
        wallet.externalSignerApprovalFailed("External signer not found.", true)

        compare(review.reviewState, "error")
        compare(review.statusText, "External signer not found.")
        compare(review.buttonText, "Retry after connecting signer")
        verify(button.enabled)

        review.beginApproval()
        waitForApproveCalls(wallet, 2)
        compare(review.reviewState, "waiting")

        wallet.externalSignerApprovalSucceeded()
        compare(review.reviewState, "signed")
        compare(review.statusText, "Signed on external signer. Ready to send.")
    }

    function test_hidden_view_retains_accepted_approval() {
        const wallet = createTemporaryObject(walletComponent, this)
        verify(wallet !== null)

        const review = createTemporaryObject(reviewComponent, this, {wallet: wallet})
        verify(review !== null)

        review.beginApproval()
        review.visible = false
        wait(50)

        compare(wallet.approveCalls, 1)
        compare(review.reviewState, "waiting")
        compare(review.errorMessage, "")
        compare(review.statusText, "Waiting for approval on external signer.")
    }

    function test_destroy_retains_accepted_approval() {
        const wallet = createTemporaryObject(walletComponent, this)
        verify(wallet !== null)

        const review = reviewComponent.createObject(this, {wallet: wallet})
        verify(review !== null)

        review.beginApproval()
        review.destroy()
        // Destroying the observer cannot withdraw an already accepted effect.
        testSetup.processDeferredDeletes()
        verify(!Qt.isQtObject(review))
        wait(50)

        compare(wallet.approveCalls, 1)
        compare(wallet.signerApprovalState, WalletModel.ApprovalWaiting)
    }

    function test_rapid_clicks_only_start_one_approval() {
        const wallet = createTemporaryObject(walletComponent, this)
        verify(wallet !== null)

        const review = createTemporaryObject(reviewComponent, this, {wallet: wallet})
        verify(review !== null)

        const button = findObjectByName(review, "externalSignerApproveButton")
        verify(button !== null)

        review.beginApproval()
        review.beginApproval()

        waitForApproveCalls(wallet, 1)
        wait(50)
        compare(wallet.approveCalls, 1)
        compare(review.reviewState, "waiting")
        verify(!button.enabled)
    }
}
