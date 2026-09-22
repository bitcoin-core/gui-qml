// Copyright (c) 2025-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0
import "../controls"

ColumnLayout {
    id: root
    objectName: "feeSelectionControl"
    property var walletModel: null
    property int currentTarget: walletModel ? walletModel.targetBlocks : 6
    readonly property bool customSelected: walletModel ? walletModel.customFeeEnabled : false
    readonly property var blockTargets: [2, 3, 4, 6, 10, 25, 50]
    readonly property int presetIndex: customSelected ? 3 : currentTarget === 2 ? 0 : currentTarget === 10 ? 2 : 1
    readonly property var speedOptions: [
        { text: qsTr("Priority"), subtitle: qsTr("Targets 2 blocks · Usually higher fee"), value: 0, blocks: 2, objectName: "feeSelectionOption0" },
        { text: qsTr("Standard"), subtitle: qsTr("Targets 6 blocks · Balanced speed and cost"), value: 1, blocks: 6, objectName: "feeSelectionOption1" },
        { text: qsTr("Flexible"), subtitle: qsTr("Targets 10 blocks · Usually lower fee"), value: 2, blocks: 10, objectName: "feeSelectionOption2" },
        { text: qsTr("Custom"), subtitle: qsTr("Choose your fee rate and block target"), value: 3, blocks: -1, objectName: "feeSelectionOption3" }
    ]
    signal feeChanged(int target)
    spacing: 0

    function nearestBlockTarget(target) {
        let nearest = blockTargets[0]
        for (const candidate of blockTargets) {
            if (Math.abs(candidate - target) < Math.abs(nearest - target)) nearest = candidate
        }
        return nearest
    }

    BitcoinAmount { id: fee; satoshi: root.walletModel ? Math.max(0, root.walletModel.estimatedFeeSatoshi) : 0; unit: optionsModel.displayUnit }

    FormRow {
        Layout.fillWidth: true
        title: qsTr("Speed")
        trailingItem: PopupPicker {
            objectName: "feeSelectionPicker"
            model: root.speedOptions
            subtitleRole: "subtitle"
            objectNameRole: "objectName"
            currentValue: root.presetIndex
            minimumMenuWidth: 340
            embedded: true
            onActivated: function(index) {
                if (!root.walletModel) return
                const option = root.speedOptions[index]
                if (option.blocks < 0) root.walletModel.setCustomFeeTarget(root.nearestBlockTarget(root.currentTarget))
                else {
                    root.walletModel.customFeeEnabled = false
                    root.walletModel.targetBlocks = option.blocks
                    root.feeChanged(option.blocks)
                }
            }
        }
    }

    FormRow {
        objectName: "feeTargetBlocksRow"
        visible: root.customSelected
        Layout.fillWidth: true
        title: qsTr("Targeted blocks")
        description: root.currentTarget >= 50 ? qsTr("50+ blocks") : qsTr("%1 blocks").arg(root.currentTarget)
        bodyItem: ColumnLayout {
            spacing: 0

            Slider {
                id: targetSlider
                objectName: "feeTargetBlocksSlider"
                Layout.fillWidth: true
                discrete: true
                from: 0
                to: root.blockTargets.length - 1
                value: root.blockTargets.indexOf(root.nearestBlockTarget(root.currentTarget))
                Accessible.name: qsTr("Targeted blocks")
                onMoved: root.walletModel.setCustomFeeTarget(root.blockTargets[Math.round(value)])
            }

            Item {
                Layout.fillWidth: true
                implicitHeight: 18
                Repeater {
                    model: root.blockTargets
                    CoreText {
                        required property int index
                        required property var modelData
                        objectName: "feeTargetBlocksTick" + index
                        width: 32
                        x: index / (root.blockTargets.length - 1) * (parent.width - width)
                        text: index === root.blockTargets.length - 1 ? qsTr("50+") : modelData
                        font: Theme.text.caption.font
                        color: Theme.color.neutral6
                        horizontalAlignment: Text.AlignHCenter
                    }
                }
            }
        }
    }

    ValueRow {
        visible: !root.customSelected
        Layout.fillWidth: true
        title: qsTr("Fee rate")
        value: !root.walletModel || root.walletModel.feeEstimatePending || root.walletModel.estimatedFeeSatoshi < 0
            ? "—" : root.walletModel.estimatedFeeRate + " sat/vB"
        valueTextStyle: Theme.text.monoDescription
    }

    TextFieldRow {
        visible: root.customSelected
        objectName: "feeSelectionCustomRateRow"
        fieldObjectName: "feeSelectionCustomRateInput"
        Layout.fillWidth: true
        title: qsTr("Fee rate")
        description: "sat/vB"
        text: root.walletModel ? root.walletModel.customFeeRate : ""
        validator: RegularExpressionValidator { regularExpression: /^(|[0-9]+(\.[0-9]{0,3})?)$/ }
        errorText: root.walletModel && !root.walletModel.customFeeRateValid ? qsTr("Enter a fee rate greater than zero.") : ""
        onTextEdited: function(text) { if (root.walletModel) root.walletModel.customFeeRate = text }
    }

    ValueRow {
        objectName: "feeSelectionEstimateLabel"
        Layout.fillWidth: true
        title: qsTr("Fee amount")
        showDivider: false
        value: root.walletModel && !root.walletModel.feeEstimatePending && root.walletModel.estimatedFeeSatoshi >= 0
            ? fee.displayWithUnit : "—"
        valueTextStyle: Theme.text.monoDescription
    }

    CoreText { objectName: "feeSelectionCustomEstimateLabel"; visible: false; text: fee.displayWithUnit }
}
