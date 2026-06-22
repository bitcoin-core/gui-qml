// Copyright (c) 2023-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0

Popup {
    id: externalConfirmPopup
    objectName: "externalLinkPopup"
    property string link: ""

    // Set once an open attempt has failed. Switches the popup to its error and
    // copy-URL fallback layout instead of closing.
    property bool openFailed: false
    // Set while the copy confirmation is showing. The copy button is disabled
    // for that window so a second click cannot go unacknowledged.
    property bool linkCopied: false

    // How long the copy button stays in its "Copied" state before returning to
    // "Copy link". Kept close to the app's other copy confirmation
    // (ToastPopup.visibleDurationMs) so copy feedback feels the same.
    property int copiedFeedbackMs: 2000

    modal: true
    // Without this the popup never enters the active focus chain, so Tab keeps
    // cycling the page behind it instead of the dialog's own buttons.
    focus: true
    padding: 0

    anchors.centerIn: Overlay.overlay
    width: Math.min(420, (Overlay.overlay ? Overlay.overlay.width : 460) - 40)

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

    // Entering the error state replaces the button the user just activated, so
    // move focus onto its replacement: otherwise a keyboard user is left with
    // no focused control and no signal that anything changed.
    onOpenFailedChanged: {
        if (externalConfirmPopup.openFailed) copyButton.forceActiveFocus()
    }

    Timer {
        id: copiedResetTimer
        interval: externalConfirmPopup.copiedFeedbackMs
        onTriggered: {
            externalConfirmPopup.linkCopied = false
            // The button was disabled while confirming, which drops focus, so
            // hand it back rather than stranding the keyboard user.
            if (externalConfirmPopup.opened && externalConfirmPopup.openFailed) {
                copyButton.forceActiveFocus()
            }
        }
    }

    background: Rectangle {
        color: Theme.color.background
        radius: 10
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        CoreText {
            Layout.fillWidth: true
            Layout.preferredHeight: 55
            text: {
                if (externalConfirmPopup.openFailed) {
                    //: Title of the dialog shown when an external link could not be opened.
                    return qsTr("Couldn't open link")
                }
                //: Title of the dialog asking to confirm opening an external link.
                return qsTr("External Link")
            }
            bold: true
            font.pixelSize: 24
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }

        Separator {
            Layout.fillWidth: true
        }

        Header {
            Layout.fillWidth: true
            Layout.margins: 20
            Layout.topMargin: 20
            header: {
                if (externalConfirmPopup.openFailed) {
                    //: Shown when an external link could not be handed to any application, prompting the user to copy it instead.
                    return qsTr("This link could not be opened. Copy it and open it manually.")
                }
                //: Confirmation question shown before opening a website in the user's browser.
                return qsTr("Do you want to open the following website in your browser?")
            }
            headerBold: false
            headerSize: 16
            description: "\"" + externalConfirmPopup.link + "\""
            descriptionMargin: 8
            descriptionTextFormat: Text.PlainText
            // A URL carries an unbreakable run (a txid is 64 characters), which
            // WordWrap cannot split, so it would spill past the dialog edge.
            descriptionWrapMode: Text.Wrap
        }

        // Confirmation buttons, shown before an open attempt.
        GridLayout {
            Layout.fillWidth: true
            Layout.margins: 20
            Layout.topMargin: 0
            visible: !externalConfirmPopup.openFailed
            columns: AppMode.isDesktop ? 2 : 1
            columnSpacing: 15
            rowSpacing: 10

            OutlineButton {
                objectName: "externalLinkCancel"
                //: Button that dismisses the dialog without opening the external link.
                text: qsTr("Cancel")
                Accessible.role: Accessible.Button
                Accessible.name: text
                Layout.fillWidth: true
                Layout.minimumWidth: 120
                onClicked: externalConfirmPopup.close()
            }

            ContinueButton {
                objectName: "externalLinkConfirm"
                //: Button that confirms opening the external link in the browser.
                text: qsTr("Ok")
                Accessible.role: Accessible.Button
                Accessible.name: text
                Layout.fillWidth: true
                Layout.minimumWidth: 120
                onClicked: externalConfirmPopup.attemptOpen()
            }
        }

        // Copy-URL fallback buttons, shown after an open attempt failed.
        GridLayout {
            Layout.fillWidth: true
            Layout.margins: 20
            Layout.topMargin: 0
            visible: externalConfirmPopup.openFailed
            columns: AppMode.isDesktop ? 2 : 1
            columnSpacing: 15
            rowSpacing: 10

            OutlineButton {
                objectName: "externalLinkClose"
                //: Button that dismisses the external-link error dialog.
                text: qsTr("Close")
                Accessible.role: Accessible.Button
                Accessible.name: text
                Layout.fillWidth: true
                Layout.minimumWidth: 120
                onClicked: externalConfirmPopup.close()
            }

            ContinueButton {
                id: copyButton
                objectName: "externalLinkCopy"
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
                Accessible.role: Accessible.Button
                Accessible.name: text
                Layout.fillWidth: true
                Layout.minimumWidth: 120
                onClicked: externalConfirmPopup.copyLink()
            }
        }
    }
}
