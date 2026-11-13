// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import org.bitcoincore.qt 1.0

AbstractButton {
    id: root

    property alias title: row.title
    property alias description: row.description
    property alias supportingText: row.supportingText
    property alias errorText: row.errorText
    property alias leadingItem: row.leadingItem
    property alias loadedLeadingItem: row.loadedLeadingItem
    property alias trailingItem: row.trailingItem
    property alias loadedTrailingItem: row.loadedTrailingItem
    property alias showDivider: row.showDivider
    property alias dividerColor: row.dividerColor
    property alias showsDisclosureIndicator: row.showsDisclosureIndicator
    property alias disclosureIndicatorObjectName: row.disclosureIndicatorObjectName
    property alias disclosureIndicatorColor: row.disclosureIndicatorColor
    property bool selected: false
    property int accessibleRole: Accessible.ListItem
    property int cornerRadius: Theme.radius.listRow
    property color selectedBackgroundColor: Theme.color.surfaceSelected
    property color hoverBackgroundColor: Theme.color.surfaceHover
    property color pressedBackgroundColor: Theme.color.surfacePressed
    property color selectedTextColor: Theme.color.neutral9

    // A trailing switch only mirrors the row, which is the control and
    // reports the switch's state.
    readonly property var trailingControl: loadedTrailingItem

    Accessible.name: title.length > 0 ? title : description
    Accessible.description: title.length > 0 ? description : ""
    Accessible.role: accessibleRole
    checkable: trailingControl !== null && trailingControl.checkable === true
    checked: checkable && trailingControl.checked
    hoverEnabled: AppMode.isDesktop
    focusPolicy: Qt.StrongFocus
    padding: 0
    implicitWidth: row.implicitWidth
    implicitHeight: row.implicitHeight

    HoverHandler {
        enabled: root.enabled && AppMode.isDesktop
        cursorShape: Qt.PointingHandCursor
    }

    background: StateBackground {
        radius: root.cornerRadius
        stateColor: root.down ? root.pressedBackgroundColor
            : root.selected ? root.selectedBackgroundColor
            : root.hovered ? root.hoverBackgroundColor
            : "transparent"

        // Inset, so the clipped FormSection card does not cut the ring off.
        FocusBorder {
            objectName: root.objectName.length > 0 ? root.objectName + "FocusBorder" : ""
            visible: root.visualFocus
            targetRadius: root.cornerRadius
            topMargin: Theme.space.xxs
            bottomMargin: Theme.space.xxs
            leftMargin: Theme.space.xxs
            rightMargin: Theme.space.xxs
        }
    }

    contentItem: FormRow {
        id: row
        enabled: root.enabled
        width: root.availableWidth
        titleColor: !root.enabled ? Theme.color.disabledContent : root.selected ? root.selectedTextColor : Theme.color.neutral9
    }
}
