// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../../controls"
import "../../components"

Page {
    background: null
    Layout.fillWidth: true
    clip: true
    SwipeView {
        id: introductions
        anchors.fill: parent
        interactive: false
        orientation: Qt.Horizontal
        Page {
            background: null
            clip: true
            Layout.fillWidth: true
            header: OnboardingNav {
                alignLeft: false
                navButton: NavButton {
                    iconSource: "image://images/info"
                    iconHeight: 24
                    onClicked: {
                        introductions.incrementCurrentIndex()
                        swipeView.inSubPage = true
                    }
                }
            }
            OnboardingInfo {
                width: 600
                anchors.top: parent.top
                anchors.horizontalCenter: parent.horizontalCenter
                banner: Image {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignCenter
                    source: "image://images/app"
                    // Bitcoin icon has ~11% padding
                    sourceSize.width: 112
                    sourceSize.height: 112
                }
                bold: true
                header: qsTr("Bitcoin Core App")
                headerSize: 36
                description: qsTr("Be part of the Bitcoin network.")
                descriptionSize: 24
                subtext: qsTr("100% open-source & open-design")
                buttonText: "Start"
            }
        }
        Page {
            background: null
            clip: true
            Layout.fillWidth: true
            header: OnboardingNav {
                navButton: NavButton {
                    iconSource: "image://images/caret-left"
                    text: "Back"
                    onClicked: {
                        introductions.decrementCurrentIndex()
                        swipeView.inSubPage = false
                    }
                }
            }
            ColumnLayout {
                width: 600
                spacing: 0
                anchors.top: parent.top
                anchors.horizontalCenter: parent.horizontalCenter
                Header {
                    Layout.fillWidth: true
                    bold: true
                    header: "About"
                    description: qsTr("Bitcoin Core is an open source project.\nIf you find it useful, please contribute.\n\n This is experimental software.")
                    descriptionMargin: 20
                }
                AboutOptions {
                    Layout.topMargin: 30
                }
            }
        }
        Page {
            background: null
            clip: true
            Layout.fillWidth: true
            header: OnboardingNav {
                navButton: NavButton {
                    iconSource: "image://images/caret-left"
                    text: "Back"
                    onClicked: {
                        introductions.decrementCurrentIndex()
                        swipeView.inSubPage = true
                    }
                }
            }
            ColumnLayout {
                width: 600
                spacing: 0
                anchors.top: parent.top
                anchors.horizontalCenter: parent.horizontalCenter
                Header {
                    Layout.fillWidth: true
                    bold: true
                    header: "Developer options"
                }
                DeveloperOptions {
                    Layout.topMargin: 30
                }
            }
        }
    }
}
