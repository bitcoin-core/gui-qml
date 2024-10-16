// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../controls"

ColumnLayout {
    // TODO: Remove this once storing the snapshot path is implemented
    property bool isOnboarding: false
    property bool snapshotImported: false
    function setSnapshotImported(imported) {
        snapshotImported = imported
    }
    spacing: 4
    Item {
        // TODO: Remove this once storing the snapshot path is implemented
        visible: !isOnboarding
        height: visible ? implicitHeight : 0
        Layout.fillWidth: true
        Layout.preferredHeight: gotoSnapshot.height

        Setting {
            id: gotoSnapshot
            visible: parent.visible
            Layout.fillWidth: true
            header: qsTr("Load snapshot")
            description: qsTr("Instant use with background sync")
            actionItem: Item {
                width: 26
                height: 26
                CaretRightIcon {
                    // TODO: aligment will be fixed once Onboarding snapshot works
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    visible: !snapshotImported
                    color: gotoSnapshot.stateColor
                }
                GreenCheckIcon {
                    anchors.centerIn: parent
                    visible: snapshotImported
                    color: Theme.color.transparent
                }
            }
            onClicked: {
                connectionSwipe.incrementCurrentIndex()
                connectionSwipe.incrementCurrentIndex()
            }
        }
    }
    Separator {
        Layout.fillWidth: true
        // TODO: Remove this once storing the snapshot path is implemented
        visible: !isOnboarding
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
        onClicked: connectionSwipe.incrementCurrentIndex()
    }
}
