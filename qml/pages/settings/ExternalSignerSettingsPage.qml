// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import org.bitcoincore.qt 1.0
import QtQuick.Layouts 1.15

import "../../controls"
import "../../components"

SettingsPage {
    id: root
    objectName: "externalSignerSettingsPage"
    title: qsTr("External signer")
    showBackButton: false

    property SettingsEditorModel editor: null
    readonly property SettingsEntryModel signerEntry: editor ? editor.entry("signer") : null
    readonly property bool savingConfiguration: editor ? editor.phase === SettingsEditorModel.Checking || editor.phase === SettingsEditorModel.Saving : discovery && discovery.saving
    readonly property bool canApplyConfiguration: editor ? editor.canSave : discovery && discovery.canApply
    readonly property SignerDiscoveryModel discovery: ApplicationModels.signers
    readonly property bool signerConnected: discovery && !savingConfiguration && discovery.fresh && discovery.uniqueSignerId.length > 0
    readonly property string signerStatusText: !discovery ? qsTr("Signer discovery is unavailable.")
        : savingConfiguration ? qsTr("Saving signer configuration…")
        : discovery.pending ? qsTr("Checking external signer…")
        : discovery.error.message.length > 0 ? discovery.error.message
        : signerConnected ? qsTr("Detected external signer: %1").arg(discovery.uniqueSignerName)
        : qsTr("No external signer is currently detected.")

    PageHeading {
        objectName: "externalSignerIntroduction"
        Layout.fillWidth: true
        description: qsTr("Connect a hardware wallet or another external signing tool.")
    }

    FormSection {
        objectName: "externalSignerPathSection"
        Layout.fillWidth: true
        title: qsTr("Signer path")
        footerText: qsTr("The add wallet flow can offer external wallets when exactly one supported signer is connected.")

        FormRow {
            objectName: "externalSignerPathRow"
            Layout.fillWidth: true
            enabled: root.discovery !== null
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

                    LabeledTextField {
                        id: signerPathInput
                        Layout.fillWidth: true
                        // The section title is the visible label.
                        accessibleName: qsTr("Signer path")
                        fieldObjectName: "externalSignerPathInput"
                        fieldHeight: Theme.size.control
                        fieldBackgroundColor: Theme.color.surface2
                        fieldTextStyle: Theme.text.monoDescription
                        text: root.signerEntry ? root.signerEntry.text : root.discovery ? root.discovery.editedPath : ""
                        maximumLength: 32769
                        readOnly: root.editor ? !root.signerEntry || !root.signerEntry.editable : !root.discovery || !root.discovery.configurationKnown || !root.discovery.editable || root.discovery.saving
                        placeholderText: qsTr("Enter external signer path")
                        onTextEdited: function(text) { if (root.signerEntry) root.signerEntry.editText(text); else if (root.discovery) root.discovery.editPath(text) }
                        onAccepted: signerPathInput.field.focus = false
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
                            color: !root.discovery || root.savingConfiguration || !root.discovery.fresh ? Theme.color.neutral7
                                : root.signerConnected ? Theme.color.success : Theme.color.danger

                            Behavior on color {
                                enabled: Theme.motion.animated
                                ColorAnimation { duration: Theme.motion.normal }
                            }
                        }

                        CoreText {
                            objectName: "externalSignerStatusText"
                            Layout.fillWidth: true
                            text: root.signerStatusText
                            color: Theme.color.neutral7
                            textStyle: Theme.text.description
                            horizontalAlignment: Text.AlignLeft
                            wrap: true
                        }
                    }
                }

                PrimaryButton {
                    objectName: "externalSignerCheckDeviceButton"
                    Layout.preferredWidth: Math.max(140, implicitWidth)
                    Layout.preferredHeight: Math.max(40, implicitHeight)
                    Layout.alignment: Qt.AlignVCenter
                    text: root.editor && root.editor.inputPending ? qsTr("Checking edited path…") : root.canApplyConfiguration ? qsTr("Save and check") : qsTr("Check device")
                    textStyle: Theme.text.subheading
                    enabled: root.discovery && (!root.editor || !root.editor.inputPending) && !root.savingConfiguration && (root.canApplyConfiguration || root.discovery.canRefresh)
                    onClicked: {
                        if (!enabled) return
                        if (root.canApplyConfiguration) { if (root.editor) root.editor.save(); else root.discovery.apply(); }
                        else root.discovery.refresh()
                    }
                }
            }
        }
    }

    CoreText {
        objectName: "externalSignerEditErrorText"
        Layout.fillWidth: true
        text: root.editor ? root.signerEntry.error || root.editor.error : root.discovery ? root.discovery.editError.message : ""
        visible: text.length > 0
        color: Theme.color.dangerText
        wrap: true
    }
    CoreText {
        objectName: "externalSignerEffectiveConfigurationText"
        Layout.fillWidth: true
        text: root.discovery && root.discovery.configurationSource === "CommandLine" ? qsTr("Set by command line. Remove the override to edit it here.")
            : root.discovery && (root.signerEntry ? root.signerEntry.text : root.discovery.editedPath) !== root.discovery.effectivePath ? qsTr("The edited path has not been applied. Device status refers to the effective configuration.") : ""
        visible: text.length > 0
        wrap: true
    }
    onVisibleChanged: if (visible && root.discovery) root.discovery.refresh()
    Component.onCompleted: ApplicationModels.walletController.creation.refreshSigners()
}
