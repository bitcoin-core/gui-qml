pragma ComponentBehavior: Bound

// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick.Layouts 1.15

import "../../controls"

SettingsPage {
    id: root
    objectName: "windowBehaviorSettingsPage"
    title: qsTr("Window behavior")
    showBackButton: false

    property var windowBehaviorModel: desktopWindowBehaviorModel

    FormSection {
        Layout.fillWidth: true
        title: qsTr("Window behavior")

        FormRow {
            Layout.fillWidth: true
            title: qsTr("Show tray icon")
            description: qsTr("Keep the app available in the system tray.")
            enabled: root.windowBehaviorModel.desktopPlatform
            trailingItem: OptionSwitch {
                objectName: "showTrayIconSwitch"
                checked: root.windowBehaviorModel.showTrayIcon
                onToggled: root.windowBehaviorModel.showTrayIcon = checked
            }
        }

        FormRow {
            Layout.fillWidth: true
            title: qsTr("Minimize to tray")
            description: qsTr("Hide the window in the tray when minimized.")
            enabled: root.windowBehaviorModel.desktopPlatform
                && root.windowBehaviorModel.showTrayIcon
            trailingItem: OptionSwitch {
                objectName: "minimizeToTraySwitch"
                checked: root.windowBehaviorModel.minimizeToTray
                onToggled: root.windowBehaviorModel.minimizeToTray = checked
            }
        }

        FormRow {
            Layout.fillWidth: true
            title: qsTr("Minimize on close")
            description: qsTr("Keep the node running when the window is closed.")
            enabled: root.windowBehaviorModel.desktopPlatform
            showDivider: false
            trailingItem: OptionSwitch {
                objectName: "minimizeOnCloseSwitch"
                checked: root.windowBehaviorModel.minimizeOnClose
                onToggled: root.windowBehaviorModel.minimizeOnClose = checked
            }
        }
    }
}
