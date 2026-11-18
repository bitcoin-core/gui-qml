// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Layouts 1.15

import "../../controls"
import "../../components"

SettingsPage {
    id: root
    objectName: "storageSettingsPage"
    title: qsTr("Storage")
    showBackButton: false

    required property var settingsModel
    property bool onboarding: false
    readonly property string pruneTargetError: settingsModel.pruneTargetError
    property var coreSettingsModel: settingsModel.coreSettings
    readonly property var pruneSetting: coreSettingsModel.entry("prune")
    FormSection {
        Layout.fillWidth: true
        title: qsTr("Block storage")
        surfaceLevel: root.onboarding ? 1 : 0

        SwitchRow {
            Layout.fillWidth: true
            title: qsTr("Store recent blocks only")
            dividerColor: Theme.color.surfaceDivider
            supportingText: root.pruneSetting.infoText
            enabled: root.pruneSetting.canEdit
            switchObjectName: "pruneSwitch"
            checked: root.pruneSetting.enabled
            onToggled: root.pruneSetting.enabled = checked
        }

        TextFieldRow {
            id: pruneTargetRow
            Layout.fillWidth: true
            title: qsTr("Block storage target (MiB; 1 is manual)")
            enabled: root.pruneSetting.enabled && root.pruneSetting.canEdit
            fieldObjectName: "pruneTargetInput"
            fieldWidth: 80
            text: root.settingsModel.pruneTargetText
            errorText: root.settingsModel.pruneTargetError
            supportingText: root.settingsModel.pruneTargetError.length === 0 ? root.pruneSetting.infoText : ""
            showDivider: false
            onTextEdited: function(text) { root.settingsModel.editPruneTarget(text) }
        }
    }

    PrimaryButton {
        objectName: "restoreValidPruneInput"
        visible: root.settingsModel.pruneTargetError.length > 0
        text: qsTr("Restore the last valid target")
        onClicked: root.coreSettingsModel.discardPruneInput()
    }

    PrimaryButton {
        objectName: "onboardingConfirmUnpruning"
        visible: root.onboarding && !root.settingsModel.prune
        text: qsTr("Allow rebuilding and downloading blocks if this profile was pruned")
        onClicked: root.settingsModel.confirmUnpruning(root.settingsModel.unpruneConsentToken)
    }

    FormSection {
        Layout.fillWidth: true
        title: qsTr("Data directory")
        surfaceLevel: root.onboarding ? 1 : 0
        description: qsTr("Selected before startup. The data directory cannot be changed while the node is running.")

        FormRow {
            Layout.fillWidth: true
            title: qsTr("Location")
            showDivider: false
            bodyItem: CoreText {
                objectName: "dataDirectoryValue"
                Layout.fillWidth: true
                text: root.settingsModel.dataDir
                color: Theme.color.neutral7
                textStyle: Theme.text.monoCaption
                horizontalAlignment: Text.AlignLeft
                wrap: true
            }
        }
    }

    ToastBanner {
        objectName: "storageRestartNotice"
        shown: !root.onboarding && root.settingsModel.storageSettingsDirty
        Layout.fillWidth: true
        Layout.maximumWidth: root.contentLayout.width
        text: qsTr("Restart the application for these changes to take effect.")
    }
}
