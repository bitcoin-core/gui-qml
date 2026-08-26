// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

import "../../controls"
import "../../components"

SettingsPage {
    id: root

    objectName: "debugLogView"
    title: qsTr("Debug log")
    showBackButton: false
    maximumContentWidth: width
    contentSpacing: 20

    property bool ownsDebugLogActivity: false
    property bool followNewMessages: true
    property bool followAppend: false
    property int prependAnchorIndex: -1
    property real prependAnchorOffset: 0

    function emptyMessage() {
        if (debugLogModel.filter.length > 0) return qsTr("No log messages match this search")
        if (debugLogModel.warningsAndErrorsOnly) return qsTr("No warnings or errors in the loaded messages")
        return qsTr("No log messages")
    }

    function updateDebugLogActivity() {
        if (root.visible) {
            debugLogModel.active = true
            root.ownsDebugLogActivity = true
        } else {
            if (root.ownsDebugLogActivity) debugLogModel.active = false
            root.ownsDebugLogActivity = false
        }
    }

    function scrollToTop() {
        logList.forceLayout()
        logList.positionViewAtBeginning()
        logList.contentY = logList.originY
        logList.returnToBounds()
    }

    function scrollToBottom() {
        logList.forceLayout()
        logList.positionViewAtEnd()
        logList.returnToBounds()
    }

    function firstVisibleIndex() {
        const firstY = logList.contentY
        for (let offset = 0; offset <= 48; ++offset) {
            const candidate = logList.indexAt(1, firstY + offset)
            if (candidate >= 0) return candidate
        }
        return -1
    }

    PageHeading {
        id: pageHeading
        objectName: "debugLogPageHeading"
        Layout.fillWidth: true
        description: qsTr("Live diagnostic messages from Bitcoin Core.")
    }

    RowLayout {
        id: toolsRow
        objectName: "debugLogToolsRow"
        Layout.fillWidth: true
        spacing: 16

        TextField {
            id: searchField
            objectName: "debugLogSearchField"
            Layout.fillWidth: true
            Layout.minimumWidth: 140
            Layout.maximumWidth: 340
            implicitHeight: 36
            leftPadding: 38
            rightPadding: 12
            topPadding: 0
            bottomPadding: 0
            text: debugLogModel.filter
            placeholderText: qsTr("Search messages")
            placeholderTextColor: Theme.color.neutral7
            color: Theme.color.neutral9
            font: Theme.text.caption.font
            verticalAlignment: TextInput.AlignVCenter
            selectByMouse: true
            Accessible.name: qsTr("Search debug log messages")
            onTextChanged: searchDebounce.restart()

            background: Rectangle {
                color: Theme.color.neutral1
                radius: 8
                border.width: searchField.activeFocus ? 2 : 0
                border.color: Theme.color.orange

                Behavior on border.color { ColorAnimation { duration: 150 } }
            }

            Icon {
                anchors.left: parent.left
                anchors.leftMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                source: "image://images/search"
                color: Theme.color.neutral7
                size: 16
            }
        }

        Item { Layout.fillWidth: true }

        IconButton {
            id: logOptionsButton
            objectName: "debugLogOptionsButton"
            size: 36
            iconSize: 20
            iconSource: "image://images/ellipsis"
            checked: logOptionsMenu.opened
            Accessible.name: qsTr("Debug log options")
            onClicked: {
                if (logOptionsMenu.opened) {
                    logOptionsMenu.close()
                } else {
                    logOptionsMenu.open()
                }
            }
        }

        ContextMenu {
            id: logOptionsMenu
            objectName: "debugLogOptionsMenu"
            parent: logOptionsButton
            x: parent.width - width
            y: parent.height + 2
            modal: true
            dim: false

            ContextMenuPicker {
                id: messageFilterPicker
                objectName: "debugLogMessageFilterPicker"
                objectNameRole: "objectName"
                currentValue: debugLogModel.warningsAndErrorsOnly
                    ? "warnings-and-errors"
                    : "all"
                model: [
                    { text: qsTr("All messages"), value: "all", objectName: "debugLogFilterAllMessages" },
                    { text: qsTr("Warnings and errors"), value: "warnings-and-errors", objectName: "debugLogFilterWarningsAndErrors" }
                ]
                onActivated: function(value) {
                    debugLogModel.warningsAndErrorsOnly = value === "warnings-and-errors"
                    logOptionsMenu.close()
                }
            }

            ContextMenuDivider {
                objectName: "debugLogOptionsDivider"
            }

            ContextMenuButton {
                objectName: "debugLogOpenFileButton"
                text: qsTr("Open debug.log")
                iconSource: "image://images/export"
                onTriggered: debugLogModel.openLogFile()
            }
        }
    }

    Shortcut {
        objectName: "debugLogFindShortcut"
        enabled: root.visible
        sequences: [StandardKey.Find]
        onActivated: {
            searchField.forceActiveFocus()
            searchField.selectAll()
        }
    }

    FormSection {
        id: tableSection
        objectName: "debugLogTableSection"
        Layout.fillWidth: true
        rowSpacing: 0
        backgroundColor: Theme.color.neutral1

        DebugLogTitlesHeader {
            id: titlesHeader
            objectName: "debugLogTitlesHeader"
            Layout.fillWidth: true
        }

        ListView {
            id: logList
            objectName: "debugLogListView"
            Layout.fillWidth: true
            Layout.preferredHeight: Math.max(300, root.height - 294)
            clip: true
            model: debugLogModel
            spacing: 0
            cacheBuffer: 48 * 6
            reuseItems: false
            boundsBehavior: Flickable.StopAtBounds

            header: Item {
                width: logList.width
                height: debugLogModel.hasMoreLines ? 44 : 0

                OutlineButton {
                    objectName: "debugLogLoadMoreButton"
                    anchors.centerIn: parent
                    height: 32
                    visible: parent.height > 0
                    text: qsTr("Load older messages")
                    textFontPixelSize: 13
                    bold: false
                    onClicked: debugLogModel.loadMore()
                }
            }
            headerPositioning: ListView.InlineHeader

            ScrollBar.vertical: ScrollBar {
                policy: ScrollBar.AsNeeded
                minimumSize: 0.05
            }

            onAtYEndChanged: root.followNewMessages = atYEnd

            delegate: DebugLogItemRow {
                required property var model
                required property int index

                objectName: "debugLogItemRow_" + index
                width: logList.width
                alternate: index % 2 === 1
                timestamp: model.timestamp ?? ""
                message: model.message ?? ""
                isError: Boolean(model.isError ?? false)
                isWarning: Boolean(model.isWarning ?? false)
                typeColumnWidth: titlesHeader.typeColumnWidth
                timeColumnWidth: titlesHeader.timeColumnWidth
            }

            CoreText {
                anchors.centerIn: parent
                width: Math.max(0, parent.width - 48)
                visible: logList.count === 0
                text: debugLogModel.openError.length > 0
                    ? debugLogModel.openError
                    : root.emptyMessage()
                color: debugLogModel.openError.length > 0
                    ? Theme.color.red
                    : Theme.color.neutral7
                font: Theme.text.caption.font
                horizontalAlignment: Text.AlignHCenter
                wrap: true
            }
        }

        Rectangle {
            id: tableFooter
            objectName: "debugLogTableFooter"
            Layout.fillWidth: true
            Layout.preferredHeight: 44
            color: Theme.color.neutral3
            radius: 16

            Rectangle {
                objectName: "debugLogTableFooterTopFill"
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                height: parent.radius
                color: parent.color
            }

            OutlineButton {
                id: scrollToBottomButton
                objectName: "debugLogScrollToBottomButton"
                anchors.right: parent.right
                anchors.rightMargin: 16
                anchors.verticalCenter: parent.verticalCenter
                height: 32
                text: qsTr("Scroll to bottom")
                textFontPixelSize: 13
                bold: false
                enabled: logList.count > 0 && !logList.atYEnd
                onClicked: root.scrollToBottom()
            }
        }
    }

    Timer {
        id: searchDebounce
        interval: 150
        repeat: false
        onTriggered: debugLogModel.filter = searchField.text
    }

    Connections {
        target: debugLogModel

        function onRowsAboutToBeInserted(parent, first, last) {
            root.followAppend = first === logList.count && logList.atYEnd
            if (first === 0 && logList.count > 0) {
                logList.forceLayout()
                const anchorIndex = root.firstVisibleIndex()
                const anchorItem = anchorIndex >= 0 ? logList.itemAtIndex(anchorIndex) : null
                if (anchorItem) {
                    root.prependAnchorIndex = anchorIndex + last - first + 1
                    root.prependAnchorOffset = anchorItem.y - logList.contentY
                }
            }
        }

        function onRowsInserted(parent, first, last) {
            if (root.prependAnchorIndex >= 0 && first === 0) {
                Qt.callLater(function() {
                    logList.forceLayout()
                    logList.positionViewAtIndex(root.prependAnchorIndex, ListView.Beginning)
                    logList.forceLayout()
                    const anchorItem = logList.itemAtIndex(root.prependAnchorIndex)
                    if (anchorItem) {
                        logList.contentY = anchorItem.y - root.prependAnchorOffset
                        logList.returnToBounds()
                    }
                    root.prependAnchorIndex = -1
                })
            } else if (root.followAppend) {
                root.followAppend = false
                Qt.callLater(root.scrollToBottom)
            }
        }

        function onModelReset() {
            if (logList.count > 0) Qt.callLater(root.scrollToBottom)
        }
    }

    Component.onCompleted: {
        root.pageHeader.objectName = "debugLogSettingsHeader"
        root.contentLayout.objectName = "debugLogContentLayout"
        root.updateDebugLogActivity()
        if (logList.count > 0) Qt.callLater(root.scrollToBottom)
    }
    onVisibleChanged: root.updateDebugLogActivity()
    Component.onDestruction: {
        if (root.ownsDebugLogActivity) debugLogModel.active = false
    }
}
