// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

AbstractButton {
    id: root
    required property string parentState
    required property string link
    property string description: ""
    property int descriptionSize: 18
    property url iconSource: "image://images/export"
    property int iconWidth: 22
    property int iconHeight: 22
    property int iconSlotSize: 30
    property color iconColor: Theme.color.neutral9
    property color textColor: Theme.color.neutral9
    enabled: root.parentState !== "DISABLED"
    state: root.parentState

    states: [
        State {
            name: "ACTIVE"
            PropertyChanges {
                target: root
                iconColor: Theme.color.orange
                textColor: Theme.color.orange
            }
        },
        State {
            name: "HOVER"
            PropertyChanges {
                target: root
                iconColor: Theme.color.orangeLight1
                textColor: Theme.color.orangeLight1
            }
        },
        State {
            name: "DISABLED"
            PropertyChanges {
                target: root
                iconColor: Theme.color.neutral4
                textColor: Theme.color.neutral4
            }
        }
    ]

    contentItem: RowLayout {
        spacing: 0
        width: parent.width
        Loader {
            Layout.fillWidth: true
            active: root.description.length > 0
            visible: active
            sourceComponent: CoreText {
                font.pixelSize: root.descriptionSize
                color: root.textColor
                textFormat: Text.RichText
                text: root.description
                wrap: false

                Behavior on color {
                    ColorAnimation { duration: 150 }
                }
            }
        }
        RightContentIcon {
            objectName: "externalLinkIconSlot"
            Layout.alignment: Qt.AlignVCenter
            source: root.iconSource
            color: root.iconColor
            iconSize: Math.max(root.iconWidth, root.iconHeight)
            slotSize: root.iconSlotSize
        }
    }
    onClicked: Qt.openUrlExternally(link)
}
