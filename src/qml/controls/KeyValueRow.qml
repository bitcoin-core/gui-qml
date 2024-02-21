// Copyright (c) 2024 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0

RowLayout {
    id: root
    property alias key: key_field.contentItem
    property alias value: value_field.contentItem
    width: parent.width

    spacing: 10
    Pane {
        id: key_field
        implicitWidth: 125
        Layout.alignment: Qt.AlignLeft
        background: null
        padding: 0
    }
    Pane {
        id: value_field
        Layout.fillWidth: true
        Layout.alignment: Qt.AlignLeft
        implicitHeight: Math.max(value_field.contentHeight, 21)
        padding: 0
        background: null
    }
}
