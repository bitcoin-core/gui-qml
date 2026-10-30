// Copyright (c) 2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0

// A choice tile. Selection is the caller's: bind `selected` to the model and
// update the model in onClicked. `checked` only mirrors `selected`; a click
// or Space press toggles it, and the binding is put back at once.
AbstractButton {
    id: button
    property bool selected: false
    property string description
    property string errorText: ""
    property bool showErrorText: errorText.length > 0
    property bool recommended: false
    property string image: ""
    property string customDir: ""
    property bool surfaceSelectionStyle: false
    readonly property int cornerRadius: Theme.radius.field

    padding: 15
    checkable: true
    checked: selected
    onToggled: checked = Qt.binding(function() { return button.selected })
    hoverEnabled: enabled && AppMode.isDesktop
    focusPolicy: Qt.StrongFocus
    implicitWidth: 450
    implicitHeight: implicitContentHeight + topPadding + bottomPadding

    Accessible.role: Accessible.RadioButton
    Accessible.name: text
    Accessible.description: description

    HoverHandler {
        enabled: button.enabled && AppMode.isDesktop
        cursorShape: Qt.PointingHandCursor
    }

    background: Rectangle {
        objectName: "optionButtonBackground"
        border.width: button.surfaceSelectionStyle ? 0 : 1
        border.color: !button.enabled ? Theme.color.disabledContent : button.checked ? Theme.color.neutral9 : button.hovered ? Theme.color.neutral7 : Theme.color.neutral5
        radius: button.cornerRadius
        color: button.surfaceSelectionStyle
            ? Qt.tint(Theme.color.surface1, button.down ? Theme.color.surfacePressed
                : button.checked ? Theme.color.surfaceSelected
                : button.hovered ? Theme.color.surfaceHover : "transparent")
            : "transparent"

        Behavior on color {
            enabled: Theme.motion.animated
            ColorAnimation { duration: Theme.motion.normal }
        }
        FocusBorder {
            visible: button.enabled && button.visualFocus
            targetRadius: button.cornerRadius
        }
    }

    contentItem: RowLayout {
        opacity: button.enabled ? 1 : 0.55
        spacing: 3

        Icon {
            visible: button.image !== ""
            Layout.rightMargin: 7
            source: button.image
            size: 40
            color: Theme.color.neutral9
        }

        ColumnLayout {
            spacing: 3
            Layout.fillWidth: true
            CoreText {
                Layout.fillWidth: true
                Layout.preferredWidth: 0
                visible: text.length > 0
                text: button.text
                textStyle: Theme.text.button
                color: Theme.color.neutral9
                horizontalAlignment: Text.AlignLeft
                Accessible.ignored: true
            }
            CoreText {
                Layout.fillWidth: true
                Layout.preferredWidth: 0
                visible: text.length > 0
                text: button.description
                textStyle: Theme.text.description
                color: Theme.color.neutral8
                horizontalAlignment: Text.AlignLeft
                Accessible.ignored: true
            }
            Rectangle {
                visible: button.recommended
                Layout.topMargin: 2
                implicitWidth: recommendedText.implicitWidth + 14
                implicitHeight: recommendedText.implicitHeight + 4
                color: Theme.color.activeFill
                radius: Theme.radius.xs

                Behavior on color {
                    enabled: Theme.motion.animated
                    ColorAnimation { duration: Theme.motion.normal }
                }

                CoreText {
                    id: recommendedText
                    anchors.centerIn: parent
                    textStyle: Theme.text.captionStrong
                    color: Theme.color.activeForeground
                    text: qsTr("Recommended")
                    wrap: false
                }
            }
            Rectangle {
                objectName: "optionButtonCustomDir"
                visible: button.customDir.length > 0
                Layout.topMargin: 12
                Layout.fillWidth: true
                implicitHeight: customDirText.implicitHeight + Theme.space.m * 2
                color: Theme.color.surface2
                radius: Theme.radius.s

                CoreText {
                    id: customDirText
                    anchors.fill: parent
                    anchors.leftMargin: Theme.space.m
                    anchors.rightMargin: Theme.space.m
                    textStyle: Theme.text.monoCaption
                    color: Theme.color.neutral9
                    text: button.customDir
                    horizontalAlignment: Text.AlignLeft
                    wrapMode: Text.WrapAnywhere
                }
            }
            CoreText {
                objectName: "optionButtonErrorText"
                visible: button.showErrorText && button.errorText.length > 0
                Layout.topMargin: 3
                Layout.fillWidth: true
                Layout.preferredWidth: 0
                textStyle: Theme.text.description
                color: Theme.color.dangerText
                text: button.errorText
                horizontalAlignment: Text.AlignLeft
            }
        }
        Item {
            visible: button.checked
            Layout.alignment: Qt.AlignVCenter
            Layout.minimumWidth: visible ? 30 : 0
            Layout.preferredWidth: visible ? 30 : 0
            Layout.maximumWidth: visible ? 30 : 0
            Layout.preferredHeight: 30
            Icon {
                anchors.centerIn: parent
                source: "image://images/check"
                color: Theme.color.neutral9
                size: Theme.size.icon.xl
            }
        }
    }
}
