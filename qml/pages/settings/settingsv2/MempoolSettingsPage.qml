// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Layouts 1.15

import "../../../controls"
import "../../../components"

SettingsPage {
    id: root
    objectName: "settingsv2MempoolSettingsPage"
    title: qsTr("Mempool information")
    showBackButton: false

    readonly property var maxMempoolStatus: (optionsModel.coreSettingStatuses || ({})).maxmempool || ({})
    property string mempoolSizeText: String(optionsModel.maxMempoolSizeMB)
    property string mempoolSizeError: ""

    function formatMegabytes(valueMb) {
        const rounded = Math.round(valueMb)
        const decimals = Math.abs(valueMb - rounded) < 0.005 ? 0 : 2
        return Number(valueMb).toLocaleString(Qt.locale(), "f", decimals) + " MB"
    }

    function validateMempoolSize(valueMb) {
        if (isNaN(valueMb)
                || valueMb < optionsModel.minMaxMempoolSizeMB
                || valueMb > optionsModel.maxMaxMempoolSizeMB) {
            return qsTr("Choose a value between %1 MB and %2 MB.")
                .arg(optionsModel.minMaxMempoolSizeMB)
                .arg(optionsModel.maxMaxMempoolSizeMB)
        }
        return ""
    }

    SettingsRestartNotice {
        objectName: "settingsv2MempoolRestartNotice"
        visible: optionsModel.mempoolSettingsDirty
        Layout.fillWidth: true
    }

    FormSection {
        Layout.fillWidth: true
        title: qsTr("Mempool")

        ValueRow {
            objectName: "settingsv2MempoolTransactionsRow"
            Layout.fillWidth: true
            title: qsTr("Transactions")
            value: Number(nodeModel.mempoolTransactionCount).toLocaleString(Qt.locale(), "f", 0)
        }

        ValueRow {
            objectName: "settingsv2MempoolMemoryUsedRow"
            Layout.fillWidth: true
            title: qsTr("Memory used")
            value: qsTr("%1 / %2")
                .arg(root.formatMegabytes(nodeModel.mempoolUsageMB))
                .arg(root.formatMegabytes(nodeModel.mempoolMaxUsageMB))
        }

        TextFieldRow {
            id: mempoolSizeRow
            objectName: "settingsv2MempoolSizeLimitRow"
            Layout.fillWidth: true
            title: qsTr("Mempool size limit (MB)")
            enabled: root.maxMempoolStatus.canEdit !== false
            fieldObjectName: "settingsv2MempoolSizeLimitInput"
            fieldWidth: 80
            text: root.mempoolSizeText
            validator: IntValidator {
                bottom: optionsModel.minMaxMempoolSizeMB
                top: optionsModel.maxMaxMempoolSizeMB
            }
            inputMethodHints: Qt.ImhDigitsOnly
            errorText: root.mempoolSizeError
            supportingText: root.mempoolSizeError.length === 0
                ? root.maxMempoolStatus.infoText || ""
                : ""
            showDivider: false
            onTextEdited: function(text) {
                root.mempoolSizeText = text
                root.mempoolSizeError = ""
            }
            onEditingFinished: {
                const parsed = parseInt(mempoolSizeRow.text, 10)
                root.mempoolSizeError = root.validateMempoolSize(parsed)
                if (root.mempoolSizeError.length === 0) {
                    optionsModel.maxMempoolSizeMB = parsed
                    root.mempoolSizeText = String(parsed)
                }
            }
        }
    }

    Component.onCompleted: nodeModel.mempoolInfoPollingActive = visible
    Component.onDestruction: nodeModel.mempoolInfoPollingActive = false
    onVisibleChanged: nodeModel.mempoolInfoPollingActive = visible
}
