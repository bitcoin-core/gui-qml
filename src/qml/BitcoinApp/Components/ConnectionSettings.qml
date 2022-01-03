// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BitcoinApp.Controls

ColumnLayout {
    spacing: 20
    Setting {
        Layout.fillWidth: true
        header: "Use cellular data"
    }
    Setting {
        Layout.fillWidth: true
        header: "Daily upload limit"
    }
    Setting {
        Layout.fillWidth: true
        header: "Connection limit"
    }
    Setting {
        Layout.fillWidth: true
        header: "Listening enabled"
        description: "Reduces data usage."
    }
    Setting {
        last: true
        Layout.fillWidth: true
        header: "Blocks Only"
        description: "Do not transfer unconfirmed transactions. Also disabled listening."
    }
}
