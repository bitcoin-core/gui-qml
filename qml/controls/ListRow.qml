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
    property alias showsDisclosureIndicator: row.showsDisclosureIndicator
    property alias disclosureIndicatorObjectName: row.disclosureIndicatorObjectName
    property alias disclosureIndicatorColor: row.disclosureIndicatorColor
    property bool selected: false
    property int accessibleRole: Accessible.ListItem
    property int cornerRadius: 16
    property color selectedBackgroundColor: Qt.rgba(Theme.color.orange.r, Theme.color.orange.g, Theme.color.orange.b, 0.15)
    property color hoverBackgroundColor: Theme.color.neutral2
    property color selectedTextColor: Theme.color.orange

    Accessible.name: title
    Accessible.description: description
    Accessible.role: accessibleRole
    hoverEnabled: AppMode.isDesktop
    focusPolicy: Qt.StrongFocus
    padding: 0
    implicitWidth: row.implicitWidth
    implicitHeight: row.implicitHeight

    HoverHandler {
        enabled: root.enabled && AppMode.isDesktop
        cursorShape: Qt.PointingHandCursor
    }

    background: Rectangle {
        radius: root.cornerRadius
        color: root.selected
            ? root.selectedBackgroundColor
            : root.down || root.hovered
                ? root.hoverBackgroundColor
                : "transparent"

        FocusBorder {
            visible: root.visualFocus
            borderRadius: root.cornerRadius + 2
            topMargin: -2
            bottomMargin: -2
            leftMargin: -2
            rightMargin: -2
        }
    }

    contentItem: FormRow {
        id: row
        enabled: root.enabled
        width: root.availableWidth
        titleColor: root.selected && root.enabled ? root.selectedTextColor : (root.enabled ? Theme.color.neutral9 : Theme.color.neutral4)
    }
}
