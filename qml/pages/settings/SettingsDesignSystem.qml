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

    readonly property var motionModes: ["full", "reduced", "off"]
    readonly property var devices: ["Desktop", "Mobile"]
    readonly property var previewWidths: [
        { text: "Window", value: 0 },
        { text: "390", value: 390 },
        { text: "720", value: 720 },
        { text: "1000", value: 1000 }
    ]
    // Lays the content out as on a page this wide (0: the real width).
    property real previewWidth: 0
    readonly property real layoutWidth: previewWidth > 0 ? Math.min(previewWidth, width) : width

    contentHorizontalPadding: Theme.space.pagePadding(layoutWidth) + (width - layoutWidth) / 2

    // The motion mode is switched for this page only. Settings keeps the page
    // alive when you move to another section, so reset it on hide as well.
    onVisibleChanged: if (!visible) Theme.motion.resetMode()
    Component.onDestruction: Theme.motion.resetMode()

    FormSection {
        objectName: "designSystemPageControlsSection"
        Layout.fillWidth: true
        title: "Page controls"
        description: "Interacting with the samples below never changes a setting or wallet state; only the palette picker does. The motion mode resets when you leave this page."

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

        FormRow {
            objectName: "designSystemMotionModeRow"
            Layout.fillWidth: true
            title: "Motion"
            description: "Starts from the platform and Display settings; off is for tests and makes everything instant."
            showDivider: false
            trailingItem: SegmentedPicker {
                objectName: "designSystemMotionModePicker"
                implicitWidth: 260
                model: root.motionModes
                currentIndex: Math.max(0, root.motionModes.indexOf(Theme.motion.mode))
                onSelected: function(index, option) {
                    Theme.motion.mode = option
                }
            }
        }

        FormRow {
            objectName: "designSystemDeviceRow"
            Layout.fillWidth: true
            title: "Device"
            description: "Mobile lowers the window's minimum width to 360 so you can narrow it and check compact layouts, such as the settings split view. It stays on until you switch back."
            showDivider: false
            trailingItem: SegmentedPicker {
                objectName: "designSystemDevicePicker"
                implicitWidth: 260
                model: root.devices
                currentIndex: SizeClass.mobilePreview ? 1 : 0
                onSelected: function(index, option) {
                    SizeClass.mobilePreview = index === 1
                }
            }
        }

        FormRow {
            objectName: "designSystemPageWidthRow"
            Layout.fillWidth: true
            title: "Page width"
            description: "Lay this page out as on a window this wide, to check compact and regular layouts."
            showDivider: false
            trailingItem: SegmentedPicker {
                objectName: "designSystemPageWidthPicker"
                implicitWidth: 260
                model: root.previewWidths
                currentIndex: Math.max(0, root.previewWidths.findIndex(function(option) {
                    return option.value === root.previewWidth
                }))
                onSelected: function(index, option) {
                    root.previewWidth = option.value
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

    DesignSystemMotion {
        objectName: "designSystemMotionSection"
        Layout.fillWidth: true
    }

    DesignSystemLayout {
        objectName: "designSystemLayoutSection"
        Layout.fillWidth: true
        layoutWidth: root.layoutWidth
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
