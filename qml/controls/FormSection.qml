// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

ColumnLayout {
    id: root

    default property alias content: contentColumn.data
    property string title: ""
    property string description: ""
    property string footerText: ""
    property bool showBackground: true
    property int rowSpacing: 0
    property int sectionSpacing: 8
    property int cornerRadius: 16
    property color backgroundColor: Theme.color.neutral1
    property var titleTextStyle: Theme.text.subheading
    property var descriptionTextStyle: Theme.text.caption
    property var footerTextStyle: Theme.text.caption

    spacing: sectionHeader.visible ? sectionSpacing : 0
    implicitWidth: 450
    implicitHeight: sectionColumn.implicitHeight

    ColumnLayout {
        id: sectionColumn
        Layout.fillWidth: true
        spacing: sectionHeader.visible || sectionFooter.visible ? root.sectionSpacing : 0

        ColumnLayout {
            id: sectionHeader
            visible: root.title.length > 0 || root.description.length > 0
            Layout.fillWidth: true
            Layout.leftMargin: 4
            Layout.rightMargin: 4
            spacing: 2

            CoreText {
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
                visible: root.description.length > 0
                Layout.fillWidth: true
                text: root.description
                color: Theme.color.neutral7
                font: root.descriptionTextStyle.font
                lineHeight: root.descriptionTextStyle.lineHeight
                lineHeightMode: Text.FixedHeight
                horizontalAlignment: Text.AlignLeft
                wrap: true
            }
        }

        // Let Control track content sizing without a direct nested-layout
        // height binding, which loops for wrapped rows on Qt 6.2.
        Control {
            Layout.fillWidth: true
            padding: 0
            implicitHeight: implicitContentHeight
            clip: true

            background: Rectangle {
                objectName: root.objectName.length > 0 ? root.objectName + "Card" : ""
                radius: root.cornerRadius
                color: root.showBackground ? root.backgroundColor : "transparent"

                Behavior on color {
                    ColorAnimation { duration: 150 }
                }
            }

            contentItem: ColumnLayout {
                id: contentColumn
                spacing: root.rowSpacing
            }
        }

        CoreText {
            id: sectionFooter
            objectName: root.objectName.length > 0 ? root.objectName + "Footer" : ""
            visible: root.footerText.length > 0
            Layout.fillWidth: true
            Layout.leftMargin: 4
            Layout.rightMargin: 4
            text: root.footerText
            color: Theme.color.neutral7
            font: root.footerTextStyle.font
            lineHeight: root.footerTextStyle.lineHeight
            lineHeightMode: Text.FixedHeight
            horizontalAlignment: Text.AlignLeft
            wrap: true
        }
    }
}
