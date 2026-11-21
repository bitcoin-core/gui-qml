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
    property WalletQmlModel walletModel: null
    readonly property FeeSelectionModel fees: walletModel ? walletModel.fees : null
    readonly property int currentTarget: fees ? fees.targetBlocks : 6
    readonly property bool customSelected: fees && fees.mode === FeeSelectionModel.Manual
    readonly property var blockTargets: [2, 3, 4, 6, 10, 25, 50]
    readonly property int presetIndex: fees ? fees.presetIndex : 1
    readonly property var speedOptions: [
        { text: qsTr("Priority"), subtitle: qsTr("Requested target: 2 blocks"), value: 0, blocks: 2, objectName: "feeSelectionOption0" },
        { text: qsTr("Standard"), subtitle: qsTr("Requested target: 6 blocks"), value: 1, blocks: 6, objectName: "feeSelectionOption1" },
        { text: qsTr("Flexible"), subtitle: qsTr("Requested target: 10 blocks"), value: 2, blocks: 10, objectName: "feeSelectionOption2" },
        { text: qsTr("Custom"), subtitle: qsTr("Choose your fee rate and block target"), value: 3, blocks: -1, objectName: "feeSelectionOption3" }
    ]
    signal feeChanged(int target)
    spacing: 0

    BitcoinAmount { id: fee; satoshi: root.walletModel ? Math.max(0, root.walletModel.estimatedFeeSatoshi) : 0; unit: ApplicationModels.optionsModel.displayUnit }

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
            surfaceLevel: 1
            onActivated: function(index) {
                if (!root.fees) return
                const option = root.speedOptions[index]
                if (option.blocks < 0) root.fees.chooseCustom()
                else { root.fees.choosePreset(option.blocks); root.feeChanged(option.blocks) }
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
                value: root.fees ? root.fees.targetIndex : 3
                Accessible.name: qsTr("Targeted blocks")
                onMoved: if (root.fees) root.fees.chooseTargetIndex(Math.round(value))
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
                        textStyle: Theme.text.caption
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
            ? "—" : qsTr("%1 sat/vB").arg(root.walletModel.estimatedFeeRate)
        valueTextStyle: Theme.text.monoDescription
    }

    TextFieldRow {
        visible: root.customSelected
        objectName: "feeSelectionCustomRateRow"
        fieldObjectName: "feeSelectionCustomRateInput"
        enabled: root.fees && !root.fees.customDefaultPending
        Layout.fillWidth: true
        title: qsTr("Fee rate")
        description: "sat/vB"
        maximumLength: 33 // One invalid sentinel beyond the original 32-character budget.
        text: root.fees ? root.fees.customRate.text : ""
        //: Placeholder for the empty custom fee rate field, in sat/vB.
        placeholderText: qsTr("Enter custom fee")
        //: Validation error for the custom transaction fee rate. sat/vB means satoshis per virtual byte.
        errorText: root.fees && !root.fees.customRate.pending ? root.fees.customRate.error : ""
        onTextEdited: function(text) { if (root.fees) root.fees.customRate.edit(text) }
    }

    ValueRow {
        objectName: "feeSelectionEstimateLabel"
        amountValue: true
        animateUnitChanges: false
        Layout.fillWidth: true
        title: qsTr("Fee amount")
        showDivider: false
        value: root.walletModel && !root.walletModel.feeEstimatePending && root.walletModel.estimatedFeeSatoshi >= 0
            ? fee.localizedDisplay : "—"
        valueUnit: fee.unitLabel
    }

    NeutralButton {
        objectName: "feeRefreshButton"
        visible: root.fees && root.fees.errorText.length > 0
        enabled: root.fees && !root.fees.policyPending && !root.fees.estimatePending
        buttonSize: NeutralButton.Large
        textStyle: Theme.text.buttonStrong
        text: qsTr("Refresh estimate")
        onClicked: root.fees.refresh()
    }
    CoreText {
        objectName: "feePreviousEstimateText"
        Layout.fillWidth: true
        visible: root.fees && root.fees.hasPreviousEstimate
        text: root.fees ? qsTr("Previous estimate: %1 sat/vB. Waiting for a current estimate.").arg(root.fees.estimatedRate) : ""
        wrapMode: Text.WordWrap
        color: Theme.color.neutral7
    }
    CoreText {
        objectName: "feeEstimateProvenanceText"
        Layout.fillWidth: true
        visible: root.fees && root.fees.estimateFresh && root.fees.provenance === FeeSelectionModel.Fallback
        text: qsTr("Fee estimation is unavailable. The wallet fallback rate is shown; it has no confirmation target.")
        wrapMode: Text.WordWrap
    }
    CoreText {
        objectName: "feeEstimateErrorText"
        Layout.fillWidth: true
        visible: root.fees && !root.fees.estimatePending && root.fees.errorText.length > 0
        text: root.fees ? root.fees.errorText : ""
        wrapMode: Text.WordWrap
        color: Theme.color.dangerText
    }
    CoreText { objectName: "feeSelectionCustomEstimateLabel"; visible: false; text: fee.displayWithUnit }
}
