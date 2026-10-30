// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0

import "../controls"

DialogPopup {
    id: root
    objectName: "bannedPeersPopup"

    property var model: banListModel

    function unbanPeer(row) {
        if (!root.model.unbanAt(row)) {
            unbanActionError.message = qsTr("Could not unban peer. The ban list may have changed.")
            unbanActionError.open()
        }
    }

    title: qsTr("Banned peers")
    surfaceObjectName: "bannedPeersPopupSurface"
    implicitWidth: 640

    contentItem: ColumnLayout {
        spacing: 0

        DialogHeader {
            Layout.fillWidth: true
            Layout.bottomMargin: Theme.space.xl
            title: root.title
            description: qsTr("These peers are blocked from connecting to your node.")
            closeButtonObjectName: "bannedPeersCloseButton"
            onCloseRequested: root.close()
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(360, Math.max(72, bannedPeersList.contentHeight))
            color: Theme.color.surface2
            radius: Theme.radius.cardInset
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
                    leftPadding: Theme.space.l
                    rightPadding: Theme.space.ml
                    topPadding: 10
                    bottomPadding: 10
                    hoverEnabled: AppMode.isDesktop
                    background: Item {
                        StateBackground {
                            anchors.fill: parent
                            anchors.margins: Theme.space.xs
                            radius: Theme.radius.sidebarRow
                            stateColor: bannedPeerRow.down ? Theme.color.surfacePressed
                                : bannedPeerRow.hovered ? Theme.color.surfaceHover : "transparent"
                        }
                        Separator {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            anchors.leftMargin: Theme.space.l
                            anchors.rightMargin: Theme.space.l
                            visible: bannedPeerRow.index < bannedPeersList.count - 1
                        }
                    }
                    contentItem: RowLayout {
                        spacing: Theme.space.ml
                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            spacing: Theme.space.xs
                            CoreText {
                                Layout.fillWidth: true
                                text: bannedPeerRow.address
                                textStyle: Theme.text.monoDescription
                                color: Theme.color.neutral9
                                horizontalAlignment: Text.AlignLeft
                                elide: Text.ElideMiddle
                                wrap: false
                            }
                            CoreText {
                                Layout.fillWidth: true
                                text: qsTr("Until %1").arg(bannedPeerRow.banUntil)
                                textStyle: Theme.text.caption
                                color: Theme.color.neutral6
                                horizontalAlignment: Text.AlignLeft
                                elide: Text.ElideRight
                                wrap: false
                            }
                        }
                        NeutralButton {
                            objectName: "unbanButton_" + bannedPeerRow.index
                            buttonSize: NeutralButton.Large
                            textStyle: Theme.text.button
                            horizontalPadding: Theme.space.l
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
                textStyle: Theme.text.description
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
