// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0
import "../../controls"
import "../../components"

Page {
    signal back

    id: root
    objectName: "settingsDebugLog"
    background: null

    property int pendingNewLines: 0
    property int displayedLines: 0
    onPendingNewLinesChanged: if (pendingNewLines > 0) displayedLines = pendingNewLines
    property bool userIsScrolled: false

    Connections {
        target: debugLogModel
        function onNewLinesAdded(count) {
            if (root.userIsScrolled) root.pendingNewLines += count
        }
    }

    // Debounce search text so the C++ filter does not run synchronously on
    // every key press for large log files.
    Timer {
        id: searchDebounce
        interval: 150
        repeat: false
        onTriggered: debugLogModel.filter = searchField.text
    }

    // Periodically refresh the "N min ago" labels in-place.
    Timer {
        interval: 60000
        repeat: true
        running: true
        onTriggered: debugLogModel.updateRelativeTimes()
    }

    property bool showBackButton: true

    header: SettingsHeader {
        title: "debug.log"
        showBackButton: root.showBackButton
        backButtonObjectName: "debugLogBackButton"
        onBack: root.back()
        rightItem: RowLayout {
            spacing: 0

            AbstractButton {
                id: exportBtn
                objectName: "debugLogExportButton"
                implicitWidth: 52
                implicitHeight: 52
                hoverEnabled: true
                focusPolicy: Qt.TabFocus
                Accessible.name: qsTr("Export")
                Accessible.role: Accessible.Button

                background: Rectangle {
                    radius: 5
                    color: exportBtn.hovered ? Theme.color.neutral2
                                             : Theme.color.background
                    Behavior on color { ColorAnimation { duration: 150 } }
                }

                contentItem: Item {
                    Icon {
                        anchors.centerIn: parent
                        source: "image://images/export"
                        color: Theme.color.neutral9
                        size: 28
                    }
                }

                onClicked: debugLogModel.openLogFile()

                HoverHandler { cursorShape: Qt.PointingHandCursor }
            }
        }
    }

    ColumnLayout {
        id: contentLayout
        objectName: "debugLogContentLayout"
        width: Math.max(0, Math.min(parent.width - 40, 600))
        anchors {
            top: parent.top
            bottom: parent.bottom
            horizontalCenter: parent.horizontalCenter
            topMargin: 20
            bottomMargin: 20
        }
        spacing: 0

        RowLayout {
            id: searchRow
            objectName: "debugLogSearchRow"
            Layout.fillWidth: true
            Layout.preferredHeight: 44
            spacing: 10

            Icon {
                objectName: "debugLogSearchIcon"
                source: "image://images/search"
                color: Theme.color.neutral5
                size: 24

                Layout.preferredWidth: 24
                Layout.preferredHeight: 24
                Layout.alignment: Qt.AlignVCenter
            }

            TextField {
                id: searchField
                objectName: "debugLogSearchField"
                Layout.fillWidth: true
                Layout.preferredHeight: 44
                leftPadding: 0
                rightPadding: 0
                topPadding: 0
                bottomPadding: 0
                font: Theme.text.description.font
                color: Theme.color.neutral9
                placeholderTextColor: Theme.color.neutral5
                placeholderText: qsTr("Search...")
                // The page is unloaded whenever another Settings section is
                // selected, while the C++ model intentionally retains its
                // filter. Mirror that retained value on re-entry so the field
                // and the rows cannot disagree.
                text: debugLogModel.filter
                verticalAlignment: TextInput.AlignVCenter
                selectByMouse: true
                Accessible.name: qsTr("Search debug log")
                Accessible.role: Accessible.EditableText
                onTextChanged: searchDebounce.restart()

                background: Item {}
            }

            AbstractButton {
                id: refreshBtn
                objectName: "debugLogRefreshButton"
                Layout.preferredWidth: 20
                Layout.preferredHeight: 20
                implicitWidth: 20
                implicitHeight: 20
                padding: 0
                hoverEnabled: AppMode.isDesktop
                focusPolicy: Qt.TabFocus
                Accessible.name: qsTr("Refresh debug log")
                Accessible.role: Accessible.Button

                background: Item {}

                contentItem: Icon {
                    id: refreshIcon
                    objectName: "debugLogRefreshIcon"
                    source: "image://images/refresh"
                    color: refreshBtn.enabled ? Theme.color.neutral9 : Theme.color.neutral4
                    size: 20
                    opacity: refreshBtn.hovered && refreshBtn.enabled ? 0.75 : 1

                    RotationAnimation on rotation {
                        id: spinAnimation
                        from: 0
                        to: 360
                        duration: 600
                        running: false
                        easing.type: Easing.InOutQuad
                    }
                }

                onClicked: {
                    debugLogModel.refresh()
                    spinAnimation.restart()
                }

                HoverHandler {
                    cursorShape: refreshBtn.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                }
            }
        }

        Separator {
            objectName: "debugLogSearchDivider"
            Layout.fillWidth: true
            Layout.preferredHeight: 1
        }

        DebugLogOutputView {
            id: logView
            objectName: "debugLogListView"
            Layout.fillWidth: true
            Layout.fillHeight: true

            listModel: debugLogModel
            topPadding: 10
            accessibleName: qsTr("Debug log entries")
            autoScrollToBottom: false
            // DebugLogModel renders newest-first at the top, so leaving the
            // beginning is exactly when the "N new entries" pill applies.
            onScrolled: function() {
                // A ListView's content origin is not guaranteed to be zero,
                // particularly with variable-height rows and incremental model
                // changes. Its boundary state is the authoritative answer.
                root.userIsScrolled = !logView.atTop
                if (logView.atTop && root.pendingNewLines > 0) {
                    root.pendingNewLines = 0
                }
            }
        }

        // Reserved slot so the log view's height does not jitter when the
        // "Load more" affordance appears / disappears. Only the button
        // itself toggles visibility.
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 36

            TextButton {
                objectName: "debugLogLoadMoreButton"
                anchors.centerIn: parent
                text: qsTr("Load more")
                textSize: 13
                bold: false
                visible: debugLogModel.hasMoreLines && logView.atBottom
                onClicked: debugLogModel.loadMore()
            }
        }
    }

    Item {
        anchors.fill: contentLayout
        z: 10

        Rectangle {
            id: newEntriesPill
            anchors.horizontalCenter: parent.horizontalCenter
            y: logView.y + 10

            visible: opacity > 0
            opacity: (root.pendingNewLines > 0 && root.userIsScrolled) ? 1.0 : 0.0
            Behavior on opacity { NumberAnimation { duration: 150 } }

            width: 16 + arrowText.implicitWidth + 8 + countText.implicitWidth + 24 + closeText.width + 24
            height: 32
            radius: 16

            Behavior on color { ColorAnimation { duration: 150 } }
            color: pressHandler.pressed ? Theme.color.orangeLight2
                : hoverHandler.hovered ? Theme.color.orangeLight1
                : Theme.color.orange

            Text {
                id: arrowText
                text: "↑"
                color: "white"
                font.pixelSize: 15
                font.family: Theme.text.family
                font.bold: true
                anchors.left: parent.left
                anchors.leftMargin: 16
                anchors.verticalCenter: parent.verticalCenter
            }

            Text {
                id: countText
                text: root.displayedLines === 1
                    ? qsTr("1 new entry")
                    : qsTr("%1 new entries").arg(root.displayedLines)
                color: "white"
                font.pixelSize: 13
                font.family: Theme.text.family
                anchors.left: arrowText.right
                anchors.leftMargin: 8
                anchors.verticalCenter: parent.verticalCenter
            }

            Text {
                id: closeText
                text: "×"
                color: "white"
                font.pixelSize: 20
                font.bold: true
                anchors.right: parent.right
                anchors.rightMargin: 14
                anchors.verticalCenter: parent.verticalCenter
                opacity: closeArea.containsMouse ? 1.0 : 0.85
            }

            MouseArea {
                id: closeArea
                anchors {
                    right: parent.right
                    top: parent.top
                    bottom: parent.bottom
                    rightMargin: 6
                }
                width: 32
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: root.pendingNewLines = 0
            }

            HoverHandler {
                id: hoverHandler
                acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                cursorShape: Qt.PointingHandCursor
            }

            TapHandler {
                id: pressHandler
                acceptedButtons: Qt.LeftButton
                onTapped: {
                    logView.scrollToTop()
                    root.pendingNewLines = 0
                }
            }
        }
    }

    Component.onCompleted: debugLogModel.active = true
    Component.onDestruction: debugLogModel.active = false
}
