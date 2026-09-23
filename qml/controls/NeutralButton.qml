// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0

AbstractButton {
    id: root

    enum Size { Medium, Large }

    property int buttonSize: NeutralButton.Medium
    readonly property bool large: buttonSize === NeutralButton.Large
    property url iconSource: ""
    property int iconSize: large ? 20 : 15
    property var textStyle: large ? Theme.text.buttonStrong : Theme.text.captionStrong
    property int textFontPixelSize: textStyle.pixelSize
    property color textColor: enabled ? Theme.color.neutral9 : Theme.color.neutral5
    property color backgroundColor: Theme.color.neutral2
    property color hoverBackgroundColor: Theme.color.neutral3
    property bool forceHoverBackground: false
    property bool showBorder: true
    property int backgroundRadius: 5
    property string focusBorderObjectName: ""
    readonly property color currentBackgroundColor: forceHoverBackground || hovered || down
        ? hoverBackgroundColor : backgroundColor
    readonly property bool useNeutral1BorderGradient: Qt.colorEqual(backgroundColor, Theme.color.neutral1)
    readonly property color borderReferenceColor: useNeutral1BorderGradient ? Theme.color.neutral1 : Theme.color.neutral2
    readonly property var borderGradientReference: useNeutral1BorderGradient
        ? Theme.color.neutral1SurfaceBorderGradient : Theme.color.neutral2SurfaceBorderGradient
    readonly property color borderTopReferenceColor: borderGradientReference[0]
    readonly property color borderBottomReferenceColor: borderGradientReference[1]
    readonly property color borderTopColor: shiftedBorderColor(currentBackgroundColor, borderTopReferenceColor)
    readonly property color borderBottomColor: shiftedBorderColor(currentBackgroundColor, borderBottomReferenceColor)

    function shiftedBorderColor(base, referenceBorder) {
        const reference = borderReferenceColor
        return Qt.rgba(
            Math.max(0, Math.min(1, base.r + referenceBorder.r - reference.r)),
            Math.max(0, Math.min(1, base.g + referenceBorder.g - reference.g)),
            Math.max(0, Math.min(1, base.b + referenceBorder.b - reference.b)),
            base.a)
    }

    implicitHeight: large ? 46 : 34
    implicitWidth: contentItem.implicitWidth + leftPadding + rightPadding
    horizontalPadding: large ? 20 : 13
    hoverEnabled: enabled && AppMode.isDesktop
    focusPolicy: Qt.StrongFocus
    scale: enabled && down ? 0.98 : 1
    Accessible.name: text

    Behavior on scale {
        NumberAnimation { duration: 100; easing.type: Easing.OutCubic }
    }

    HoverHandler { cursorShape: Qt.PointingHandCursor }

    contentItem: Item {
        implicitWidth: contentRow.implicitWidth
        implicitHeight: contentRow.implicitHeight

        RowLayout {
            id: contentRow
            anchors.centerIn: parent
            width: Math.min(implicitWidth, parent.width)
            spacing: root.text.length > 0 && root.iconSource.toString().length > 0
                ? (root.large ? 4 : 8) : 0
            Icon {
                visible: root.iconSource.toString().length > 0
                source: root.iconSource
                size: root.iconSize
                color: root.textColor
            }
            CoreText {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                wrap: false
                elide: Text.ElideRight
                visible: root.text.length > 0
                text: root.text
                font: Qt.font({
                    family: root.textStyle.family,
                    styleName: root.textStyle.styleName,
                    pixelSize: root.textFontPixelSize
                })
                color: root.textColor
                horizontalAlignment: Text.AlignHCenter
            }
        }
    }

    background: Rectangle {
        objectName: root.objectName.length > 0 ? root.objectName + "Background" : ""
        radius: root.backgroundRadius
        color: root.currentBackgroundColor
        gradient: Gradient {
            GradientStop {
                position: 0
                color: root.showBorder ? root.borderTopColor : root.currentBackgroundColor
                Behavior on color { ColorAnimation { duration: 150 } }
            }
            GradientStop {
                position: 1
                color: root.showBorder ? root.borderBottomColor : root.currentBackgroundColor
                Behavior on color { ColorAnimation { duration: 150 } }
            }
        }

        Rectangle {
            visible: root.showBorder
            anchors.fill: parent
            anchors.margins: 1
            radius: Math.max(0, parent.radius - 1)
            color: root.currentBackgroundColor
            Behavior on color { ColorAnimation { duration: 150 } }
        }

        Rectangle {
            anchors.fill: parent
            radius: parent.radius
            color: "transparent"
            border.width: root.visualFocus ? 2 : 0
            border.color: Theme.color.orange
            objectName: root.focusBorderObjectName
        }
    }
}
