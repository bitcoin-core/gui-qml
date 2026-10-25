// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

import "../components"

Popup {
    id: root

    property string title: ""
    property bool keyboardOpening: false
    onAboutToShow: {
        const window = contentItem ? contentItem.Window.window : null
        const item = window ? window.activeFocusItem : null
        keyboardOpening = !!item && (item.visualFocus === true || item.keyboardActivation === true)
    }
    onOpened: if (keyboardOpening) moveFocus("first")
    property int minMenuWidth: 240
    property int itemSpacing: 0
    property int menuPadding: Theme.space.s
    property color backgroundColor: Theme.color.surface1
    readonly property alias titleItem: _title

    default property alias menuItems: _column.data

    padding: menuPadding
    focus: true
    // Menus are modal without a dim: a press outside only closes the menu,
    // so it never also lands on (and reopens from) the trigger.
    modal: true
    dim: false
    // Keep the menu inside the window.
    margins: Theme.space.ml

    // Open below `trigger`, right edges aligned. Margins keep it on screen.
    function openBelow(trigger, gap) {
        const host = root.parent ? root.parent : trigger
        const offset = gap === undefined ? Theme.space.xs : gap
        const point = trigger.mapToItem(host, trigger.width, trigger.height + offset)
        root.x = point.x - root.width
        root.y = point.y
        root.open()
    }

    // Up/Down move between items, Home/End jump to the ends. Pickers and
    // other items that use the arrows themselves accept them first.
    function focusableItems(item, result) {
        for (let i = 0; i < item.children.length; ++i) {
            const child = item.children[i]
            if (!child.visible || !child.enabled) continue
            const takesTab = child.activeFocusOnTab === true
                || (child.focusPolicy !== undefined && (child.focusPolicy & Qt.TabFocus) !== 0)
            if (takesTab) result.push(child)
            else focusableItems(child, result)
        }
        return result
    }

    function moveFocus(step) {
        const items = focusableItems(_column, [])
        if (items.length === 0) return
        let current = -1
        for (let i = 0; i < items.length; ++i) {
            if (items[i].activeFocus) { current = i; break }
        }
        let next
        if (step === "first") next = 0
        else if (step === "last") next = items.length - 1
        else if (current < 0) next = step > 0 ? 0 : items.length - 1
        else next = (current + step + items.length) % items.length
        items[next].forceActiveFocus(Qt.TabFocusReason)
    }

    implicitWidth: Math.max(
        root.minMenuWidth,
        _column.implicitWidth + 2 * root.menuPadding)
    implicitHeight: _column.implicitHeight + 2 * root.menuPadding

    // Pin the actual size to the implicit size. The menu items have fixed-width
    // content (CoreText with wrap: false), so the implicit size is stable, and
    // an explicit width/height stops Qt from re-deriving the Popup width from
    // the contentItem on every polish pass. Without this, the Popup <->
    // ColumnLayout implicit-size round-trip (the layout's fillWidth children
    // feed back into the Popup width) can spin QQuickItem::polish() into an
    // infinite loop on Qt 6.4 when the popup is clamped against a narrow window
    // edge.
    width: implicitWidth
    height: implicitHeight

    background: Rectangle {
        color: root.backgroundColor
        border.color: Theme.color.menuBorder
        border.width: 1
        radius: Theme.radius.m
        SurfaceGradientBorder {
            anchors.fill: parent
            visible: root.modal
            surfaceColor: parent.color
            cornerRadius: parent.radius
        }
    }

    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0.0; to: 1.0; duration: Theme.motion.normal; easing.type: Theme.motion.easing.enter }
    }
    exit: Transition {
        NumberAnimation { property: "opacity"; to: 0.0; duration: Theme.motion.fast; easing.type: Theme.motion.easing.exit }
    }

    contentItem: ColumnLayout {
        id: _column
        spacing: root.itemSpacing
        readonly property bool _contextMenuMarker: true
        function _closeMenu() { root.close() }

        // Takes the keys while no item has focus yet.
        focus: true
        Keys.onDownPressed: root.moveFocus(1)
        Keys.onUpPressed: root.moveFocus(-1)
        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Home) {
                root.moveFocus("first")
                event.accepted = true
            } else if (event.key === Qt.Key_End) {
                root.moveFocus("last")
                event.accepted = true
            }
        }

        CoreText {
            id: _title
            visible: root.title !== ""
            Layout.fillWidth: true
            Layout.leftMargin: 10
            Layout.rightMargin: Theme.space.m
            Layout.topMargin: visible ? 8 : 0
            Layout.bottomMargin: visible ? 4 : 0
            text: root.title
            horizontalAlignment: Text.AlignLeft
            textStyle: Theme.text.captionStrong
            wrap: false
            color: Theme.color.neutral6
        }
    }
}
