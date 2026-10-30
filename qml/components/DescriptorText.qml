// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0

import "../controls"

// Lays out a single-key descriptor one part per line: script, key origin, key, chains, checksum.
// Without parts it shows the raw descriptor. Clicking copies the raw descriptor.
AbstractButton {
    id: root

    property var parts: ({})
    property string descriptor: ""
    property int indent: Theme.space.l
    property int surfaceLevel: 0
    property var textStyle: Theme.text.monoCaption
    property var clipboard: Clipboard
    readonly property bool structured: Object.keys(parts).length > 0
    readonly property bool showCopiedStatus: copiedResetTimer.running
    readonly property string objectNamePrefix: root.objectName.length > 0 ? root.objectName : "descriptorText"

    signal copied()

    function chunkedKey(value) {
        let html = ""
        for (let i = 0; i < value.length; i += 4) {
            const color = Math.floor(i / 4) % 2 === 0 ? Theme.color.neutral9 : Theme.color.neutral7
            if (i > 0) html += " "
            html += "<nobr><font color=\"" + color + "\">" + value.substring(i, i + 4) + "</font></nobr>"
        }
        return html
    }

    function copy() {
        if (root.descriptor === "" || !root.clipboard) return
        root.clipboard.setText(root.descriptor)
        copiedResetTimer.restart()
        root.copied()
    }

    hoverEnabled: AppMode.isDesktop
    enabled: descriptor !== ""
    focusPolicy: Qt.StrongFocus
    padding: Theme.space.m
    implicitHeight: content.implicitHeight + topPadding + bottomPadding
    Accessible.role: Accessible.Button
    Accessible.name: showCopiedStatus ? qsTr("Copied") : qsTr("Copy descriptor")

    onClicked: copy()

    HoverHandler {
        enabled: root.enabled
        cursorShape: Qt.PointingHandCursor
    }

    FocusBorder {
        objectName: root.objectName.length > 0 ? root.objectName + "FocusBorder" : ""
        visible: root.visualFocus
        targetRadius: Theme.radius.s
        z: 1
    }

    component Part: CoreText {
        Layout.fillWidth: true
        textStyle: root.textStyle
        horizontalAlignment: Text.AlignLeft
        Accessible.ignored: true
    }

    contentItem: Item {
        id: content
        implicitHeight: Math.max(partsColumn.implicitHeight, copiedRow.implicitHeight)

        ColumnLayout {
            id: partsColumn
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            spacing: Theme.space.xxs
            opacity: root.showCopiedStatus ? 0 : 1

            Behavior on opacity {
                enabled: Theme.motion.animated
                NumberAnimation { duration: Theme.motion.fade(Theme.motion.normal) }
            }

            Part {
                objectName: root.objectNamePrefix + "Opening"
                visible: root.structured
                text: root.parts.opening || ""
                color: Theme.color.accentText
            }

            Part {
                objectName: root.objectNamePrefix + "Origin"
                visible: root.structured && text !== ""
                Layout.leftMargin: root.indent
                text: root.parts.origin || ""
                color: Theme.color.neutral7
            }

            Part {
                objectName: root.objectNamePrefix + "Key"
                visible: root.structured
                Layout.leftMargin: root.indent
                textFormat: Text.RichText
                text: root.chunkedKey(root.parts.key || "")
            }

            Part {
                objectName: root.objectNamePrefix + "Path"
                visible: root.structured
                Layout.leftMargin: root.indent
                text: root.parts.path || ""
                color: Theme.color.infoText
            }

            Part {
                objectName: root.objectNamePrefix + "Closing"
                visible: root.structured
                textFormat: Text.RichText
                text: "<font color=\"" + Theme.color.accentText + "\">" + (root.parts.closing || "") + "</font>"
                    + (root.parts.checksum ? "<font color=\"" + Theme.color.neutral7 + "\">#" + root.parts.checksum + "</font>" : "")
            }

            Part {
                objectName: root.objectNamePrefix + "Plain"
                visible: !root.structured
                text: root.descriptor
                color: Theme.color.neutral9
                wrapMode: Text.WrapAnywhere
            }
        }

        RowLayout {
            id: copiedRow
            objectName: root.objectNamePrefix + "CopiedStatus"
            anchors.centerIn: parent
            spacing: Theme.space.s
            opacity: root.showCopiedStatus ? 1 : 0
            scale: root.showCopiedStatus || Theme.motion.reduced ? 1 : 0.8

            Behavior on opacity {
                enabled: Theme.motion.animated
                NumberAnimation { duration: Theme.motion.fade(Theme.motion.normal) }
            }

            Behavior on scale {
                enabled: Theme.motion.animated
                NumberAnimation {
                    duration: Theme.motion.normal
                    easing.type: Theme.motion.easing.move
                }
            }

            Icon {
                Layout.alignment: Qt.AlignVCenter
                source: "qrc:/icons/copy.svg"
                color: Theme.color.neutral9
                size: Theme.size.icon.l
            }

            CoreText {
                Layout.alignment: Qt.AlignVCenter
                text: qsTr("Copied")
                color: Theme.color.neutral9
                textStyle: root.textStyle
                Accessible.ignored: true
            }
        }
    }

    background: Rectangle {
        radius: Theme.radius.s
        color: Theme.color.surface(Math.min(root.surfaceLevel + 1, 3))
        opacity: root.hovered || root.down ? 1 : 0

        Behavior on opacity {
            enabled: Theme.motion.animated
            NumberAnimation { duration: Theme.motion.normal }
        }
    }

    Timer {
        id: copiedResetTimer
        interval: 1000
    }
}
