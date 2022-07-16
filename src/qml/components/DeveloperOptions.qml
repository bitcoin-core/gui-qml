// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../controls"

ColumnLayout {
    spacing: 20
    Information {
        Layout.fillWidth: true
        header: qsTr("Developer documentation")
        hasIcon: true
        iconSource: "qrc:/icons/export"
        link: "https://bitcoin.org/en/bitcoin-core/contribute/documentation"
    }
    Setting {
        Layout.fillWidth: true
        header: qsTr("Storage limit")
    }
    Information {
        Layout.fillWidth: true
        header: qsTr("Network")
        description: qsTr("Mainnet")
        isReadonly: false
    }
    Information {
        Layout.fillWidth: true
        header: qsTr("Other option...")
        description: qsTr("42")
        isReadonly: false
    }
    Setting {
        Layout.fillWidth: true
        header: qsTr("Other option...")
        description: qsTr("Description...")
    }
}
