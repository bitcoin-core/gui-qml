// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

import "../../controls"
import "designsystem"

DesignSystemPage {
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

    readonly property var foundations: [
        { name: "Colors", id: "Colors", section: colorsSection,
          description: "Semantic roles by purpose: surfaces, states, text and status." },
        { name: "Typography", id: "Typography", section: typographySection,
          description: "Text roles at their real size, with line height and weight." },
        { name: "Motion", id: "Motion", section: motionSection,
          description: "Durations, easing curves and the three motion modes." },
        { name: "Layout", id: "Layout", section: layoutSection,
          description: "Spacing, corner radii, sizes, focus rings and breakpoints." }
    ]
    readonly property var componentGroups: [
        { name: "Buttons", id: "Buttons", section: buttonsSection,
          description: "Actions, from the one primary action to icon-only buttons." },
        { name: "Selection", id: "Selection", section: selectionSection,
          description: "Controls that pick a value or a range." },
        { name: "Fields", id: "Fields", section: fieldsSection,
          description: "Text entry, with labels outside the field." },
        { name: "Rows and sections", id: "Rows", section: rowsSection,
          description: "The cards and rows that settings and detail pages are built from." },
        { name: "Navigation", id: "Navigation", section: navigationSection,
          description: "Top bars, page headings and tabs." },
        { name: "Feedback", id: "Feedback", section: feedbackSection,
          description: "Banners, pills, tooltips and loading indicators." },
        { name: "Menus and dialogs", id: "Menus", section: menusSection,
          description: "Menus hold actions on one object; dialogs ask for a short decision." },
        { name: "Data display", id: "DataDisplay", section: dataDisplaySection,
          description: "Amounts, addresses, QR codes, transaction parts and log output." }
    ]
    readonly property var pages: foundations.concat(componentGroups)
    // The segmented pickers move under their labels on a narrow page.
    readonly property bool stacksControls: SizeClass.isCompact(layoutWidth)

    // Subpages keep the mode while they're open; leaving the design system resets it.
    Component.onDestruction: Theme.motion.resetMode()

    function openPage(page) {
        StackView.view.push(sectionPage, {
            objectName: "designSystem" + page.id + "Page",
            title: page.name,
            section: page.section,
            previewWidth: root.previewWidth
        })
    }

    FormSection {
        objectName: "designSystemPageControlsSection"
        Layout.fillWidth: true
        title: "Page controls"
        description: "Only these pickers change anything. The samples on each page never change a setting or wallet state, and the motion mode resets when you leave the design system."

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
            stacksTrailingItem: root.stacksControls
            title: "Motion"
            description: "Starts from the platform and Display settings; off is for tests and makes everything instant."
            showDivider: false
            trailingItem: SegmentedPicker {
                objectName: "designSystemMotionModePicker"
                Layout.fillWidth: true
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
            stacksTrailingItem: root.stacksControls
            title: "Device"
            description: "Mobile lowers the window's minimum width to 360 so you can narrow it and check compact layouts, such as the settings split view. It stays on until you switch back."
            showDivider: false
            trailingItem: SegmentedPicker {
                objectName: "designSystemDevicePicker"
                Layout.fillWidth: true
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
            stacksTrailingItem: root.stacksControls
            title: "Page width"
            description: "Lay the pages out as on a window this wide, to check compact and regular layouts."
            showDivider: false
            trailingItem: SegmentedPicker {
                objectName: "designSystemPageWidthPicker"
                Layout.fillWidth: true
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

    DesignSystemPageList {
        objectName: "designSystemFoundationsSection"
        title: "Foundations"
        description: "The tokens every control is built from."
        pages: root.foundations
        onOpened: function(page) { root.openPage(page) }
    }

    DesignSystemPageList {
        objectName: "designSystemComponentsSection"
        title: "Components"
        description: "Every shared control in each of its states, grouped by what it's for."
        pages: root.componentGroups
        onOpened: function(page) { root.openPage(page) }
    }

    Component {
        id: sectionPage

        DesignSystemSectionPage {
            onBack: root.StackView.view.pop()
        }
    }

    Component { id: colorsSection; DesignSystemColors { objectName: "designSystemColorsSection" } }
    Component { id: typographySection; DesignSystemTypography { objectName: "designSystemTypographySection" } }
    Component { id: motionSection; DesignSystemMotion { objectName: "designSystemMotionSection" } }
    Component { id: layoutSection; DesignSystemLayout { objectName: "designSystemLayoutSection" } }
    Component { id: buttonsSection; DesignSystemButtons { objectName: "designSystemButtonsSection" } }
    Component { id: selectionSection; DesignSystemSelection { objectName: "designSystemSelectionSection" } }
    Component { id: fieldsSection; DesignSystemFields { objectName: "designSystemFieldsSection" } }
    Component { id: rowsSection; DesignSystemRows { objectName: "designSystemRowsSection" } }
    Component { id: navigationSection; DesignSystemNavigation { objectName: "designSystemNavigationSection" } }
    Component { id: feedbackSection; DesignSystemFeedback { objectName: "designSystemFeedbackSection" } }
    Component { id: menusSection; DesignSystemMenus { objectName: "designSystemMenusSection" } }
    Component { id: dataDisplaySection; DesignSystemDataDisplay { objectName: "designSystemDataDisplaySection" } }
}
