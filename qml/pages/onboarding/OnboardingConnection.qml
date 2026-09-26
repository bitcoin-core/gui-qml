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
    objectName: "onboardingConnection"
    signal back()
    signal next()
    property var settingsModel: optionsModel

    isOnSurface: false
    backButtonInFooter: true
    backButtonObjectName: "onboardingWizardBackButton"
    autoNavigateBack: false
    maximumContentWidth: 640
    childTopMargin: 24
    heading: qsTr("Starting initial download")
    subheading: qsTr("The application will connect to the Bitcoin network and start downloading and verifying transactions.\n\nThis may take several hours, or even days, based on your connection.")
    imageSource: Theme.image.storage
    imageSize: 200
    primaryButtonText: qsTr("Start")
    primaryButtonObjectName: "onboardingConnectionButton"
    onBackClicked: root.back()
    onPrimaryClicked: root.next()

    childView: RowLayout {
        LinkButton {
            objectName: "connectionSettingsButton"
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("Network settings")
            onClicked: connectionPopup.open()
        }
    }

    OnboardingSettingsPopup {
        id: connectionPopup
        objectName: "onboardingConnectionSettingsPopup"
        closeButtonObjectName: "onboardingConnectionSettingsCloseButton"
        initialPage: ConnectionSettingsPage {
            settingsModel: root.settingsModel
            onboardingModal: true
        }
    }
}
