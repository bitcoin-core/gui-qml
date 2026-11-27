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
    // The sharing model owns its export and cancels it while it is inactive.
    readonly property RequestImageExportOperation exportOperation: sharing ? sharing.qrExport : null
    readonly property bool exportPending: exportOperation !== null && exportOperation.busy
    Binding { target: root.sharing; property: "active"; value: root.visible && root.interested; when: root.sharing !== null }
    onVisibleChanged: if (!visible) saveDialog.close()
    onInterestedChanged: if (!interested) saveDialog.close()
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
        text: root.exportOperation && root.exportOperation.error.length > 0 ? root.exportOperation.error
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
            onClicked: root.sharing.beginQrExport()
        }
    }
    Instantiator {
        model: root.exportOperation ? [root.exportOperation] : []
        delegate: SaveConfirmationPopup {
            required property FileSaveOperation modelData
            objectName: "savedRequestExportConfirmation"
            // Keeps the overlay when the page leaves the window. A popup whose parent
            // goes null there loses its window while its item is still in the overlay,
            // and a hover event then crashes Qt 6.8.4 and newer.
            Binding on parent {
                when: root.Overlay.overlay !== null
                value: root.Overlay.overlay
                restoreMode: Binding.RestoreNone
            }
            operation: modelData
            active: root.visible && root.interested
            saveTitle: qsTr("Save QR image?")
            replaceTitle: qsTr("Replace QR image?")
            pathObjectName: "savedRequestExportPath"
            approveObjectName: "savedRequestApproveExportButton"
            refuseObjectName: "savedRequestCancelExportButton"
        }
    }
    AppFileDialog {
        id: saveDialog
        objectName: "savedRequestPngDialog"
        fileMode: AppFileDialog.SaveFile
        nameFilters: [qsTr("PNG images (*.png)")]
        suggestedName: "payment-request.png"
        confirmOverwrite: false
        contextObject: root.exportOperation
        enabled: root.visible && root.interested && root.sharing !== null && root.sharing.active
        onChosen: function(context, file) {
            const original = context as RequestImageExportOperation
            if (original) original.choose(file)
        }
        onRejected: if (root.exportOperation) root.exportOperation.cancel()
    }
    // The export asks for its file; the native dialog is the view's.
    readonly property bool chooserRequested: visible && interested && exportOperation !== null && exportOperation.choosing
    onChooserRequestedChanged: if (chooserRequested && !saveDialog.visible && !saveDialog.open()) exportOperation.chooserFailed()
}
