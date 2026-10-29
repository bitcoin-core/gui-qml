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
    readonly property var pathSection: loadedChildView ? loadedChildView.pathSection : null
    readonly property bool deviceReady: root.pathSection !== null && root.pathSection.deviceReady

    signal cancel()
    signal ready()

    title: ""
    //: Heading of the wallet creation step for setting up HWI, the program Bitcoin Core uses to talk to hardware wallets.
    heading: qsTr("Set up HWI")
    //: Explains HWI at the start of external signer wallet creation. "HWI" is a program name; keep it untranslated.
    subheading: qsTr("HWI (Hardware Wallet Interface) lets Bitcoin Core communicate with your hardware wallet. Download HWI for your operating system from the releases page, extract it, and select the HWI executable below. If you already have HWI installed, select your existing executable.")
    maximumContentWidth: 800
    // The intro and the status rows fill the step; a smaller gap keeps them
    // in view without scrolling.
    childTopMargin: Theme.space.xxl
    showBackButton: navigationStack && navigationStack.depth > 1
    showCloseButton: modalView
    primaryButtonObjectName: "externalSignerSetupContinueButton"
    primaryButtonText: {
        //: Continues external signer wallet creation with the hardware wallet that was found.
        if (root.deviceReady) return qsTr("Use this device")
        //: Checks again for a connected hardware wallet after HWI responded.
        if (root.pathSection && root.pathSection.commandResponding) return qsTr("Check again")
        //: Saves the HWI path and checks that HWI responds.
        return qsTr("Check HWI")
    }
    primaryButtonEnabled: root.pathSection !== null && root.pathSection.editable
        && root.pathSection.pathText.trim().length > 0 && root.pathSection.pathError.length === 0
    onCloseClicked: root.cancel()
    onPrimaryClicked: {
        if (root.deviceReady) root.ready()
        else root.pathSection.check()
    }

    childView: ColumnLayout {
        property alias pathSection: pathSection
        spacing: Theme.space.l

        Flow {
            Layout.fillWidth: true
            spacing: Theme.space.m

            LinkButton {
                objectName: "externalSignerSetupReleasesLink"
                //: Link to the download page for HWI releases.
                text: qsTr("HWI releases")
                onClicked: linkPopup.openLink("https://github.com/bitcoin-core/HWI/releases")
            }
            LinkButton {
                objectName: "externalSignerSetupGuideLink"
                //: Link to the HWI setup documentation.
                text: qsTr("HWI setup guide")
                onClicked: linkPopup.openLink("https://github.com/bitcoin-core/HWI#readme")
            }
        }

        ExternalSignerPathSection {
            id: pathSection
            objectName: "externalSignerSetupPathSection"
            Layout.fillWidth: true
            namePrefix: "externalSignerSetup"
            // The step sits on a dialog, so the card is one level up.
            surfaceLevel: root.surfaceLevel
            //: Title of the field for the path of the HWI executable during wallet creation.
            title: qsTr("HWI executable")
            //: Title of the file dialog for choosing the HWI executable during wallet creation.
            pathDialogTitle: qsTr("Select HWI executable")
        }
    }

    ExternalPopup {
        id: linkPopup
        objectName: "externalSignerSetupLinkPopup"
        parent: Overlay.overlay
    }
}
