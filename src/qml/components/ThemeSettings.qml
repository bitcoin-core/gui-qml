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
        actionItem: Button {
            anchors.centerIn: parent
            visible: !Theme.dark
            icon.source: "image://images/check"
            icon.color: Theme.color.neutral9
            icon.height: 24
            icon.width: 24
            background: null

            Behavior on icon.color {
                ColorAnimation { duration: 150 }
            }
        }
        onClicked: {
            Theme.dark = false
        }
    }
    Separator { Layout.fillWidth: true }
    Setting {
        Layout.fillWidth: true
        header: qsTr("Dark")
        actionItem: Button {
            anchors.centerIn: parent
            visible: Theme.dark
            icon.source: "image://images/check"
            icon.color: Theme.color.neutral9
            icon.height: 24
            icon.width: 24
            background: null

            Behavior on icon.color {
                ColorAnimation { duration: 150 }
            }
        }
        onClicked: {
            Theme.dark = true;
        }
    }
}
