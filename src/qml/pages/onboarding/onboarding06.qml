// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../../controls"
import "../../components"

Page {
    background: null
    Layout.fillWidth: true
    clip: true
    SwipeView {
        id: connections
        anchors.fill: parent
        interactive: false
        orientation: Qt.Vertical
        Loader {
            source:"onboarding06a.qml"
        }
        Loader {
            source:"../settings/SettingsConnection.qml"
        }
    }
}
