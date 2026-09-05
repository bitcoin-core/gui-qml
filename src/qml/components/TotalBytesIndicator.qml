// Copyright (c) 2023 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../controls"

RowLayout {
    id: root
    property string indicatorText
    property color indicatorColor

    Rectangle {
        Layout.alignment: Qt.AlignVCenter
        height: 10
        width: 10
        radius: 5
        color: root.indicatorColor
    }
    CoreText {
        Layout.alignment: Qt.AlignHCenter
        color: Theme.color.neutral9
        text: root.indicatorText
        font.pixelSize: 18
    }
}
