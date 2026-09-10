// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../components"
import "../controls"

Item {
    id: root
    objectName: "applicationShell"
    readonly property string currentRoute: applicationRouter.currentRoute
    readonly property bool settingsVisible: currentRoute === "settings" || currentRoute.indexOf("settings/") === 0

    function showDestination() {
        page.setSource(applicationRouter.currentSource, applicationRouter.currentParameters)
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        ToolBar {
            Layout.fillWidth: true
            visible: !applicationRouter.shuttingDown
            RowLayout {
                anchors.fill: parent
                GlobalMenu {}
                Label {
                    text: applicationRouter.currentTitle
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                }
                ToolButton {
                    objectName: "applicationBackButton"
                    text: qsTr("Back")
                    enabled: applicationRouter.canGoBack
                    onClicked: applicationRouter.back()
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0
            Loader {
                id: settingsSidebar
                visible: root.settingsVisible
                Layout.preferredWidth: 185
                Layout.fillHeight: true
                active: root.settingsVisible
                source: active ? "qrc:///qml/pages/node/NodeSettings.qml" : ""
            }
            Loader {
                id: page
                objectName: "applicationPageHost"
                Layout.fillWidth: true
                Layout.fillHeight: true
                focus: true
            }
        }
    }

    Connections {
        target: applicationRouter
        function onRouteChanged() { root.showDestination() }
    }
    Connections {
        target: settingsSidebar.item
        function onNavigateRequested(route) { applicationRouter.navigate(route) }
        function onDoneClicked() { applicationRouter.navigate("node") }
    }
    // Feature pages report intent. Only the shell converts intent to routes.
    Connections {
        target: page.item
        ignoreUnknownSignals: true
        function onBack() { applicationRouter.back() }
        function onDoneClicked() { applicationRouter.back() }
        function onNavigateRequested(route) { applicationRouter.navigate(route) }
        function onSettingsClicked() { applicationRouter.navigate("settings") }
        function onPeersClicked() { applicationRouter.navigate("peers") }
        function onConsoleClicked() { applicationRouter.navigate("console") }
        function onPeerSelected(details) { applicationRouter.navigate("peer-details", {"details": details}) }
        function onBannedPeers() { applicationRouter.navigate("banned-peers") }
        function onDesignSystemRequested() { applicationRouter.navigate("settings/design-system") }
        function onDeveloperRequested() { applicationRouter.navigate("settings/developer") }
        function onProxyRequested() { applicationRouter.navigate("settings/proxy") }
    }
    Keys.onReleased: (event) => {
        if (event.key === Qt.Key_Back && applicationRouter.canGoBack) {
            applicationRouter.back()
            event.accepted = true
        }
    }
    Component.onCompleted: root.showDestination()
}
