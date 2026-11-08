// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import org.bitcoincore.qt 1.0
import QtQuick.Layouts 1.15

import "../../controls"
import "../../components"

SettingsPage {
    id: root
    objectName: "mempoolSettingsPage"
    title: qsTr("Mempool information")
    showBackButton: false

    readonly property var maxMempoolStatus: (ApplicationModels.optionsModel.coreSettingStatuses || ({})).maxmempool || ({})
    property string mempoolSizeText: String(ApplicationModels.optionsModel.maxMempoolSizeMB)
    property string mempoolSizeError: ""

    function formatMegabytes(valueMb) {
        const rounded = Math.round(valueMb)
        const decimals = Math.abs(valueMb - rounded) < 0.005 ? 0 : 2
        return Number(valueMb).toLocaleString(Qt.locale(), "f", decimals) + " MB"
    }

    function validateMempoolSize(valueMb) {
        if (isNaN(valueMb)
                || valueMb < ApplicationModels.optionsModel.minMaxMempoolSizeMB
                || valueMb > ApplicationModels.optionsModel.maxMaxMempoolSizeMB) {
            return qsTr("Choose a value between %1 MB and %2 MB.")
                .arg(ApplicationModels.optionsModel.minMaxMempoolSizeMB)
                .arg(ApplicationModels.optionsModel.maxMaxMempoolSizeMB)
        }
        return ""
    }

    SettingsRestartNotice {
        objectName: "mempoolRestartNotice"
        visible: ApplicationModels.optionsModel.mempoolSettingsDirty
        Layout.fillWidth: true
    }

    FormSection {
        Layout.fillWidth: true
        title: qsTr("Mempool")

        ValueRow {
            objectName: "mempoolTransactionsRow"
            Layout.fillWidth: true
            title: qsTr("Transactions")
            value: Number(ApplicationModels.nodeModel.mempoolTransactionCount).toLocaleString(Qt.locale(), "f", 0)
        }

        ValueRow {
            objectName: "mempoolMemoryUsedRow"
            Layout.fillWidth: true
            title: qsTr("Memory used")
            value: qsTr("%1 / %2")
                .arg(root.formatMegabytes(ApplicationModels.nodeModel.mempoolUsageMB))
                .arg(root.formatMegabytes(ApplicationModels.nodeModel.mempoolMaxUsageMB))
        }

        TextFieldRow {
            id: mempoolSizeRow
            objectName: "mempoolSizeLimitRow"
            Layout.fillWidth: true
            title: qsTr("Mempool size limit (MB)")
            enabled: ApplicationModels.optionsModel.settingsReady !== false && !ApplicationModels.optionsModel.settingsPending
                && root.maxMempoolStatus.canEdit !== false
            fieldObjectName: "mempoolSizeLimitInput"
            fieldWidth: 80
            text: root.mempoolSizeText
            validator: IntValidator {
                bottom: ApplicationModels.optionsModel.minMaxMempoolSizeMB
                top: ApplicationModels.optionsModel.maxMaxMempoolSizeMB
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
                    ApplicationModels.optionsModel.maxMempoolSizeMB = parsed
                    root.mempoolSizeText = String(parsed)
                }
            }
        }
    }

    Component.onCompleted: ApplicationModels.nodeModel.mempoolInfoPollingActive = visible
    Component.onDestruction: ApplicationModels.nodeModel.mempoolInfoPollingActive = false
    onVisibleChanged: ApplicationModels.nodeModel.mempoolInfoPollingActive = visible
}
