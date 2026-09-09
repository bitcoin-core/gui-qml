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

    property int compactColumn: NavigationSplitView.Primary
    property real primaryMinimumWidth: 280
    property real primaryPreferredWidth: 320
    property real primaryMaximumWidth: 498
    property real primaryWidthRatio: 0.4
    property real detailMinimumWidth: 240
    property real separatorWidth: 1
    property color separatorColor: Theme.color.neutral2
    property int transitionDuration: 300

    readonly property bool isCompact: SizeClass.widthClassFor(width) === SizeClass.compact
    readonly property real effectivePrimaryWidth: {
        const preferred = Math.max(root.primaryPreferredWidth, root.width * root.primaryWidthRatio)
        const constrained = Math.min(root.primaryMaximumWidth, Math.max(root.primaryMinimumWidth, preferred))
        return Math.max(0, Math.min(constrained,
            root.width - root.separatorWidth - root.detailMinimumWidth))
    }
    readonly property alias primaryItem: primaryLoader.item
    readonly property alias detailItem: detailLoader.item

    function showPrimary() {
        root.compactColumn = NavigationSplitView.Primary
    }

    function showDetail() {
        root.compactColumn = NavigationSplitView.Detail
    }

    clip: true

    Loader {
        id: primaryLoader
        objectName: "navigationSplitPrimary"
        sourceComponent: root.primaryComponent
        x: root.isCompact
            ? (root.compactColumn === NavigationSplitView.Primary ? 0 : -root.width)
            : 0
        width: root.isCompact ? root.width : root.effectivePrimaryWidth
        height: root.height
        enabled: !root.isCompact || root.compactColumn === NavigationSplitView.Primary

        Behavior on x {
            enabled: root.isCompact
            NumberAnimation {
                duration: root.transitionDuration
                easing.type: Easing.InOutCubic
            }
        }
    }

    Rectangle {
        id: separator
        objectName: "navigationSplitSeparator"
        visible: !root.isCompact
        x: primaryLoader.width
        width: root.separatorWidth
        height: root.height
        color: root.separatorColor
    }

    Loader {
        id: detailLoader
        objectName: "navigationSplitDetail"
        sourceComponent: root.detailComponent
        x: root.isCompact
            ? (root.compactColumn === NavigationSplitView.Detail ? 0 : root.width)
            : primaryLoader.width + root.separatorWidth
        width: root.isCompact
            ? root.width
            : Math.max(0, root.width - primaryLoader.width - root.separatorWidth)
        height: root.height
        enabled: !root.isCompact || root.compactColumn === NavigationSplitView.Detail

        Behavior on x {
            enabled: root.isCompact
            NumberAnimation {
                duration: root.transitionDuration
                easing.type: Easing.InOutCubic
            }
        }
    }
}
