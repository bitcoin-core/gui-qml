// Copyright (c) 2022-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0
import "../../controls"

Item {
    id: root
    objectName: "nodeSettingsStack"
    signal doneClicked
    signal navigateRequested(string route)

    readonly property var sections: [
        { label: qsTr("Display"), route: "settings", section: "display" },
        { label: qsTr("Window behavior"), route: "settings/window", section: "windowbehavior" },
        { label: qsTr("Storage"), route: "settings/storage", section: "storage" },
        { label: qsTr("Connection"), route: "settings/connection", section: "connection" },
        { label: qsTr("About"), route: "settings/about", section: "about" }
    ]
    readonly property string selectedSection: {
        const route = applicationRouter.currentRoute
        if (route === "settings/window") return "windowbehavior"
        if (route === "settings/storage") return "storage"
        if (route === "settings/connection" || route === "settings/proxy") return "connection"
        if (route === "settings/about" || route === "settings/developer") return "about"
        return "display"
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 15
        spacing: 5
        Repeater {
            model: root.sections
            delegate: ItemDelegate {
                required property var modelData
                objectName: "settings_" + modelData.section
                Layout.fillWidth: true
                visible: modelData.section !== "windowbehavior" || AppMode.isDesktop
                text: modelData.label
                highlighted: root.selectedSection === modelData.section
                onClicked: root.navigateRequested(modelData.route)
            }
        }
        Item { Layout.fillHeight: true }
        NavButton {
            objectName: "nodeSettingsDoneButton"
            text: qsTr("Done")
            Layout.alignment: Qt.AlignHCenter
            onClicked: root.doneClicked()
        }
    }
}
