// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Dialogs 1.3

import org.bitcoincore.qt 1.0

import "../controls"

ColumnLayout {
    ButtonGroup {
        id: group
    }
    spacing: 15
    OptionButton {
        id: defaultDirOption
        Layout.fillWidth: true
        ButtonGroup.group: group
        text: qsTr("Default")
        description: qsTr("Your application directory.")
        customDir: optionsModel.getDefaultDataDirString
        checked: optionsModel.dataDir === optionsModel.getDefaultDataDirString
        onClicked: {
            defaultDirOption.checked = true
            optionsModel.dataDir = optionsModel.getDefaultDataDirString
        }
    }
    OptionButton {
        id: customDirOption
        Layout.fillWidth: true
        ButtonGroup.group: group
        text: qsTr("Custom")
        description: qsTr("Choose the directory and storage device.")
        customDir: customDirOption.checked ? fileDialog.folder : ""
        checked: optionsModel.dataDir !== optionsModel.getDefaultDataDirString
        onClicked: {
            if (AppMode.isDesktop) {
                if (!singleClickTimer.running) {
                    // Start the timer if it's not already running
                    singleClickTimer.start();
                } else {
                    // If the timer is running, it's a double-click
                    singleClickTimer.stop();
                }
            } else {
                fileDialog.open()
            }
        }
        Timer {
            id: singleClickTimer
            interval: 300
            onTriggered: {
                // If the timer times out, it's a single-click
                fileDialog.open()
            }
            repeat: false // No need to repeat the timer
        }
    }
    FileDialog {
        id: fileDialog
        selectFolder: true
        folder: shortcuts.home
        onAccepted: {
            optionsModel.setCustomDataDirString(fileDialog.fileUrls[0].toString())
            var customDataDir = fileDialog.fileUrl.toString();
            if (customDataDir !== "") {
                optionsModel.setCustomDataDirArgs(customDataDir)
                customDirOption.customDir = optionsModel.getCustomDataDirString()
                if (optionsModel.dataDir !== optionsModel.getDefaultDataDirString) {
                    customDirOption.checked = true
                    defaultDirOption.checked = false
                }
            }
        }
        onRejected: {
            console.log("Custom datadir selection canceled")
            if (optionsModel.dataDir !== optionsModel.getDefaultDataDirString) {
                customDirOption.checked = true
                defaultDirOption.checked = false
            } else {
                defaultDirOption.checked = true
            }
        }
    }
}
