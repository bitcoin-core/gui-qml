pragma ComponentBehavior: Bound

// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

import "../../../controls"
import "../../../components"

SettingsPage {
    id: root
    objectName: "settingsv2ExternalSignerSettingsPage"
    title: qsTr("External signer")
    showBackButton: false

    readonly property var signerStatus: (optionsModel.coreSettingStatuses || ({})).signer || ({})
    readonly property string signerPathError: optionsModel.externalSignerPathValidationError(signerPathInput.text)
    readonly property bool signerConnected: root.signerPathError.length === 0
        && walletController.canCreateExternalSignerWallet
    readonly property string signerStatusText: {
        if (root.signerPathError.length > 0) return root.signerPathError
        if (walletController.canCreateExternalSignerWallet) {
            return qsTr("Detected external signer: %1").arg(walletController.externalSignerName)
        }
        if (walletController.externalSignerError.length > 0) {
            return walletController.externalSignerError
        }
        if ((root.signerStatus.infoText || "").length > 0) {
            return root.signerStatus.infoText
        }
        if (optionsModel.walletSettingsDirty) {
            return qsTr("Path updated. Press Check device to rescan with the current signer command.")
        }
        if (optionsModel.externalSignerPath.length > 0) {
            return qsTr("No external signer is currently detected.")
        }
        return qsTr("Set the command path for HWI or another external signer tool.")
    }

    function commitSignerPath() {
        if (root.signerPathError.length > 0) return false
        const normalizedPath = signerPathInput.text.trim()
        if (normalizedPath !== optionsModel.externalSignerPath) {
            optionsModel.externalSignerPath = normalizedPath
        }
        return true
    }

    function checkDevice() {
        if (root.commitSignerPath()) walletController.refreshExternalSignerStatus()
    }

    PageHeading {
        objectName: "settingsv2ExternalSignerIntroduction"
        Layout.fillWidth: true
        description: qsTr("Connect a hardware wallet or another external signing tool.")
    }

    FormSection {
        objectName: "settingsv2ExternalSignerPathSection"
        Layout.fillWidth: true
        title: qsTr("Signer path")
        footerText: qsTr("The add wallet flow can offer external wallets when exactly one supported signer is connected.")

        FormRow {
            objectName: "settingsv2ExternalSignerPathRow"
            Layout.fillWidth: true
            enabled: root.signerStatus.canEdit !== false
            showDivider: false
            bodySpacing: 0
            topPadding: 16
            bottomPadding: 16
            bodyItem: RowLayout {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                spacing: 16

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    spacing: 12

                    TextField {
                        id: signerPathInput
                        objectName: "externalSignerPathInput"
                        Layout.fillWidth: true
                        implicitHeight: 37
                        text: optionsModel.externalSignerPath
                        placeholderText: qsTr("Enter external signer path")
                        placeholderTextColor: Theme.color.neutral7
                        color: Theme.color.neutral9
                        font: Theme.text.description.font
                        selectByMouse: true
                        leftPadding: 15
                        rightPadding: 10
                        topPadding: 0
                        bottomPadding: 0
                        verticalAlignment: TextInput.AlignVCenter
                        background: Rectangle {
                            color: Theme.color.neutral2
                            radius: 5

                            FocusBorder {
                                objectName: "externalSignerPathFocusBorder"
                                visible: signerPathInput.activeFocus
                                border.color: Theme.color.orange
                                borderRadius: 7
                                topMargin: -2
                                bottomMargin: -2
                                leftMargin: -2
                                rightMargin: -2
                            }
                        }
                        onEditingFinished: root.checkDevice()
                        onAccepted: focus = false
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Rectangle {
                            objectName: "externalSignerStatusIndicator"
                            Layout.preferredWidth: 10
                            Layout.preferredHeight: 10
                            Layout.alignment: Qt.AlignTop
                            Layout.topMargin: 4
                            radius: width / 2
                            color: root.signerConnected ? Theme.color.green : Theme.color.red

                            Behavior on color {
                                ColorAnimation { duration: 150 }
                            }
                        }

                        CoreText {
                            objectName: "externalSignerStatusText"
                            Layout.fillWidth: true
                            text: root.signerStatusText
                            color: Theme.color.neutral7
                            font: Theme.text.description.font
                            lineHeight: Theme.text.description.lineHeight
                            lineHeightMode: Text.FixedHeight
                            horizontalAlignment: Text.AlignLeft
                            wrap: true
                        }
                    }
                }

                ContinueButton {
                    objectName: "externalSignerCheckDeviceButton"
                    Layout.preferredWidth: 140
                    Layout.preferredHeight: 40
                    Layout.alignment: Qt.AlignVCenter
                    text: qsTr("Check device")
                    textStyle: Theme.text.subheading
                    enabled: root.signerPathError.length === 0
                        && root.signerStatus.canEdit !== false
                    onClicked: root.checkDevice()
                }
            }
        }
    }

    Component.onCompleted: walletController.refreshExternalSignerStatus()
}
