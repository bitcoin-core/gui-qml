// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15

import "../controls"
import "../controls/utils.js" as Utils

// Node state shared by the block clock, its status bar and Node details.
QtObject {
    id: root

    property var nodeModelRef: typeof nodeModel !== "undefined" ? nodeModel : null
    property var chainModelRef: typeof chainModel !== "undefined" ? chainModel : null
    property var blockClockModelRef: typeof blockClockModel !== "undefined" ? blockClockModel : null
    property var networkStatusModelRef: typeof networkStatusModel !== "undefined" ? networkStatusModel : null

    readonly property bool connected: nodeModelRef !== null && nodeModelRef.numPeers > 0
    readonly property bool synced: nodeModelRef !== null && nodeModelRef.initialSyncComplete
    readonly property bool paused: nodeModelRef !== null && nodeModelRef.pause
    readonly property bool faulted: nodeModelRef !== null && nodeModelRef.faulted
    readonly property bool offline: networkStatusModelRef !== null && networkStatusModelRef.networkOffline
    readonly property bool headerSyncActive: nodeModelRef !== null && nodeModelRef.headerSyncActive
    // Pause and resume run asynchronously: offer them once the node accepts
    // network commands, and not again while one is in flight.
    readonly property bool networkActionReady: nodeModelRef !== null && nodeModelRef.networkActionReady === true
    readonly property bool networkActionPending: nodeModelRef !== null && nodeModelRef.networkActionPending === true
    readonly property string networkActionError: nodeModelRef !== null && nodeModelRef.networkActionError !== undefined
        ? nodeModelRef.networkActionError : ""

    // One of: error, offline, paused, connecting, headers, blocks, synced.
    readonly property string key: faulted ? "error"
        : offline ? "offline"
        : paused ? "paused"
        : !connected ? "connecting"
        : synced ? "synced"
        : headerSyncActive ? "headers" : "blocks"

    // Warnings the user has not dismissed. Dismissed ones stay out of the
    // count, the banner and the status tone until their text changes.
    readonly property var activeWarnings: nodeModelRef === null ? []
        : (nodeModelRef.activeWarningList !== undefined ? nodeModelRef.activeWarningList
            : (nodeModelRef.warningList !== undefined ? nodeModelRef.warningList : []))
    readonly property var dismissedWarnings: nodeModelRef !== null && nodeModelRef.dismissedWarningList !== undefined
        ? nodeModelRef.dismissedWarningList : []
    readonly property int warningCount: activeWarnings.length

    function dismissWarnings(warnings) {
        if (nodeModelRef !== null && nodeModelRef.dismissWarnings !== undefined) nodeModelRef.dismissWarnings(warnings)
    }
    function restoreDismissedWarnings() {
        if (nodeModelRef !== null && nodeModelRef.restoreDismissedWarnings !== undefined) nodeModelRef.restoreDismissedWarnings()
    }
    readonly property string label: {
        switch (key) {
        case "error": return qsTr("Error")
        case "offline": return qsTr("Offline")
        case "paused": return qsTr("Paused")
        case "connecting": return qsTr("Connecting")
        case "headers": return qsTr("Syncing headers")
        case "blocks": return qsTr("Syncing blocks")
        }
        return qsTr("Synced")
    }
    // Shown after the label as a link to the warnings list, in any state.
    readonly property string warningsText: warningCount === 0 ? ""
        : (warningCount === 1 ? qsTr("1 warning") : qsTr("%1 warnings").arg(warningCount))
    readonly property color tone: {
        switch (key) {
        case "error": return Theme.color.red
        case "offline": return Theme.color.amber
        case "headers": return Theme.color.blue
        case "blocks": return Theme.color.orange
        case "synced": return warningCount === 0 ? Theme.color.green : Theme.color.amber
        }
        return Theme.color.neutral6
    }

    readonly property int peerCount: nodeModelRef !== null ? nodeModelRef.numPeers : 0
    readonly property string peersText: peerCount === 0
        ? qsTr("No peers")
        : (peerCount === 1 ? qsTr("1 peer") : qsTr("%1 peers").arg(peerCount))
    readonly property string peersDetail: peerCount === 0
        ? qsTr("None")
        : qsTr("%1 out · %2 in").arg(nodeModelRef.numOutboundPeers).arg(nodeModelRef.numInboundPeers)
    readonly property int lastBlockAge: blockClockModelRef !== null && blockClockModelRef.lastBlockAge !== undefined
        ? blockClockModelRef.lastBlockAge
        : -1

    // The action the status bar and Node details offer for this state.
    readonly property string primaryAction: key === "paused" ? "resume"
        : key === "error" ? "shutdown"
        : key === "offline" ? "networkSettings"
        : "pause"
    readonly property string primaryActionText: {
        switch (primaryAction) {
        case "resume": return qsTr("Resume")
        case "shutdown": return qsTr("Shut down")
        case "networkSettings": return qsTr("Network settings")
        }
        return qsTr("Pause network")
    }
    readonly property bool primaryActionAccent: primaryAction !== "pause"
    readonly property bool primaryActionIsNetwork: primaryAction === "pause" || primaryAction === "resume"
    readonly property bool primaryActionEnabled: !primaryActionIsNetwork || (networkActionReady && !networkActionPending)
    readonly property string secondaryAction: key === "error" ? "debugLog" : ""
    readonly property string secondaryActionText: secondaryAction === "debugLog" ? qsTr("View debug log") : ""
    readonly property string pauseText: {
        const peers = peerCount > 1 ? qsTr("all %1 peers").arg(peerCount)
            : peerCount === 1 ? qsTr("its peer") : qsTr("the network")
        let text = qsTr("Your node will disconnect from %1 and stop receiving blocks and transactions until you resume.").arg(peers)
        if (!synced) text += " " + qsTr("Sync progress is kept.")
        return text
    }

    // The banner shown above the clock, most severe first: a node error,
    // losing the network, then warnings reported by Core.
    readonly property string bannerKind: key === "error" ? "error"
        : key === "offline" ? "offline"
        : warningCount > 0 ? "warning" : ""
    readonly property string bannerTitle: bannerKind === "error" ? qsTr("Error:")
        : bannerKind === "offline" ? qsTr("Offline.")
        : bannerKind === "warning" ? qsTr("Warning:") : ""
    readonly property string bannerText: {
        if (bannerKind === "error") {
            const message = nodeModelRef !== null && nodeModelRef.startupError !== undefined ? nodeModelRef.startupError : ""
            return message.length > 0 ? message
                : qsTr("The node stopped and needs attention. Fix the cause, then restart Bitcoin Core.")
        }
        if (bannerKind === "offline") {
            return qsTr("Bitcoin Core can't reach the network. Check your internet connection, firewall or proxy settings.")
        }
        if (bannerKind === "warning") return activeWarnings.join("\n")
        return ""
    }
    readonly property color bannerColor: bannerKind === "error" ? Theme.color.red : Theme.color.amber

    readonly property string networkLabel: {
        switch (chainModelRef !== null ? chainModelRef.networkName : "MAIN") {
        case "TEST": return qsTr("Testnet")
        case "TESTNET4": return qsTr("Testnet4")
        case "SIGNET": return qsTr("Signet")
        case "REGTEST": return qsTr("Regtest")
        }
        return qsTr("Mainnet")
    }

    function formatHeight(height) {
        return Number(height).toLocaleString(Qt.locale(), 'f', 0)
    }

    function formatLastBlockAge(seconds) {
        const minutes = Math.floor(seconds / 60)
        if (minutes < 1) return qsTr("just now")
        if (minutes < 60) return qsTr("%1 min ago").arg(minutes)
        return qsTr("%1h %2m ago").arg(Math.floor(minutes / 60)).arg(minutes % 60)
    }

    function formatLastBlock(seconds) {
        if (seconds < 0) return qsTr("Block height")
        const minutes = Math.floor(seconds / 60)
        if (minutes < 1) return qsTr("Last block just now")
        if (minutes < 60) return qsTr("Last block %1 min ago").arg(minutes)
        return qsTr("Last block %1h %2m ago").arg(Math.floor(minutes / 60)).arg(minutes % 60)
    }

    // Verification progress as the block clock shows it: one decimal from
    // 10%, two below, and never 100% while initial sync is pending.
    function formatProgress(fraction) {
        const percent = fraction * 100
        if (synced) return "100%"
        if (percent >= 99.9) return "99.9%"
        return (percent >= 10 ? percent.toFixed(1) : Math.max(0, percent).toFixed(2)) + "%"
    }

    function formatMegabytes(megabytes) {
        return megabytes < 10 ? megabytes.toFixed(1) : megabytes.toFixed(0)
    }

    // Header download is the phase before any block past genesis is verified.
    // Core reports a tiny verification progress for genesis alone, so the
    // height decides.
    readonly property bool headerPhase: !synced && (headerSyncActive ||
        (nodeModelRef !== null && nodeModelRef.blockTipHeight === 0))

    // Peers, with the network named unless it is mainnet.
    readonly property string peersSummary: chainModelRef !== null && chainModelRef.networkName !== "MAIN"
        ? qsTr("%1 · %2").arg(peersDetail).arg(networkLabel) : peersDetail

    // The few rows Node details shows by default. They add what the dial
    // cannot show; percentage, time left and last block age are on its face.
    function detailRows() {
        const tipHeight = nodeModelRef !== null ? nodeModelRef.blockTipHeight : 0
        const headerHeight = nodeModelRef !== null && nodeModelRef.headerTipHeight !== undefined ? nodeModelRef.headerTipHeight : 0
        const rows = []

        if (synced) {
            rows.push({ key: qsTr("Block height"), value: tipHeight > 0 ? formatHeight(tipHeight) : "—" })
            let mempool = key === "paused" ? qsTr("Not updating") : "—"
            if (key !== "paused" && nodeModelRef !== null && nodeModelRef.mempoolTransactionCount !== undefined) {
                mempool = qsTr("%1 tx").arg(formatHeight(nodeModelRef.mempoolTransactionCount))
                if (nodeModelRef.mempoolMaxUsageMB !== undefined && nodeModelRef.mempoolMaxUsageMB > 0) {
                    mempool += " · " + qsTr("%1 / %2 MB")
                        .arg(formatMegabytes(nodeModelRef.mempoolUsageMB))
                        .arg(formatMegabytes(nodeModelRef.mempoolMaxUsageMB))
                }
            }
            rows.push({ key: qsTr("Mempool"), value: mempool })
        } else if (headerPhase) {
            let headers = qsTr("Not started")
            if (headerSyncActive) {
                const percent = Math.floor(nodeModelRef.headerSyncProgress * 100) + "%"
                headers = nodeModelRef.headerPresync ? qsTr("Pre-syncing headers · %1").arg(percent) : qsTr("Headers · %1").arg(percent)
            }
            rows.push({ key: qsTr("Progress"), value: headers })
        } else {
            // Blocks count the chain; verification is weighted by transactions,
            // which is why it trails the block count early in the chain.
            let progress = tipHeight > 0 && headerHeight > tipHeight
                ? qsTr("%1 of %2").arg(formatHeight(tipHeight)).arg(formatHeight(headerHeight))
                : (tipHeight > 0 ? formatHeight(tipHeight) : "—")
            if (nodeModelRef !== null && nodeModelRef.verificationProgress > 0) {
                progress += " · " + qsTr("%1 of transactions").arg(formatProgress(nodeModelRef.verificationProgress))
            }
            rows.push({ key: qsTr("Progress"), value: progress })
        }
        rows.push({ key: qsTr("Peers"), value: peersSummary })
        return rows
    }

    // Rows for "More details": context that is useful, but not at a glance.
    function moreRows() {
        const tipTime = nodeModelRef !== null && nodeModelRef.blockTipTime !== undefined ? nodeModelRef.blockTipTime : 0
        const rows = []
        if (!synced && !headerPhase) {
            rows.push({ key: qsTr("Tip block from"), value: tipTime > 0 ? Qt.formatDate(new Date(tipTime * 1000), "MMM yyyy") : "—" })
        }
        return rows
    }
}
