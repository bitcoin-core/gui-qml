// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Layouts 1.15
import "../../controls"
import "../../components"
import "../settings"

OnboardingView {
    id: root
    objectName: "onboardingCover"
    signal next()

    isOnSurface: false
    backButtonInFooter: true
    backButtonObjectName: "onboardingWizardBackButton"
    showBackButton: false
    maximumContentWidth: 640
    childTopMargin: 16
    heading: qsTr("Bitcoin Core App")
    subheading: qsTr("Be part of the Bitcoin network.")
    imageSource: "image://images/app"
    imageSize: 224
    primaryButtonText: qsTr("Start")
    primaryButtonObjectName: "onboardingCoverButton"
    onPrimaryClicked: root.next()

    childView: ColumnLayout {
        spacing: 16

        CoreText {
            Layout.fillWidth: true
            text: qsTr("100% open-source & open-design")
            color: Theme.color.neutral7
            font: Theme.text.body.font
            lineHeight: Theme.text.body.lineHeight
            lineHeightMode: Text.FixedHeight
            horizontalAlignment: Text.AlignHCenter
        }

        LinkButton {
            objectName: "onboardingCoverInfoButton"
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("About Bitcoin Core")
            onClicked: aboutPopup.open()
        }
    }

    OnboardingSettingsPopup {
        id: aboutPopup
        objectName: "onboardingAboutPopup"
        closeButtonObjectName: "onboardingAboutCloseButton"
        initialPage: AboutSettingsPage { onboardingModal: true }
    }
}
