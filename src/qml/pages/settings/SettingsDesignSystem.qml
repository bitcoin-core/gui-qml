// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../../controls"
import "../../components"

Page {
    signal back

    id: root
    background: null
    leftPadding: 20
    rightPadding: 20
    topPadding: 30

    readonly property var typographyRoles: [
        { name: "display", group: "Headers" },
        { name: "headline", group: "Headers" },
        { name: "title", group: "Headers" },
        { name: "subtitle", group: "Headers" },
        { name: "heading", group: "Headers" },
        { name: "subheading", group: "Headers" },
        { name: "lead", group: "Body" },
        { name: "bodyLarge", group: "Body" },
        { name: "body", group: "Body" },
        { name: "description", group: "Body" },
        { name: "caption", group: "Body" },
        { name: "button", group: "Controls" },
        { name: "buttonStrong", group: "Controls" },
        { name: "monoLead", group: "Mono" },
        { name: "monoBody", group: "Mono" },
        { name: "monoDescription", group: "Mono" },
        { name: "monoCaption", group: "Mono" }
    ]

    readonly property var paletteTokens: [
        "background", "white",
        "orange", "orangeLight1", "orangeLight2",
        "red", "green", "blue", "amber", "purple",
        "neutral0", "neutral1", "neutral2", "neutral3", "neutral4",
        "neutral5", "neutral6", "neutral7", "neutral8", "neutral9"
    ]

    header: SettingsHeader {
        title: qsTr("Design system")
        onBack: root.back()
    }

    Flickable {
        anchors.fill: parent
        contentWidth: width
        contentHeight: contentColumn.height
        clip: true

        ColumnLayout {
            id: contentColumn
            width: Math.min(parent.width, 450)
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 24

            // ── Typography ──────────────────────────────────────────
            Text {
                Layout.topMargin: 8
                Layout.fillWidth: true
                font: Theme.text.title.font
                color: Theme.color.neutral9
                text: qsTr("Typography")
            }

            Repeater {
                model: root.typographyRoles
                delegate: ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4

                    Text {
                        Layout.fillWidth: true
                        font: Theme.text[modelData.name].font
                        lineHeight: Theme.text[modelData.name].lineHeight
                        lineHeightMode: Text.FixedHeight
                        color: Theme.color.neutral9
                        text: modelData.name
                        elide: Text.ElideRight
                    }
                    Text {
                        Layout.fillWidth: true
                        font: Theme.text.caption.font
                        color: Theme.color.neutral6
                        text: Theme.text[modelData.name].family + " " +
                              Theme.text[modelData.name].styleName + " · " +
                              Theme.text[modelData.name].pixelSize + "/" +
                              Theme.text[modelData.name].lineHeight + " · " +
                              modelData.group
                    }
                    Rectangle {
                        Layout.topMargin: 8
                        Layout.fillWidth: true
                        height: 1
                        color: Theme.color.neutral3
                    }
                }
            }

            // ── Colors ──────────────────────────────────────────────
            Text {
                Layout.topMargin: 16
                Layout.fillWidth: true
                font: Theme.text.title.font
                color: Theme.color.neutral9
                text: qsTr("Colors")
            }
            Text {
                Layout.fillWidth: true
                font: Theme.text.caption.font
                color: Theme.color.neutral6
                text: qsTr("Palette tokens for the active theme. Toggle Theme to compare.")
                wrapMode: Text.WordWrap
            }

            GridLayout {
                Layout.fillWidth: true
                columns: 2
                columnSpacing: 12
                rowSpacing: 8

                Repeater {
                    model: root.paletteTokens
                    delegate: RowLayout {
                        Layout.fillWidth: true
                        spacing: 10

                        Rectangle {
                            Layout.preferredWidth: 32
                            Layout.preferredHeight: 32
                            radius: 4
                            color: Theme.color[modelData]
                            border.color: Theme.color.neutral4
                            border.width: 1
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0
                            Text {
                                Layout.fillWidth: true
                                font: Theme.text.description.font
                                color: Theme.color.neutral9
                                text: modelData
                                elide: Text.ElideRight
                            }
                            Text {
                                Layout.fillWidth: true
                                font: Theme.text.caption.font
                                color: Theme.color.neutral6
                                text: Theme.color[modelData].toString().toUpperCase()
                            }
                        }
                    }
                }
            }

            Item { Layout.preferredHeight: 24 }
        }
    }
}
