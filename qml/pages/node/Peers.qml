// Copyright (c) 2023 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../../controls"
import "../../components"

Page {
    background: null
    property alias navLeftDetail: navbar.leftDetail

    header: NavigationBar {
        id: navbar
    }

    CoreText {
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        id: description
        width: Math.min(parent.width - 40, 450)
        text: qsTr("Peers are nodes you are connected to. You want to ensure that you are connected" +
            " to x, y and z, but not a, b, and c. Learn more.")
        font.pixelSize: 13
        color: Theme.color.neutral7
    }

    Flickable {
        id: sortSelection
        anchors.top: description.bottom
        anchors.topMargin: 20
        anchors.horizontalCenter: parent.horizontalCenter
        width: Math.min(parent.width - 40, toggleButtons.width)
        height: toggleButtons.height
        contentWidth: toggleButtons.width
        boundsMovement: width == toggleButtons.width ?
            Flickable.StopAtBounds : Flickable.FollowBoundsBehavior
        RowLayout {
            id: toggleButtons
            spacing: 10
            ToggleButton {
                text: qsTr("ID")
                autoExclusive: true
                checked: true
                onClicked: {
                    peerListModelProxy.sortBy = "nodeId"
                }
            }
            ToggleButton {
                text: qsTr("Direction")
                autoExclusive: true
                onClicked: {
                    peerListModelProxy.sortBy = "direction"
                }
            }
            ToggleButton {
                text: qsTr("User Agent")
                autoExclusive: true
                onClicked: {
                    peerListModelProxy.sortBy = "subversion"
                }
            }
            ToggleButton {
                text: qsTr("Type")
                autoExclusive: true
                onClicked: {
                    peerListModelProxy.sortBy = "connectionType"
                }
            }
            ToggleButton {
                text: qsTr("Ip")
                autoExclusive: true
                onClicked: {
                    peerListModelProxy.sortBy = "address"
                }
            }
            ToggleButton {
                text: qsTr("Network")
                autoExclusive: true
                onClicked: {
                    peerListModelProxy.sortBy = "network"
                }
            }
        }
    }

    ListView {
        id: listView
        clip: true
        width: Math.min(parent.width - 40, 450)
        anchors.top: sortSelection.bottom
        anchors.topMargin: 30
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        model: peerListModelProxy
        spacing: 15

        footer: Loader {
            height: 75
            active: nodeModel.numOutboundPeers < nodeModel.maxNumOutboundPeers
            width: listView.width
            visible: active
            sourceComponent: Item {
                RowLayout {
                    anchors.centerIn: parent
                    spacing: 20
                    PeersIndicator {
                        paused: false
                        numOutboundPeers: nodeModel.numOutboundPeers
                        maxNumOutboundPeers: nodeModel.maxNumOutboundPeers
                    }
                    CoreText {
                        text: qsTr("Looking for %1 more peer(s)").arg(
                                nodeModel.maxNumOutboundPeers - nodeModel.numOutboundPeers)
                        font.pixelSize: 15
                        color: Theme.color.neutral7
                    }
                }
            }
        }

        delegate: Item {
            required property int nodeId;
            required property string address;
            required property string subversion;
            required property string direction;
            required property string connectionType;
            required property string network;
            implicitHeight: 60
            implicitWidth: listView.width

            Connections {
                target: peerListModelProxy
                function onSortByChanged(roleName) {
                    setTextByRole(roleName)
                }
                function onDataChanged(startIndex, endIndex) {
                    setTextByRole(peerListModelProxy.sortBy)
                }
            }

            Component.onCompleted: {
                setTextByRole(peerListModelProxy.sortBy)
            }

            function setTextByRole(roleName) {
                if (roleName == "nodeId") {
                    primary.text = "#" + nodeId
                    secondary.text = direction
                    tertiary.text = address
                    quaternary.text = subversion
                } else if (roleName == "direction") {
                    primary.text = direction
                    secondary.text = "#" + nodeId
                    tertiary.text = address
                    quaternary.text = subversion
                } else if (roleName == "subversion") {
                    primary.text = subversion
                    secondary.text = "#" + nodeId
                    tertiary.text = address
                    quaternary.text = direction
                } else if (roleName == "address") {
                    primary.text = address
                    secondary.text = direction
                    tertiary.text = "#" + nodeId
                    quaternary.text = subversion
                } else if (roleName == "connectionType") {
                    primary.text = connectionType
                    secondary.text = direction
                    tertiary.text = address
                    quaternary.text = subversion
                } else if (roleName == "network") {
                    primary.text = network
                    secondary.text = direction
                    tertiary.text = address
                    quaternary.text = subversion
                } else {
                    primary.text = "#" + nodeId
                    secondary.text = direction
                    tertiary.text = address
                    quaternary.text = subversion
                }
            }

            ColumnLayout {
                anchors.left: parent.left
                CoreText {
                    Layout.alignment: Qt.AlignLeft
                    id: primary
                    font.pixelSize: 18
                    color: Theme.color.neutral9
                }
                CoreText {
                    Layout.alignment: Qt.AlignLeft
                    id: tertiary
                    font.pixelSize: 15
                    color: Theme.color.neutral7
                }
            }
            ColumnLayout {
                anchors.right: parent.right
                CoreText {
                    Layout.alignment: Qt.AlignRight
                    id: secondary
                    font.pixelSize: 18
                    color: Theme.color.neutral9
                }
                CoreText {
                    Layout.alignment: Qt.AlignRight
                    id: quaternary
                    font.pixelSize: 15
                    color: Theme.color.neutral7
                }
            }
            Separator {
                anchors.bottom: parent.bottom
                width: parent.width
            }
        }
    }
}
