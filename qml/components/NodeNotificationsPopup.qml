// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../controls"

Popup {
    id: root
    objectName: "nodeNotificationsPopup"
    property var notifications: []
    readonly property int notificationCount: notifications.length
    readonly property int contentMargin: width < 480 ? 16 : 24
    width: Math.min(640, parent ? parent.width - 32 : 640)
    height: Math.min(implicitHeight, parent ? parent.height - 40 : implicitHeight)
    implicitHeight: contentColumn.implicitHeight + topPadding + bottomPadding
    anchors.centerIn: parent
    padding: contentMargin
    modal: true
    dim: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    background: Rectangle { color: Theme.color.neutral1; radius: 16 }
    Overlay.modal: Rectangle { color: Qt.rgba(0, 0, 0, 0.6) }
    enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 180 } }
    exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 150 } }
    onAboutToShow: if (notificationScroll.contentItem) notificationScroll.contentItem.contentY = 0

    contentItem: ColumnLayout {
        id: contentColumn
        spacing: 24
        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            CoreText {
                Layout.minimumWidth: 0
                text: qsTr("Notifications")
                font: Theme.text.title.font
                horizontalAlignment: Text.AlignLeft
                wrap: false
                elide: Text.ElideRight
            }
            Rectangle {
                implicitWidth: Math.max(28, notificationCountText.implicitWidth + 12)
                implicitHeight: implicitWidth
                radius: width / 2
                color: Theme.color.orange
                CoreText {
                    id: notificationCountText
                    anchors.centerIn: parent
                    text: root.notificationCount
                    font: Theme.text.subheading.font
                    color: Theme.color.neutral9
                }
            }
            Item { Layout.fillWidth: true }
            CloseButton {
                objectName: "nodeNotificationsCloseButton"
                Accessible.name: qsTr("Close notifications")
                onClicked: root.close()
            }
        }
        ScrollView {
            id: notificationScroll
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(notificationColumn.implicitHeight, root.parent ? Math.max(80, root.parent.height - 180) : 500)
            contentWidth: availableWidth
            contentHeight: notificationColumn.implicitHeight
            clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            Column {
                id: notificationColumn
                width: notificationScroll.availableWidth
                spacing: 12
                Repeater {
                    model: root.notifications
                    delegate: ToastBanner {
                        id: notificationRow
                        required property var modelData
                        required property int index
                        objectName: "nodeNotificationBanner_" + index
                        width: notificationColumn.width
                        textObjectName: "nodeNotificationText_" + index
                        text: modelData.text
                        iconSource: "image://images/" + modelData.icon
                        tintColor: modelData.color
                        textColor: Theme.color.neutral9
                        backgroundOpacity: 0.12
                    }
                }
                CoreText {
                    width: parent.width
                    visible: root.notifications.length === 0
                    text: qsTr("No current notifications.")
                    font: Theme.text.description.font
                    color: Theme.color.neutral7
                }
            }
        }
    }
}
