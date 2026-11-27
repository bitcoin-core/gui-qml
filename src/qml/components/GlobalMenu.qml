// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQml.Models 2.15

ToolButton {
    id: root
    objectName: "globalMenuButton"
    property var router: applicationRouter
    property var navigation: navigationModel
    property var lifecycle: nodeLifecycleModel
    text: qsTr("Menu")
    Accessible.name: qsTr("Application menu")
    enabled: !root.router.shuttingDown
    onClicked: menu.open()

    Menu {
        id: menu
        objectName: "globalMenu"
        y: root.height

        Instantiator {
            model: root.navigation
            delegate: MenuItem {
                required property string routeId
                required property string label
                required property bool selected
                required property bool available
                objectName: "navigate_" + routeId
                text: label
                checkable: true
                checked: selected
                enabled: available
                onTriggered: root.router.navigate(routeId)
            }
            onObjectAdded: (index, object) => menu.insertItem(index, object)
            onObjectRemoved: (index, object) => menu.removeItem(object)
        }
        MenuSeparator {}
        MenuItem {
            objectName: "globalMenuBack"
            text: qsTr("Back")
            enabled: root.router.canGoBack
            onTriggered: root.router.back()
        }
        MenuItem {
            objectName: "globalMenuQuit"
            text: qsTr("Quit")
            onTriggered: root.lifecycle.requestShutdown()
        }
    }
}
