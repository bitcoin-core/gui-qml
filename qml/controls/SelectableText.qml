// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15

// Read-only text the user can select and copy.
TextEdit {
    id: root

    property var textStyle: Theme.text.description
    property string placeholderText: ""
    property color placeholderTextColor: Theme.color.placeholder

    readOnly: true
    selectByMouse: true
    persistentSelection: false
    activeFocusOnPress: true
    textFormat: TextEdit.PlainText
    wrapMode: TextEdit.Wrap
    horizontalAlignment: TextEdit.AlignLeft
    color: enabled ? Theme.color.neutral9 : Theme.color.disabledContent
    selectionColor: Theme.color.selection
    selectedTextColor: Theme.color.selectedText
    font.family: textStyle ? textStyle.family : Theme.text.family
    font.weight: textStyle ? textStyle.weight : Theme.text.regularWeight
    font.pixelSize: textStyle ? textStyle.pixelSize : 13

    HoverHandler {
        cursorShape: Qt.IBeamCursor
    }

    // Loaded only when used: long log and console views have one per row.
    Loader {
        anchors.fill: parent
        active: root.placeholderText.length > 0
        visible: root.text.length === 0
        sourceComponent: Text {
            text: root.placeholderText
            font: root.font
            color: root.placeholderTextColor
            horizontalAlignment: root.horizontalAlignment
            verticalAlignment: root.verticalAlignment
            wrapMode: root.wrapMode
            Accessible.ignored: true
        }
    }
}
