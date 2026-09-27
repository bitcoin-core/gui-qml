// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0

AbstractButton {
    id: root

    property string subtitle: ""
    property bool opened: false
    property bool active: false
    property bool isOnSurface: false
    property int caretSize: 20
    property url caretSource: "image://images/caret-down-medium-filled"
    property int textAlignment: Text.AlignLeft
    property var labelTextStyle: Theme.text.description
    property var subtitleTextStyle: labelTextStyle
    property color textColor: Theme.color.neutral9
    property color subtitleColor: Theme.color.neutral7
    property color caretColor: Theme.color.neutral9
    property color hoverBgColor: isOnSurface ? Theme.color.neutral3 : Theme.color.neutral2
    property color defaultBgColor: isOnSurface ? Theme.color.neutral2 : Theme.color.neutral1

    implicitHeight: Math.max(30, caretSize + topPadding + bottomPadding)
    leftPadding: 10
    rightPadding: 4
    topPadding: 2
    bottomPadding: 2
    hoverEnabled: AppMode.isDesktop
    focusPolicy: Qt.StrongFocus

    Accessible.name: text
    Accessible.description: subtitle

    HoverHandler {
        cursorShape: Qt.PointingHandCursor
    }

    contentItem: Item {
        implicitWidth: _row.implicitWidth
        implicitHeight: _row.implicitHeight

        RowLayout {
            id: _row
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            spacing: 5

            CoreText {
                id: _label
                objectName: "dropdownButtonLabel"
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignVCenter
                text: root.text
                color: root.enabled ? root.active ? Theme.color.white : root.textColor : Theme.color.neutral4
                horizontalAlignment: root.textAlignment
                elide: Text.ElideRight
                wrap: false
                font: root.labelTextStyle.font
                lineHeight: root.labelTextStyle.lineHeight
                lineHeightMode: Text.FixedHeight
            }

            CoreText {
                id: _subtitle
                objectName: "dropdownButtonSubtitle"
                visible: root.subtitle !== ""
                Layout.alignment: Qt.AlignVCenter
                text: root.subtitle
                color: root.enabled ? root.subtitleColor : Theme.color.neutral4
                wrap: false
                font: root.subtitleTextStyle.font
                lineHeight: root.subtitleTextStyle.lineHeight
                lineHeightMode: Text.FixedHeight
            }

            Icon {
                id: _caret
                objectName: "dropdownButtonCaret"
                Layout.alignment: Qt.AlignVCenter
                Layout.preferredWidth: root.caretSize
                Layout.preferredHeight: root.caretSize
                source: root.caretSource
                size: root.caretSize
                color: root.enabled ? root.active ? Theme.color.white : root.caretColor : Theme.color.neutral4
                rotation: root.opened ? 180 : 0

                Behavior on rotation {
                    NumberAnimation { duration: 150; easing.type: Easing.OutCubic }
                }
            }
        }
    }

    background: Rectangle {
        radius: 6
        color: root.active ? Theme.color.orange : (root.hovered || root.down || root.visualFocus || root.opened)
            ? root.hoverBgColor
            : root.defaultBgColor

        FocusBorder {
            id: _focusBorder
            objectName: "dropdownButtonFocusBorder"
            visible: root.visualFocus
        }
    }
}
