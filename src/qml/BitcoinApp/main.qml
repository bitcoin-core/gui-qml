// Copyright (c) 2021-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BitcoinApp.Components
import BitcoinApp.Controls

ApplicationWindow {
    id: appWindow
    title: "Bitcoin Core App"
    minimumWidth: 640
    minimumHeight: 665
    color: Theme.color.background
    visible: true

    StackView {
        id: main
        initialItem: onboardingWizard
        anchors.fill: parent
    }

    Wizard {
        id: onboardingWizard
        anchors.fill: parent
        views: [
            "OnboardingCover.qml",
            "OnboardingStrengthen.qml",
            "OnboardingBlockclock.qml",
            "OnboardingStorageLocation.qml",
            "OnboardingStorageAmount.qml",
            "OnboardingConnection.qml"
        ]
        onFinishedChanged: main.push(node)
    }
    Component {
        id: node
        Page {
            anchors.fill: parent
            background: null
            ColumnLayout {
                width: 600
                spacing: 0
                anchors.centerIn: parent
                Component.onCompleted: nodeModel.startNodeInitializionThread();
                Image {
                    Layout.alignment: Qt.AlignCenter
                    source: "image://images/app"
                    sourceSize.width: 64
                    sourceSize.height: 64
                }
                BlockCounter {
                    Layout.alignment: Qt.AlignCenter
                    blockHeight: nodeModel.blockTipHeight
                }
                ProgressIndicator {
                    width: 200
                    Layout.alignment: Qt.AlignCenter
                    progress: nodeModel.verificationProgress
                }
            }
         }
    }
}
