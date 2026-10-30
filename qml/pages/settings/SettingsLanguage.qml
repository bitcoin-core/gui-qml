// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../../controls"
import "../../components"

SettingsPage {
    id: root

    objectName: "settingsLanguagePage"
    title: qsTr("Choose language")
    backButtonObjectName: "settingsLanguageBack"

    readonly property var languageStatus: (optionsModel.coreSettingStatuses || ({})).lang || ({})
    readonly property var languages: {
        const all = optionsModel.availableLanguages
        if (searchField.text.length === 0) return all
        const query = searchField.text.toLowerCase()
        return all.filter(function(tag) {
            return optionsModel.languageLabel(tag).toLowerCase().indexOf(query) !== -1
        })
    }

    SearchBar {
        id: searchField
        Layout.fillWidth: true
        fieldObjectName: "languageSearch"
        placeholderText: qsTr("Search…")
        accessibleName: qsTr("Search languages")
    }

    FormSection {
        objectName: "languageListSection"
        Layout.fillWidth: true
        visible: root.languages.length > 0

        // A positioner keeps the filtered Repeater out of the Qt 6.4 layout engine.
        Column {
            Layout.fillWidth: true

            Repeater {
                model: root.languages

                delegate: ListRow {
                    id: delegate
                    // Declare modelData as a required property so Qt properly binds the
                    // list element value (a language tag string) into this delegate and
                    // all of its children. This avoids the delegate-scope leakage that
                    // occurs when model roles are referenced inside a Loader.sourceComponent.
                    required property string modelData
                    required property int index

                    readonly property bool current: delegate.modelData === optionsModel.language

                    objectName: "language_" + delegate.modelData
                    width: parent.width
                    title: optionsModel.languageLabel(delegate.modelData)
                    enabled: root.languageStatus.canEdit !== false
                    showDivider: delegate.index < root.languages.length - 1
                    Accessible.description: delegate.current ? qsTr("Selected") : ""
                    trailingItem: Icon {
                        visible: delegate.current
                        source: "image://images/check"
                        color: delegate.enabled ? Theme.color.neutral9 : Theme.color.disabledContent
                        size: Theme.size.icon.xl
                    }

                    onClicked: {
                        optionsModel.language = delegate.modelData
                        root.back()
                    }
                }
            }
        }
    }

    CoreText {
        objectName: "languageNoResults"
        visible: root.languages.length === 0
        Layout.fillWidth: true
        text: qsTr("No languages match your search.")
        textStyle: Theme.text.description
        color: Theme.color.neutral7
        horizontalAlignment: Text.AlignLeft
    }
}
