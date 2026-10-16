// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15

Popup {
    id: root
    property InteractionContext interactionContext: null
    property InteractionContext openingContext: null
    property var opening: null
    property bool guardedOpening: false

    function contextIsValid() {
        return guardedOpening
            ? openingContext !== null && openingContext === interactionContext && openingContext.isValid(opening)
            : interactionContext === null
    }
    onInteractionContextChanged: if (visible) close()

    property Connections lifecycle: Connections {
        target: root
        function onAboutToShow() {
            root.guardedOpening = root.interactionContext !== null
            root.openingContext = root.interactionContext
            root.opening = root.openingContext ? root.openingContext.begin() : null
            if (root.openingContext && !root.opening) {
                Qt.callLater(function() {
                    if (root && !root.contextIsValid()) root.close()
                })
            }
        }
        function onAboutToHide() {
            if (root.openingContext) root.openingContext.invalidate()
        }
    }
    property Connections invalidation: Connections {
        target: root.openingContext
        function onInvalidated() { root.close() }
    }
    property Binding interactionEnabled: Binding {
        target: root.contentItem
        property: "enabled"
        value: root.contextIsValid()
        when: (root.interactionContext !== null || root.guardedOpening)
            && root.visible && root.contentItem !== null
    }
}
