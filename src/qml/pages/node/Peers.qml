// Copyright (c) 2023 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import Qt.labs.settings 1.0
import org.bitcoincore.qt 1.0
import "../../controls"
import "../../components"

Page {
    signal back
    signal peerSelected(PeerDetailsModel peerDetails)

    id: root
    background: null

    header: NavigationBar2 {
        leftItem: NavButton {
            iconSource: "image://images/caret-left"
            text: qsTr("Back")
            onClicked: root.back()
        }
        centerItem: Header {
            headerBold: true
            headerSize: 18
            header: qsTr("Peers")
        }
    }

    Settings {
        id: settings
        property string peerListSortBy: "nodeId"
    }

    Component.onCompleted: {
        peerListModelProxy.sortBy = settings.peerListSortBy
    }

    ListView {
        id: listView
        clip: true
        width: Math.min(parent.width - 40, 450)
        height: parent.height
        anchors.horizontalCenter: parent.horizontalCenter
        model: peerListModelProxy
        spacing: 15

        header: ColumnLayout {
            spacing: 20
            width: parent.width
            CoreText {
                id: description
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignHCenter
                text: qsTr("Peers are nodes you exchange data with.")
                font.pixelSize: 13
                color: Theme.color.neutral7
            }

            Flickable {
                id: sortSelection
                Layout.fillWidth: true
                Layout.bottomMargin: 30
                Layout.alignment: Qt.AlignHCenter
                height: toggleButtons.height
                contentWidth: toggleButtons.width
                boundsMovement: width == toggleButtons.width ?
                    Flickable.StopAtBound : Flickable.FollowBoundsBehavior
                RowLayout {
                    id: toggleButtons
                    spacing: 10
                    ToggleButton {
                        text: qsTr("ID")
                        autoExclusive: true
                        checked: settings.peerListSortBy === "nodeId"
                        onClicked: {
                            peerListModelProxy.sortBy = "nodeId"
                            settings.peerListSortBy = "nodeId"
                        }
                    }
                    ToggleButton {
                        text: qsTr("Direction")
                        autoExclusive: true
                        checked: settings.peerListSortBy === "direction"
                        onClicked: {
                            peerListModelProxy.sortBy = "direction"
                            settings.peerListSortBy = "direction"
                        }
                    }
                    ToggleButton {
                        text: qsTr("User Agent")
                        autoExclusive: true
                        checked: settings.peerListSortBy === "subversion"
                        onClicked: {
                            peerListModelProxy.sortBy = "subversion"
                            settings.peerListSortBy = "subversion"
                        }
                    }
                    ToggleButton {
                        text: qsTr("Type")
                        autoExclusive: true
                        checked: settings.peerListSortBy === "connectionType"
                        onClicked: {
                            peerListModelProxy.sortBy = "connectionType"
                            settings.peerListSortBy = "connectionType"
                        }
                    }
                    ToggleButton {
                        text: qsTr("Ip")
                        autoExclusive: true
                        checked: settings.peerListSortBy === "address"
                        onClicked: {
                            peerListModelProxy.sortBy = "address"
                            settings.peerListSortBy = "address"
                        }
                    }
                    ToggleButton {
                        text: qsTr("Network")
                        autoExclusive: true
                        checked: settings.peerListSortBy === "network"
                        onClicked: {
                            peerListModelProxy.sortBy = "network"
                            settings.peerListSortBy = "network"
                        }
                    }
                }
            }
        }

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

        delegate: ItemDelegate {
            id: delegate
            required property int nodeId;
            required property string address;
            required property string subversion;
            required property string direction;
            required property string connectionType;
            required property string network;
            required property PeerDetailsModel stats;
            readonly property color stateColor: {
                if (delegate.down) {
                    return Theme.color.orange
                } else if (delegate.hovered) {
                    return Theme.color.orangeLight1
                }
                return Theme.color.neutral9
            }
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
            leftPadding: 0
            rightPadding: 0
            topPadding: 0
            bottomPadding: 14
            width: listView.width
            background: Item {
                Separator {
                    anchors.bottom: parent.bottom
                    width: parent.width
                }
            }
            onClicked: {
                root.peerSelected(stats)
            }
            contentItem: ColumnLayout {
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 15
                    CoreText {
                        Layout.alignment: Qt.AlignLeft
                        Layout.fillWidth: true
                        Layout.preferredWidth: 0
                        id: primary
                        font.pixelSize: 18
                        color: delegate.stateColor
                        elide: Text.ElideMiddle
                        wrapMode: Text.NoWrap
                        horizontalAlignment: Text.AlignLeft
                    }
                    CoreText {
                        Layout.alignment: Qt.AlignRight
                        id: secondary
                        font.pixelSize: 18
                        color: delegate.stateColor
                    }
                }
                RowLayout {
                    CoreText {
                        Layout.alignment: Qt.AlignLeft
                        Layout.fillWidth: true
                        Layout.preferredWidth: 0
                        id: tertiary
                        font.pixelSize: 15
                        color: Theme.color.neutral7
                        elide: Text.ElideMiddle
                        wrapMode: Text.NoWrap
                        horizontalAlignment: Text.AlignLeft
                    }
                    CoreText {
                        Layout.alignment: Qt.AlignRight
                        id: quaternary
                        font.pixelSize: 15
                        color: Theme.color.neutral7
                    }
                }
            }
        }
    }
}
