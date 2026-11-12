// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import org.bitcoincore.qt 1.0
import "../controls"
import "./onboarding"

ApplicationWindow {
    id: appWindow
    objectName: "preInitWindow"
    title: qsTr("Bitcoin Core App")
    minimumWidth: 800
    minimumHeight: 665
    color: Theme.color.background
    visible: true

    property bool completed: false
    property bool starting: false
    signal finished()

    OnboardingWizard {
        anchors.fill: parent
        preInit: true
        settingsModel: ApplicationModels.onboardingOptionsModel
        assumedBlockchainSize: ApplicationModels.onboardingOptionsModel.assumedBlockchainSize
        assumedChainstateSize: ApplicationModels.onboardingOptionsModel.assumedChainstateSize
        onFinished: {
            if (!ApplicationModels.onboardingOptionsModel.canFinish) return
            appWindow.completed = true
            appWindow.starting = true
            appWindow.finished()
        }
    }

    CoreText {
        objectName: "preInitPreviewErrorText"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 20
        visible: !appWindow.starting && ApplicationModels.onboardingOptionsModel.previewError.length > 0
        text: ApplicationModels.onboardingOptionsModel.previewError
        color: Theme.color.blue
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        font.pixelSize: 15
    }

    Item {
        objectName: "preInitStartupOverlay"
        anchors.fill: parent
        visible: appWindow.starting
        z: 100

        Rectangle {
            anchors.fill: parent
            color: "black"
        }

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.AllButtons
            hoverEnabled: true
        }

        BusyIndicator {
            objectName: "preInitStartupBusyIndicator"
            anchors.centerIn: parent
            running: appWindow.starting
        }

        focus: visible
        onVisibleChanged: {
            if (visible) {
                forceActiveFocus()
            }
        }
        Keys.onPressed: (event) => {
            event.accepted = true
        }
    }
}
