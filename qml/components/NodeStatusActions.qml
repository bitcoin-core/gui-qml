// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

import "../controls"

RowLayout {
    id: root
    spacing: Theme.space.xs

    IconButton {
        objectName: "nodeWarningsButton"
        visible: nodeModel.hasWarnings
        iconSource: "image://images/alert-filled"
        iconColor: Theme.color.warning
        hoverColor: Theme.color.warning
        size: 34
        accessibleName: qsTr("Node warnings")
        onClicked: root.openWindowPopup("openNodeWarnings")
    }

    IconButton {
        objectName: "nodeInformationButton"
        iconSource: "image://images/info"
        iconColor: Theme.color.neutral7
        size: 34
        accessibleName: qsTr("Node information")
        onClicked: root.openWindowPopup("openNodeInformation")
    }

    // The window (MainWindow) owns the popups.
    function openWindowPopup(name) {
        const window = ApplicationWindow.window
        if (window && typeof window[name] === "function") window[name]()
    }
}
