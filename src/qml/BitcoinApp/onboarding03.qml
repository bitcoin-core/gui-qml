// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BitcoinApp.Controls

Page {
    background: null
    clip: true
    Layout.fillWidth: true
    header: RowLayout {
        height: 50
        Layout.leftMargin: 10
        Loader {
            active: true
            visible: active
            sourceComponent: Item {
                width: 73
                height: 46
                RowLayout {
                    anchors.fill: parent
                    spacing: 0
                    Image {
                        source: ":/qt/qml/BitcoinApp/res/icons/caret-left.png"
                        mipmap: true
                        Layout.preferredWidth: 30
                        Layout.preferredHeight: 30
                        Layout.alignment: Qt.AlignVCenter
                        fillMode: Image.PreserveAspectFit
                    }
                    Text {
                        Layout.alignment: Qt.AlignVCenter
                        text: "Back"
                        color: Theme.color.neutral9
                        font.family: "Inter"
                        font.styleName: "Semi Bold"
                        font.pixelSize: 18
                    }
                }
                MouseArea {
                    anchors.fill: parent
                    onClicked: swipeView.currentIndex -= 1
                }
            }
        }
    }
    OnboardingInfo {
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        banner: Image {
            source: "image://images/app"
            sourceSize.width: 200
            sourceSize.height: 200
        }
        bold: true
        header: qsTr("The block clock")
        description: qsTr("The Bitcoin network targets a new block every\n10 minutes. Sometimes it's faster and sometimes slower.\n\nThe block clock indicates each block on a dial\nthat represents the current day.")
        buttonText: "Next"
    }
}
