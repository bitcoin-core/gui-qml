// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
import QtQuick
import QtQuick.Layouts
import org.bitcoincore.qt 1.0
import "../../components"
import "../../controls"
SettingsPage {
    id: root
    required property SettingsEditorModel editor
    property bool settingsEditable: true
    required property string section
    objectName: root.section === "connection" ? "connectionSettingsPage" : root.section === "storage" ? "storageSettingsPage" : root.section === "mempool" ? "mempoolSettingsPage" : "externalSignerSettingsPage"
    showBackButton: false
    title: section === "connection" ? qsTr("Connection") : section === "storage" ? qsTr("Storage") : section === "mempool" ? qsTr("Mempool") : qsTr("External signer")
    CoreText {
        Layout.fillWidth: true
        text: qsTr("Edit this section, then save the complete settings batch. Use configuration removes the GUI override and follows bitcoin.conf or defaults.")
        wrap: true
    }
    ValueRow {
        objectName: "mempoolTransactionsRow"
        Layout.fillWidth: true
        visible: root.section === "mempool"
        title: qsTr("Transactions")
        value: Number(ApplicationModels.nodeModel.mempoolTransactionCount).toLocaleString(Qt.locale(), "f", 0)
    }
    ValueRow {
        objectName: "mempoolMemoryUsedRow"
        Layout.fillWidth: true
        visible: root.section === "mempool"
        title: qsTr("Memory used (MB)")
        value: qsTr("%1 / %2").arg(Number(ApplicationModels.nodeModel.mempoolUsageMB).toLocaleString(Qt.locale(), "f", 2)).arg(Number(ApplicationModels.nodeModel.mempoolMaxUsageMB).toLocaleString(Qt.locale(), "f", 2))
    }
    Binding {
        target: ApplicationModels.nodeModel
        property: "mempoolInfoPollingActive"
        when: root.section === "mempool"
        value: root.visible
        restoreMode: Binding.RestoreBindingOrValue
    }
    // Qt may destroy the binding without a final visible=false publication.
    Component.onDestruction: if (section === "mempool") ApplicationModels.nodeModel.mempoolInfoPollingActive = false
    Repeater {
        model: root.editor ? root.editor.entries : []
        EffectiveSettingRow {
            required property SettingsEntryModel modelData
            Layout.fillWidth: true
            visible: modelData.section === root.section
            enabled: root.settingsEditable
            entry: modelData
        }
    }
}
