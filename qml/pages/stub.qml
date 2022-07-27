// Copyright (c) 2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../components"
import "../controls"

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
            "onboarding/onboarding01.qml",
            "onboarding/onboarding02.qml",
            "onboarding/onboarding03.qml",
            "onboarding/onboarding04.qml",
            "onboarding/onboarding05.qml",
            "onboarding/onboarding06.qml",
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
