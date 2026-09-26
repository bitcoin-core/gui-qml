// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Layouts 1.15
import "../../controls"
import "../../components"

SettingsPage {
    id: root
    objectName: "settingsDeveloper"
    property bool onboarding: false

    title: qsTr("Developer settings")
    showBackButton: true
    backButtonObjectName: "settingsDeveloperBack"

    DeveloperOptions {
        Layout.fillWidth: true
        isOnSurface: root.onboarding
        showRestartNotice: !root.onboarding && optionsModel.developerSettingsDirty
    }
}
