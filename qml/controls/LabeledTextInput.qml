// Copyright (c) 2024-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0

Item {
    property alias labelText: label.text
    property alias text: input.text
    property alias inputObjectName: input.objectName
    property alias placeholderText: input.placeholderText
    property alias iconSource: icon.source
    property alias customIcon: iconContainer.data
    property alias enabled: input.enabled
    property alias validator: input.validator
    property alias maximumLength: input.maximumLength
    property alias cursorPosition: input.cursorPosition
    property alias inputActiveFocus: input.activeFocus
    property bool interceptPaste: false
    property string paymentUriRestoreText: ""
    // Optional field explanation, offered behind a small info button next to
    // the label (tooltip on hover, press on touch). Only shown while the
    // field is editable, where the explanation matters.
    property string hintText: ""
    property alias hintButtonObjectName: hintButton.objectName

    signal iconClicked
    signal textEdited
    signal editingFinished
    signal inputFocusChanged
    signal pasteRequested()

    function paymentUri(text) {
        const trimmedText = String(text).trim()
        return root.interceptPaste && trimmedText.toLowerCase().startsWith("bitcoin:")
            ? trimmedText
            : ""
    }

    function pastedPaymentUri(previousText, currentText) {
        const clipboardText = Clipboard.text()
        const uri = paymentUri(clipboardText)
        if (uri.length === 0 || previousText === currentText) return ""

        let offset = String(currentText).indexOf(clipboardText)
        while (offset !== -1) {
            const prefix = String(currentText).slice(0, offset)
            const suffix = String(currentText).slice(offset + clipboardText.length)
            if (String(previousText).startsWith(prefix)
                    && String(previousText).endsWith(suffix)
                    && prefix.length + suffix.length <= String(previousText).length) {
                return uri
            }
            offset = String(currentText).indexOf(clipboardText, offset + 1)
        }
        return ""
    }

    function paste() {
        input.paste()
    }

    onPaymentUriRestoreTextChanged: if (root.interceptPaste) input.syncFromModel()

    id: root
    implicitHeight: 56

    CoreText {
        id: label
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        horizontalAlignment: Text.AlignLeft
        width: 128
        font: Theme.text.body.font
        lineHeight: Theme.text.body.lineHeight
        lineHeightMode: Text.FixedHeight
    }

    IconButton {
        id: hintButton
        visible: root.hintText.length > 0 && input.enabled
        anchors.verticalCenter: parent.verticalCenter
        anchors.right: iconContainer.left
        iconSource: "image://images/info-filled"
        iconColor: hovered || pressed ? Theme.color.neutral9 : Theme.color.neutral7
        size: 24
        iconSize: 16
        Accessible.name: root.hintText

        ToolTip {
            id: hintTooltip
            text: root.hintText
            visible: hintButton.hovered || hintButton.pressed
            delay: 100
            padding: 8
            background: Rectangle {
                color: Theme.color.neutral0
                border.color: Theme.color.neutral4
                border.width: 1
                radius: 5
            }
            contentItem: CoreText {
                text: hintTooltip.text
                color: Theme.color.neutral9
                font.pixelSize: 13
                horizontalAlignment: Text.AlignLeft
                wrapMode: Text.WordWrap
                width: Math.min(implicitWidth, 260)
            }
        }
    }

    TextField {
        id: input
        property bool syncingFromModel: false

        function syncFromModel() {
            if (text === root.paymentUriRestoreText) return
            syncingFromModel = true
            text = root.paymentUriRestoreText
            syncingFromModel = false
        }

        function handlePaymentUriInput() {
            const uri = root.pastedPaymentUri(root.paymentUriRestoreText, text)
            if (uri.length === 0) return false
            syncFromModel()
            root.pasteRequested()
            syncFromModel()
            return true
        }

        anchors.left: label.right
        anchors.right: hintButton.visible ? hintButton.left : iconContainer.left
        anchors.verticalCenter: parent.verticalCenter
        leftPadding: 0
        font: Theme.text.body.font
        color: Theme.color.neutral9
        placeholderTextColor: enabled ? Theme.color.neutral7 : Theme.color.neutral4
        background: Item {}
        selectByMouse: true

        Keys.onPressed: (event) => {
            if (root.interceptPaste && event.matches(StandardKey.Paste)) {
                root.pasteRequested()
                event.accepted = true
            }
        }

        Component.onCompleted: if (root.interceptPaste) syncFromModel()
        onTextChanged: if (!syncingFromModel) handlePaymentUriInput()
        onTextEdited: if (!handlePaymentUriInput()) root.textEdited()
        onEditingFinished: root.editingFinished()
        onActiveFocusChanged: root.inputFocusChanged()
    }

    Item {
        id: iconContainer
        anchors.right: parent.right
        anchors.verticalCenter: input.verticalCenter

        Icon {
            id: icon
            source: ""
            color: enabled ? Theme.color.neutral8 : Theme.color.neutral4
            size: 30
            enabled: source != ""
            onClicked: root.iconClicked()
        }
    }
}
