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
        Layout.fillWidth: true
        ButtonGroup.group: group
        text: qsTr("Default")
        description: qsTr("Your application directory.")
        recommended: true
        checked: true
        onClicked: {
            onboardingModel.defaultReset()
        }
    }
    OptionButton {
        Layout.fillWidth: true
        ButtonGroup.group: group
        text: qsTr("Custom")
        description: qsTr("Choose the directory and storage device.")
        onClicked: fileDialog.open()
    }
    FileDialog {
        id: fileDialog
        selectFolder: true
        folder: onboardingModel.getDefaultDataDirectory
        onAccepted: {
            onboardingModel.setCustomDataDirString(fileDialog.fileUrls[0].toString())
            var customDataDir = fileDialog.fileUrl.toString();
            if (customDataDir !== "") {
            onboardingModel.setCustomDataDirArgs(customDataDir);
            }
        }
        onRejected: {
            console.log("Canceled")
        }
    }
}