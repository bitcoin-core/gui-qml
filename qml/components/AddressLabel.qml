// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0

import "../controls"

AbstractButton {
    id: root

    property string address: ""
    property bool truncated: false
    property bool truncateWhenNeeded: false
    property bool embedded: false
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
    readonly property bool showCopiedStatus: copiedResetTimer.running

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
        if (root.address === "" || !root.clipboard) return
        root.clipboard.setText(root.address)
        copiedResetTimer.restart()
        root.copied()
    }

    hoverEnabled: AppMode.isDesktop
    enabled: address !== ""
    leftPadding: 8
    rightPadding: 8
    topPadding: 4
    bottomPadding: 4
    implicitHeight: content.implicitHeight + topPadding + bottomPadding
    Accessible.name: showCopiedStatus ? qsTr("Copied") : qsTr("Copy address")

    HoverHandler {
        cursorShape: Qt.PointingHandCursor
    }

    onClicked: copy()

    contentItem: Item {
        id: content
        implicitHeight: Math.max(addressText.paintedHeight, copiedRow.implicitHeight)

        CoreText {
            id: addressText
            objectName: root.objectName + "Value"
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: paintedHeight
            horizontalAlignment: root.textAlignment
            verticalAlignment: Text.AlignTop
            font: root.textStyle.font
            lineHeight: root.textStyle.lineHeight
            lineHeightMode: Text.FixedHeight
            textFormat: Text.RichText
            text: root.formattedText
            wrapMode: root.isTruncated ? Text.NoWrap : Text.WordWrap
            opacity: root.showCopiedStatus ? 0 : 1

            Behavior on opacity {
                NumberAnimation { duration: 150 }
            }
        }

        RowLayout {
            id: copiedRow
            objectName: root.objectName + "CopiedStatus"
            anchors.centerIn: parent
            spacing: 6
            opacity: root.showCopiedStatus ? 1 : 0
            scale: root.showCopiedStatus ? 1 : 0.8

            Behavior on opacity {
                NumberAnimation { duration: 150 }
            }

            Behavior on scale {
                NumberAnimation {
                    duration: 150
                    easing.type: Easing.InOutCubic
                }
            }

            Icon {
                Layout.alignment: Qt.AlignVCenter
                source: "qrc:/icons/copy"
                color: Theme.color.neutral9
                size: 20
            }

            CoreText {
                Layout.alignment: Qt.AlignVCenter
                text: qsTr("Copied")
                color: Theme.color.neutral9
                font: Theme.text.body.font
                lineHeight: Theme.text.body.lineHeight
                lineHeightMode: Text.FixedHeight
            }
        }
    }

    TextMetrics {
        id: fullAddressMetrics
        font: root.textStyle.font
        text: root.chunkedPlainText(root.address)
    }

    background: Rectangle {
        objectName: root.objectName.length > 0 ? root.objectName + "Background" : ""
        radius: 5
        color: root.embedded ? Theme.color.neutral3 : Theme.color.neutral2
        opacity: root.hovered || root.down ? 1 : 0

        Behavior on opacity {
            NumberAnimation { duration: 150 }
        }
    }

    Timer {
        id: copiedResetTimer
        interval: 1000
    }
}
