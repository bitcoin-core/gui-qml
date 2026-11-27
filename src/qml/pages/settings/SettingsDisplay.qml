// Copyright (c) 2023 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../../controls"
import "../../components"

Page {
    id: root
    signal back
    signal navigateRequested(string route)
    property bool showBackButton: false
    background: null
    leftPadding: 20
    rightPadding: 20
    topPadding: 30
    header: SettingsHeader {
        title: qsTr("Display")
        showBackButton: root.showBackButton
        backButtonObjectName: "settingsDisplayBack"
        onBack: root.back()
    }
    ColumnLayout {
        spacing: 4
        width: Math.min(parent.width, 450)
        anchors.horizontalCenter: parent.horizontalCenter
        Setting {
            id: theme
            objectName: "gotoTheme"
            Layout.fillWidth: true
            header: qsTr("Theme")
            actionItem: CaretRightIcon { color: theme.stateColor }
            onClicked: root.navigateRequested("settings/theme")
        }
        Separator { Layout.fillWidth: true }
        Setting {
            id: clock
            objectName: "gotoBlockClockSize"
            Layout.fillWidth: true
            header: qsTr("Block status size")
            actionItem: CaretRightIcon { color: clock.stateColor }
            onClicked: root.navigateRequested("settings/block-clock")
        }
        Separator { Layout.fillWidth: true }
        Setting {
            id: unit
            objectName: "gotoDisplayUnit"
            Layout.fillWidth: true
            header: qsTr("Display unit")
            actionItem: CaretRightIcon { color: unit.stateColor }
            onClicked: root.navigateRequested("settings/unit")
        }
        Separator { Layout.fillWidth: true }
        Setting {
            id: language
            objectName: "gotoLanguage"
            readonly property var settingStatus: languageSettingsModel.status
            Layout.fillWidth: true
            header: qsTr("Language")
            state: settingStatus.canEdit === false ? "DISABLED" : "FILLED"
            infoText: settingStatus.infoText || ""
            showInfoText: infoText.length > 0
            actionItem: CaretRightIcon { color: language.stateColor }
            onClicked: root.navigateRequested("settings/language")
        }
    }
}
