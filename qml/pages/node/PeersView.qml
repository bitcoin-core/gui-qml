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
    readonly property alias selection: peerSelection
    readonly property alias selectedDetails: peerSelection.selectedDetails

    PeerSelectionModel {
        id: peerSelection
        objectName: "peerSelection"
        source: ApplicationModels.peerListModelProxy
        autoSelectFirst: !splitView.isCompact
        onChanged: if (selectedDetails === null && splitView.isCompact) splitView.showPrimary()
    }

    background: Rectangle { color: Theme.color.background }

    header: NavigationBar2 {
        visible: root.showHeader
        showBackButton: root.showBackButton
        backButtonObjectName: "peersBackButton"
        onBackClicked: root.back()
        title: qsTr("Peers")
    }

    function selectPeer(peerDetails) {
        if (peerSelection.select(peerDetails)) splitView.showDetail()
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
        separatorColor: Theme.color.surfaceDivider

        primaryComponent: Component {
            Peers {
                compact: splitView.isCompact
                showHeader: false
                popupParent: root
                selectedConnectionId: peerSelection.connectionId
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
                }
                CoreText {
                    objectName: "noPeerDetailsLabel"
                    anchors.centerIn: parent
                    visible: root.selectedDetails === null
                    text: ApplicationModels.nodeModel.numPeers === 0
                        ? qsTr("No peers connected")
                        : ApplicationModels.peerListModelProxy.count === 0
                            ? qsTr("No peers match your search or filters")
                            : qsTr("Select a peer to view its details")
                    textStyle: Theme.text.description
                    color: Theme.color.neutral6
                }
            }
        }
    }
}
