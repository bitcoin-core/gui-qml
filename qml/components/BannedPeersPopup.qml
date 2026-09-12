// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0

import "../controls"

Popup {
    id: root
    objectName: "bannedPeersPopup"

    property var model: banListModel
    readonly property bool compact: parent ? parent.width <= SizeClass.compactWidthMax : false
    readonly property color modalOverlayColor: Qt.rgba(0, 0, 0, 0.4)
    property real verticalOffset: 0

    function unbanPeer(row) {
        if (!root.model.unbanAt(row)) {
            unbanActionError.message = qsTr("Could not unban peer. The ban list may have changed.")
            unbanActionError.open()
        }
    }

    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: parent ? Math.round((parent.height - height) / 2) + verticalOffset : verticalOffset
    width: Math.min(640, parent ? parent.width - 40 : 640)
    height: Math.min(implicitHeight, parent ? parent.height - 40 : implicitHeight)
    modal: true
    focus: true
    leftPadding: root.compact ? 24 : 40
    rightPadding: root.compact ? 24 : 40
    topPadding: 30
    bottomPadding: 30
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    enter: Transition {
        NumberAnimation {
            property: "opacity"
            from: 0
            to: 1
            duration: 300
            easing.type: Easing.OutCubic
        }
        NumberAnimation {
            property: "verticalOffset"
            from: -30
            to: 0
            duration: 300
            easing.type: Easing.OutCubic
        }
    }

    exit: Transition {
        NumberAnimation {
            property: "opacity"
            from: 1
            to: 0
            duration: 250
            easing.type: Easing.InCubic
        }
        NumberAnimation {
            property: "verticalOffset"
            from: 0
            to: -30
            duration: 250
            easing.type: Easing.InCubic
        }
    }

    Overlay.modal: Rectangle {
        color: root.modalOverlayColor
        opacity: root.opacity
    }

    background: Rectangle {
        objectName: "bannedPeersPopupSurface"
        color: Theme.color.neutral1
        border.color: Theme.color.neutral2
        border.width: 1
        radius: 10
    }

    contentItem: ColumnLayout {
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.bottomMargin: 12

            Header {
                Layout.fillWidth: true
                header: qsTr("Banned peers")
                headerBold: true
                center: false
            }

            IconButton {
                id: closeButton
                objectName: "bannedPeersCloseButton"
                size: 28
                iconSize: 12
                iconSource: "image://images/cross"
                iconColor: Theme.color.neutral8
                Accessible.name: qsTr("Close")
                background: Rectangle {
                    radius: 5
                    color: closeButton.down || closeButton.hovered
                        ? Theme.color.neutral2 : Theme.color.neutral1
                }
                onClicked: root.close()
            }
        }

        CoreText {
            Layout.fillWidth: true
            Layout.bottomMargin: 20
            text: qsTr("These peers are blocked from connecting to your node.")
            font: Theme.text.description.font
            lineHeight: Theme.text.description.lineHeight
            lineHeightMode: Text.FixedHeight
            color: Theme.color.neutral6
            horizontalAlignment: Text.AlignLeft
            wrapMode: Text.WordWrap
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(360, Math.max(72, bannedPeersList.contentHeight))
            color: Theme.color.neutral2
            radius: 12
            clip: true

            ListView {
                id: bannedPeersList
                objectName: "bannedPeersList"
                anchors.fill: parent
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                model: root.model

                delegate: ItemDelegate {
                    id: bannedPeerRow
                    required property string address
                    required property string banUntil
                    required property int index
                    width: bannedPeersList.width
                    height: 72
                    leftPadding: 16
                    rightPadding: 12
                    topPadding: 10
                    bottomPadding: 10
                    hoverEnabled: AppMode.isDesktop
                    background: Item {
                        Rectangle {
                            anchors.fill: parent
                            anchors.margins: 4
                            radius: 10
                            color: bannedPeerRow.down || bannedPeerRow.hovered
                                ? Theme.color.neutral3 : "transparent"
                        }
                        Separator {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            anchors.leftMargin: 16
                            anchors.rightMargin: 16
                            visible: bannedPeerRow.index < bannedPeersList.count - 1
                            color: Theme.color.neutral3
                        }
                    }
                    contentItem: RowLayout {
                        spacing: 12
                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            spacing: 3
                            CoreText {
                                Layout.fillWidth: true
                                text: bannedPeerRow.address
                                font: Theme.text.monoDescription.font
                                lineHeight: Theme.text.monoDescription.lineHeight
                                lineHeightMode: Text.FixedHeight
                                color: Theme.color.neutral9
                                horizontalAlignment: Text.AlignLeft
                                elide: Text.ElideMiddle
                                wrap: false
                            }
                            CoreText {
                                Layout.fillWidth: true
                                text: qsTr("Until %1").arg(bannedPeerRow.banUntil)
                                font: Theme.text.caption.font
                                lineHeight: Theme.text.caption.lineHeight
                                lineHeightMode: Text.FixedHeight
                                color: Theme.color.neutral6
                                horizontalAlignment: Text.AlignLeft
                                elide: Text.ElideRight
                                wrap: false
                            }
                        }
                        OutlineButton {
                            objectName: "unbanButton_" + bannedPeerRow.index
                            bold: false
                            horizontalPadding: 18
                            text: qsTr("Unban")
                            onClicked: root.unbanPeer(bannedPeerRow.index)
                        }
                    }
                }
            }

            CoreText {
                anchors.centerIn: parent
                visible: bannedPeersList.count === 0
                text: qsTr("No banned peers")
                font: Theme.text.description.font
                color: Theme.color.neutral6
            }
        }
    }

    AlertPopup {
        id: unbanActionError
        objectName: "unbanActionErrorPopup"
        parent: root.parent ? root.parent : root
        title: qsTr("Peer action failed")
        messageObjectName: "actionErrorMessage"

        AlertAction {
            text: qsTr("OK")
            buttonObjectName: "actionErrorCloseButton"
        }
    }
}
