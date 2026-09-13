// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Control {
    id: root

    property string title: ""
    property string description: ""
    property alias trailingItem: trailingLoader.sourceComponent
    property alias loadedTrailingItem: trailingLoader.item
    property int contentSpacing: 16
    property var titleTextStyle: Theme.text.headline
    property var descriptionTextStyle: Theme.text.description
    property int descriptionTextFormat: Text.AutoText

    Accessible.name: title
    Accessible.description: description
    padding: 0
    implicitWidth: Math.max(320, contentItem.implicitWidth)
    implicitHeight: contentItem.implicitHeight
    background: null

    contentItem: RowLayout {
        spacing: root.contentSpacing

        ColumnLayout {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            spacing: 4

            CoreText {
                objectName: root.objectName.length > 0 ? root.objectName + "Title" : ""
                visible: root.title.length > 0
                Layout.fillWidth: true
                text: root.title
                color: Theme.color.neutral9
                font: root.titleTextStyle.font
                lineHeight: root.titleTextStyle.lineHeight
                lineHeightMode: Text.FixedHeight
                horizontalAlignment: Text.AlignLeft
                wrap: false
                elide: Text.ElideRight
            }

            CoreText {
                objectName: root.objectName.length > 0 ? root.objectName + "Description" : ""
                visible: root.description.length > 0
                Layout.fillWidth: true
                text: root.description
                color: Theme.color.neutral7
                font: root.descriptionTextStyle.font
                lineHeight: root.descriptionTextStyle.lineHeight
                lineHeightMode: Text.FixedHeight
                horizontalAlignment: Text.AlignHCenter
                textFormat: root.descriptionTextFormat
                wrap: true
            }
        }

        Loader {
            id: trailingLoader
            active: sourceComponent !== null
            visible: item !== null
            enabled: root.enabled
            Layout.alignment: Qt.AlignTop | Qt.AlignRight
        }
    }
}
