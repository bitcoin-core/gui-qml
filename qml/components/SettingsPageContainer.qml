// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15

import "../controls"

Page {
    id: root

    readonly property string currentSectionId: internal.currentSectionId
    readonly property int depth: internal.currentStack ? internal.currentStack.depth : 0
    readonly property bool canGoBack: internal.currentStack ? internal.currentStack.canGoBack : false
    readonly property var currentItem: internal.currentStack ? internal.currentStack.currentItem : null
    readonly property var stack: internal.currentStack

    signal sectionChanged(string sectionId)

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

    background: null
    clip: true

    QtObject {
        id: internal
        property string currentSectionId: ""
        property var currentStack: null
        property var sectionStacks: ({})
    }

    Item {
        id: stackHost
        anchors.fill: parent
    }

    Component {
        id: sectionStackComponent

        PageStack {
            required property string sectionId
            objectName: "settingsNavigationStack_" + sectionId
            anchors.fill: parent
            visible: false
        }
    }
}
