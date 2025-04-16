// Copyright (c) 2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Button {
    id: button
    property string description
    property bool recommended: false
    property string image: ""
    padding: 15
    checkable: true
    implicitWidth: 450
    background: Rectangle {
        border.width: 1
        border.color: button.checked ? Theme.color.orange : button.hovered ? Theme.color.neutral9 : Theme.color.neutral5
        radius: 10
        color: "transparent"
        FocusBorder {
            visible: button.visualFocus
            borderRadius: 14
        }
    }
    contentItem: RowLayout {
        spacing: 3
        Loader {
            active: button.image !== ""
            visible: active
            Layout.rightMargin: 7
            sourceComponent: Button {
                icon.source: button.image
                icon.height: 40
                icon.width: 40
                icon.color: Theme.color.neutral9
                padding: 0
                background: null
            }
        }
        ColumnLayout {
            spacing: 3
            Layout.fillWidth: true
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
                        color: Theme.color.neutral9
                        radius: 3

                        Behavior on color {
                            ColorAnimation { duration: 150 }
                        }
                    }
                    font.styleName: "Regular"
                    font.pixelSize: 13
                    topPadding: 3
                    rightPadding: 7
                    bottomPadding: 4
                    leftPadding: 7
                    color: Theme.color.neutral0
                    text: qsTr("Recommended")

                    Behavior on color {
                        ColorAnimation { duration: 150 }
                    }
                }
            }
        }
        Item {
            height: parent.height
            width: 40
            Button {
                anchors.centerIn: parent
                visible: button.checked
                icon.source: "image://images/check"
                icon.color: Theme.color.neutral9
                icon.height: 24
                icon.width: 24
                background: null

                Behavior on icon.color {
                    ColorAnimation { duration: 150 }
                }
            }
        }
    }
}
