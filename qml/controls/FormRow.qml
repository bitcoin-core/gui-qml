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
    property string supportingText: ""
    property string errorText: ""
    property alias leadingItem: leadingContainer.data
    readonly property Item loadedLeadingItem: leadingContainer.children.length > 0 ? leadingContainer.children[0] : null
    property alias trailingItem: trailingContainer.data
    readonly property Item loadedTrailingItem: trailingContainer.children.length > 0 ? trailingContainer.children[0] : null
    property alias bodyItem: bodyContainer.data
    readonly property Item loadedBodyItem: bodyContainer.children.length > 0 ? bodyContainer.children[0] : null
    property bool showDivider: true
    property int minimumRowHeight: description.length > 0 || supportingText.length > 0 || errorText.length > 0 ? 62 : 48
    property int dividerLeftInset: leftPadding
    property int dividerRightInset: rightPadding
    property int contentSpacing: 12
    property int bodySpacing: 8
    property bool showsDisclosureIndicator: false
    property string disclosureIndicatorObjectName: root.objectName.length > 0
        ? root.objectName + "DisclosureIndicator"
        : ""
    property color disclosureIndicatorColor: enabled ? Theme.color.neutral7 : Theme.color.neutral4
    property color titleColor: enabled ? Theme.color.neutral9 : Theme.color.neutral4
    property color descriptionColor: enabled ? Theme.color.neutral7 : Theme.color.neutral4
    property color supportingTextColor: enabled ? Theme.color.blue : Theme.color.neutral4
    property color errorTextColor: enabled ? Theme.color.red : Theme.color.neutral4
    property var titleTextStyle: Theme.text.description
    property var descriptionTextStyle: Theme.text.caption
    property var supportingTextStyle: Theme.text.caption

    Accessible.name: title
    Accessible.description: description
    padding: 0
    leftPadding: 16
    rightPadding: 16
    topPadding: 10
    bottomPadding: 10
    implicitWidth: Math.max(320, contentItem.implicitWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(minimumRowHeight, contentItem.implicitHeight + topPadding + bottomPadding)

    background: Item {
        Rectangle {
            visible: root.showDivider
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.leftMargin: root.dividerLeftInset
            anchors.rightMargin: root.dividerRightInset
            height: 1
            color: Theme.color.neutral2

            Behavior on color {
                ColorAnimation { duration: 150 }
            }
        }
    }

    contentItem: ColumnLayout {
        spacing: root.bodySpacing

        RowLayout {
            Layout.fillWidth: true
            spacing: root.contentSpacing

            RowLayout {
                id: leadingContainer
                visible: children.length > 0
                enabled: root.enabled
                Layout.alignment: Qt.AlignVCenter
                spacing: 0
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                Layout.alignment: Qt.AlignVCenter
                spacing: 2

                CoreText {
                    objectName: root.objectName.length > 0 ? root.objectName + "Title" : ""
                    visible: root.title.length > 0
                    Layout.fillWidth: true
                    text: root.title
                    color: root.titleColor
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
                    color: root.descriptionColor
                    font: root.descriptionTextStyle.font
                    lineHeight: root.descriptionTextStyle.lineHeight
                    lineHeightMode: Text.FixedHeight
                    horizontalAlignment: Text.AlignLeft
                    wrap: true
                }

                CoreText {
                    objectName: root.objectName.length > 0 ? root.objectName + "SupportingText" : ""
                    visible: root.errorText.length > 0 || root.supportingText.length > 0
                    Layout.fillWidth: true
                    text: root.errorText.length > 0 ? root.errorText : root.supportingText
                    color: root.errorText.length > 0 ? root.errorTextColor : root.supportingTextColor
                    font: root.supportingTextStyle.font
                    lineHeight: root.supportingTextStyle.lineHeight
                    lineHeightMode: Text.FixedHeight
                    horizontalAlignment: Text.AlignLeft
                    wrap: true
                }
            }

            RowLayout {
                id: trailingContainer
                visible: children.length > 0
                enabled: root.enabled
                Layout.alignment: Qt.AlignVCenter
                spacing: 0
            }

            CaretRightIcon {
                id: disclosureIcon
                objectName: root.disclosureIndicatorObjectName
                visible: root.showsDisclosureIndicator
                Layout.preferredWidth: visible ? disclosureIcon.size : 0
                Layout.preferredHeight: visible ? disclosureIcon.size : 0
                Layout.alignment: Qt.AlignVCenter
                color: root.disclosureIndicatorColor
            }
        }

        ColumnLayout {
            id: bodyContainer
            visible: children.length > 0
            enabled: root.enabled
            Layout.fillWidth: true
            spacing: 0
        }
    }
}
