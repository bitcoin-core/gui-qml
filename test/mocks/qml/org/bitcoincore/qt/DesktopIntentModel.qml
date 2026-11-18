// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
import QtQuick 2.15

// Presentation fixture. The production owner/factory and Native timing are
// exercised separately through the existing C++ unit/integration runners.
QtObject {
    enum State { WaitingReady, Ready, Delivering, ReconcilingDelivery, Stopping, Stopped, Forwarding }
    property int state: DesktopIntentModel.WaitingReady
    property int pendingCount: 0
    property bool canInspect: false
    property bool canDismiss: false
    property bool canReconcile: false
    property string error: ""
    property PaymentImportOperation inspection: null
    readonly property bool hasInspection: inspection !== null
    readonly property bool inspectionPending: inspection !== null && inspection.pending
    readonly property bool canContinue: inspection !== null && !inspection.pending && inspection.state === PaymentImportOperation.Inspecting
    readonly property string inspectionText: inspection ? inspection.address : ""
    property int takenCount: 0
    property var beforeReturn: null
    function takeInspection(receiver) {
        const original = inspection
        if (!original) return null
        inspection = null
        takenCount++
        if (beforeReturn) beforeReturn(receiver)
        return original
    }
    function closeInspection() { if (inspection) inspection.cancel(); inspection = null }
    function inspectNext() {}
    function dismiss() {}
    function reconcile() {}
}
