// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

import "../controls"

RowLayout {
    id: root
    spacing: 4

    IconButton {
        objectName: "nodeWarningsButton"
        visible: runtimeDialogModel.hasWarnings
        iconSource: "image://images/alert-filled"
        iconColor: Theme.color.orange
        hoverColor: Theme.color.orangeLight1
        activeColor: Theme.color.orange
        size: 34
        onClicked: warningsPopup.open()
    }

    NodeWarningsPopup {
        id: warningsPopup
        parent: Overlay.overlay
    }

}
