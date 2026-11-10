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
    objectName: "backupWalletPopup"

    property bool canChooseType: true
    property bool watchOnly: false
    property bool encrypted: false
    property string destinationPath: ""
    property bool pending: false
    property string errorText: ""

    signal locationChangeRequested()
    signal saveRequested()

    parent: Overlay.overlay
    width: Math.min(460, parent ? parent.width - 32 : 460)
    x: parent ? (parent.width - width) / 2 : 0
    y: parent ? Math.max(20, (parent.height - height) / 2) : 0
    padding: 0
    modal: true
    dim: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    background: null

    Overlay.modal: Rectangle {
        color: Qt.rgba(0, 0, 0, 0.6)
        Behavior on opacity { NumberAnimation { duration: 180 } }
    }
    enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 180 } }
    exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 150 } }

    contentItem: Pane {
        padding: root.width < 400 ? 16 : 20
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
                    text: root.canChooseType ? qsTr("Back up or export wallet") : qsTr("Back up wallet")
                    font: Theme.text.title.font
                    horizontalAlignment: Text.AlignLeft
                    wrap: true
                }

                CloseButton {
                    objectName: "backupWalletClose"
                    iconColor: Theme.color.neutral7
                    Accessible.name: qsTr("Close")
                    onClicked: root.close()
                }
            }

            ColumnLayout {
                visible: root.canChooseType
                Layout.fillWidth: true
                spacing: 10

                OptionButton {
                    objectName: "backupTypeFull"
                    Layout.fillWidth: true
                    checkable: false
                    checked: !root.watchOnly
                    enabled: !root.pending
                    surfaceSelectionStyle: true
                    text: qsTr("Full backup")
                    description: qsTr("Includes private keys. Restores a wallet that can spend.")
                    onClicked: root.watchOnly = false
                }

                OptionButton {
                    objectName: "backupTypeWatchOnly"
                    Layout.fillWidth: true
                    checkable: false
                    checked: root.watchOnly
                    enabled: !root.pending
                    surfaceSelectionStyle: true
                    text: qsTr("Watch-only copy")
                    //: Describes a watch-only wallet file for restoring on another Bitcoin Core node.
                    description: qsTr("No private keys. Shows balance and history, but cannot spend.")
                    onClicked: root.watchOnly = true
                }
            }

            CoreText {
                objectName: "backupWalletWarning"
                Layout.fillWidth: true
                text: root.watchOnly
                    ? qsTr("Anyone with this file can see your balance and every transaction.")
                    : root.encrypted
                    ? qsTr("Anyone with this file and your password can spend your bitcoin. Keep it somewhere safe.")
                    : qsTr("Anyone with this file can spend your bitcoin. Keep it somewhere safe.")
                font: Theme.text.caption.font
                lineHeight: Theme.text.caption.lineHeight
                lineHeightMode: Text.FixedHeight
                color: Theme.color.neutral8
                horizontalAlignment: Text.AlignLeft
                wrap: true
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 12

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2

                    CoreText {
                        Layout.fillWidth: true
                        text: qsTr("Save to")
                        font: Theme.text.caption.font
                        color: Theme.color.neutral7
                        horizontalAlignment: Text.AlignLeft
                    }

                    CoreText {
                        objectName: "backupDestinationPath"
                        Layout.fillWidth: true
                        text: root.destinationPath
                        font: Theme.text.monoCaption.font
                        color: Theme.color.neutral9
                        horizontalAlignment: Text.AlignLeft
                        // A cut path could hide the part that tells two folders apart.
                        wrapMode: Text.WrapAnywhere
                    }
                }

                OutlineButton {
                    objectName: "backupChangeLocation"
                    Layout.alignment: Qt.AlignVCenter
                    enabled: !root.pending
                    text: qsTr("Change")
                    onClicked: root.locationChangeRequested()
                }
            }

            CoreText {
                objectName: "backupWalletErrorText"
                visible: root.errorText.length > 0
                Layout.fillWidth: true
                text: root.errorText
                font: Theme.text.caption.font
                color: Theme.color.red
                horizontalAlignment: Text.AlignLeft
                wrap: true
            }

            ContinueButton {
                objectName: "backupSaveButton"
                Layout.fillWidth: true
                enabled: !root.pending && root.destinationPath.length > 0
                busy: root.pending
                text: root.pending ? qsTr("Saving...")
                    : root.watchOnly ? qsTr("Save watch-only copy")
                    : qsTr("Save backup")
                onClicked: root.saveRequested()
            }

            CoreText {
                Layout.fillWidth: true
                text: root.canChooseType ? qsTr("Restore either file with Import wallet.") : qsTr("Restore it with Import wallet.")
                font: Theme.text.caption.font
                lineHeight: Theme.text.caption.lineHeight
                lineHeightMode: Text.FixedHeight
                color: Theme.color.neutral7
                horizontalAlignment: Text.AlignHCenter
                wrap: true
            }
        }
    }
}
