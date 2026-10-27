// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Window 2.15

import "../controls"

Page {
    id: root

    property var disabledSectionIds: []

    readonly property string currentSectionId: internal.currentSectionId
    readonly property int depth: internal.currentStack ? internal.currentStack.depth : 0
    readonly property bool canGoBack: internal.currentStack ? internal.currentStack.canGoBack : false
    readonly property var currentItem: internal.currentStack ? internal.currentStack.currentItem : null
    readonly property var stack: internal.currentStack
    // Gives each section's root page a Back button that emits rootBackRequested
    // (compact SettingsView, where the sidebar is the previous screen).
    property bool showRootBackButton: false

    signal sectionChanged(string sectionId)
    signal rootBackRequested()

    function configureRootPage(rootPage) {
        if (!rootPage || rootPage.showBackButton === undefined || rootPage.back === undefined) return
        rootPage.showBackButton = Qt.binding(function() { return root.showRootBackButton })
        if (rootPage.backButtonObjectName === "") rootPage.backButtonObjectName = "settingsSectionBackButton"
        rootPage.back.connect(root.rootBackRequested)
    }

    function showSection(sectionId, page, properties) {
        if (!page) {
            if (internal.currentStack) internal.currentStack.visible = false
            internal.currentStack = null
            internal.currentSectionId = ""
            root.sectionChanged("")
            return null
        }

        if (sectionId === root.currentSectionId && internal.currentStack) {
            return internal.currentStack.currentItem
        }

        let nextStack = internal.sectionStacks[sectionId]
        if (!nextStack) {
            nextStack = sectionStackComponent.createObject(stackHost, {
                "sectionId": sectionId
            })
            if (!nextStack) return null

            const nextSectionStacks = {}
            for (const cachedSectionId in internal.sectionStacks) {
                nextSectionStacks[cachedSectionId] = internal.sectionStacks[cachedSectionId]
            }
            nextSectionStacks[sectionId] = nextStack
            internal.sectionStacks = nextSectionStacks
            nextStack.push(page, properties || {}, StackView.Immediate)
            root.configureRootPage(nextStack.currentItem)
        }

        if (internal.currentStack) internal.currentStack.visible = false
        internal.currentStack = nextStack
        internal.currentSectionId = sectionId
        nextStack.visible = true
        root.sectionChanged(sectionId)
        return nextStack.currentItem
    }

    function push(page, properties) {
        if (!page || !internal.currentStack) return null
        return internal.currentStack.push(page, properties || {})
    }

    function pop() {
        if (!internal.currentStack || !internal.currentStack.canGoBack) return null
        return internal.currentStack.pop()
    }

    function sectionDepth(sectionId) {
        const sectionStack = internal.sectionStacks[sectionId]
        return sectionStack ? sectionStack.depth : 0
    }

    function resetSection(sectionId) {
        const sectionStack = internal.sectionStacks[sectionId]
        if (!sectionStack || sectionStack.depth <= 1) return
        sectionStack.pop(null, StackView.Immediate)
    }

    function clear() {
        const sectionStacks = internal.sectionStacks
        if (internal.currentStack) internal.currentStack.visible = false
        internal.currentStack = null
        internal.currentSectionId = ""
        internal.sectionStacks = ({})

        for (const sectionId in sectionStacks) {
            sectionStacks[sectionId].clear(StackView.Immediate)
            sectionStacks[sectionId].destroy()
        }
        root.sectionChanged("")
    }

    function focusReasonFor(item) {
        return item.focusReason === undefined ? Qt.OtherFocusReason : item.focusReason
    }

    function trackEditorFocus() {
        if (!internal.currentStack || !internal.currentStack.enabled) return
        const focused = root.Window.window ? root.Window.window.activeFocusItem : null
        let item = focused
        while (item && item !== internal.currentStack) item = item.parent
        if (!item) {
            internal.focusedEditor = null
        } else if (focused !== internal.focusedEditor) {
            internal.focusedEditor = focused
            internal.editorFocusReason = root.focusReasonFor(focused)
        }
    }

    function restoreEditorFocusReason() {
        const item = internal.focusedEditor
        if (!item || !item.visible || !item.enabled || !item.activeFocus
            || root.focusReasonFor(item) === internal.editorFocusReason) return
        // Disabling a focused editor for a save replaces its focus reason.
        // Restore the ring only if focus has stayed on that same editor.
        const reason = internal.editorFocusReason
        item.focus = false
        item.forceActiveFocus(reason)
    }

    background: null
    clip: true

    QtObject {
        id: internal
        property string currentSectionId: ""
        property var currentStack: null
        property var sectionStacks: ({})
        property Item focusedEditor: null
        property int editorFocusReason: Qt.OtherFocusReason
    }

    Connections {
        target: root.Window.window
        function onActiveFocusItemChanged() { root.trackEditorFocus() }
    }

    Connections {
        target: internal.focusedEditor
        ignoreUnknownSignals: true
        function onFocusReasonChanged() {
            if (internal.currentStack && internal.currentStack.enabled && internal.focusedEditor.enabled) {
                internal.editorFocusReason = root.focusReasonFor(internal.focusedEditor)
            }
        }
    }

    Item {
        id: stackHost
        anchors.fill: parent
    }

    Component {
        id: sectionStackComponent

        PageStack {
            required property string sectionId
            enabled: root.disabledSectionIds.indexOf(sectionId) < 0
            onEnabledChanged: if (enabled) root.restoreEditorFocusReason()
            objectName: "settingsNavigationStack_" + sectionId
            anchors.fill: parent
            visible: false
        }
    }
}
