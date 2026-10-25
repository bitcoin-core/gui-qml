// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15

Item {
    id: root

    enum Column {
        Primary,
        Detail
    }

    property Component primaryComponent
    property Component detailComponent

    // The column shown while compact. Prefer showPrimary() and showDetail(),
    // which also carry keyboard focus over to the column they show.
    property int compactColumn: NavigationSplitView.Primary
    // Equal minimum, preferred and maximum widths give a fixed-width primary column.
    property real primaryMinimumWidth: 280
    property real primaryPreferredWidth: 320
    property real primaryMaximumWidth: 498
    property real primaryWidthRatio: 0.4
    property real detailMinimumWidth: 240
    // 0 leaves the columns to separate themselves by their backgrounds.
    property real separatorWidth: 1
    property color separatorColor: Theme.color.surfaceDivider
    property int transitionDuration: Theme.motion.navigation

    readonly property bool isCompact: SizeClass.isCompact(width)
    readonly property real effectivePrimaryWidth: {
        const preferred = Math.max(root.primaryPreferredWidth, root.width * root.primaryWidthRatio)
        const constrained = Math.min(root.primaryMaximumWidth, Math.max(root.primaryMinimumWidth, preferred))
        return Math.max(0, Math.min(constrained,
            root.width - root.separatorWidth - root.detailMinimumWidth))
    }
    readonly property alias primaryItem: primaryLoader.item
    readonly property alias detailItem: detailLoader.item

    function showPrimary() {
        root.showColumn(NavigationSplitView.Primary, primaryLoader, detailLoader)
    }

    function showDetail() {
        root.showColumn(NavigationSplitView.Detail, detailLoader, primaryLoader)
    }

    function showColumn(column, shownLoader, hiddenLoader) {
        // Read before switching: disabling the hidden column drops its focus.
        const moveFocus = root.isCompact && hiddenLoader.activeFocus
        root.compactColumn = column
        if (moveFocus) (shownLoader.item || shownLoader).forceActiveFocus()
    }

    clip: true

    // States drive these plain flags rather than the loaders' own visible
    // and enabled. Qt records an item's effective visible/enabled when a
    // state applies, so a state entered while an ancestor was hidden would
    // later "restore" the column to hidden.
    QtObject {
        id: columns
        property bool primaryShown: true
        property bool primaryActive: true
        property bool detailShown: true
        property bool detailActive: true
    }

    Loader {
        id: primaryLoader
        objectName: "navigationSplitPrimary"
        sourceComponent: root.primaryComponent
        width: root.isCompact ? root.width : root.effectivePrimaryWidth
        height: root.height
        visible: columns.primaryShown
        enabled: columns.primaryActive
    }

    Rectangle {
        id: separator
        objectName: "navigationSplitSeparator"
        visible: !root.isCompact && root.separatorWidth > 0
        x: primaryLoader.width
        width: root.separatorWidth
        height: root.height
        color: root.separatorColor
    }

    Loader {
        id: detailLoader
        objectName: "navigationSplitDetail"
        sourceComponent: root.detailComponent
        x: root.isCompact ? 0 : primaryLoader.width + root.separatorWidth
        width: root.isCompact
            ? root.width
            : Math.max(0, root.width - primaryLoader.width - root.separatorWidth)
        height: root.height
        visible: columns.detailShown
        enabled: columns.detailActive
    }

    // Compact shows one column. The other is disabled and pushed off-screen
    // (under reduced motion the columns swap in place with a fade), then
    // hidden once the transition ends so it leaves the focus chain and the
    // accessibility tree. Its item stays loaded and keeps its state.
    states: [
        State {
            name: "compactPrimary"
            when: root.isCompact && root.compactColumn === NavigationSplitView.Primary
            PropertyChanges {
                target: detailLoader
                x: root.width
                opacity: Theme.motion.reduced ? 0 : 1
            }
            PropertyChanges {
                target: columns
                detailActive: false
                detailShown: false
            }
        },
        State {
            name: "compactDetail"
            when: root.isCompact && root.compactColumn === NavigationSplitView.Detail
            PropertyChanges {
                target: primaryLoader
                x: -root.width
                opacity: Theme.motion.reduced ? 0 : 1
            }
            PropertyChanges {
                target: columns
                primaryActive: false
                primaryShown: false
            }
        }
    ]

    transitions: [
        Transition {
            from: "compactPrimary"
            to: "compactDetail"
            enabled: Theme.motion.animated && root.transitionDuration > 0

            SequentialAnimation {
                PropertyAction { target: columns; property: "detailShown" }
                ParallelAnimation {
                    NumberAnimation {
                        targets: [primaryLoader, detailLoader]
                        property: "x"
                        duration: Theme.motion.travel(root.transitionDuration)
                        easing.type: Theme.motion.easing.move
                    }
                    NumberAnimation {
                        targets: [primaryLoader, detailLoader]
                        property: "opacity"
                        duration: Theme.motion.fade(root.transitionDuration)
                        easing.type: Theme.motion.easing.move
                    }
                }
                PropertyAction { target: columns; property: "primaryShown" }
            }
        },
        Transition {
            from: "compactDetail"
            to: "compactPrimary"
            enabled: Theme.motion.animated && root.transitionDuration > 0

            SequentialAnimation {
                PropertyAction { target: columns; property: "primaryShown" }
                ParallelAnimation {
                    NumberAnimation {
                        targets: [primaryLoader, detailLoader]
                        property: "x"
                        duration: Theme.motion.travel(root.transitionDuration)
                        easing.type: Theme.motion.easing.move
                    }
                    NumberAnimation {
                        targets: [primaryLoader, detailLoader]
                        property: "opacity"
                        duration: Theme.motion.fade(root.transitionDuration)
                        easing.type: Theme.motion.easing.move
                    }
                }
                PropertyAction { target: columns; property: "detailShown" }
            }
        }
    ]
}
