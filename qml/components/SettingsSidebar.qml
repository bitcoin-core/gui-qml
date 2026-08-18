pragma ComponentBehavior: Bound

// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15

import org.bitcoincore.qt 1.0

import "../controls"

Control {
    id: root

    property var model: []
    property string currentSectionId: ""
    property int rowHeight: 36
    property int groupSpacing: 16
    property int cornerRadius: 8
    property color selectedBackgroundColor: Qt.rgba(Theme.color.orange.r, Theme.color.orange.g, Theme.color.orange.b, 0.15)
    property color hoverBackgroundColor: Theme.color.neutral2
    readonly property var visibleSections: root.filteredSections()
    readonly property alias listView: sectionList

    signal sectionActivated(string sectionId)

    function filteredSections() {
        const result = []
        if (!root.model) return result

        if (root.model.count !== undefined && root.model.get !== undefined) {
            for (let index = 0; index < root.model.count; ++index) {
                const section = root.model.get(index)
                if (section.visible !== false) result.push(section)
            }
            return result
        }

        for (let index = 0; index < root.model.length; ++index) {
            const section = root.model[index]
            if (section.visible !== false) result.push(section)
        }
        return result
    }

    background: null
    padding: 0
    implicitWidth: 190
    implicitHeight: sectionList.contentHeight

    contentItem: ListView {
        id: sectionList
        objectName: "settingsSidebarList"
        model: root.visibleSections
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        keyNavigationEnabled: true

        delegate: Item {
            id: delegate
            required property var modelData
            required property int index

            readonly property bool startsGroup: delegate.index > 0
                && root.visibleSections[delegate.index - 1].group !== delegate.modelData.group

            width: sectionList.width
            height: root.rowHeight + (delegate.startsGroup ? root.groupSpacing : 0)

            AbstractButton {
                id: button
                objectName: delegate.modelData.objectName || "settingsSidebar_" + delegate.modelData.id
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: root.rowHeight
                hoverEnabled: AppMode.isDesktop
                focusPolicy: Qt.TabFocus
                leftPadding: 10
                rightPadding: 10
                Accessible.name: delegate.modelData.label
                Accessible.role: Accessible.ListItem

                onClicked: root.sectionActivated(delegate.modelData.id)

                background: Rectangle {
                    radius: root.cornerRadius
                    color: delegate.modelData.id === root.currentSectionId
                        ? root.selectedBackgroundColor
                        : button.hovered
                            ? root.hoverBackgroundColor
                            : "transparent"

                    FocusBorder {
                        visible: button.visualFocus
                        borderRadius: root.cornerRadius + 2
                        topMargin: -2
                        bottomMargin: -2
                        leftMargin: -2
                        rightMargin: -2
                    }
                }

                contentItem: CoreText {
                    text: delegate.modelData.label
                    color: delegate.modelData.id === root.currentSectionId
                        ? Theme.color.orange
                        : Theme.color.neutral9
                    font: Theme.text.description.font
                    horizontalAlignment: Text.AlignLeft
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight

                    Behavior on color {
                        ColorAnimation { duration: 150 }
                    }
                }

                HoverHandler {
                    cursorShape: Qt.PointingHandCursor
                }
            }
        }
    }
}
