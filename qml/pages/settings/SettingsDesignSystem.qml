// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Layouts 1.15

import "../../controls"
import "designsystem"

SettingsPage {
    id: root
    title: qsTr("Design system")

    FormSection {
        objectName: "designSystemPageControlsSection"
        Layout.fillWidth: true
        title: "Page controls"
        description: "Interacting with the samples below never changes a setting or wallet state; only the palette picker does."

        FormRow {
            objectName: "designSystemThemeRow"
            Layout.fillWidth: true
            title: "Palette"
            description: "Switch the palette every sample uses."
            showDivider: false
            trailingItem: PopupPicker {
                objectName: "designSystemPalettePicker"
                surfaceLevel: 1
                subtitleRole: "description"
                currentValue: Theme.paletteId
                model: {
                    const items = []
                    for (let i = 0; i < Theme.palettes.length; ++i) {
                        const palette = Theme.palettes[i]
                        items.push({
                            text: palette.displayName,
                            value: palette.paletteId,
                            description: palette.paletteId + (palette.isDark ? ", dark" : ", light")
                        })
                    }
                    return items
                }
                onActivated: function(value) {
                    Theme.paletteId = value
                }
            }
        }
    }

    PageHeading {
        objectName: "designSystemFoundationsHeading"
        Layout.fillWidth: true
        Layout.topMargin: 16
        title: "Foundations"
    }

    DesignSystemColors {
        objectName: "designSystemColorsSection"
        Layout.fillWidth: true
    }

    DesignSystemTypography {
        objectName: "designSystemTypographySection"
        Layout.fillWidth: true
    }

    PageHeading {
        objectName: "designSystemControlsHeading"
        Layout.fillWidth: true
        Layout.topMargin: 16
        title: "Controls"
    }

    DesignSystemButtons {
        objectName: "designSystemButtonsSection"
        Layout.fillWidth: true
    }

    DesignSystemSelection {
        objectName: "designSystemSelectionSection"
        Layout.fillWidth: true
    }

    DesignSystemFields {
        objectName: "designSystemFieldsSection"
        Layout.fillWidth: true
    }

    DesignSystemRows {
        objectName: "designSystemRowsSection"
        Layout.fillWidth: true
    }

    DesignSystemNavigation {
        objectName: "designSystemNavigationSection"
        Layout.fillWidth: true
    }

    DesignSystemFeedback {
        objectName: "designSystemFeedbackSection"
        Layout.fillWidth: true
    }

    DesignSystemMenus {
        objectName: "designSystemMenusSection"
        Layout.fillWidth: true
    }

    DesignSystemDataDisplay {
        objectName: "designSystemDataDisplaySection"
        Layout.fillWidth: true
    }
}
