// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BitcoinApp.Controls
import BitcoinApp.Components

InformationPage {
    bannerActive: false
    bold: true
    headerText: qsTr("Storage settings")
    headerMargin: 0
    detailActive: true
    detailItem: StorageSettings {
        // Set default prune values
        Component.onCompleted: {
            optionsModel.prune = true
            optionsModel.pruneSizeGB = 2
        }
    }
}
