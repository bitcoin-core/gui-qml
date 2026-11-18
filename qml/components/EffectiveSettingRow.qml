// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
import QtQuick
import QtQuick.Layouts
import org.bitcoincore.qt 1.0
import "../controls"
FormRow {
    id: root
    required property SettingsEntryModel entry
    objectName: "effectiveSetting_" + entry.key
    title: entry.title
    description: qsTr("Running: %1 · Next start: %2 · Source: %3").arg(entry.effectiveText).arg(entry.nextStartText).arg(entry.source)
    supportingText: entry.information
    errorText: entry.error
    trailingItem: RowLayout {
        OptionSwitch {
            objectName: root.entry.key + "SettingSwitch"
            visible: root.entry.kind === SettingsEntryModel.Boolean
            enabled: root.entry.editable
            checked: root.entry.booleanValue
            onToggled: root.entry.editBoolean(checked)
        }
        // The shared field draws its own Theme surface. A plain TextField kept
        // the platform's light background under dark-theme text on Windows.
        LabeledTextField {
            fieldObjectName: root.entry.key + "SettingInput"
            Layout.preferredWidth: 180
            visible: root.entry.kind !== SettingsEntryModel.Boolean
            enabled: root.entry.editable
            text: root.entry.text
            maximumLength: 32769
            accessibleName: root.entry.title
            onTextEdited: (text) => root.entry.editText(text)
        }
    }
    bodyItem: RowLayout {
        PrimaryButton {
            objectName: root.entry.key + "UseConfigurationButton"
            text: qsTr("Use configuration")
            visible: root.entry.overridden
            onClicked: root.entry.useConfiguration()
        }
        PrimaryButton { text: qsTr("Keep my edit"); visible: root.entry.conflict; onClicked: root.entry.resolve(true) }
        PrimaryButton { text: qsTr("Use current value"); visible: root.entry.conflict; onClicked: root.entry.resolve(false) }
    }
}
