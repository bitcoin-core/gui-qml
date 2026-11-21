// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../../controls"
import "../../components"

// First step of an external signer wallet when HWI is not set up or does not
// respond. It saves the signer path like External signer settings do, so
// people don't have to leave wallet creation to configure it.
OnboardingView {
    id: root
    objectName: "externalSignerSetupPage"
    showNavigationBar: false
    usesSharedNavigation: true

    property bool modalView: false
    // Closing the download warning hides it until the step opens again,
    // like the RPC console's warning.
    property bool downloadWarningShown: true
    readonly property var pathSection: loadedChildView ? loadedChildView.pathSection : null
    readonly property bool deviceReady: root.pathSection !== null && root.pathSection.deviceReady

    signal cancel()
    signal ready()

    title: ""
    //: Heading of the wallet creation step for setting up HWI, the program Bitcoin Core uses to talk to hardware wallets.
    heading: qsTr("Set up HWI")
    //: Explains HWI at the start of external signer wallet creation. "HWI" is a program name; keep it untranslated.
    subheading: qsTr("HWI (Hardware Wallet Interface) lets Bitcoin Core communicate with your hardware wallet.")
    maximumContentWidth: 800
    // The intro and the status rows fill the step; a smaller gap keeps them
    // in view without scrolling.
    childTopMargin: Theme.space.xxl
    showBackButton: navigationStack && navigationStack.depth > 1
    showCloseButton: modalView
    primaryButtonObjectName: "externalSignerSetupContinueButton"
    primaryButtonText: {
        //: Footer button while HWI and the hardware wallet are being checked.
        if (root.pathSection && root.pathSection.checking) return qsTr("Checking…")
        //: Continues external signer wallet creation with the hardware wallet that was found.
        if (root.deviceReady) return qsTr("Use this device")
        //: Checks again for a connected hardware wallet after HWI responded.
        if (root.pathSection && root.pathSection.commandResponding) return qsTr("Check again")
        //: Saves the signer path and checks the signer command and the connected hardware wallet. Same label as on the external signer settings page.
        return qsTr("Check device")
    }
    primaryButtonEnabled: root.deviceReady || (root.pathSection !== null && root.pathSection.canCheck
        && root.pathSection.pathText.trim().length > 0)
    onCloseClicked: root.cancel()
    onPrimaryClicked: {
        if (root.deviceReady) root.ready()
        else root.pathSection.check()
    }

    childView: ColumnLayout {
        property alias pathSection: pathSection
        spacing: Theme.space.l

        // Titled like the "Signer executable" section under it, so the step
        // reads as what to do, then where to do it. The links are part of
        // the sentences (LinkText).
        ColumnLayout {
            objectName: "externalSignerSetupSteps"
            Layout.fillWidth: true
            spacing: Theme.space.m

            CoreText {
                objectName: "externalSignerSetupStepsTitle"
                Layout.fillWidth: true
                Layout.leftMargin: Theme.space.xs
                Layout.rightMargin: Theme.space.xs
                //: Title of the HWI install instructions in the external signer setup step.
                text: qsTr("Install HWI")
                color: Theme.color.neutral9
                textStyle: Theme.text.subheading
                horizontalAlignment: Text.AlignLeft
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.space.xs
                Layout.rightMargin: Theme.space.xs
                spacing: Theme.space.s

                StepRow {
                    number: 1
                    LinkText {
                        objectName: "externalSignerSetupReleasesLink"
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignBaseline
                        onActivated: function(link) { linkPopup.openLink(link) }
                        link: "https://github.com/bitcoin-core/HWI/releases"
                        //: Link text that ends the first HWI install step, with the sentence's period, so the external-link icon after it comes last.
                        linkText: qsTr("releases page.")
                        //: First HWI install step. %1 is the "releases page." link, which already carries the period.
                        sentence: qsTr("Download HWI for your operating system from its %1")
                    }
                }

                StepRow {
                    number: 2
                    CoreText {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignBaseline
                        //: Second HWI install step.
                        text: qsTr("Extract it, then select the HWI executable below. If HWI is already installed, select that one.")
                        color: Theme.color.neutral7
                        textStyle: Theme.text.description
                        wrap: true
                        horizontalAlignment: Text.AlignLeft
                    }
                }
            }

            LinkText {
                objectName: "externalSignerSetupGuideLink"
                Layout.fillWidth: true
                onActivated: function(link) { linkPopup.openLink(link) }
                Layout.leftMargin: Theme.space.xs
                Layout.rightMargin: Theme.space.xs
                link: "https://github.com/bitcoin-core/HWI#readme"
                //: Link text that ends the HWI help line, with the sentence's period, so the external-link icon after it comes last.
                linkText: qsTr("HWI setup guide.")
                //: Offers help with installing HWI. %1 is the "HWI setup guide." link, which already carries the period.
                sentence: qsTr("Need help? Follow the %1")
            }
        }

        // The signer runs with access to the wallet, as the Qt Widgets
        // options dialog warns next to this setting. Same banner as the RPC
        // console's warning about commands that can steal funds.
        ToastBanner {
            objectName: "externalSignerSetupDownloadWarning"
            Layout.fillWidth: true
            tone: ToastBanner.Error
            textColor: Theme.color.neutral9
            shown: root.downloadWarningShown
            showsCloseButton: true
            onDismissed: root.downloadWarningShown = false
            //: Shown under the HWI download links. The selected executable is run by Bitcoin Core and can act on the wallet.
            text: qsTr("Only use HWI from its official releases page. A modified executable can steal your coins.")
        }

        ExternalSignerPathSection {
            id: pathSection
            objectName: "externalSignerSetupPathSection"
            Layout.fillWidth: true
            namePrefix: "externalSignerSetup"
            // The step sits on a dialog, so the card is one level up.
            surfaceLevel: root.surfaceLevel
            // Named as on the external signer settings page: the field takes
            // any signer, HWI or another.
            //: Title of the field for the external signer executable, such as HWI.
            title: qsTr("Signer executable")
        }
    }

    // A numbered step: the number, then the step's text with a hanging
    // indent. The number lines up with the text's first line, which an
    // inline link icon can make taller than the others.
    component StepRow: RowLayout {
        id: stepRow
        required property int number

        Layout.fillWidth: true
        spacing: Theme.space.s

        CoreText {
            Layout.alignment: Qt.AlignBaseline
            Layout.preferredWidth: Theme.space.l
            //: Number of a step in a list of instructions, such as "1.".
            text: qsTr("%1.").arg(stepRow.number)
            color: Theme.color.neutral7
            textStyle: Theme.text.description
            horizontalAlignment: Text.AlignLeft
        }
    }

    ExternalPopup {
        id: linkPopup
        objectName: "externalSignerSetupLinkPopup"
        parent: Overlay.overlay
    }
}
