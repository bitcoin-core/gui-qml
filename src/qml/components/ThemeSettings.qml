// Copyright (c) 2023 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import Qt.labs.settings 1.0
import "../controls"

ColumnLayout {
    id: root
    spacing: 4

    Settings {
        id: settings
    }

    Setting {
        Layout.fillWidth: true
        header: qsTr("Light")
        actionItem: Icon {
            anchors.centerIn: parent
            visible: !Theme.manualDark && Theme.manualTheme
            source: "image://images/check"
            color: Theme.color.neutral9
            size: 24
        }
        onClicked: {
            Theme.manualTheme = true;
            Theme.manualDark = false;
        }
    }
    Separator { Layout.fillWidth: true }
    Setting {
        Layout.fillWidth: true
        header: qsTr("Dark")
        actionItem: Icon {
            anchors.centerIn: parent
            visible: Theme.manualDark && Theme.manualTheme
            source: "image://images/check"
            color: Theme.color.neutral9
            size: 24
        }
        onClicked: {
            Theme.manualTheme = true;
            Theme.manualDark = true;
        }
    }
    Separator { Layout.fillWidth: true }
    Setting {
        id: systemThemeSetting
        property bool systemThemeAvailable: Theme.systemThemeAvailable
        Layout.fillWidth: true
        header: qsTr("System")
        actionItem: Icon {
            anchors.centerIn: parent
            visible: !Theme.manualTheme
            source: "image://images/check"
            size: 24
        }
        Component.onCompleted: {
            if (systemThemeAvailable) {
                systemThemeSetting.state = "FILLED"
            } else {
                systemThemeSetting.state = "DISABLED"
            }
        }
        onClicked: {
            Theme.manualTheme = false
        }
    }
}
