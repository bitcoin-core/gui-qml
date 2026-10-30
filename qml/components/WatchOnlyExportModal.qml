// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0

import "../controls"

Popup {
    id: root
    objectName: "watchOnlyExportModal"

    property var wallet: walletController.selectedWallet
    property var exports: []
    property bool extendedKeyFormat: false
    // Empty means all address types, which only the descriptor format supports.
    property string addressType: ""
    property string errorText: ""
    property var clipboard: Clipboard
    readonly property bool allTypes: !extendedKeyFormat && addressType === ""
    readonly property var currentExport: {
        for (const item of exports) {
            if (item.id === addressType) return item
        }
        return null
    }
    readonly property string payload: allTypes ? exports.map(item => item.descriptor).join("\n")
        : !currentExport ? "" : extendedKeyFormat ? currentExport.extendedKey : currentExport.descriptor
    readonly property string sharedFingerprint: {
        const fingerprints = new Set(exports.map(item => item.fingerprint))
        return fingerprints.size === 1 ? exports[0].fingerprint : ""
    }
    readonly property var typeOptions: {
        const options = exports.map(item => ({
            id: item.id,
            label: item.label,
            description: item.description,
            objectName: "watchOnlyExportAddressType_" + item.id
        }))
        if (extendedKeyFormat || exports.length < 2) return options
        return [{
            id: "",
            label: qsTr("All address types"),
            description: qsTr("Complete watch-only wallet"),
            objectName: "watchOnlyExportAddressType_all"
        }].concat(options)
    }

    function preferredAddressType() {
        const preferred = wallet ? wallet.defaultReceiveAddressType() : ""
        return exports.some(item => item.id === preferred) ? preferred : exports[0].id
    }

    function openForWallet() {
        exports = wallet ? wallet.watchOnlyExports() : []
        if (exports.length === 0) return false
        extendedKeyFormat = false
        addressType = exports.length > 1 ? "" : exports[0].id
        errorText = ""
        open()
        return true
    }

    function setExtendedKeyFormat(enabled) {
        extendedKeyFormat = enabled
        if (enabled && addressType === "") addressType = preferredAddressType()
    }

    function shortKey(key) {
        return key.length > 17 ? key.substring(0, 8) + "..." + key.substring(key.length - 8) : key
    }

    // "sh(wpkh)" and a key become "sh(wpkh(tpubDDen...WQTz))".
    function shortDescriptor(item) {
        const scripts = item.scriptName.replace(/\)/g, "").split("(")
        return scripts.join("(") + "(" + shortKey(item.xpub) + ")".repeat(scripts.length)
    }

    function copy() {
        if (payload === "") return
        clipboard.setText(payload)
        copiedTimer.restart()
    }

    function save(fileUrl) {
        if (!wallet || payload === "") return
        errorText = wallet.saveWatchOnlyExport(fileUrl.toString(), payload)
    }

    function openSaveDialog() {
        const name = wallet && wallet.name.length > 0 ? wallet.name.replace(/[\\/]/g, "_") : "wallet"
        saveDialog.currentFile = "file://" + walletController.homePath() + "/" + name + "-watch-only.txt"
        saveDialog.open()
    }

    parent: Overlay.overlay
    width: Math.min(440, parent ? parent.width - 32 : 440)
    height: Math.min(card.implicitHeight, parent ? parent.height - 40 : card.implicitHeight)
    x: parent ? (parent.width - width) / 2 : 0
    y: parent ? Math.max(20, (parent.height - height) / 2) : 0
    padding: 0
    modal: true
    dim: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    background: null

    onWalletChanged: if (opened) close()
    onClosed: {
        exports = []
        addressType = ""
        errorText = ""
    }

    Overlay.modal: Rectangle {
        color: Qt.rgba(0, 0, 0, 0.6)
        Behavior on opacity { NumberAnimation { duration: 180 } }
    }
    enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 180 } }
    exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 150 } }

    Timer {
        id: copiedTimer
        interval: 1000
    }

    AppFileDialog {
        id: saveDialog
        objectName: "watchOnlyExportSaveDialog"
        fileMode: AppFileDialog.SaveFile
        nameFilters: [qsTr("Text files (*.txt)"), qsTr("All files (*)")]
        defaultSuffix: "txt"
        onAccepted: root.save(selectedFile)
    }

    contentItem: ScrollView {
        id: scroll
        clip: true
        contentWidth: availableWidth
        contentHeight: card.implicitHeight
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

        Pane {
            id: card
            width: scroll.availableWidth
            padding: width < 400 ? 16 : 20
            implicitHeight: contentItem.implicitHeight + topPadding + bottomPadding
            background: Rectangle {
                radius: 16
                color: Theme.color.neutral1
                border.width: 1
                border.color: Theme.color.neutral3
                SurfaceGradientBorder {
                    anchors.fill: parent
                    surfaceColor: parent.color
                    cornerRadius: parent.radius
                }
            }

            contentItem: ColumnLayout {
                spacing: 16

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10

                    CoreText {
                        Layout.fillWidth: true
                        text: qsTr("Export watch-only wallet")
                        font: Theme.text.title.font
                        horizontalAlignment: Text.AlignLeft
                        wrap: true
                    }

                    OverflowMenuButton {
                        id: moreButton
                        objectName: "watchOnlyExportMoreButton"
                        circular: true
                        size: 30
                        iconSize: 24
                        Accessible.name: qsTr("More options")
                        checked: moreMenu.opened
                        onClicked: moreMenu.opened ? moreMenu.close() : moreMenu.open()

                        ContextMenu {
                            id: moreMenu
                            x: moreButton.width - width
                            y: moreButton.height + 8

                            ContextMenuButton {
                                objectName: "watchOnlyExportSaveMenuButton"
                                enabled: root.payload !== ""
                                text: qsTr("Save to file")
                                onTriggered: root.openSaveDialog()
                            }
                        }
                    }

                    CloseButton {
                        objectName: "watchOnlyExportClose"
                        iconColor: Theme.color.neutral7
                        Accessible.name: qsTr("Close watch-only export")
                        onClicked: root.close()
                    }
                }

                SegmentedPicker {
                    objectName: "watchOnlyExportFormatPicker"
                    Layout.fillWidth: true
                    implicitHeight: 36
                    //: Export formats: an output descriptor, or a bare xpub/ypub/zpub key for wallets that only accept those.
                    model: [qsTr("Descriptor"), qsTr("Extended key")]
                    currentIndex: root.extendedKeyFormat ? 1 : 0
                    onSelected: index => root.setExtendedKeyFormat(index === 1)
                }

                Rectangle {
                    readonly property real side: Math.min(parent.width, root.allTypes ? 280 : 220)
                    Layout.preferredWidth: side
                    Layout.preferredHeight: side
                    Layout.alignment: Qt.AlignHCenter
                    radius: 10
                    color: "white"

                    QRImage {
                        objectName: "watchOnlyExportQRImage"
                        anchors.fill: parent
                        anchors.margins: 14
                        code: root.payload
                        backgroundColor: "white"
                        foregroundColor: "black"
                    }
                }

                AddressLabel {
                    objectName: "watchOnlyExportKeyText"
                    visible: root.extendedKeyFormat
                    Layout.fillWidth: true
                    address: visible && root.currentExport ? root.currentExport.extendedKey : ""
                    textStyle: Theme.text.monoDescription
                    clipboard: root.clipboard
                    embedded: true
                }

                DescriptorText {
                    objectName: "watchOnlyExportDescriptor"
                    visible: !root.extendedKeyFormat && !root.allTypes && !!root.currentExport
                    Layout.fillWidth: true
                    parts: visible ? root.currentExport.descriptorParts : ({})
                    descriptor: visible ? root.currentExport.descriptor : ""
                    clipboard: root.clipboard
                }

                FormSection {
                    objectName: "watchOnlyExportTypeList"
                    visible: root.allTypes
                    Layout.fillWidth: true
                    isOnSurface: true
                    showGradientBorder: false
                    title: qsTr("All address types")
                    footerText: root.sharedFingerprint !== ""
                        //: %1 is the wallet's BIP32 master key fingerprint.
                        ? qsTr("Fingerprint %1. Each descriptor covers receive and change addresses.").arg(root.sharedFingerprint)
                        : qsTr("Each descriptor covers receive and change addresses.")

                    Repeater {
                        model: root.allTypes ? root.exports : []

                        ListRow {
                            required property var modelData
                            required property int index
                            objectName: "watchOnlyExportType_" + modelData.id
                            Layout.fillWidth: true
                            title: modelData.label
                            description: root.shortDescriptor(modelData)
                            showDivider: index < root.exports.length - 1
                            showsDisclosureIndicator: true
                            onClicked: root.addressType = modelData.id
                            trailingItem: CopyButton {
                                objectName: "watchOnlyExportTypeCopy_" + modelData.id
                                displayMode: CopyButton.IconOnly
                                Accessible.name: qsTr("Copy %1 descriptor").arg(modelData.label)
                                onCopyRequested: root.clipboard.setText(modelData.descriptor)
                            }
                        }
                    }
                }

                FormSection {
                    visible: !root.allTypes
                    Layout.fillWidth: true
                    isOnSurface: true
                    showGradientBorder: false

                    ListRow {
                        id: addressTypeRow
                        objectName: "watchOnlyExportAddressType"
                        Layout.fillWidth: true
                        title: qsTr("Address type")
                        showsDisclosureIndicator: root.typeOptions.length > 1
                        trailingItem: CoreText {
                            objectName: "watchOnlyExportAddressTypeValue"
                            text: root.currentExport ? root.currentExport.label : ""
                            font: Theme.text.description.font
                            color: Theme.color.neutral9
                            wrap: false
                        }
                        onClicked: {
                            if (root.typeOptions.length < 2) return
                            addressTypeMenu.opened ? addressTypeMenu.close() : addressTypeMenu.open()
                        }

                        ContextMenu {
                            id: addressTypeMenu
                            x: addressTypeRow.width - width
                            y: addressTypeRow.height
                            minMenuWidth: 320

                            ContextMenuPicker {
                                objectName: "watchOnlyExportAddressTypePicker"
                                title: qsTr("Address Type")
                                textRole: "label"
                                valueRole: "id"
                                subtitleRole: "description"
                                objectNameRole: "objectName"
                                model: root.typeOptions
                                currentValue: root.addressType
                                onActivated: function(value) {
                                    addressTypeMenu.close()
                                    root.addressType = value
                                }
                            }
                        }
                    }

                    ValueRow {
                        objectName: "watchOnlyExportFingerprint"
                        visible: !!root.currentExport && root.currentExport.fingerprint !== ""
                        Layout.fillWidth: true
                        //: BIP32 fingerprint of the wallet's master key.
                        title: qsTr("Master fingerprint")
                        value: root.currentExport ? root.currentExport.fingerprint : ""
                        valueTextStyle: Theme.text.monoDescription
                    }

                    ValueRow {
                        objectName: "watchOnlyExportDerivationPath"
                        visible: !!root.currentExport && root.currentExport.derivationPath !== ""
                        Layout.fillWidth: true
                        title: qsTr("Derivation path")
                        value: root.currentExport ? root.currentExport.derivationPath : ""
                        valueTextStyle: Theme.text.monoDescription
                        showDivider: !root.extendedKeyFormat
                    }

                    ValueRow {
                        objectName: "watchOnlyExportChains"
                        visible: !root.extendedKeyFormat && !!root.currentExport
                        Layout.fillWidth: true
                        //: Derivation branches the descriptor covers: receiving addresses and change addresses.
                        title: qsTr("Chains")
                        value: !root.currentExport ? ""
                            : root.currentExport.changeChain === "" ? qsTr("Receive %1").arg(root.currentExport.receiveChain)
                            : qsTr("Receive %1, change %2").arg(root.currentExport.receiveChain).arg(root.currentExport.changeChain)
                        showDivider: false
                    }
                }

                CoreText {
                    objectName: "watchOnlyExportNote"
                    Layout.fillWidth: true
                    text: {
                        if (root.allTypes || !root.currentExport) {
                            return qsTr("Anyone with this export can see all of this wallet's addresses and transactions, but cannot spend from it.")
                        }
                        let note = qsTr("Only includes funds on %1 addresses.").arg(root.currentExport.label)
                        if (root.extendedKeyFormat && root.currentExport.id === "bech32m") {
                            note += " " + qsTr("Most wallets need the descriptor format to import Taproot keys.")
                        }
                        return note
                    }
                    font: Theme.text.caption.font
                    lineHeight: Theme.text.caption.lineHeight
                    lineHeightMode: Text.FixedHeight
                    color: Theme.color.neutral7
                    horizontalAlignment: Text.AlignLeft
                    wrap: true
                }

                CoreText {
                    objectName: "watchOnlyExportError"
                    visible: root.errorText !== ""
                    Layout.fillWidth: true
                    text: root.errorText
                    color: Theme.color.red
                    font: Theme.text.caption.font
                    horizontalAlignment: Text.AlignLeft
                    wrap: true
                }

                OutlineButton {
                    objectName: "watchOnlyExportCopyButton"
                    Layout.fillWidth: true
                    enabled: root.payload !== ""
                    text: copiedTimer.running ? qsTr("Copied")
                        : root.allTypes ? qsTr("Copy all descriptors")
                        : root.extendedKeyFormat ? qsTr("Copy extended key")
                        : qsTr("Copy descriptor")
                    onClicked: root.copy()
                }
            }
        }
    }
}
