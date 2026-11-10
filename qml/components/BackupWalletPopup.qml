// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0

import "../controls"

DialogPopup {
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

    title: canChooseType ? qsTr("Back up or export wallet") : qsTr("Back up wallet")
    implicitWidth: 520

    contentItem: ColumnLayout {
        spacing: Theme.space.xl

        DialogHeader {
            Layout.fillWidth: true
            title: root.title
            closeButtonObjectName: "backupWalletClose"
            onCloseRequested: root.close()
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.space.l

            ColumnLayout {
                visible: root.canChooseType
                Layout.fillWidth: true
                spacing: Theme.space.m

                OptionButton {
                    objectName: "backupTypeFull"
                    Layout.fillWidth: true
                    selected: !root.watchOnly
                    enabled: !root.pending
                    surfaceSelectionStyle: true
                    text: qsTr("Full backup")
                    description: qsTr("Includes private keys. Restores a wallet that can spend.")
                    onClicked: root.watchOnly = false
                }

                OptionButton {
                    objectName: "backupTypeWatchOnly"
                    Layout.fillWidth: true
                    selected: root.watchOnly
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
                textStyle: Theme.text.description
                color: Theme.color.neutral8
                horizontalAlignment: Text.AlignLeft
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.space.ml

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.space.xxs

                    CoreText {
                        Layout.fillWidth: true
                        text: qsTr("Save to")
                        textStyle: Theme.text.caption
                        color: Theme.color.neutral7
                        horizontalAlignment: Text.AlignLeft
                    }

                    CoreText {
                        objectName: "backupDestinationPath"
                        Layout.fillWidth: true
                        text: root.destinationPath
                        textStyle: Theme.text.monoCaption
                        color: Theme.color.neutral9
                        horizontalAlignment: Text.AlignLeft
                        // A cut path could hide the part that tells two folders apart.
                        wrapMode: Text.WrapAnywhere
                    }
                }

                NeutralButton {
                    objectName: "backupChangeLocation"
                    Layout.alignment: Qt.AlignVCenter
                    enabled: !root.pending
                    text: qsTr("Change…")
                    onClicked: root.locationChangeRequested()
                }
            }

            CoreText {
                objectName: "backupWalletErrorText"
                visible: root.errorText.length > 0
                Layout.fillWidth: true
                text: root.errorText
                textStyle: Theme.text.caption
                color: Theme.color.dangerText
                horizontalAlignment: Text.AlignLeft
            }
        }

        PrimaryButton {
            objectName: "backupSaveButton"
            Layout.fillWidth: true
            enabled: !root.pending && root.destinationPath.length > 0
            busy: root.pending
            text: root.pending ? qsTr("Saving…")
                : root.watchOnly ? qsTr("Save watch-only copy")
                : qsTr("Save backup")
            onClicked: root.saveRequested()
        }

        CoreText {
            Layout.fillWidth: true
            text: root.canChooseType ? qsTr("Restore either file with Import wallet.") : qsTr("Restore it with Import wallet.")
            textStyle: Theme.text.caption
            color: Theme.color.neutral7
            horizontalAlignment: Text.AlignHCenter
        }
    }
}
