// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
import QtQuick 2.15
import QtQml 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0
import "../controls"

ColumnLayout {
    id: root
    required property RequestSharingModel sharing
    property bool interested: true
    property RequestImageExportOperation exportOperation: null
    property RequestImageExportOperation chooserOperation: null
    readonly property bool exportPending: exportOperation !== null && (exportOperation.busy || exportOperation.choosing || exportOperation.confirmationRequired)
    Binding { target: root.sharing; property: "active"; value: root.visible && root.interested; when: root.sharing !== null }
    onVisibleChanged: if (!visible) {
        saveDialog.close()
        if (exportOperation && exportOperation.canCancel) exportOperation.cancel()
    }
    onInterestedChanged: if (!interested) {
        saveDialog.close()
        if (exportOperation && exportOperation.canCancel) exportOperation.cancel()
    }
    Component.onDestruction: if (sharing) sharing.retire()
    OwnedQrImage {
        id: qr
        objectName: "savedRequestQrImage"
        Layout.alignment: Qt.AlignHCenter
        Layout.preferredWidth: 260
        Layout.preferredHeight: 260
        pixels: root.sharing ? root.sharing.image : qr.empty
    }
    CoreText {
        objectName: "savedRequestSharingError"
        Layout.fillWidth: true
        text: root.exportOperation && root.exportOperation.errorText.length > 0 ? root.exportOperation.errorText
            : root.sharing && root.sharing.lastOperation && root.sharing.lastOperation.errorText.length > 0 ? root.sharing.lastOperation.errorText : root.sharing ? root.sharing.disabledReason : ""
        visible: text.length > 0
        color: Theme.color.dangerText
    }
    RowLayout {
        NeutralButton { objectName: "savedRequestCopyUriButton"; text: qsTr("Copy request"); enabled: root.sharing !== null && root.sharing.canCopyUri; onClicked: root.sharing.copyUri() }
        NeutralButton { objectName: "savedRequestCopyQrButton"; text: qsTr("Copy QR image"); enabled: root.sharing !== null && root.sharing.canCopyQr; onClicked: root.sharing.copyQr() }
        NeutralButton {
            objectName: "savedRequestExportQrButton"
            text: qsTr("Save QR image")
            enabled: root.sharing !== null && root.sharing.canExportQr && !root.exportPending
            onClicked: root.exportOperation = root.sharing.beginQrExport()
        }
    }
    Instantiator {
        model: root.exportOperation ? [root.exportOperation] : []
        delegate: SaveConfirmationPopup {
            required property RequestImageExportOperation modelData
            objectName: "savedRequestExportConfirmation"
            parent: root.Overlay.overlay
            visible: root.visible && root.interested && modelData.confirmationRequired
            replacing: modelData.replacing
            finalPath: modelData.finalPath
            saveTitle: qsTr("Save QR image?")
            replaceTitle: qsTr("Replace QR image?")
            pathObjectName: "savedRequestExportPath"
            approveObjectName: "savedRequestApproveExportButton"
            refuseObjectName: "savedRequestCancelExportButton"
            onApproved: modelData.approve()
            onRefused: modelData.cancel()
        }
    }
    AppFileDialog {
        id: saveDialog
        objectName: "savedRequestPngDialog"
        fileMode: AppFileDialog.SaveFile
        nameFilters: [qsTr("PNG images (*.png)")]
        suggestedName: "payment-request.png"
        confirmOverwrite: false
        contextObject: root.chooserOperation
        enabled: root.visible && root.interested && root.sharing !== null && root.sharing.active
        onChosen: function(context, file) {
            const original = context as RequestImageExportOperation
            if (original) original.choose(file)
        }
        onRejected: if (root.chooserOperation) root.chooserOperation.cancel()
    }
    Connections {
        target: root.exportOperation
        function onChanged() {
            if (root.visible && root.interested && root.exportOperation && root.exportOperation.choosing && !saveDialog.visible) {
                root.chooserOperation = root.exportOperation
                if (!saveDialog.open()) root.chooserOperation.chooserFailed()
            }
        }
    }
}
