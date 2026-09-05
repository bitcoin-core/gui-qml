// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick 2.15 as QtQuickBase

import "../controls"

ColumnLayout {
    id: root
    spacing: 4
    readonly property var maxMempoolStatus: (optionsModel.coreSettingStatuses || ({})).maxmempool || ({})

    function formatMegabytes(valueMb) {
        const rounded = Math.round(valueMb)
        const decimals = Math.abs(valueMb - rounded) < 0.005 ? 0 : 2
        return Number(valueMb).toLocaleString(Qt.locale(), 'f', decimals) + " MB"
    }

    Setting {
        objectName: "mempoolTransactionsRow"
        Layout.fillWidth: true
        interactive: false
        header: qsTr("Transactions")
        actionItem: CoreText {
            text: Number(mempoolModel.mempoolTransactionCount).toLocaleString(Qt.locale(), 'f', 0)
            color: Theme.color.neutral9
            font.pixelSize: 18
            fontStyleName: "Regular"
            horizontalAlignment: Text.AlignRight
            verticalAlignment: Text.AlignVCenter
            wrapMode: Text.NoWrap
        }
    }

    Separator { Layout.fillWidth: true }

    Setting {
        objectName: "mempoolMemoryUsedRow"
        Layout.fillWidth: true
        interactive: false
        header: qsTr("Memory used")
        actionItem: CoreText {
            text: qsTr("%1 / %2")
                .arg(root.formatMegabytes(mempoolModel.mempoolUsageMB))
                .arg(root.formatMegabytes(mempoolModel.mempoolMaxUsageMB))
            color: Theme.color.neutral9
            font.pixelSize: 18
            fontStyleName: "Regular"
            horizontalAlignment: Text.AlignRight
            verticalAlignment: Text.AlignVCenter
            wrapMode: Text.NoWrap
        }
    }

    Separator { Layout.fillWidth: true }

    Setting {
        id: mempoolLimitSetting
        objectName: "mempoolSizeLimitRow"
        Layout.fillWidth: true
        header: qsTr("Mempool size limit")
        state: root.maxMempoolStatus.canEdit === false ? "DISABLED" : "FILLED"
        errorText: qsTr("This is not a valid mempool size. Please choose a value between %1 and %2 MB.")
            .arg(optionsModel.minMaxMempoolSizeMB)
            .arg(optionsModel.maxMaxMempoolSizeMB)
        showErrorText: false
        infoText: root.maxMempoolStatus.infoText || ""
        showInfoText: !showErrorText && infoText.length > 0
        actionItem: ValueInput {
            parentState: mempoolLimitSetting.visualState
            description: optionsModel.maxMempoolSizeMB
            validator: QtQuickBase.IntValidator {
                bottom: optionsModel.minMaxMempoolSizeMB
                top: optionsModel.maxMaxMempoolSizeMB
            }
            onEditingFinished: {
                const mempoolSize = parseInt(text, 10)
                if (acceptableInput && checkValidity(optionsModel.minMaxMempoolSizeMB, optionsModel.maxMaxMempoolSizeMB, mempoolSize)) {
                    optionsModel.maxMempoolSizeMB = mempoolSize
                    mempoolLimitSetting.forceActiveFocus()
                    mempoolLimitSetting.showErrorText = false
                } else {
                    mempoolLimitSetting.showErrorText = true
                }
            }
        }
        onClicked: {
            loadedItem.filled = true
            loadedItem.forceActiveFocus()
        }
    }
}
