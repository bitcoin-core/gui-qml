// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0

import "../../controls"
import "../../components"

Page {
    id: root
    objectName: "settingsLayoutPage"

    signal back()

    background: null
    leftPadding: 20
    rightPadding: 20
    topPadding: 30

    header: SettingsHeader {
        title: qsTr("Layout")
        backButtonObjectName: "settingsLayoutBack"
        onBack: root.back()
    }

    ColumnLayout {
        spacing: 15
        width: Math.min(parent.width, 450)
        anchors.horizontalCenter: parent.horizontalCenter

        OptionButton {
            objectName: "layoutDefault"
            Layout.fillWidth: true
            text: qsTr("Default layout")
            checked: !AppMode.adaptiveSidebarLayout
            onClicked: AppMode.adaptiveSidebarLayout = false
        }

        OptionButton {
            objectName: "layoutAdaptiveSidebar"
            Layout.fillWidth: true
            text: qsTr("[Experimental] Adaptive sidebar layout")
            checked: AppMode.adaptiveSidebarLayout
            onClicked: AppMode.adaptiveSidebarLayout = true
        }
    }
}
