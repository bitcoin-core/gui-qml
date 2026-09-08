// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import Qt5Compat.GraphicalEffects
import QtQuick.Layouts 1.15
import QtQuick.Window 2.15
import org.bitcoincore.qt 1.0

Item {
    id: root

    enum DisplayMode {
        None    = 0,
        Sidebar = 1,
        TabBar  = 2
    }

    default property list<QtObject> items
    property int currentIndex: 0
    property int tabBarMaxItems: 5

    property real sidebarWidth: 220
    property real tabBarHeight: 64
    property real chromeMargin: 12

    readonly property var currentTab: _allTabs[currentIndex] || null
    readonly property int widthClass:
        SizeClass.widthClassFor(Window.window ? Window.window.width : root.width)
    readonly property int heightClass:
        SizeClass.heightClassFor(Window.window ? Window.window.height : root.height)
    readonly property bool isCompact: widthClass === SizeClass.compact

    property var _allTabs: []
    property var _sidebarFlat: []
    property var _tabBarVisible: []

    readonly property var _tabBarOverflow:
        _tabBarVisible.length > tabBarMaxItems
            ? _tabBarVisible.slice(tabBarMaxItems - 1)
            : []

    readonly property var _tabBarRendered:
        _tabBarVisible.length > tabBarMaxItems
            ? _tabBarVisible.slice(0, tabBarMaxItems - 1).concat([_moreTab])
            : _tabBarVisible

    function selectTab(tab) {
        var idx = _allTabs.indexOf(tab)
        if (idx >= 0)
            currentIndex = idx

    }

    function _rebuild() {
        var oldTab = _allTabs[currentIndex] || null
        var all = []
        var sidebar = []
        var tabbar = []
        for (var i = 0; i < items.length; ++i) {
            var item = items[i]
            if (!item || item.enabled === false)
                continue

            if (item.kind === "tab") {
                if (!item.actionOnly)
                    all.push(item)

                if (item.displayModes & TabView.Sidebar)
                    sidebar.push(item)

                if (item.displayModes & TabView.TabBar)
                    tabbar.push(item)

            } else if (item.kind === "section") {
                var sectionInSidebar = (item.displayModes & TabView.Sidebar) !== 0
                var sectionInTabBar  = (item.displayModes & TabView.TabBar)  !== 0
                var sectionTabs = item.effectiveTabs !== undefined ? item.effectiveTabs : item.tabs
                for (var j = 0; j < sectionTabs.length; ++j) {
                    var tab = sectionTabs[j]
                    if (!tab || tab.enabled === false || tab.kind !== "tab")
                        continue

                    if (!tab.actionOnly)
                        all.push(tab)

                    if (sectionInSidebar && (tab.displayModes & TabView.Sidebar))
                        sidebar.push(tab)

                    if (sectionInTabBar && (tab.displayModes & TabView.TabBar))
                        tabbar.push(tab)

                }
            }
        }
        all.push(_moreTab)
        _allTabs = all
        _sidebarFlat = sidebar
        _tabBarVisible = tabbar

        if (oldTab && !oldTab.actionOnly) {
            var preservedIdx = all.indexOf(oldTab)
            if (preservedIdx >= 0) {
                if (preservedIdx !== currentIndex)
                    currentIndex = preservedIdx

                return
            }
        }
        if (currentIndex >= _allTabs.length)
            currentIndex = Math.max(0, _allTabs.length - 1)

        _ensureCurrentVisible()
    }

    function _firstSelectableVisible(list) {
        for (var i = 0; i < list.length; ++i) {
            var tab = list[i]
            if (tab && !tab.actionOnly)
                return tab

        }
        return null
    }

    function _ensureCurrentVisible() {
        var current = _allTabs[currentIndex]
        if (!current)
            return

        if (isCompact) {
            if (_tabBarRendered.indexOf(current) < 0) {
                var fallback = _firstSelectableVisible(_tabBarRendered)
                if (fallback)
                    selectTab(fallback)

            }
        } else {
            if (_sidebarFlat.indexOf(current) < 0) {
                var fallback = _firstSelectableVisible(_sidebarFlat)
                if (fallback)
                    selectTab(fallback)

            }
        }
    }

    onCurrentIndexChanged: _emitCurrentTabSelected()

    function _emitCurrentTabSelected() {
        var tab = _allTabs[currentIndex] || null
        if (tab && !tab.actionOnly)
            tab.selected()

    }

    function _connectItemSignals(item) {
        if (!item)
            return

        if (item.enabledChanged)
            item.enabledChanged.connect(_rebuild)

        if (item.displayModesChanged)
            item.displayModesChanged.connect(_rebuild)

        if (item.pinToBottomChanged)
            item.pinToBottomChanged.connect(_rebuild)

    }

    Component.onCompleted: {
        _rebuild()
        _emitCurrentTabSelected()
        for (var i = 0; i < items.length; ++i) {
            var item = items[i]
            if (!item)
                continue

            _connectItemSignals(item)
            if (item.kind === "section") {
                if (item.effectiveTabsChanged)
                    item.effectiveTabsChanged.connect(_rebuild)

                var tabs = item.effectiveTabs !== undefined ? item.effectiveTabs : item.tabs
                for (var j = 0; j < tabs.length; ++j)
                    _connectItemSignals(tabs[j])

            }
        }
    }

    Tab {
        id: _moreTab
        title: qsTr("More")
        iconSource: "image://images/ellipsis"
        contentComponent: Component {
            ListView {
                model: root._tabBarOverflow
                clip: true
                spacing: 0
                delegate: ItemDelegate {
                    width: ListView.view ? ListView.view.width : 0
                    height: 56
                    text: modelData ? modelData.title : ""
                    onClicked: {
                        if (!modelData)
                            return

                        if (modelData.actionOnly)
                            modelData.triggered()
                        else
                            root.selectTab(modelData)

                    }
                }
            }
        }
    }

    StackLayout {
        id: contentStack
        clip: true
        currentIndex: root.currentIndex
        anchors.fill: parent
        anchors.leftMargin: root.isCompact ? 0 : root.sidebarWidth
        anchors.bottomMargin: root.isCompact ? (root.tabBarHeight + root.chromeMargin * 2) : 0

        Repeater {
            model: root._allTabs

            Loader {
                // `tab` exposes the per-Loader Tab for the loaded contentComponent to read from
                // (avoids id-based scope-capture pitfalls inside Instantiator delegate Components).
                property var tab: modelData
                property bool _everActive: false
                active: _everActive || StackLayout.isCurrentItem
                onActiveChanged: {
                    if (active)
                        _everActive = true

                }
                sourceComponent: tab && tab.contentComponent ? tab.contentComponent : null
                source: tab && !tab.contentComponent && tab.contentUrl ? tab.contentUrl : ""
            }
        }
    }

    Loader {
        id: regularChrome
        active: !root.isCompact
        visible: active
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: root.sidebarWidth
        sourceComponent: regularShell
    }

    Loader {
        id: compactChrome
        active: root.isCompact
        visible: active
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: root.chromeMargin
        anchors.rightMargin: root.chromeMargin
        anchors.bottomMargin: root.chromeMargin
        height: root.tabBarHeight
        sourceComponent: compactShell
    }

    Component {
        id: regularShell
        Rectangle {
            color: Theme.color.neutral1

            Rectangle {
                anchors.top: parent.top
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                width: 1
                color: Theme.color.neutral2
                z: 1
            }

            ColumnLayout {
                id: sidebarColumn
                anchors.fill: parent
                anchors.topMargin: 8
                anchors.bottomMargin: 8
                spacing: 2

                Repeater {
                    model: root.items

                    Loader {
                        Layout.fillWidth: true
                        Layout.leftMargin: entry && entry.kind === "tab" ? 8 : 0
                        Layout.rightMargin: entry && entry.kind === "tab" ? 8 : 0
                        readonly property var entry: modelData
                        readonly property bool _isPinned:
                            entry && entry.kind === "tab" && entry.pinToBottom
                        active: entry && entry.enabled !== false
                            && (entry.displayModes & TabView.Sidebar)
                            && !_isPinned
                        visible: active
                        sourceComponent: !active ? null
                            : (entry.kind === "section" ? sidebarSectionDelegate : sidebarTopLevelTab)
                    }
                }

                Item {
                    Layout.fillHeight: true
                    Layout.fillWidth: true
                }

                Repeater {
                    model: root.items

                    Loader {
                        Layout.fillWidth: true
                        Layout.leftMargin: 8
                        Layout.rightMargin: 8
                        readonly property var entry: modelData
                        active: entry && entry.enabled !== false
                            && entry.kind === "tab"
                            && entry.pinToBottom
                            && (entry.displayModes & TabView.Sidebar)
                        visible: active
                        sourceComponent: active ? sidebarTopLevelTab : null
                    }
                }
            }
        }
    }

    Component {
        id: compactShell
        Rectangle {
            color: Theme.color.neutral1
            radius: root.tabBarHeight / 2
            layer.enabled: true
            border.width: 1
            border.color: Theme.color.neutral2

            RowLayout {
                anchors.fill: parent
                spacing: 0

                Repeater {
                    model: root._tabBarRendered

                    Loader {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.preferredWidth: 1
                        readonly property var tabRef: modelData
                        sourceComponent: tabBarButton
                    }
                }
            }
        }
    }

    Component {
        id: sidebarSectionDelegate
        ColumnLayout {
            spacing: 2

            CoreText {
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.topMargin: 14
                Layout.bottomMargin: 4
                text: entry ? entry.title.toUpperCase() : ""
                visible: text !== ""
                horizontalAlignment: Text.AlignLeft
                color: Theme.color.neutral5
                font: Theme.text.caption.font
            }

            Repeater {
                model: entry ? (entry.effectiveTabs !== undefined ? entry.effectiveTabs : entry.tabs) : null

                Loader {
                    Layout.fillWidth: true
                    Layout.leftMargin: 8
                    Layout.rightMargin: 8
                    readonly property var tabRef: modelData
                    active: tabRef && tabRef.enabled !== false && tabRef.kind === "tab"
                        && (tabRef.displayModes & TabView.Sidebar)
                    sourceComponent: active ? sidebarTabButton : null
                }
            }
        }
    }

    Component {
        id: sidebarTopLevelTab
        Loader {
            readonly property var tabRef: entry
            sourceComponent: sidebarTabButton
        }
    }

    Component {
        id: sidebarTabButton
        Button {
            id: sidebarButton
            readonly property bool isActive: root.currentTab === tabRef && !(tabRef && tabRef.actionOnly)

            width: parent ? parent.width : 0
            implicitHeight: 36
            leftPadding: 12
            rightPadding: 12
            topPadding: 0
            bottomPadding: 0
            hoverEnabled: AppMode.isDesktop
            focusPolicy: Qt.StrongFocus
            opacity: tabRef && tabRef.dimmed ? 0.45 : 1

            Behavior on opacity { NumberAnimation { duration: 120 } }

            onClicked: {
                if (!tabRef)
                    return

                if (tabRef.actionOnly)
                    tabRef.triggered()
                else
                    root.selectTab(tabRef)

            }

            background: Rectangle {
                radius: 10
                color: sidebarButton.isActive ? Qt.rgba(Theme.color.orange.r, Theme.color.orange.g, Theme.color.orange.b, 0.3)
                     : (sidebarButton.hovered ? Theme.color.neutral2 : "transparent")
                Behavior on color { ColorAnimation { duration: 120 } }

                FocusBorder {
                    visible: sidebarButton.visualFocus
                    borderRadius: 10
                }
            }

            contentItem: RowLayout {
                spacing: 10

                Item {
                    Layout.preferredWidth: 20
                    Layout.preferredHeight: 20
                    visible: tabRef && tabRef.iconSource !== ""

                    Image {
                        id: sbIconImg
                        anchors.fill: parent
                        source: sidebarButton.isActive && tabRef && tabRef.activeIconSource
                                ? tabRef.activeIconSource
                                : (tabRef ? tabRef.iconSource : "")
                        sourceSize.width: 40
                        sourceSize.height: 40
                        fillMode: Image.PreserveAspectFit
                        smooth: true
                        visible: false
                    }

                    ColorOverlay {
                        anchors.fill: sbIconImg
                        source: sbIconImg
                        color: sidebarButton.isActive ? Theme.color.orange : Theme.color.neutral7
                    }
                }

                CoreText {
                    Layout.fillWidth: true
                    text: tabRef ? tabRef.title : ""
                    color: sidebarButton.isActive ? Theme.color.orange : Theme.color.neutral8
                    horizontalAlignment: Text.AlignLeft
                    font: Qt.font({
                        family: Theme.text.caption.font.family,
                        pixelSize: Theme.text.caption.font.pixelSize,
                        styleName: "Semi Bold"
                    })
                    wrap: false
                    elide: Text.ElideRight
                }
            }
        }
    }

    Component {
        id: tabBarButton
        Button {
            id: tabButton
            readonly property bool isActive:
                root.currentTab === tabRef
                || (tabRef === root._moreTab && root._tabBarOverflow.indexOf(root.currentTab) >= 0)

            padding: 0
            hoverEnabled: AppMode.isDesktop
            focusPolicy: Qt.StrongFocus

            onClicked: {
                if (!tabRef)
                    return

                if (tabRef.actionOnly)
                    tabRef.triggered()
                else
                    root.selectTab(tabRef)

            }

            background: Item {
                Rectangle {
                    id: tbPill
                    anchors.fill: parent
                    anchors.topMargin: 6
                    anchors.bottomMargin: 6
                    anchors.leftMargin: 4
                    anchors.rightMargin: 4
                    radius: height / 2
                    color: tabButton.isActive ? Qt.rgba(Theme.color.orange.r, Theme.color.orange.g, Theme.color.orange.b, 0.3) : "transparent"
                    Behavior on color { ColorAnimation { duration: 120 } }

                    FocusBorder {
                        visible: tabButton.visualFocus
                        borderRadius: tbPill.radius
                    }
                }
            }

            contentItem: ColumnLayout {
                spacing: 1

                Item {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: 24
                    Layout.preferredHeight: 24
                    visible: tabRef && tabRef.iconSource !== ""

                    Image {
                        id: tbIconImg
                        anchors.fill: parent
                        source: tabButton.isActive && tabRef && tabRef.activeIconSource
                                ? tabRef.activeIconSource
                                : (tabRef ? tabRef.iconSource : "")
                        sourceSize.width: 50
                        sourceSize.height: 50
                        fillMode: Image.PreserveAspectFit
                        smooth: true
                        visible: false
                    }

                    ColorOverlay {
                        anchors.fill: tbIconImg
                        source: tbIconImg
                        color: tabButton.isActive ? Theme.color.orange : Theme.color.neutral6
                    }
                }

                CoreText {
                    Layout.alignment: Qt.AlignHCenter
                    text: tabRef ? tabRef.title : ""
                    color: tabButton.isActive ? Theme.color.orange : Theme.color.neutral6
                    font: Qt.font({
                        family: Theme.text.caption.font.family,
                        pixelSize: Theme.text.caption.font.pixelSize,
                        styleName: "Semi Bold"
                    })
                    wrap: false
                }
            }
        }
    }
}
