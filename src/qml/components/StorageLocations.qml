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
        checked: true
        customDir: updateCustomDir()
        function updateCustomDir() {
            if (checked) {
                optionsModel.dataDir = optionsModel.getDefaultDataDirString;
                optionsModel.defaultReset();
                return optionsModel.dataDir;
            } else {
                return "";
            }
        }
        onClicked: {
            defaultDirOption.checked = true
            customDirOption.checked = false
            optionsModel.dataDir = optionsModel.getDefaultDataDirString
            optionsModel.defaultReset()
        }
    }
    OptionButton {
        id: customDirOption
        Layout.fillWidth: true
        ButtonGroup.group: group
        text: qsTr("Custom")
        description: qsTr("Choose the directory and storage device.")
        customDir: customDirOption.checked ? optionsModel.getCustomDataDirString() : ""
        checked: optionsModel.dataDir !== optionsModel.getDefaultDataDirString
        onClicked: {
            defaultDirOption.checked = false
            fileDialog.open();
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
                customDirOption.checked = true
                defaultDirOption.checked = false
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
