// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0

AbstractButton {
    id: root

    property string address: ""
    property bool truncated: false
    property bool truncateWhenNeeded: false
    property int surfaceLevel: 1
    property bool interactive: true
    property int leadingCharacterCount: 8
    property int trailingCharacterCount: 8
    property color primaryColor: Theme.color.neutral9
    property color secondaryColor: Theme.color.neutral7
    property var textStyle: Theme.text.monoBody
    property int textAlignment: Text.AlignLeft
    property var clipboard: Clipboard
    readonly property bool isTruncated: truncated
        || (truncateWhenNeeded && fullAddressMetrics.advanceWidth > availableWidth)
    readonly property string displayAddress: isTruncated ? truncatedAddress(address) : address
    readonly property string formattedText: formatAddressRichText(displayAddress)
    readonly property bool showCopiedStatus: interactive && copiedResetTimer.running
    readonly property real displayWidth: Math.ceil(displayAddressMetrics.advanceWidth)
        + leftPadding + rightPadding
    readonly property real naturalWidth: Math.max(
        Math.ceil(fullAddressMetrics.advanceWidth), copiedRow.implicitWidth
    ) + leftPadding + rightPadding

    signal copied()

    function truncatedAddress(value) {
        if (!value) return value

        const retainedCharacters = root.leadingCharacterCount + root.trailingCharacterCount
        if (value.length <= retainedCharacters + 1) return value

        return value.substring(0, root.leadingCharacterCount)
            + "…"
            + value.substring(value.length - root.trailingCharacterCount)
    }

    function formatChunks(value) {
        var html = ""
        for (var i = 0; i < value.length; i += 4) {
            var chunk = value.substring(i, Math.min(i + 4, value.length))
            var color = (Math.floor(i / 4) % 2 === 0)
                ? root.primaryColor
                : root.secondaryColor
            if (i > 0) html += " "
            html += "<nobr><font color=\"" + color + "\">" + chunk + "</font></nobr>"
        }
        return html
    }

    function chunkedPlainText(value) {
        var text = ""
        for (var i = 0; i < value.length; i += 4) {
            if (i > 0) text += " "
            text += value.substring(i, Math.min(i + 4, value.length))
        }
        return text
    }

    function formatAddressRichText(value) {
        if (!value) return ""

        const ellipsisIndex = value.indexOf("…")
        if (ellipsisIndex < 0) return root.formatChunks(value)

        const leading = value.substring(0, ellipsisIndex)
        const trailing = value.substring(ellipsisIndex + 1)
        return root.formatChunks(leading)
            + " <font color=\"" + root.secondaryColor + "\">…</font> "
            + root.formatChunks(trailing)
    }

    function copy() {
        if (!root.interactive || root.address === "" || !root.clipboard) return
        root.clipboard.setText(root.address)
        copiedResetTimer.restart()
        root.copied()
    }

    hoverEnabled: interactive && AppMode.isDesktop
    enabled: interactive && address !== ""
    focusPolicy: interactive ? Qt.StrongFocus : Qt.NoFocus
    leftPadding: Theme.space.m
    rightPadding: Theme.space.m
    topPadding: Theme.space.xs
    bottomPadding: Theme.space.xs
    // The full width, unless always truncated; a narrower width wraps or,
    // with truncateWhenNeeded, truncates.
    implicitWidth: truncated ? displayWidth : naturalWidth
    implicitHeight: content.implicitHeight + topPadding + bottomPadding
    Accessible.role: interactive ? Accessible.Button : Accessible.StaticText
    Accessible.name: address
    Accessible.description: interactive ? qsTr("Copies the address") : ""

    HoverHandler {
        enabled: root.interactive && root.enabled
        cursorShape: Qt.PointingHandCursor
    }

    onClicked: copy()

    FocusBorder {
        objectName: root.objectName.length > 0 ? root.objectName + "FocusBorder" : ""
        visible: root.visualFocus
        targetRadius: Theme.radius.s
        z: 1
    }

    contentItem: Item {
        id: content
        implicitHeight: root.interactive ? Math.max(addressText.paintedHeight, copiedRow.implicitHeight)
            : addressText.paintedHeight

        CoreText {
            id: addressText
            objectName: root.objectName + "Value"
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: paintedHeight
            horizontalAlignment: root.textAlignment
            verticalAlignment: Text.AlignTop
            textStyle: root.textStyle
            textFormat: Text.RichText
            text: root.formattedText
            wrapMode: root.isTruncated ? Text.NoWrap : Text.WordWrap
            opacity: root.showCopiedStatus ? 0 : 1
            Accessible.ignored: true

            Behavior on opacity {
                enabled: Theme.motion.animated
                NumberAnimation { duration: Theme.motion.fade(Theme.motion.normal) }
            }
        }

        RowLayout {
            id: copiedRow
            objectName: root.objectName + "CopiedStatus"
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
                source: "qrc:/icons/copy"
                color: Theme.color.neutral9
                size: Theme.size.icon.l
            }

            CoreText {
                objectName: root.objectName + "CopiedStatusText"
                Layout.alignment: Qt.AlignVCenter
                text: qsTr("Copied")
                color: Theme.color.neutral9
                textStyle: root.textStyle
                Accessible.ignored: true
            }
        }
    }

    TextMetrics {
        id: fullAddressMetrics
        font: root.textStyle.font
        text: root.chunkedPlainText(root.address)
    }

    TextMetrics {
        id: displayAddressMetrics
        font: root.textStyle.font
        text: root.displayAddress.split("…").map(function(part) {
            return root.chunkedPlainText(part)
        }).join(" … ")
    }

    background: Rectangle {
        objectName: root.objectName.length > 0 ? root.objectName + "Background" : ""
        radius: Theme.radius.s
        color: Theme.color.surface(Math.min(root.surfaceLevel + 1, 3))
        opacity: root.interactive && (root.hovered || root.down) ? 1 : 0

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
