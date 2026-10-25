// Copyright (c) 2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Button {
    id: button
    property string description
    property string errorText: ""
    property bool showErrorText: errorText.length > 0
    property bool recommended: false
    property string image: ""
    property string customDir: ""
    property bool surfaceSelectionStyle: false
    padding: 15
    checkable: true
    hoverEnabled: enabled
    implicitWidth: 450
    background: Rectangle {
        objectName: "optionButtonBackground"
        border.width: button.surfaceSelectionStyle ? 0 : 1
        border.color: !button.enabled ? Theme.color.neutral4 : button.checked ? Theme.color.orange : button.hovered ? Theme.color.neutral9 : Theme.color.neutral5
        radius: 10
        color: button.surfaceSelectionStyle
            ? button.checked
                ? Qt.rgba(Theme.color.orange.r, Theme.color.orange.g, Theme.color.orange.b, 0.15)
                : button.hovered ? Theme.color.neutral2 : Theme.color.neutral1
            : "transparent"

        Behavior on color {
            enabled: Theme.motion.animated
            ColorAnimation { duration: Theme.motion.normal }
        }
        FocusBorder {
            visible: button.enabled && button.visualFocus
            borderRadius: 14
        }
    }

    MouseArea {
        anchors.fill: parent
        enabled: button.enabled
        onClicked: button.clicked()
    }

    contentItem: RowLayout {
        opacity: button.enabled ? 1 : 0.55
        spacing: 3
        Loader {
            active: button.image !== ""
            visible: active
            Layout.rightMargin: 7
            sourceComponent: Button {
                focusPolicy: Qt.NoFocus
                Accessible.ignored: true
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
            CoreText {
                Layout.fillWidth: true
                Layout.preferredWidth: 0
                visible: text.length > 0
                text: button.text
                textStyle: Theme.text.button
                color: button.surfaceSelectionStyle && button.checked ? Theme.color.orange : Theme.color.neutral9
                horizontalAlignment: Text.AlignLeft
            }
            CoreText {
                Layout.fillWidth: true
                Layout.preferredWidth: 0
                visible: text.length > 0
                text: button.description
                textStyle: Theme.text.description
                color: Theme.color.neutral8
                horizontalAlignment: Text.AlignLeft
            }
            Loader {
                Layout.topMargin: 2
                active: button.recommended
                visible: active
                sourceComponent: Rectangle {
                    implicitWidth: recommendedText.implicitWidth + 14
                    implicitHeight: recommendedText.implicitHeight + 4
                    color: Theme.color.neutral9
                    radius: 3

                    Behavior on color {
                        enabled: Theme.motion.animated
                        ColorAnimation { duration: Theme.motion.normal }
                    }

                    CoreText {
                        id: recommendedText
                        anchors.centerIn: parent
                        textStyle: Theme.text.captionStrong
                        color: Theme.color.neutral0
                        text: qsTr("Recommended")
                        wrap: false
                    }
                }
            }
            Loader {
                Layout.topMargin: 12
                Layout.fillWidth: true
                active: button.customDir.length > 0
                visible: active
                sourceComponent: Button {
                    id: container
                    focusPolicy: Qt.NoFocus
                    Accessible.role: Accessible.StaticText
                    Accessible.name: button.customDir
                    background: Rectangle {
                        color: Theme.color.neutral2
                        radius: 5
                    }
                    contentItem: CoreText {
                        textStyle: Theme.text.monoCaption
                        color: Theme.color.neutral9
                        text: button.customDir
                        horizontalAlignment: Text.AlignLeft
                    }
                }
            }
            Loader {
                Layout.topMargin: 3
                Layout.fillWidth: true
                active: button.showErrorText && button.errorText.length > 0
                visible: active
                sourceComponent: CoreText {
                    objectName: "optionButtonErrorText"
                    textStyle: Theme.text.description
                    color: Theme.color.red
                    text: button.errorText
                    horizontalAlignment: Text.AlignLeft
                }
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
                color: button.surfaceSelectionStyle ? Theme.color.orange : Theme.color.neutral9
                size: 24
            }
        }
    }
}
