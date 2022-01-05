// Copyright (c) 2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Button {
    property string description
    property bool recommended: false
    id: button
    padding: 15
    checkable: true
    property alias detail: detail_loader.sourceComponent
    background: Rectangle {
        border.width: 1
        border.color: button.checked ? "#F7931A" : button.hovered ? "white" : "#999999"
        radius: 10
        color: "transparent"
        Rectangle {
            visible: button.visualFocus
            anchors.fill: parent
            anchors.margins: -4
            border.width: 2
            border.color: "#F7931A"
            radius: 14
            color: "transparent"
            opacity: 0.4
        }
    }
    contentItem: RowLayout {
        spacing: 3
        ColumnLayout {
            spacing: 3
            Header {
                Layout.fillWidth: true
                Layout.preferredWidth: 0
                center: false
                header: button.text
                headerSize: 18
                headerMargin: 0
                description: button.description
                descriptionSize: 15
                descriptionMargin: 0
            }
            Loader {
                Layout.topMargin: 2
                active: button.recommended
                visible: active
                sourceComponent: Label {
                    background: Rectangle {
                        color: "white"
                        radius: 3
                    }
                    font.styleName: "Regular"
                    font.pixelSize: 13
                    topPadding: 3
                    rightPadding: 7
                    bottomPadding: 4
                    leftPadding: 7
                    color: "black"
                    text: qsTr("Recommended")
                }
            }
        }
        Loader {
            id: detail_loader
            visible: item
        }
    }
}
