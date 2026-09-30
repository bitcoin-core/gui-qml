// Copyright (c) 2025 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

import "../controls"
import "../components"

ColumnLayout {
    id: root
    objectName: "feeSelectionControl"

    property var walletModel: null
    property int currentTarget: 2
    property bool customSelected: walletModel ? walletModel.customFeeEnabled : false
    property int selectedIndex: customSelected
        ? feeModel.count - 1
        : (walletModel ? walletModel.feeTargetIndex(currentTarget) : 1)
    property string selectedLabel: customSelected
        ? qsTr("sats/vbyte")
        : feeModel.get(root.selectedIndex).feeLabel
    property string selectedDuration: customSelected
        ? ""
        : feeModel.get(root.selectedIndex).feeDuration
    property int selectedTarget: feeModel.get(root.selectedIndex).target
    property string selectedEstimate: customSelected
        ? ""
        : (walletModel ? (walletModel.feeEstimateRevision, walletModel.estimatedFeeForTarget(selectedTarget)) : "")
    property bool customFeeRateValid: walletModel ? walletModel.customFeeRateValid : false
    readonly property int customFeeRateMaximumLength: 12

    readonly property var _feePickerModel: {
        const revision = root.walletModel ? root.walletModel.feeEstimateRevision : 0
        const wm = root.walletModel
        const rows = []
        for (let i = 0; i < feeModel.count; ++i) {
            const r = feeModel.get(i)
            const isCustom = r.target < 0
            rows.push({
                text: r.feeLabel + " " + r.feeDuration,
                value: r.target,
                estimate: !isCustom && wm ? wm.estimatedFeeForTarget(r.target) : "",
                objectName: "feeSelectionOption" + i,
                estimateObjectName: "feeSelectionOptionEstimate" + i
            })
        }
        return rows
    }

    signal feeChanged(int target)

    spacing: 12

    RowLayout {
        Layout.fillWidth: true
        Layout.minimumHeight: 56
        spacing: 16

        CoreText {
            Layout.preferredWidth: 128
            horizontalAlignment: Text.AlignLeft
            font: Theme.text.body.font
            lineHeight: Theme.text.body.lineHeight
            lineHeightMode: Text.FixedHeight
            text: root.customSelected ? qsTr("Fee Rate") : qsTr("Fee")
        }

        CoreText {
            id: estimateLabel
            objectName: "feeSelectionEstimateLabel"
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignLeft
            font: Theme.text.body.font
            lineHeight: Theme.text.body.lineHeight
            lineHeightMode: Text.FixedHeight
            color: Theme.color.neutral7
            text: root.selectedEstimate
            visible: !root.customSelected
        }

        TextField {
            id: customFeeRateInput
            objectName: "feeSelectionCustomRateInput"
            Layout.fillWidth: true
            visible: root.customSelected
            leftPadding: 0
            font: Theme.text.body.font
            color: Theme.color.neutral9
            placeholderTextColor: enabled ? Theme.color.neutral7 : Theme.color.neutral4
            background: Item {}
            placeholderText: "0.000"
            selectByMouse: true
            text: root.walletModel ? root.walletModel.customFeeRate : ""
            maximumLength: root.customFeeRateMaximumLength
            validator: RegularExpressionValidator {
                regularExpression: /^(|[0-9]+(\.[0-9]{0,3})?)$/
            }
            onTextChanged: {
                if (root.walletModel && root.walletModel.customFeeRate !== text) {
                    root.walletModel.customFeeRate = text
                }
            }
        }

        DropdownButton {
            id: dropDownButton
            objectName: "feeSelectionDropdownButton"
            text: root.selectedLabel
            subtitle: root.selectedDuration
            labelTextStyle: Theme.text.body
            subtitleTextStyle: Theme.text.body
            caretSize: 30
            rightPadding: 8
            topPadding: 4
            bottomPadding: 4
            implicitHeight: 32
            opened: feePopup.visible
            onClicked: feePopup.open()
        }
    }

    RowLayout {
        Layout.fillWidth: true
        Layout.minimumHeight: 56
        spacing: 16
        visible: root.customSelected

        CoreText {
            Layout.preferredWidth: 128
            horizontalAlignment: Text.AlignLeft
            font: Theme.text.body.font
            lineHeight: Theme.text.body.lineHeight
            lineHeightMode: Text.FixedHeight
            text: qsTr("Fee")
        }

        CoreText {
            id: customEstimateLabel
            objectName: "feeSelectionCustomEstimateLabel"
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignLeft
            font: Theme.text.body.font
            lineHeight: Theme.text.body.lineHeight
            lineHeightMode: Text.FixedHeight
            color: Theme.color.neutral7
            text: root.walletModel ? (root.walletModel.feeEstimateRevision, root.walletModel.estimatedFee) : ""
        }
    }

    ContextMenu {
        id: feePopup
        objectName: "feeSelectionPopup"
        modal: true
        dim: false
        x: dropDownButton.x + dropDownButton.width - width
        y: dropDownButton.height + 6

        ContextMenuPicker {
            id: feePicker
            objectName: "feeSelectionList"
            model: root._feePickerModel
            subtitleRole: "estimate"
            objectNameRole: "objectName"
            subtitleObjectNameRole: "estimateObjectName"
            currentValue: root.customSelected ? -1 : root.selectedTarget
            onActivated: function(value) {
                if (root.walletModel) {
                    root.walletModel.customFeeEnabled = (value === -1)
                }
                if (value !== -1) {
                    root.feeChanged(value)
                }
                feePopup.close()
            }
        }
    }

    ListModel {
        id: feeModel
        ListElement { feeLabel: qsTr("High"); feeDuration: qsTr("(~10 mins)"); target: 1 }
        ListElement { feeLabel: qsTr("Default"); feeDuration: qsTr("(~20 mins)"); target: 2 }
        ListElement { feeLabel: qsTr("Low"); feeDuration: qsTr("(~60 mins)"); target: 6 }
        ListElement { feeLabel: qsTr("Custom"); feeDuration: qsTr("sats/vbyte"); target: -1 }
    }
}
