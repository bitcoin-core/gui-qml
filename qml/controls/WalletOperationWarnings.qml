// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
import QtQuick 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0

ColumnLayout {
    id: root
    property WalletOperationModel operation: null
    property string summaryObjectName: "walletCreationWarnings"
    property bool expanded: false
    visible: summary.text.length > 0
    CoreText {
        id: summary
        objectName: root.summaryObjectName
        Layout.fillWidth: true
        text: root.operation ? root.operation.result.warnings.join("\n") : ""
        textFormat: Text.PlainText
        wrapMode: Text.WordWrap
    }
    LinkButton {
        objectName: root.summaryObjectName + "DetailsButton"
        visible: root.operation && root.operation.result.warningsTruncated
        text: root.expanded ? qsTr("Hide warning details") : qsTr("Show warning details")
        onClicked: root.expanded = !root.expanded
    }
    ListView {
        objectName: root.summaryObjectName + "Details"
        Layout.fillWidth: true
        Layout.preferredHeight: visible ? 120 : 0
        visible: root.expanded && root.operation && root.operation.result.warningsTruncated
        model: root.operation ? root.operation.warningDetails : null
        clip: true
        cacheBuffer: 0
        reuseItems: true
        delegate: CoreText {
            required property string content
            width: ListView.view.width
            text: content
            textFormat: Text.PlainText
            wrapMode: Text.WordWrap
        }
    }
}
