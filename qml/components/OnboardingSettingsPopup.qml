// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import "../controls"

DialogPopup {
    id: root
    property Component initialPage: null
    property string closeButtonObjectName: "onboardingSettingsCloseButton"
    readonly property alias pageStack: pages
    readonly property SettingsPage currentSettingsPage: pages.currentItem as SettingsPage
    property bool exitPending: false
    function requestClose() {
        if (root.exitPending) return
        root.exitPending = true
        if (root.currentSettingsPage) root.currentSettingsPage.requestExit()
        else root.close()
    }
    Connections {
        target: root.currentSettingsPage
        function onExitAllowed() { if (root.exitPending) root.close() }
        function onExitCancelled() { root.exitPending = false }
    }

    // Named after the hosted settings page.
    title: pages.currentItem && pages.currentItem.title ? pages.currentItem.title : "" // qmllint disable missing-property
    sheet: true
    implicitWidth: 532
    implicitHeight: 504
    padding: 0
    horizontalPadding: 0
    // The hosted settings page decides whether it can close (requestClose).
    dismissible: false
    closesOnPressOutside: false

    contentItem: Item {
        clip: true
        focus: true
        Keys.onEscapePressed: function(event) { event.accepted = true; root.requestClose() }

        PageStack {
            id: pages
            anchors.fill: parent
            initialItem: root.initialPage
        }

        CloseButton {
            objectName: root.closeButtonObjectName
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.margins: Theme.space.l
            z: 2
            onClicked: root.requestClose()
        }
    }

    onOpened: root.exitPending = false
    onClosed: {
        root.exitPending = false
        if (pages.depth > 1) pages.pop(null, StackView.Immediate)
    }
}
