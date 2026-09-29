// Copyright (c) 2024 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0

RowLayout {
    id: root
    property int keyWidth: 125
    property alias key: keyField.contentItem
    property alias value: valueField.contentItem
    width: parent.width

    spacing: 10
    Pane {
        id: keyField
        implicitWidth: root.keyWidth
        Layout.alignment: Qt.AlignLeft
        background: null
        padding: 0
    }
    Pane {
        id: valueField
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        Layout.alignment: Qt.AlignLeft
        implicitHeight: Math.max(valueField.contentHeight, 21)
        padding: 0
        background: null
    }
}
