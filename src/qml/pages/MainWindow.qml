// Copyright (c) 2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0
import "../components"
import "../controls"
import "./node"

ApplicationWindow {
    id: appWindow
    objectName: "mainWindow"
    readonly property string nodeStatus: nodeLifecycleModel.statusText
    title: qsTr("Bitcoin Core")
    minimumWidth: 800
    minimumHeight: 665
    color: Theme.color.background
    palette.window: Theme.color.neutral1 // Context menu background
    palette.windowText: Theme.color.neutral8 // Menu item text and icons
    palette.dark: Theme.dark ? Theme.color.neutral2 : Theme.color.neutral3 // Menu border
    palette.mid: Theme.dark ? Theme.color.neutral2 : Theme.color.neutral3 // Menu separators
    palette.light: Theme.color.neutral3 // Highlighted menu item background
    palette.midlight: Theme.color.neutral3 // Pressed menu item background
    palette.disabled.windowText: Theme.color.neutral4 // Disabled item text and icons
    palette.disabled.light: Theme.color.neutral1 // Disabled item highlight background
    palette.disabled.midlight: Theme.color.neutral1 // Disabled item pressed background

    visible: true
    width: minimumWidth
    height: minimumHeight
    onClosing: (close) => {
        close.accepted = false
        nodeLifecycleModel.requestShutdown()
    }
    ApplicationShell { anchors.fill: parent }
}
