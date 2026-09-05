// Copyright (c) 2023 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../../controls"
import "../../components"

Page {
    signal back
    signal designSystemRequested

    id: root
    background: null
    leftPadding: 20
    rightPadding: 20
    topPadding: 30

    header: SettingsHeader {
        title: qsTr("Theme")
        onBack: root.back()
    }
    ThemeSettings {
        width: Math.min(parent.width, 450)
        anchors.horizontalCenter: parent.horizontalCenter
        onDesignSystemRequested: root.designSystemRequested()
    }
}
