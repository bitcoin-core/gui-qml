// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Window 2.15

Page {
    id: root

    default property alias content: contentLayout.data

    property bool showBackButton: true
    property string backButtonObjectName: ""
    property string backButtonText: ""
    property alias rightItem: settingsHeader.rightItem
    property real maximumContentWidth: 840
    property real contentHorizontalPadding: Theme.space.pagePadding(width)
    property real contentSpacing: Theme.space.xxl
    property real contentTopPadding: Theme.space.xl
    property real contentBottomPadding: 40

    readonly property alias pageHeader: settingsHeader
    readonly property alias scrollView: scrollView
    readonly property alias contentLayout: contentLayout
    readonly property Flickable scrollArea: scrollView.contentItem

    function ensureFocusedItemVisible() {
        const window = root.Window.window
        const item = window ? window.activeFocusItem : null
        let ancestor = item
        while (ancestor && ancestor !== contentFrame) ancestor = ancestor.parent
        if (!ancestor) return
        const flickable = root.scrollArea
        const position = item.mapToItem(contentFrame, 0, 0)
        const margin = Theme.space.m
        let y = flickable.contentY
        if (position.y - margin < y) y = position.y - margin
        else if (position.y + item.height + margin > y + flickable.height)
            y = position.y + item.height + margin - flickable.height
        flickable.contentY = Math.max(0, Math.min(y, flickable.contentHeight - flickable.height))
    }

    // A timer instead of Qt.callLater: it's destroyed with the page, so it
    // never runs on a page that's gone, which Qt 6.2 would.
    Timer {
        id: focusScrollTimer
        interval: 0
        onTriggered: root.ensureFocusedItemVisible()
    }

    Connections {
        target: root.Window.window
        function onActiveFocusItemChanged() { focusScrollTimer.restart() }
    }

    signal back

    background: null
    padding: 0
    contentWidth: root.maximumContentWidth

    header: SettingsHeader {
        id: settingsHeader
        objectName: "settingsPageHeader"
        title: root.title
        showBackButton: root.showBackButton
        backButtonObjectName: root.backButtonObjectName
        backButtonText: root.backButtonText
        onBack: root.back()
    }

    ScrollView {
        id: scrollView
        objectName: "settingsPageScrollView"
        anchors.fill: parent
        contentWidth: availableWidth
        contentHeight: contentFrame.height
        clip: true
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

        Item {
            id: contentFrame
            width: scrollView.availableWidth
            height: contentLayout.height + root.contentTopPadding + root.contentBottomPadding

            ColumnLayout {
                id: contentLayout
                objectName: "settingsPageContentLayout"
                anchors.top: parent.top
                anchors.topMargin: root.contentTopPadding
                anchors.horizontalCenter: parent.horizontalCenter
                width: Math.max(0, Math.min(
                    parent.width - root.contentHorizontalPadding * 2,
                    root.maximumContentWidth))
                spacing: root.contentSpacing
            }
        }
    }
}
