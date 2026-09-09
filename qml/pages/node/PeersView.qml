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

    signal back

    property bool showHeader: true
    property bool showBackButton: true
    property int selectedNodeId: -1
    property PeerDetailsModel selectedDetails

    background: Rectangle { color: Theme.color.neutral0 }

    header: NavigationBar2 {
        visible: root.showHeader
        leftItem: NavButton {
            objectName: "peersBackButton"
            visible: root.showBackButton
            iconSource: "image://images/caret-left"
            text: qsTr("Back")
            onClicked: root.back()
        }
        centerItem: Header { headerBold: true; headerSize: 18; header: qsTr("Peers") }
    }

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

    Component.onCompleted: Qt.callLater(root.reconcileSelection)

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
                showHeader: false
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
