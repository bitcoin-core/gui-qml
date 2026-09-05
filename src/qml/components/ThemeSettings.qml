// Copyright (c) 2023 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0
import "../controls"

ColumnLayout {
    id: root
    spacing: 4

    signal designSystemRequested

    AppSettings {
        id: settings
    }

    Setting {
        Layout.fillWidth: true
        header: qsTr("Light")
        actionItem: Icon {
            anchors.centerIn: parent
            visible: !Theme.dark
            source: "image://images/check"
            color: Theme.color.neutral9
            size: 24
        }
        onClicked: {
            Theme.dark = false
        }
    }
    Separator { Layout.fillWidth: true }
    Setting {
        Layout.fillWidth: true
        header: qsTr("Dark")
        actionItem: Icon {
            anchors.centerIn: parent
            visible: Theme.dark
            source: "image://images/check"
            color: Theme.color.neutral9
            size: 24
        }
        onClicked: {
            Theme.dark = true;
        }
    }
    CoreText {
        Layout.topMargin: 36
        Layout.fillWidth: true
        Layout.leftMargin: 4
        visible: BuildInfo.isDebug
        horizontalAlignment: Text.AlignLeft
        bold: true
        font.pixelSize: 13
        color: Theme.color.neutral7
        text: qsTr("Developer")
    }
    Separator {
        Layout.fillWidth: true
        visible: BuildInfo.isDebug
    }
    Setting {
        id: gotoDesignSystem
        Layout.fillWidth: true
        visible: BuildInfo.isDebug
        header: qsTr("Design system")
        actionItem: CaretRightIcon {
            color: gotoDesignSystem.stateColor
        }
        onClicked: root.designSystemRequested()
    }
}
