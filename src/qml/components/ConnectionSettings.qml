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
    signal gotoSnapshot
    property bool onboarding: false
    property bool snapshotImportCompleted: onboarding ? false : chainModel.isSnapshotActive
    property bool isIBDCompleted: nodeModel.isIBDCompleted
    spacing: 4
    Setting {
        id: gotoSnapshot
        visible: !root.onboarding && !snapshotImportCompleted && !root.isIBDCompleted
        Layout.fillWidth: true
        header: qsTr("Load snapshot")
        description: qsTr("Instant use with background sync")
        actionItem: Item {
            width: 26
            height: 26
            CaretRightIcon {
                anchors.centerIn: parent
                visible: !snapshotImportCompleted
                color: gotoSnapshot.stateColor
            }
            GreenCheckIcon {
                anchors.centerIn: parent
                visible: snapshotImportCompleted
                color: Theme.color.transparent
                size: 30
            }
        }
        onClicked: root.gotoSnapshot()
    }
    Separator {
            visible: !root.onboarding && !snapshotImportCompleted
        Layout.fillWidth: true
    }
    Setting {
        Layout.fillWidth: true
        header: qsTr("Enable listening")
        description: qsTr("Allows incoming connections")
        actionItem: OptionSwitch {
            checked: optionsModel.listen
            onToggled: optionsModel.listen = checked
        }
        onClicked: {
          loadedItem.toggle()
          loadedItem.toggled()
        }
    }
    Separator { Layout.fillWidth: true }
    Setting {
        Layout.fillWidth: true
        header: qsTr("Map port using UPnP")
        actionItem: OptionSwitch {
            checked: optionsModel.upnp
            onToggled: optionsModel.upnp = checked
        }
        onClicked: {
          loadedItem.toggle()
          loadedItem.toggled()
        }
    }
    Separator { Layout.fillWidth: true }
    Setting {
        Layout.fillWidth: true
        header: qsTr("Map port using NAT-PMP")
        actionItem: OptionSwitch {
            checked: optionsModel.natpmp
            onToggled: optionsModel.natpmp = checked
        }
        onClicked: {
          loadedItem.toggle()
          loadedItem.toggled()
        }
    }
    Separator { Layout.fillWidth: true }
    Setting {
        Layout.fillWidth: true
        header: qsTr("Enable RPC server")
        actionItem: OptionSwitch {
            checked: optionsModel.server
            onToggled: optionsModel.server = checked
        }
        onClicked: {
          loadedItem.toggle()
          loadedItem.toggled()
        }
    }
    Separator { Layout.fillWidth: true }
    Setting {
        id: gotoProxy
        Layout.fillWidth: true
        header: qsTr("Proxy settings")
        actionItem: CaretRightIcon {
            color: gotoProxy.stateColor
        }
        onClicked: root.next()
    }
}
