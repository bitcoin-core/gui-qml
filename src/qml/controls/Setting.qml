// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

AbstractButton {
    id: root
    property bool last: parent && root === parent.children[parent.children.length - 1]
    required property string header
    property alias actionItem: action_loader.sourceComponent
    property alias loadedItem: action_loader.item
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
            Loader {
                id: action_loader
                active: true
                visible: active
                sourceComponent: root.actionItem
            }
        }
        Loader {
            Layout.fillWidth: true
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
