// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQml 2.15

QtObject {
    id: root
    property var owner: null
    property var target: null
    property var current: null
    signal invalidated()

    function capture() { return {} }
    function accepts(context) { return true }
    function begin() {
        // Each opening owns its token; reopening never revives old callbacks.
        if (current) current.valid = false
        current = Object.assign({}, capture(), { valid: true })
        if (!isValid(current)) {
            current.valid = false
            current = null
        }
        return current
    }
    function isValid(context) {
        return context !== null && context === current && context.valid && accepts(context)
    }
    function consume() {
        const context = current
        if (!isValid(context)) return null
        current = null
        context.valid = false
        return context
    }
    function invalidate() {
        if (!current) return
        current.valid = false
        current = null
        invalidated()
    }
    onTargetChanged: invalidate()
    onOwnerChanged: invalidate()
    Component.onDestruction: invalidate()

    property Connections ownerChanges: Connections {
        target: root.owner
        ignoreUnknownSignals: true
        function onAboutToHide() { root.invalidate() }
        function onVisibleChanged() { if (!root.owner || !root.owner.visible) root.invalidate() }
        function onDestroyed() { root.invalidate() }
    }
}
