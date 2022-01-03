// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Control {
    id: root
    property bool last: false
    property string header
    property string description
    contentItem: GridLayout {
        columns: 2
        rowSpacing: 20
        width: parent.width
        Header {
            Layout.fillWidth: true
            center: false
            header: root.header
            headerSize: 18
            description: root.description
            descriptionSize: 15
            descriptionMargin: 0
        }
        OptionSwitch {
            Layout.alignment: Qt.AlignRight
        }
        Loader {
            Layout.fillWidth:true
            Layout.columnSpan: 2
            active: !last
            visible: active
            sourceComponent: Rectangle {
                height: 1
                color: "#777777"
            }
        }
    }
}
