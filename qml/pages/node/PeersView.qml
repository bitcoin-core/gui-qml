// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import org.bitcoincore.qt 1.0
import "../../controls"

Page {
    id: root
    objectName: "peers"

    property bool showHeader: true
    property int selectedNodeId: -1
    property PeerDetailsModel selectedDetails

    background: Rectangle { color: Theme.color.neutral0 }

    function selectPeer(peerDetails) {
        if (!peerDetails) return
        selectedDetails = peerDetails
        selectedNodeId = peerDetails.nodeId
        splitView.showDetail()
    }

    function reconcileSelection() {
        if (selectedNodeId >= 0 && peerListModelProxy.indexOfNodeId(selectedNodeId) >= 0) return

        selectedNodeId = -1
        selectedDetails = null
        if (!splitView.isCompact && peerListModelProxy.count > 0) {
            selectPeer(peerListModelProxy.peerDetailsAt(0))
        } else if (splitView.isCompact) {
            splitView.showPrimary()
        }
    }

    Component.onCompleted: {
        if (visible) peerTableModel.startAutoRefresh()
        Qt.callLater(root.reconcileSelection)
    }
    onVisibleChanged: {
        if (visible) peerTableModel.startAutoRefresh()
        else peerTableModel.stopAutoRefresh()
    }
    Component.onDestruction: if (visible) peerTableModel.stopAutoRefresh()

    Connections {
        target: peerListModelProxy
        function onCountChanged() { Qt.callLater(root.reconcileSelection) }
    }

    NavigationSplitView {
        id: splitView
        objectName: "peersNavigationSplitView"
        anchors.fill: parent
        primaryMinimumWidth: 320
        primaryPreferredWidth: 430
        primaryMaximumWidth: 498
        primaryWidthRatio: 0.38
        detailMinimumWidth: 280
        separatorColor: Theme.color.neutral2

        onIsCompactChanged: {
            if (!isCompact) Qt.callLater(root.reconcileSelection)
        }

        primaryComponent: Component {
            Peers {
                compact: splitView.isCompact
                header: Item {
                    visible: root.showHeader
                    implicitHeight: nodeBackButton.implicitHeight + 12

                    NavButton {
                        id: nodeBackButton
                        objectName: "peersNodeBackButton"
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.leftMargin: 12
                        anchors.topMargin: 8
                        iconSource: "image://images/caret-left"
                        //: Back button in the peer list pane. Returns to the Node overview.
                        text: qsTr("Node")
                        onClicked: if (root.StackView.view) root.StackView.view.goBack()
                    }
                }
                popupParent: root
                selectedNodeId: root.selectedNodeId
                onPeerSelected: (peerDetails) => root.selectPeer(peerDetails)
            }
        }

        detailComponent: Component {
            Item {
                PeerDetails {
                    anchors.fill: parent
                    visible: root.selectedDetails !== null
                    compact: splitView.isCompact
                    popupParent: root
                    details: root.selectedDetails
                    onBack: splitView.showPrimary()
                    onPeerDisconnected: (nodeId) => {
                        if (nodeId === root.selectedNodeId) {
                            root.selectedNodeId = -1
                            root.selectedDetails = null
                            if (splitView.isCompact) splitView.showPrimary()
                            Qt.callLater(root.reconcileSelection)
                        }
                    }
                }
                CoreText {
                    objectName: "noPeerDetailsLabel"
                    anchors.centerIn: parent
                    visible: root.selectedDetails === null
                    text: nodeModel.numPeers === 0
                        ? qsTr("No peers connected")
                        : peerListModelProxy.count === 0
                            ? qsTr("No peers match your search or filters")
                            : qsTr("Select a peer to view its details")
                    font: Theme.text.description.font
                    color: Theme.color.neutral6
                }
            }
        }
    }
}
