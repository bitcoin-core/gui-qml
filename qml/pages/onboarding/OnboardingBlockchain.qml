// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import "../../controls"

OnboardingView {
    id: root
    objectName: "onboardingBlockchain"
    signal back()
    signal next()
    property bool useSharedMotion: false

    isOnSurface: false
    backButtonInFooter: true
    backButtonObjectName: "onboardingWizardBackButton"
    autoNavigateBack: false
    maximumContentWidth: 640
    heading: qsTr("Verify and store blocks")
    subheading: qsTr("Your node downloads and verifies blocks in order, then stores " +
        "the data it needs on this device. " +
        "Later, you can choose to keep the full history or prune to save disk space.")
    imageView: motionSpace
    primaryButtonText: qsTr("Next")
    primaryButtonObjectName: "onboardingBlockchainButton"
    onBackClicked: root.back()
    onPrimaryClicked: root.next()

    Component {
        id: motionSpace
        Item {
            implicitWidth: 224
            implicitHeight: 224

            Item {
                anchors.centerIn: parent
                width: 203
                height: 14
                visible: !root.useSharedMotion

                Rectangle {
                    x: 7
                    y: 6
                    width: 189
                    height: 2
                    color: Theme.color.neutral5
                }
                Row {
                    spacing: 7
                    Repeater {
                        model: 10
                        Rectangle {
                            width: 14
                            height: 14
                            radius: 2
                            color: index === 9 ? Theme.color.orange : "#F9D63C"
                        }
                    }
                }
            }
        }
    }
}
