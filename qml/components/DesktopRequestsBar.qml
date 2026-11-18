// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0
import "../controls"

Pane {
    id: root
    objectName: "desktopRequestsBar"
    property DesktopIntentModel mailbox: null
    property bool continuationAvailable: false
    signal continueRequested()
    visible: mailbox !== null && (mailbox.pendingCount > 0 || mailbox.hasInspection || mailbox.error.length > 0)
    padding: 12
    contentItem: ColumnLayout {
        spacing: 8
        CoreText {
            Layout.fillWidth: true
            textFormat: Text.PlainText
            text: root.mailbox && root.mailbox.hasInspection
                ? root.mailbox.inspectionText
                : qsTr("Desktop requests waiting: %1").arg(root.mailbox ? root.mailbox.pendingCount : 0)
            wrapMode: Text.WordWrap
        }
        CoreText {
            Layout.fillWidth: true
            textFormat: Text.PlainText
            visible: text.length > 0
            text: root.mailbox ? root.mailbox.error : ""
            wrapMode: Text.WordWrap
        }
        RowLayout {
            Button {
                objectName: "desktopRequestInspect"
                text: qsTr("Inspect")
                visible: root.mailbox !== null && !root.mailbox.hasInspection
                enabled: root.mailbox !== null && root.mailbox.canInspect
                onClicked: root.mailbox.inspectNext()
            }
            Button {
                objectName: "desktopRequestContinue"
                text: qsTr("Use in selected wallet")
                visible: root.mailbox !== null && root.mailbox.hasInspection
                enabled: root.continuationAvailable && root.mailbox !== null && root.mailbox.canContinue
                onClicked: root.continueRequested()
            }
            Button {
                objectName: "desktopRequestCloseInspection"
                text: qsTr("Close inspection")
                visible: root.mailbox !== null && root.mailbox.hasInspection
                onClicked: root.mailbox.closeInspection()
            }
            Button {
                objectName: "desktopRequestDismiss"
                text: root.mailbox && root.mailbox.pendingCount > 0 ? qsTr("Dismiss queued request") : qsTr("Dismiss message")
                enabled: root.mailbox !== null && root.mailbox.canDismiss
                onClicked: root.mailbox.dismiss()
            }
            Button {
                objectName: "desktopRequestReconcile"
                text: qsTr("Check status")
                visible: root.mailbox !== null && root.mailbox.canReconcile
                onClicked: root.mailbox.reconcile()
            }
        }
    }
}
