// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../../controls"
import "../../components"

Page {
    id: root
    signal back

    objectName: "settingsLanguagePage"
    background: null
    leftPadding: 20
    rightPadding: 20
    topPadding: 30
    readonly property var languageStatus: languageSettingsModel.status

    header: SettingsHeader {
        title: qsTr("Choose language")
        backButtonObjectName: "settingsLanguageBack"
        onBack: root.back()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        CoreTextField {
            id: searchField
            objectName: "languageSearch"
            Layout.fillWidth: true
            Layout.bottomMargin: 8
            placeholderText: qsTr("Search...")
        }

        Separator {
            Layout.fillWidth: true
        }

        ListView {
            id: languageList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true

            model: {
                const all = languageSettingsModel.availableLanguages
                if (searchField.text.length === 0) return all
                const query = searchField.text.toLowerCase()
                return all.filter(function(tag) {
                    return languageSettingsModel.languageLabel(tag).toLowerCase().indexOf(query) !== -1
                })
            }

            delegate: ItemDelegate {
                id: delegate
                // Declare modelData as a required property so Qt properly binds the
                // list element value (a language tag string) into this delegate and
                // all of its children. This avoids the delegate-scope leakage that
                // occurs when model roles are referenced inside a Loader.sourceComponent.
                required property string modelData

                objectName: "language_" + delegate.modelData
                Accessible.role: Accessible.ListItem
                Accessible.name: languageSettingsModel.languageLabel(delegate.modelData)
                enabled: root.languageStatus.canEdit !== false
                leftPadding: 0
                rightPadding: 0
                topPadding: 0
                bottomPadding: 0
                width: languageList.width

                background: Item {
                    Separator {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                    }
                }

                contentItem: RowLayout {
                    spacing: 0

                    CoreText {
                        Layout.fillWidth: true
                        Layout.topMargin: 14
                        Layout.bottomMargin: 14
                        text: languageSettingsModel.languageLabel(delegate.modelData)
                        font.pixelSize: 18
                        color: Theme.color.neutral9
                        horizontalAlignment: Text.AlignLeft
                        wrap: false
                        elide: Text.ElideRight
                    }

                    Icon {
                        visible: delegate.modelData === languageSettingsModel.language
                        source: "image://images/check"
                        color: Theme.color.orange
                        size: 24
                    }
                }

                onClicked: {
                    languageSettingsModel.language = delegate.modelData
                    root.back()
                }
            }
        }
    }
}
