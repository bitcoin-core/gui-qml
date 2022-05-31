// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Control {
    id: root
    property bool last: parent && root === parent.children[parent.children.length - 1]
    required property string header
    property string description
    contentItem: ColumnLayout {
        spacing: 20
        width: parent.width
        RowLayout {
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
                Layout.rightMargin: -12
            }
        }
        Loader {
            Layout.fillWidth:true
            Layout.columnSpan: 2
            active: !last
            visible: active
            sourceComponent: Rectangle {
                height: 1
                color: Theme.color.neutral5
            }
        }
    }
}
