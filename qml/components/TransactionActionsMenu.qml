// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0

import "../controls"

// The actions on one transaction, shared by its activity row and its
// details page.
ContextMenu {
    id: root

    property string txid: ""
    // The host copies the raw transaction asynchronously, so the hex never
    // passes through QML and a late result cannot target another row.
    property bool showsCopyRawTransaction: false
    property bool copyRawTransactionEnabled: true
    property string itemObjectNamePrefix: "transaction"
    property var clipboard: Clipboard
    readonly property OptionsTransactionLinks transactionLinks: OptionsTransactionLinks {
        source: ApplicationModels.optionsModel
        txid: root.txid
    }

    // Explorer links send the transaction ID to a third party, so the host
    // confirms them.
    signal explorerLinkRequested(string url)
    signal copyRawTransactionRequested()

    ContextMenuButton {
        objectName: root.itemObjectNamePrefix + "CopyTransactionId"
        text: qsTr("Copy transaction ID")
        iconSource: "qrc:/icons/copy.svg"
        onTriggered: root.clipboard.setText(root.txid)
    }
    ContextMenuButton {
        objectName: root.itemObjectNamePrefix + "CopyRawTransaction"
        visible: root.showsCopyRawTransaction
        enabled: root.copyRawTransactionEnabled
        text: qsTr("Copy raw transaction hex")
        iconSource: "qrc:/icons/copy.svg"
        // Request before closing: the host may release per-opening state on close.
        autoClose: false
        onTriggered: {
            root.copyRawTransactionRequested()
            root.close()
        }
    }
    ContextMenuDivider {
        visible: explorerLinkRepeater.count > 0
    }
    // A positioner keeps Repeater delegates out of the Qt 6.4 layout engine.
    // The preferred width stands in for its implicit width, which a Column
    // derives from the (parent-bound) widths of its delegates.
    Column {
        Layout.fillWidth: true
        Layout.preferredWidth: {
            let widest = 0
            for (let i = 0; i < explorerLinkRepeater.count; ++i) {
                const item = explorerLinkRepeater.itemAt(i)
                if (item) widest = Math.max(widest, item.implicitWidth)
            }
            return widest
        }
        visible: explorerLinkRepeater.count > 0

        Repeater {
            id: explorerLinkRepeater
            model: root.transactionLinks ? root.transactionLinks.links : null
            delegate: ContextMenuButton {
                required property int index
                required property string host
                required property string url
                objectName: "transactionExplorerLink_" + index
                width: parent ? parent.width : 0
                text: qsTr("Show in %1").arg(host)
                iconSource: "image://images/export"
                enabled: root.transactionLinks && !root.transactionLinks.pending
                    && root.transactionLinks.error.length === 0
                onTriggered: root.explorerLinkRequested(url)
            }
        }
    }
}
