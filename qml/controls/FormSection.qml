// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Layouts 1.15

Item {
    id: root

    default property alias content: contentColumn.data
    property string title: ""
    property string description: ""
    property string footerText: ""
    property Component headerTrailingItem
    property bool showBackground: true
    property bool showGradientBorder: true
    property bool isOnSurface: false
    property int rowSpacing: 0
    property int sectionSpacing: 8
    property int cornerRadius: 16
    property color backgroundColor: isOnSurface ? Theme.color.neutral2 : Theme.color.neutral1
    property var titleTextStyle: Theme.text.subheading
    property var descriptionTextStyle: Theme.text.caption
    property var footerTextStyle: Theme.text.caption

    implicitWidth: 450
    implicitHeight: sectionColumn.implicitHeight

    ColumnLayout {
        id: sectionColumn
        width: root.width
        spacing: sectionHeader.visible || sectionFooter.visible ? root.sectionSpacing : 0

        ColumnLayout {
            id: sectionHeader
            visible: root.title.length > 0 || root.description.length > 0 || headerLoader.active
            Layout.fillWidth: true
            Layout.leftMargin: 4
            Layout.rightMargin: 4
            spacing: 2

            RowLayout {
                visible: root.title.length > 0 || headerLoader.active
                Layout.fillWidth: true
                spacing: 8

                CoreText {
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

                Loader {
                    id: headerLoader
                    Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                    active: root.headerTrailingItem !== null
                    visible: active
                    sourceComponent: root.headerTrailingItem
                }
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

        Rectangle {
            id: card
            objectName: root.objectName.length > 0 ? root.objectName + "Card" : ""
            Layout.fillWidth: true
            implicitHeight: contentColumn.implicitHeight
            radius: root.cornerRadius
            color: root.showBackground ? root.backgroundColor : "transparent"
            clip: true

            SurfaceGradientBorder {
                anchors.fill: parent
                visible: root.showBackground && root.showGradientBorder
                    && Qt.colorEqual(root.backgroundColor, Theme.color.neutral2)
                surfaceColor: card.color
                cornerRadius: card.radius
            }

            Behavior on color {
                ColorAnimation { duration: 150 }
            }

            ColumnLayout {
                id: contentColumn
                anchors.left: parent.left
                anchors.right: parent.right
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
