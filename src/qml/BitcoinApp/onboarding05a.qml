// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BitcoinApp.Controls
import BitcoinApp.Components

Page {
    background: null
    Layout.fillWidth: true
    clip: true
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
    ColumnLayout {
        width: 600
        spacing: 0
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        Header {
            Layout.fillWidth: true
            bold: true
            header: qsTr("Storage")
            description: qsTr("Data retrieved from the Bitcoin network is stored\non your device.\n\nYou have 500GB of storage available.")
        }
        StorageOptions {
            Layout.topMargin: 30
            Layout.alignment: Qt.AlignCenter
        }
        TextButton {
            Layout.alignment: Qt.AlignCenter
            Layout.topMargin: 30
            text: "Detailed settings"
            textSize: 18
            textColor: "#F7931A"
            onClicked: {
              storages.incrementCurrentIndex()
              swipeView.inSubPage = true
            }
        }
        ContinueButton {
            Layout.alignment: Qt.AlignCenter
            Layout.topMargin: 40
            text: "Next"
            onClicked: swipeView.incrementCurrentIndex()
        }
    }
}
