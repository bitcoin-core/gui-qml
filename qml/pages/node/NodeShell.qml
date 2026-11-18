// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import org.bitcoincore.qt 1.0
import "../../components"
import "../../controls"

PageStack {
    id: root
    objectName: "nodeShellPage"
    vertical: true
    required property bool navigationAdmitted
    required property bool stopping
    required property Component settingsComponent
    signal settingsRequested(string section)
    signal peersRequested()

    property bool settingsInstantiated: false
    property bool settingsPresentationPending: false
    property string settingsPresentationSection: ""
    readonly property SettingsView settingsView: nodeSettings.item as SettingsView
    readonly property bool modalNavigationBlocked: settingsView !== null && settingsView.visible
        && (settingsView.settingsNavigationPending
            || (settingsView.settingsEditor !== null && settingsView.settingsEditor.closeQuestion))
    initialItem: nodePage

    onStoppingChanged: if (root.stopping) root.settingsPresentationPending = false

    function resetToRoot() {
        root.settingsPresentationPending = false
        ApplicationModels.peerTableModel.stopAutoRefresh()
        if (root.depth > 1) root.pop(null, StackView.Immediate)
    }

    function openSettings(section: string) {
        root.resetToRoot()
        root.settingsPresentationSection = section
        root.settingsPresentationPending = true
        root.settingsInstantiated = true
        root.presentSettingsIfReady()
    }

    function presentSettingsIfReady() {
        const page = root.settingsView
        if (!root.settingsPresentationPending || page === null || !root.navigationAdmitted || root.stopping) return
        const section = root.settingsPresentationSection
        root.settingsPresentationPending = false
        root.push(page, {}, StackView.Immediate)
        if (section.length > 0 && root.currentItem === page && root.navigationAdmitted
                && !root.stopping && root.settingsPresentationSection === section) page.selectSection(section)
    }

    Timer {
        interval: 0
        repeat: false
        running: root.settingsPresentationPending && root.settingsView !== null && root.navigationAdmitted && !root.stopping
        onTriggered: root.presentSettingsIfReady()
    }

    function openNode() { root.resetToRoot() }
    function openPeers() {
        root.resetToRoot()
        ApplicationModels.peerTableModel.startAutoRefresh()
        root.push(peersPage, {}, StackView.Immediate)
    }
    function openConsole() { root.openSettings("rpc-console") }
    function openNetworkTraffic() { root.openSettings("network-traffic") }
    function closeSettings() {
        if (root.currentItem === root.settingsView) root.pop()
    }

    // StackView hides/reparents this standard Item; the Loader retains the
    // original SettingsView/editor until this shell is actually retired.
    Loader {
        id: nodeSettings
        active: root.settingsInstantiated
        visible: false
        width: root.width
        height: root.height
        sourceComponent: root.settingsComponent
        onLoaded: root.presentSettingsIfReady()
    }
    Component {
        id: nodePage
        NodeRunner {
            onSettingsClicked: root.settingsRequested("")
            onPeersClicked: root.peersRequested()
        }
    }
    Component {
        id: peersPage
        PeersView {
            onBack: {
                root.pop()
                ApplicationModels.peerTableModel.stopAutoRefresh()
            }
        }
    }
}
