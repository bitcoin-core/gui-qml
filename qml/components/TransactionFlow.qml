// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQml.Models 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Shapes 1.15
import org.bitcoincore.qt 1.0
import "../controls"
import "TransactionFlowLayout.js" as FlowLayout

ColumnLayout {
    id: root
    // Plain snapshot data: no wallet, history, navigation or broadcast dependency.
    property var flow: ({})
    property string transactionId: ""
    property int displayUnit: BitcoinAmount.BTC
    property bool outputsExpanded: false
    property string title: qsTr("Transaction flow")
    property string walletName: qsTr("Your wallet")
    property string selectedEntryId: ""
    property bool interactive: true
    property color walletColor: Theme.color.purple
    property color externalColor: Theme.color.orange
    property color feeColor: Theme.color.blue
    property color dataColor: Theme.color.neutral8
    property color unknownColor: Theme.color.neutral6
    readonly property var inputEntries: flow.inputs || []
    // Select the visible outputs in one binding. Separate group/threshold
    // bindings can briefly expose every output while a new snapshot arrives.
    readonly property var outputPresentation: {
        const snapshot = flow
        const group = FlowLayout.outputGroup(snapshot.outputs || [])
        const collapsible = (snapshot.outputs || []).length > 10 && group !== null
        return { group: group, collapsible: collapsible,
            entries: FlowLayout.outputs(snapshot, collapsible && !outputsExpanded ? group : null) }
    }
    readonly property var nonWalletOutputGroup: outputPresentation.group
    readonly property bool canCollapseOutputs: outputPresentation.collapsible
    readonly property var outputEntries: outputPresentation.entries.map(function(entry) {
        return entry.kind === "output-group" && nonWalletOutputGroup
            ? Object.assign({}, entry, {amount: entry.amountKnown ? groupedAmount.displayWithUnit : ""}) : entry
    })
    readonly property real diagramWidth: Math.max(560, width - 48)
    readonly property real inputCardWidth: diagramWidth * 0.35
    readonly property real outputCardWidth: diagramWidth * 0.35
    property var nodeMeasurements: ({ inputs: [], outputs: [] })
    property var ribbonObjects: []
    readonly property var geometry: FlowLayout.calculate(inputEntries, outputEntries,
        nodeMeasurements.inputs, nodeMeasurements.outputs, diagramWidth, !!flow.complete)
    signal paymentRequestRequested(string requestId)
    onTransactionIdChanged: outputsExpanded = false

    BitcoinAmount {
        id: groupedAmount
        unit: root.displayUnit
        satoshi: root.nonWalletOutputGroup && root.nonWalletOutputGroup.amountKnown ? root.nonWalletOutputGroup.amountSat : 0
    }

    function updateNodeMeasurements() {
        // Coalesce a layout pass into one geometry update, rather than
        // recalculating every ribbon once for each input/output card.
        nodeMeasurements = { inputs: nodeHeights(inputs), outputs: nodeHeights(outputs) }
    }
    function nodeHeights(repeater) {
        const result = []
        for (let i = 0; i < repeater.count; ++i) {
            const item = repeater.itemAt(i)
            result.push(item ? item.implicitHeight : 80)
        }
        return result
    }
    function ownershipColor(ownership) {
        return ownership === "wallet" ? walletColor : ownership === "external" ? externalColor
            : ownership === "fee" ? feeColor : unknownColor
    }

    spacing: 12
    GridLayout {
        visible: root.title.length > 0 || root.canCollapseOutputs
        Layout.fillWidth: true
        columns: root.width < 640 ? 1 : 3
        columnSpacing: 12
        rowSpacing: 4
        CoreText {
            visible: root.title.length > 0
            Layout.fillWidth: true
            text: root.title
            font: Theme.text.subheading.font
            horizontalAlignment: Text.AlignLeft
        }
        CoreText {
            objectName: "transactionFlowCounts"
            readonly property int inputCount: root.flow.inputCount || root.inputEntries.length
            readonly property int outputCount: root.flow.outputCount || (root.flow.outputs || []).length
            text: (inputCount === 1 ? qsTr("1 input") : qsTr("%1 inputs").arg(inputCount)) + " · "
                + (outputCount === 1 ? qsTr("1 output") : qsTr("%1 outputs").arg(outputCount))
            color: Theme.color.neutral7
            font: Theme.text.caption.font
        }
        TextButton {
            objectName: "transactionFlowOutputsToggle"
            visible: root.canCollapseOutputs
            Layout.alignment: Qt.AlignLeft
            text: root.outputsExpanded ? qsTr("Collapse non-wallet outputs") : qsTr("Show all outputs")
            textSize: Theme.text.caption.pixelSize
            bold: false
            padding: 8
            onClicked: root.outputsExpanded = !root.outputsExpanded
        }
    }
    Rectangle {
        Layout.fillWidth: true
        implicitHeight: contents.implicitHeight + 48
        color: Theme.color.neutral1
        radius: 16
        ColumnLayout {
            id: contents
            x: 24
            y: 24
            width: parent.width - 48
            spacing: 16
            Flickable {
                id: viewport
                objectName: "transactionFlowViewport"
                Layout.fillWidth: true
                implicitHeight: root.geometry.height + 36 + (contentWidth > width ? 16 : 0)
                contentWidth: root.diagramWidth
                contentHeight: height
                flickableDirection: Flickable.HorizontalFlick
                boundsBehavior: Flickable.StopAtBounds
                clip: true
                ScrollBar.horizontal: ScrollBar { policy: viewport.contentWidth > viewport.width ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff }
                Item {
                    width: root.diagramWidth
                    height: viewport.height
                    CoreText { text: qsTr("Inputs"); color: Theme.color.neutral7; font: Theme.text.caption.font }
                    CoreText { x: root.geometry.outputX; text: qsTr("Outputs"); color: Theme.color.neutral7; font: Theme.text.caption.font }
                    Item {
                        id: diagram
                        y: 36
                        width: root.diagramWidth
                        height: root.geometry.height
                        Shape {
                            id: ribbons
                            objectName: "transactionFlowRibbons"
                            anchors.fill: parent
                            data: root.ribbonObjects
                        }
                        Instantiator {
                            // Keep paths alive while card measurements change. Replacing
                            // the array model rebuilds every path for each measured card.
                            model: root.geometry.ribbons.length
                            delegate: TransactionFlowRibbon {
                                required property int index
                                ribbon: root.geometry.ribbons[index] || {}
                                sourceColor: root.ownershipColor(ribbon.sourceOwnership)
                                targetColor: ribbon.targetKind === "data" ? root.dataColor : root.ownershipColor(ribbon.targetOwnership)
                            }
                            onObjectAdded: function(index, object) {
                                const objects = root.ribbonObjects.slice()
                                objects.splice(index, 0, object)
                                root.ribbonObjects = objects
                            }
                            onObjectRemoved: function(index, object) {
                                root.ribbonObjects = root.ribbonObjects.filter(function(item) { return item !== object })
                            }
                        }
                        Repeater {
                            id: inputs
                            model: root.inputEntries
                            onItemAdded: Qt.callLater(root.updateNodeMeasurements)
                            onItemRemoved: Qt.callLater(root.updateNodeMeasurements)
                            delegate: TransactionFlowNode {
                                required property var modelData
                                required property int index
                                objectName: "transactionFlowInput_" + index
                                entry: modelData
                                input: true
                                width: root.inputCardWidth
                                onImplicitHeightChanged: Qt.callLater(root.updateNodeMeasurements)
                                y: root.geometry.inputs[index] ? root.geometry.inputs[index].y : 0
                                walletName: root.walletName
                                accentColor: root.ownershipColor(modelData.ownership)
                                interactive: root.interactive
                            }
                        }
                        Repeater {
                            id: outputs
                            model: root.outputEntries
                            onItemAdded: Qt.callLater(root.updateNodeMeasurements)
                            onItemRemoved: Qt.callLater(root.updateNodeMeasurements)
                            delegate: TransactionFlowNode {
                                required property var modelData
                                required property int index
                                objectName: "transactionFlowOutput_" + index
                                entry: modelData
                                width: root.outputCardWidth
                                onImplicitHeightChanged: Qt.callLater(root.updateNodeMeasurements)
                                x: root.geometry.outputX
                                y: root.geometry.outputs[index] ? root.geometry.outputs[index].y : 0
                                walletName: root.walletName
                                accentColor: modelData.kind === "data" ? root.dataColor : root.ownershipColor(modelData.ownership)
                                interactive: root.interactive
                                selected: root.selectedEntryId === modelData.id
                                onPaymentRequestRequested: function(requestId) { root.paymentRequestRequested(requestId) }
                            }
                        }
                    }
                }
            }
            CoreText {
                objectName: "transactionFlowUnknownNotice"
                visible: root.inputEntries.length > 0 && !root.geometry.proportional
                Layout.fillWidth: true
                text: root.geometry.amountsKnown ? qsTr("Connections are not to scale.")
                    : qsTr("Some amounts are unavailable")
                font: Theme.text.caption.font
                color: Theme.color.neutral7
                horizontalAlignment: Text.AlignLeft
                wrap: true
            }
            Flow {
                Layout.fillWidth: true
                spacing: 16
                Repeater {
                    model: [{ label: qsTr("Wallet-owned"), color: root.walletColor },
                        { label: qsTr("Not wallet-owned"), color: root.externalColor }, { label: qsTr("Fee"), color: root.feeColor }]
                    delegate: Row {
                        required property var modelData
                        spacing: 6
                        Rectangle { anchors.verticalCenter: parent.verticalCenter; width: 7; height: 7; radius: 2; color: modelData.color }
                        CoreText { text: modelData.label; color: Theme.color.neutral7; font: Theme.text.caption.font }
                    }
                }
            }
        }
    }
}
