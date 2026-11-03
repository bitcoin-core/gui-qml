// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0

NeutralButton {
    id: root
    property bool keyboardActivation: false
    Keys.onPressed: function(event) {
        keyboardActivation = event.key === Qt.Key_Space || event.key === Qt.Key_Down
        if (event.key === Qt.Key_Down) {
            if (root.opened === undefined || !root.opened) root.clicked()
            event.accepted = true
        }
    }
    TapHandler { onPressedChanged: if (pressed) root.keyboardActivation = false }

    property string subtitle: ""
    property bool opened: false
    property bool active: false
    surfaceLevel: 0
    property int caretSize: 20
    property url caretSource: "image://images/caret-down-medium-filled"
    property int textAlignment: Text.AlignLeft
    property var labelTextStyle: Theme.text.description
    property var subtitleTextStyle: labelTextStyle
    property color subtitleColor: Theme.color.neutral7
    property color caretColor: Theme.color.neutral9
    property color hoverBgColor: Theme.color.surface(surfaceLevel + 2)
    property color defaultBgColor: Theme.color.surface(surfaceLevel + 1)

    textColor: Theme.color.neutral9
    backgroundColor: active ? Theme.color.activeFill : defaultBgColor
    hoverBackgroundColor: active ? Theme.color.activeFill : hoverBgColor
    forceHoverBackground: opened || visualFocus
    showBorder: surfaceLevel > 0
    backgroundRadius: 6
    focusBorderObjectName: "dropdownButtonFocusBorder"
    scale: 1

    implicitHeight: Math.max(30, caretSize + topPadding + bottomPadding)
    leftPadding: 10
    rightPadding: Theme.space.xs
    topPadding: Theme.space.xxs
    bottomPadding: Theme.space.xxs
    hoverEnabled: AppMode.isDesktop
    focusPolicy: Qt.StrongFocus

    Accessible.name: text
    Accessible.description: subtitle

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
                color: root.enabled ? root.active ? Theme.color.activeForeground : root.textColor : Theme.color.disabledContent
                horizontalAlignment: root.textAlignment
                elide: Text.ElideRight
                wrap: false
                textStyle: root.labelTextStyle
            }

            CoreText {
                id: _subtitle
                objectName: "dropdownButtonSubtitle"
                visible: root.subtitle !== ""
                Layout.alignment: Qt.AlignVCenter
                text: root.subtitle
                color: root.enabled ? root.active ? Theme.color.activeForeground : root.subtitleColor : Theme.color.disabledContent
                wrap: false
                textStyle: root.subtitleTextStyle
            }

            Icon {
                id: _caret
                objectName: "dropdownButtonCaret"
                Layout.alignment: Qt.AlignVCenter
                Layout.preferredWidth: root.caretSize
                Layout.preferredHeight: root.caretSize
                source: root.caretSource
                size: root.caretSize
                color: root.enabled ? root.active ? Theme.color.activeForeground : root.caretColor : Theme.color.disabledContent
                rotation: root.opened ? 180 : 0

                Behavior on rotation {
                    enabled: Theme.motion.animated
                    NumberAnimation { duration: Theme.motion.normal; easing.type: Theme.motion.easing.enter }
                }
            }
        }
    }
}
