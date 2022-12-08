// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BitcoinApp.Controls
import BitcoinApp.Components

InformationPage {
    background: null
    clip: true
    bannerActive: false
    bold: true
    headerText: qsTr("Connection settings")
    headerMargin: 0
    detailActive: true
    detailItem: ColumnLayout {
        spacing: 0
        ConnectionSettings {
            Layout.maximumWidth: 450
            Layout.alignment: Qt.AlignCenter
        }
    }
}
