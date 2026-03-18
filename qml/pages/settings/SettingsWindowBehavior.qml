// Copyright (c) 2021-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../../controls"
import "../../components"

Item {
    id: root
    objectName: "windowBehaviorPage"
    signal back

    property var windowBehaviorModel: desktopWindowBehaviorModel

    Page {
        anchors.fill: parent
        background: null
        implicitWidth: 450
        leftPadding: 20
        rightPadding: 20
        topPadding: 30

        header: NavigationBar2 {
            leftItem: NavButton {
                objectName: "windowBehaviorBack"
                iconSource: "image://images/caret-left"
                text: qsTr("Back")
                onClicked: root.back()
            }
            centerItem: Header {
                headerBold: true
                headerSize: 18
                header: qsTr("Window behavior")
            }
        }

        ColumnLayout {
            spacing: 4
            width: Math.min(parent.width, 450)
            anchors.horizontalCenter: parent.horizontalCenter

            Setting {
                id: showTrayIconSetting
                Layout.fillWidth: true
                header: qsTr("Show tray icon")
                description: qsTr("Keep the app available in the system tray")
                disabled: !windowBehaviorModel.desktopPlatform
                actionItem: OptionSwitch {
                    objectName: "showTrayIconSwitch"
                    checked: windowBehaviorModel.showTrayIcon
                    onToggled: windowBehaviorModel.showTrayIcon = checked
                }
                onClicked: windowBehaviorModel.showTrayIcon = !windowBehaviorModel.showTrayIcon
            }

            Separator { Layout.fillWidth: true }

            Setting {
                id: minimizeToTraySetting
                Layout.fillWidth: true
                header: qsTr("Minimize to tray")
                description: qsTr("Hide window to tray when minimized")
                disabled: !windowBehaviorModel.desktopPlatform ||
                          !windowBehaviorModel.showTrayIcon
                actionItem: OptionSwitch {
                    objectName: "minimizeToTraySwitch"
                    checked: windowBehaviorModel.minimizeToTray
                    enabled: windowBehaviorModel.desktopPlatform &&
                             windowBehaviorModel.showTrayIcon
                    onToggled: windowBehaviorModel.minimizeToTray = checked
                }
                onClicked: windowBehaviorModel.minimizeToTray = !windowBehaviorModel.minimizeToTray
            }

            Separator { Layout.fillWidth: true }

            Setting {
                id: minimizeOnCloseSetting
                Layout.fillWidth: true
                header: qsTr("Minimize on close")
                description: qsTr("Keep node running when the window is closed")
                disabled: !windowBehaviorModel.desktopPlatform ||
                          !windowBehaviorModel.showTrayIcon
                actionItem: OptionSwitch {
                    objectName: "minimizeOnCloseSwitch"
                    checked: windowBehaviorModel.minimizeOnClose
                    enabled: windowBehaviorModel.desktopPlatform &&
                             windowBehaviorModel.showTrayIcon
                    onToggled: windowBehaviorModel.minimizeOnClose = checked
                }
                onClicked: windowBehaviorModel.minimizeOnClose = !windowBehaviorModel.minimizeOnClose
            }
        }
    }
}
