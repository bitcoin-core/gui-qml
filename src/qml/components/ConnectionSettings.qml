// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../controls"

ColumnLayout {
    id: root
    signal next
    property var settingsModel: optionsModel
    property var coreSettingsModel: settingsModel.coreSettings
    property bool showRestartNotice: false
    readonly property var listenSetting: coreSettingsModel.entry("listen")
    readonly property var natpmpSetting: coreSettingsModel.entry("natpmp")
    readonly property var serverSetting: coreSettingsModel.entry("server")
    spacing: 4
    SettingsRestartNotice {
        visible: root.showRestartNotice
        Layout.fillWidth: true
        Layout.bottomMargin: visible ? 12 : 0
    }
    Setting {
        objectName: "listenSetting"
        Layout.fillWidth: true
        header: qsTr("Enable listening")
        description: qsTr("Allows incoming connections")
        state: root.listenSetting.canEdit ? "FILLED" : "DISABLED"
        infoText: root.listenSetting.infoText
        showInfoText: infoText.length > 0
        actionItem: OptionSwitch {
            objectName: "listenSwitch"
            checked: root.listenSetting.value
            onToggled: root.listenSetting.value = checked
        }
        onClicked: {
          loadedItem.toggle()
          loadedItem.toggled()
        }
    }
    Separator { Layout.fillWidth: true }
    Setting {
        objectName: "natpmpSetting"
        Layout.fillWidth: true
        header: qsTr("Map port using NAT-PMP")
        state: root.natpmpSetting.canEdit ? "FILLED" : "DISABLED"
        infoText: root.natpmpSetting.infoText
        showInfoText: infoText.length > 0
        actionItem: OptionSwitch {
            objectName: "natpmpSwitch"
            checked: root.natpmpSetting.value
            onToggled: root.natpmpSetting.value = checked
        }
        onClicked: {
          loadedItem.toggle()
          loadedItem.toggled()
        }
    }
    Separator { Layout.fillWidth: true }
    Setting {
        objectName: "serverSetting"
        Layout.fillWidth: true
        header: qsTr("Enable RPC server")
        state: root.serverSetting.canEdit ? "FILLED" : "DISABLED"
        infoText: root.serverSetting.infoText
        showInfoText: infoText.length > 0
        actionItem: OptionSwitch {
            objectName: "serverSwitch"
            checked: root.serverSetting.value
            onToggled: root.serverSetting.value = checked
        }
        onClicked: {
          loadedItem.toggle()
          loadedItem.toggled()
        }
    }
    Separator { Layout.fillWidth: true }
    Setting {
        id: gotoProxy
        objectName: "gotoProxy"
        Layout.fillWidth: true
        header: qsTr("Proxy settings")
        actionItem: CaretRightIcon {
            color: gotoProxy.stateColor
        }
        onClicked: root.next()
    }
}
