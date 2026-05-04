// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Dialogs
import org.bitcoincore.qt 1.0

import "../controls"

Popup {
    id: root

    property string code: ""
    property string label: ""
    signal copyRequested()

    anchors.centerIn: Overlay.overlay
    width: 320
    modal: true
    dim: true
    padding: 20

    Overlay.modal: Rectangle {
        color: Qt.rgba(0, 0, 0, 0.6)
    }

    background: Rectangle {
        color: Theme.color.neutral0
        border.color: Theme.color.neutral4
        border.width: 1
        radius: 10
    }

    QRImage {
        id: qrSaveImage
        visible: false
        width: 280
        height: 280
        backgroundColor: "white"
        foregroundColor: "black"
        code: root.code
    }

    FileDialog {
        id: saveDialog
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("PNG files (*.png)")]
        currentFile: "file:///" + (root.label !== "" ? root.label : qsTr("payment-request")) + ".png"
        onAccepted: {
            qrSaveImage.grabToImage(function(result) {
                var path = saveDialog.selectedFile.toString().replace("file://", "")
                if (!path.endsWith(".png")) path += ".png"
                result.saveToFile(path)
            })
        }
    }

    contentItem: ColumnLayout {
        spacing: 15

        Item {
            Layout.fillWidth: true
            implicitHeight: closeButton.height

            IconButton {
                id: closeButton
                anchors.right: parent.right
                iconSource: "image://images/cross"
                Accessible.name: qsTr("Close")
                size: 30
                onClicked: root.close()
            }
        }

        QRImage {
            id: qrImage
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: 280
            Layout.preferredHeight: 280
            backgroundColor: "transparent"
            foregroundColor: Theme.color.neutral9
            code: root.code
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            OutlineButton {
                Layout.fillWidth: true
                Layout.preferredWidth: 0
                text: qsTr("Download")
                onClicked: saveDialog.open()
            }

            OutlineButton {
                Layout.fillWidth: true
                Layout.preferredWidth: 0
                text: qsTr("Copy")
                onClicked: root.copyRequested()
            }
        }
    }
}
