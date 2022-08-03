// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Control {
    id: root
    required property string link
    property string description: ""
    property int descriptionSize: 18
    property url iconSource: ""
    property int iconWidth: 30
    property int iconHeight: 30

    contentItem: RowLayout {
        spacing: 0
        width: parent.width
        Loader {
            Layout.fillWidth: true
            active: root.description.length > 0
            visible: active
            sourceComponent: Text {
                font.family: "Inter"
                font.styleName: "Regular"
                font.pixelSize: root.descriptionSize
                color: Theme.color.neutral7
                textFormat: Text.RichText
                text: "<style>a:link { color: " + Theme.color.neutral7 + "; text-decoration: none;}</style>" + "<a href=\"" + link + "\">" + root.description + "</a>"
                onLinkActivated: Qt.openUrlExternally(link)
            }
        }
        Loader {
            Layout.fillWidth: true
            active: root.iconSource.toString().length > 0
            visible: active
            sourceComponent: Button {
                icon.source: root.iconSource
                icon.color: Theme.color.neutral9
                icon.height: root.iconHeight
                icon.width: root.iconWidth
                background: null
                onClicked: Qt.openUrlExternally(link)
            }
        }
    }
}
