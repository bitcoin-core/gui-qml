// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
import QtQuick 2.15
import QtQuick.Controls 2.15
import org.bitcoincore.qt 1.0

// Right-click edit menu and middle-click paste for one editor. The editor
// handles pasteRequested(), so it can route a payment URI instead of
// inserting it.
QtObject {
    id: root
    property TextField textInput: null
    property TextArea textEdit: null
    property bool pasteEnabled: true
    property string menuObjectName: ""
    readonly property Item editorItem: textInput && !textEdit ? textInput
        : textEdit && !textInput ? textEdit : null
    readonly property bool editorReady: editorItem !== null
    readonly property bool editorReadOnly: textInput ? textInput.readOnly : textEdit ? textEdit.readOnly : true
    readonly property bool editorHasSelection: textInput ? textInput.selectedText.length > 0
        : textEdit ? textEdit.selectedText.length > 0 : false

    signal pasteRequested(bool primary)

    function pointerPressed(mouse) {
        if (!editorReady) { mouse.accepted = false; return }
        if (mouse.button === Qt.MiddleButton) {
            if (!Clipboard.primarySelectionSupported || !pasteEnabled) { mouse.accepted = false; return }
            editorItem.forceActiveFocus()
            if (textInput) {
                textInput.deselect()
                textInput.cursorPosition = textInput.positionAt(mouse.x, mouse.y, TextInput.CursorBetweenCharacters)
            } else {
                textEdit.deselect()
                textEdit.cursorPosition = textEdit.positionAt(mouse.x, mouse.y)
            }
            pasteRequested(true)
        } else {
            const point = editorItem.mapToItem(menu.parent, mouse.x, mouse.y)
            menu.x = point.x
            menu.y = point.y
            menu.open()
        }
        mouse.accepted = true
    }

    property Menu menu: Menu {
        id: menu
        // Keeps the overlay when the editor leaves the window. A menu whose
        // parent goes null there loses its window while its item is still in
        // the overlay, and a hover event then crashes Qt 6.8.4 and newer.
        readonly property Item editorOverlay: root.editorItem ? root.editorItem.Overlay.overlay : null
        Binding on parent {
            when: menu.editorOverlay !== null
            value: menu.editorOverlay
            restoreMode: Binding.RestoreNone
        }
        objectName: root.menuObjectName
        MenuItem {
            text: qsTr("Cut")
            enabled: root.editorReady && !root.editorReadOnly && root.editorHasSelection
            onTriggered: root.textInput ? root.textInput.cut() : root.textEdit.cut()
        }
        MenuItem {
            text: qsTr("Copy")
            enabled: root.editorHasSelection
            onTriggered: root.textInput ? root.textInput.copy() : root.textEdit.copy()
        }
        MenuItem {
            text: qsTr("Paste")
            objectName: root.menuObjectName + "Paste"
            enabled: root.pasteEnabled && root.editorReady && !root.editorReadOnly
            onTriggered: root.pasteRequested(false)
        }
        MenuItem {
            text: qsTr("Select all")
            enabled: root.editorReady
            onTriggered: root.textInput ? root.textInput.selectAll() : root.textEdit.selectAll()
        }
    }
}
