// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15

Control {
    id: root

    property var model: []
    property string textRole: "text"
    property string valueRole: "value"
    property string subtitleRole: ""
    property string iconRole: ""
    property string objectNameRole: ""
    property string subtitleObjectNameRole: ""
    property var currentValue
    property string displayText: ""
    property string placeholderText: ""
    property string menuTitle: ""
    property int minimumMenuWidth: 240
    property int textAlignment: Text.AlignRight
    property int caretSize: 20
    property url selectionIconSource: "image://images/check"
    property int iconSize: 18
    property var labelTextStyle: Theme.text.description
    property bool embedded: false
    readonly property string currentText: displayText.length > 0 ? displayText : _currentText()
    readonly property bool opened: popup.visible

    signal activated(var value)

    function _count() {
        if (root.model === null || root.model === undefined) return 0
        if (root.model.count !== undefined) return root.model.count
        return root.model.length !== undefined ? root.model.length : 0
    }

    function _itemAt(index) {
        if (root.model && typeof root.model.get === "function") return root.model.get(index)
        return root.model[index]
    }

    function _itemText(item) {
        if (typeof item === "object" && item !== null && item[root.textRole] !== undefined) return item[root.textRole]
        return item === undefined || item === null ? "" : String(item)
    }

    function _itemValue(item) {
        if (typeof item === "object" && item !== null && item[root.valueRole] !== undefined) return item[root.valueRole]
        return item
    }

    function _currentText() {
        const count = root._count()
        for (let i = 0; i < count; ++i) {
            const item = root._itemAt(i)
            if (root._itemValue(item) === root.currentValue) return root._itemText(item)
        }
        return root.placeholderText
    }

    function open() { popup.open() }
    function close() { popup.close() }
    function itemAtIndex(index) { return picker.itemAtIndex(index) }

    padding: 0
    implicitWidth: button.implicitWidth
    implicitHeight: button.implicitHeight
    background: null

    contentItem: DropdownButton {
        id: button
        objectName: root.objectName.length > 0 ? root.objectName + "Button" : ""
        enabled: root.enabled && root._count() > 0
        text: root.currentText
        opened: root.opened
        textAlignment: root.textAlignment
        caretSize: root.caretSize
        labelTextStyle: root.labelTextStyle
        defaultBgColor: root.embedded ? Theme.color.neutral2 : Theme.color.background
        hoverBgColor: root.embedded ? Theme.color.neutral3 : Theme.color.neutral2
        onClicked: root.opened ? root.close() : root.open()
    }

    ContextMenu {
        id: popup
        objectName: root.objectName.length > 0 ? root.objectName + "Menu" : ""
        parent: button
        modal: true
        dim: false
        backgroundColor: root.embedded ? Theme.color.neutral2 : Theme.color.neutral1
        minMenuWidth: Math.max(root.minimumMenuWidth, root.width)
        x: button.width - width
        y: button.height + 2

        ContextMenuPicker {
            id: picker
            objectName: root.objectName.length > 0 ? root.objectName + "List" : ""
            title: root.menuTitle
            model: root.model
            textRole: root.textRole
            valueRole: root.valueRole
            subtitleRole: root.subtitleRole
            iconRole: root.iconRole
            objectNameRole: root.objectNameRole
            subtitleObjectNameRole: root.subtitleObjectNameRole
            currentValue: root.currentValue
            selectionIconSource: root.selectionIconSource
            iconSize: root.iconSize
            onActivated: function(value) {
                root.close()
                root.activated(value)
            }
        }
    }
}
