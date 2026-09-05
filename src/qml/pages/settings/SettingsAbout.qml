// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../../controls"
import "../../components"

InformationPage {
    signal developerRequested
    property bool onboarding: false
    property bool showBackButton: true
    id: root
    objectName: "settingsAbout"
    bannerActive: false
    bannerMargin: 0
    bold: true
    showHeader: root.onboarding
    headerText: qsTr("About")
    headerMargin: 0
    description: qsTr("Bitcoin Core is an open source project.\nIf you find it useful, please contribute.\n\nThis is experimental software.")
    descriptionMargin: 10
    detailActive: true
    detailItem: AboutOptions {
        showDeveloper: !root.onboarding
        onNext: root.developerRequested()
    }

    showNavBar: false
    header: SettingsHeader {
        title: root.onboarding ? "" : qsTr("About")
        showBackButton: root.onboarding || root.showBackButton
        backButtonObjectName: "settingsAboutBack"
        backButtonText: qsTr("Back")
        onBack: root.back()
    }

}
