// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import org.bitcoincore.qt 1.0
import QtQuick.Layouts 1.15
import "../../components"

SettingsPage {
    id: root
    objectName: "settingsDeveloper"
    property bool onboarding: false
    property SettingsEditorModel editor: null

    title: qsTr("Developer settings")
    showBackButton: true
    backButtonObjectName: "settingsDeveloperBack"

    DeveloperOptions {
        editor: root.editor
        Layout.fillWidth: true
        surfaceLevel: root.onboarding ? 1 : 0
        showRestartNotice: !root.onboarding && ApplicationModels.optionsModel.developerSettingsDirty
    }
}
