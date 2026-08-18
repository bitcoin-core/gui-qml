pragma ComponentBehavior: Bound

// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15


import "../controls"

Page {
    id: root
    objectName: "settingsView"

    signal doneClicked()

    property bool showDoneButton: true
    property string selectedSectionId: ""
    property int sidebarWidth: 286
    readonly property alias sidebar: sidebar
    readonly property alias pageContainer: pageContainer
    readonly property var sections: [
    ]

    function sectionForId(sectionId) {
        for (let index = 0; index < root.sections.length; ++index) {
            if (root.sections[index].id === sectionId) return root.sections[index]
        }
        return null
    }

    function componentForSection(sectionId) {
        const section = root.sectionForId(sectionId)
        return section ? section.pageComponent : null
    }

    function sectionIsVisible(sectionId) {
        const section = root.sectionForId(sectionId)
        return section !== null && section.visible !== false
    }

    function firstVisibleSectionId() {
        for (let index = 0; index < root.sections.length; ++index) {
            if (root.sections[index].visible !== false) return root.sections[index].id
        }
        return ""
    }

    function selectSection(sectionId, forceReload) {
        const resolvedId = root.sectionIsVisible(sectionId) ? sectionId : root.firstVisibleSectionId()
        if (resolvedId.length === 0) {
            root.selectedSectionId = ""
            pageContainer.clear()
            return
        }

        if (forceReload === true) pageContainer.clear()
        root.selectedSectionId = resolvedId
        if (root.visible) pageContainer.showSection(resolvedId, root.componentForSection(resolvedId))
    }

    function ensureVisibleSelection() {
        if (!root.sectionIsVisible(root.selectedSectionId)) root.selectSection(root.firstVisibleSectionId())
    }

    background: null
    padding: 0

    onSectionsChanged: ensureVisibleSelection()
    onVisibleChanged: {
        if (visible) root.selectSection(root.selectedSectionId)
    }

    Component.onCompleted: root.selectSection(
        root.sectionIsVisible(root.selectedSectionId) ? root.selectedSectionId : root.firstVisibleSectionId())

    contentItem: RowLayout {
        spacing: 0

        Rectangle {
            id: sidebarSurface
            objectName: "settingsv2SettingsSidebarSurface"
            Layout.preferredWidth: root.sidebarWidth
            Layout.minimumWidth: root.sidebarWidth
            Layout.maximumWidth: root.sidebarWidth
            Layout.fillHeight: true
            color: Theme.color.neutral1

            Behavior on color {
                ColorAnimation { duration: 150 }
            }

            ColumnLayout {
                anchors.fill: parent
                anchors.leftMargin: 20
                anchors.rightMargin: 20
                anchors.topMargin: 20
                anchors.bottomMargin: 16
                spacing: 0

                SettingsSidebar {
                    id: sidebar
                    objectName: "settingsv2SettingsSidebar"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    model: root.sections
                    currentSectionId: root.selectedSectionId
                    onSectionActivated: function(sectionId) { root.selectSection(sectionId) }
                }

                NavButton {
                    objectName: "settingsv2SettingsDoneButton"
                    visible: root.showDoneButton
                    text: qsTr("Done")
                    Layout.alignment: Qt.AlignHCenter
                    Layout.bottomMargin: 20
                    onClicked: root.doneClicked()
                }
            }
        }

        SettingsPageContainer {
            id: pageContainer
            objectName: "settingsv2SettingsPageContainer"
            Layout.minimumWidth: 0
            Layout.fillWidth: true
            Layout.fillHeight: true
        }
    }

}
