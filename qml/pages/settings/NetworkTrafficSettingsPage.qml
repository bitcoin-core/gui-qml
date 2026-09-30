// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Layouts 1.15
import "../../controls"
import "../node" as NodePages

SettingsPage {
    id: root
    objectName: "networkTrafficSettingsPage"
    title: qsTr("Network traffic")
    showBackButton: false
    maximumContentWidth: width
    property alias trafficGraphScale: traffic.trafficGraphScale
    NodePages.NetworkTraffic {
        id: traffic
        Layout.fillWidth: true
        active: root.visible
    }
}
