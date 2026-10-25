// Copyright (c) 2023-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0
import "../controls"

AlertPopup {
    id: externalConfirmPopup
    objectName: "externalLinkPopup"
    property string link: ""

    function openLink(url) {
        link = url
        open()
    }

    title: {
        if (openFailed) {
            //: Title of the dialog shown when an external link could not be opened.
            return qsTr("Couldn't open link")
        }
        //: Title of the dialog asking to confirm opening an external link.
        return qsTr("Open link?")
    }
    message: {
        if (openFailed) {
            //: Shown when an external link could not be handed to any application, prompting the user to copy it instead.
            return qsTr("This link could not be opened. Copy it and open it manually.")
        }
        //: Confirmation question shown before opening a website in the user's browser.
        return qsTr("Do you want to open the following website in your browser?")
    }

    // Set once an open attempt has failed. Switches the popup to its error and
    // copy-URL fallback layout instead of closing.
    property bool openFailed: false
    // Set while the copy confirmation is showing. The copy button is disabled
    // for that window so a second click cannot go unacknowledged.
    property bool linkCopied: false

    // How long the copy button stays in its "Copied" state before returning to
    // "Copy link". Keep the feedback consistent with other copy confirmations.
    property int copiedFeedbackMs: 2000

    // Without this the popup never enters the active focus chain, so Tab keeps
    // cycling the page behind it instead of the dialog's own buttons.
    focus: true
    implicitWidth: Math.max(420, horizontalActionsWidth + 40)
    actionList: openFailed ? [closeLinkAction, copyLinkAction] : [cancelLinkAction, confirmLinkAction]

    // Always start from the confirmation layout when (re)opened so a previous
    // failure does not leak into the next link.
    onAboutToShow: {
        externalConfirmPopup.openFailed = false
        externalConfirmPopup.linkCopied = false
        copiedResetTimer.stop()
    }

    // Try to open link in the user's browser. On success the popup closes; on
    // failure it stays open and offers the copy-URL fallback.
    function attemptOpen() {
        if (UrlOpener.openUrl(externalConfirmPopup.link)) {
            externalConfirmPopup.close()
        } else {
            externalConfirmPopup.openFailed = true
        }
    }

    function copyLink() {
        Clipboard.setText(externalConfirmPopup.link)
        externalConfirmPopup.linkCopied = true
        copiedResetTimer.restart()
    }

    function focusCopyAction() {
        if (opened && openFailed && !linkCopied) focusAction(copyLinkAction)
    }

    // Entering the error state replaces the button the user just activated, so
    // move focus onto its replacement: otherwise a keyboard user is left with
    // no focused control and no signal that anything changed.
    onOpenFailedChanged: if (openFailed) Qt.callLater(focusCopyAction)

    Timer {
        id: copiedResetTimer
        interval: externalConfirmPopup.copiedFeedbackMs
        onTriggered: {
            externalConfirmPopup.linkCopied = false
            // The button was disabled while confirming, which drops focus, so
            // hand it back rather than stranding the keyboard user.
            externalConfirmPopup.focusCopyAction()
        }
    }

    // Selectable so the destination can be copied and opened by hand
    // whether or not the open works, not only from the failure fallback.
    content: SelectableText {
        objectName: "externalLinkUrl"
        Layout.fillWidth: true
        text: externalConfirmPopup.link
        // A URL carries an unbreakable run (a txid is 64 characters), which
        // WordWrap cannot split, so it would spill past the dialog edge.
        wrapMode: TextEdit.Wrap
        horizontalAlignment: TextEdit.AlignHCenter
        color: Theme.color.neutral8
        textStyle: Theme.text.description
        Accessible.role: Accessible.StaticText
        Accessible.name: text
    }

    // Confirmation buttons, shown before an open attempt.
    property AlertAction cancelLinkAction: AlertAction {
        //: Button that dismisses the dialog without opening the external link.
        text: qsTr("Cancel")
        role: AlertAction.Cancel
        buttonObjectName: "externalLinkCancel"
    }
    property AlertAction confirmLinkAction: AlertAction {
        //: Button that confirms opening the external link in the browser.
        text: qsTr("OK")
        buttonObjectName: "externalLinkConfirm"
        closesPopup: false
        onTriggered: externalConfirmPopup.attemptOpen()
    }

    // Copy-URL fallback buttons, shown after an open attempt failed.
    property AlertAction closeLinkAction: AlertAction {
        //: Button that dismisses the external-link error dialog.
        text: qsTr("Close")
        role: AlertAction.Neutral
        buttonObjectName: "externalLinkClose"
    }
    property AlertAction copyLinkAction: AlertAction {
        text: {
            if (externalConfirmPopup.linkCopied) {
                //: Button label confirming an external link was copied to the clipboard.
                return qsTr("Copied")
            }
            //: Button that copies an external link to the clipboard after it could not be opened.
            return qsTr("Copy link")
        }
        // Disabled while confirming so the label always describes what
        // the button will do, and a repeat copy is not silently a no-op.
        enabled: !externalConfirmPopup.linkCopied
        buttonObjectName: "externalLinkCopy"
        closesPopup: false
        onTriggered: externalConfirmPopup.copyLink()
    }
}
