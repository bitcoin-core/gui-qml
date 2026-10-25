// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Layouts 1.15

import "../../../controls"

DesignSystemSection {
    id: root

    // Width of the page being simulated, from the page width control.
    property real layoutWidth: width

    readonly property var spaceTokens: ["xxs", "xs", "s", "m", "ml", "l", "xl", "xxl", "xxxl", "page", "pageWide",
        "cardInset", "pageCompact", "pageRegular"]
    readonly property var radiusTokens: ["xs", "s", "m", "field", "popup", "sheet", "cardInset", "card", "listRow",
        "sidebarRow"]
    readonly property var sizeTokens: ["controlSmall", "control", "controlLarge", "field", "row", "rowWithDescription",
        "iconButtonSmall", "iconButton"]
    readonly property var iconSizeTokens: ["xs", "s", "m", "l", "xl"]
    readonly property var breakpointRows: [
        ["SizeClass.compactWidthMax", SizeClass.compactWidthMax + " (widths below are compact)"],
        ["SizeClass.stackColumnsMax", SizeClass.stackColumnsMax + " (two-column grids below stack)"],
        ["SizeClass.wideWidthMin", SizeClass.wideWidthMin + " (wide arrangement from here)"],
        ["SizeClass.compactHeightMax", SizeClass.compactHeightMax],
        ["page width", Math.round(root.layoutWidth) + (SizeClass.isCompact(root.layoutWidth) ? ", compact"
            : root.layoutWidth >= SizeClass.wideWidthMin ? ", wide" : ", regular")],
        ["Theme.space.pagePadding(width)", Theme.space.pagePadding(root.layoutWidth)]
    ]

    description: "Theme.space, Theme.radius, Theme.size and Theme.focusRing drawn at their real size, and the SizeClass breakpoints. Set Page width on the Design system page to lay this page out as on a narrower window."

    DesignSystemSpecimen {
        objectName: "designSystemSpaceScale"
        Layout.fillWidth: true
        name: "Spacing"
        note: "Spacing, margins and padding use these steps. cardInset is the inset inside cards; pageCompact and pageRegular are page side padding."

        Repeater {
            model: root.spaceTokens

            delegate: DesignSystemState {
                id: spaceSample
                required property string modelData
                objectName: "designSystemSpace_" + modelData
                width: 104
                label: modelData + "\n" + Theme.space[modelData]

                Rectangle {
                    Layout.preferredWidth: Theme.space[spaceSample.modelData]
                    Layout.preferredHeight: Theme.space.l
                    color: Theme.color.accent
                }
            }
        }
    }

    DesignSystemSpecimen {
        objectName: "designSystemRadii"
        Layout.fillWidth: true
        name: "Corner radii"
        note: "One radius per surface type. sheet is only for modals that host a multi-step flow; cardInset is for cards inside a card or popup. Pills and circles use height / 2."

        Repeater {
            model: root.radiusTokens

            delegate: DesignSystemState {
                id: radiusSample
                required property string modelData
                objectName: "designSystemRadius_" + modelData
                width: 104
                label: modelData + "\n" + Theme.radius[modelData]

                Rectangle {
                    Layout.preferredWidth: 88
                    Layout.preferredHeight: 52
                    radius: Theme.radius[radiusSample.modelData]
                    color: Theme.color.surface2
                    border.width: 1
                    border.color: Theme.color.controlBorder
                }
            }
        }

        DesignSystemState {
            objectName: "designSystemRadius_pill"
            width: 104
            label: "pill\nheight / 2"

            Rectangle {
                Layout.preferredWidth: 88
                Layout.preferredHeight: Theme.size.controlSmall
                radius: height / 2
                color: Theme.color.surface2
                border.width: 1
                border.color: Theme.color.controlBorder
            }
        }
    }

    DesignSystemSpecimen {
        objectName: "designSystemSizes"
        Layout.fillWidth: true
        name: "Control and row heights"
        note: "Heights of buttons, fields and rows; iconButton sizes are square targets."

        Repeater {
            model: root.sizeTokens

            delegate: DesignSystemState {
                id: sizeSample
                required property string modelData
                readonly property bool square: modelData.indexOf("iconButton") === 0
                objectName: "designSystemSize_" + modelData
                width: 104
                label: modelData + "\n" + Theme.size[modelData]

                Rectangle {
                    Layout.preferredWidth: sizeSample.square ? Theme.size[sizeSample.modelData] : 88
                    Layout.preferredHeight: Theme.size[sizeSample.modelData]
                    radius: Theme.radius.s
                    color: Theme.color.surface2
                    border.width: 1
                    border.color: Theme.color.controlBorder
                }
            }
        }
    }

    DesignSystemSpecimen {
        objectName: "designSystemIconSizes"
        shows: ["CaretRightIcon"]
        Layout.fillWidth: true
        name: "Icon artwork"
        note: "Theme.size.icon steps, and the caret sizes for disclosure and back carets."

        Repeater {
            model: root.iconSizeTokens

            delegate: DesignSystemState {
                id: iconSample
                required property string modelData
                objectName: "designSystemIconSize_" + modelData
                width: 72
                label: "icon." + modelData + "\n" + Theme.size.icon[modelData]

                Icon {
                    source: "image://images/info-filled"
                    color: Theme.color.neutral9
                    size: Theme.size.icon[iconSample.modelData]
                }
            }
        }

        DesignSystemState {
            objectName: "designSystemSize_caret"
            width: 72
            label: "caret\n" + Theme.size.caret

            CaretRightIcon { color: Theme.color.neutral9 }
        }

        DesignSystemState {
            objectName: "designSystemSize_caretDense"
            width: 72
            label: "caretDense\n" + Theme.size.caretDense

            CaretRightIcon { color: Theme.color.neutral9; size: Theme.size.caretDense }
        }
    }

    DesignSystemSpecimen {
        objectName: "designSystemFocusRing"
        shows: ["FocusBorder"]
        Layout.fillWidth: true
        name: "Focus ring"
        note: "FocusBorder draws Theme.focusRing.width at offset around buttons and rows, or offsetDense where space is tight."

        DesignSystemState {
            objectName: "designSystemFocusRing_offset"
            width: 120
            label: "offset " + Theme.focusRing.offset

            Item {
                Layout.preferredWidth: 104
                Layout.preferredHeight: Theme.size.control + Theme.focusRing.offset * 2

                Rectangle {
                    anchors.centerIn: parent
                    width: 88
                    height: Theme.size.control
                    radius: Theme.radius.s
                    color: Theme.color.surface2

                    FocusBorder { targetRadius: parent.radius }
                }
            }
        }

        DesignSystemState {
            objectName: "designSystemFocusRing_offsetDense"
            width: 120
            label: "offsetDense " + Theme.focusRing.offsetDense

            Item {
                Layout.preferredWidth: 104
                Layout.preferredHeight: Theme.size.control + Theme.focusRing.offset * 2

                Rectangle {
                    anchors.centerIn: parent
                    width: 88
                    height: Theme.size.control
                    radius: Theme.radius.s
                    color: Theme.color.surface2

                    FocusBorder {
                        topMargin: -Theme.focusRing.offsetDense
                        bottomMargin: -Theme.focusRing.offsetDense
                        leftMargin: -Theme.focusRing.offsetDense
                        rightMargin: -Theme.focusRing.offsetDense
                        targetRadius: parent.radius
                    }
                }
            }
        }
    }

    DesignSystemSpecimen {
        objectName: "designSystemBreakpoints"
        shows: ["NavigationSplitView"]
        Layout.fillWidth: true
        name: "Size classes"
        note: "The split view below is a NavigationSplitView as wide as this page's content: two columns when regular, one when compact."

        Column {
            width: parent.width
            spacing: Theme.space.xs

            Repeater {
                model: root.breakpointRows

                delegate: Row {
                    id: breakpointRow
                    required property var modelData
                    width: parent.width
                    spacing: Theme.space.l

                    CoreText {
                        id: breakpointName
                        width: 200
                        text: modelData[0]
                        color: Theme.color.neutral7
                        textStyle: Theme.text.monoCaption
                        horizontalAlignment: Text.AlignLeft
                        elide: Text.ElideRight
                        wrap: false
                    }

                    CoreText {
                        width: Math.max(0, breakpointRow.width - breakpointName.width - breakpointRow.spacing)
                        text: String(modelData[1])
                        color: Theme.color.neutral9
                        textStyle: Theme.text.monoCaption
                        horizontalAlignment: Text.AlignLeft
                    }
                }
            }
        }

        NeutralButton {
            objectName: "designSystemSplitViewToggle"
            enabled: splitView.isCompact
            text: splitView.compactColumn === NavigationSplitView.Primary ? "Show detail" : "Show list"
            onClicked: splitView.compactColumn === NavigationSplitView.Primary
                ? splitView.showDetail() : splitView.showPrimary()
        }

        NavigationSplitView {
            id: splitView
            objectName: "designSystemLayoutSplitView"
            width: parent.width
            height: 160
            primaryMinimumWidth: 200
            primaryPreferredWidth: 220

            primaryComponent: Rectangle {
                color: Theme.color.surface1
                CoreText {
                    anchors.centerIn: parent
                    text: "List"
                    color: Theme.color.neutral9
                }
            }
            detailComponent: Rectangle {
                color: Theme.color.surface2
                CoreText {
                    anchors.centerIn: parent
                    text: "Detail"
                    color: Theme.color.neutral9
                }
            }
        }
    }
}
