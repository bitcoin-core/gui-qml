// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Control {
    id: root
    property alias navButton: button_loader.sourceComponent
    property bool alignLeft: true
    height: 46
    contentItem: RowLayout {
        spacing: 0
        Layout.fillWidth: true
        Loader {
            id: button_loader
            Layout.rightMargin: 10
            Layout.alignment: root.alignLeft ? Qt.AlignLeft : Qt.AlignRight
            active: true
            visible: active
            sourceComponent: root.navButton
        }
    }
}
