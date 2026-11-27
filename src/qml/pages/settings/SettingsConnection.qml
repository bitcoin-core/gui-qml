// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../../controls"
import "../../components"

InformationPage {
    id: root
    signal proxyRequested
    property var settingsModel: optionsModel
    property bool onboarding: false
    property bool showBackButton: true
    background: null
    clip: true
    bannerActive: false
    bold: true
    showHeader: root.onboarding
    headerText: qsTr("Connection settings")
    headerMargin: 0
    detailActive: true
    showNavBar: false
    header: SettingsHeader {
        title: root.onboarding ? "" : qsTr("Connection settings")
        showBackButton: !root.onboarding && root.showBackButton
        backButtonObjectName: "settingsConnectionBack"
        onBack: root.back()
        rightItem: NavButton {
            objectName: "connectionSettingsDoneButton"
            visible: root.onboarding
            text: qsTr("Done")
            onClicked: root.back()
        }
    }
    detailItem: ConnectionSettings {
        settingsModel: root.settingsModel
        showRestartNotice: !root.onboarding && root.settingsModel.connectionSettingsDirty
        onNext: root.proxyRequested()
    }
}
