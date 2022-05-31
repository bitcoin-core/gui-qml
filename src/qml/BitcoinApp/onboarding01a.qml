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
        Loader {
            active: true
            visible: active
            Layout.alignment: Qt.AlignRight
            Layout.margins: 11
            sourceComponent: Item {
                width: 24
                height: 24
                Rectangle {
                    anchors.fill: parent
                    color: Theme.color.white
                    radius: width*0.5
                }
                Image {
                    id: icon
                    source: ":/qt/qml/BitcoinApp/res/icons/info"
                    width: parent.width
                    height: parent.height
                    anchors.centerIn: parent
                    fillMode: Image.PreserveAspectFit
                    mipmap: true
                    MouseArea {
                        anchors.fill: parent
                        onClicked: {
                            introductions.incrementCurrentIndex()
                            swipeView.inSubPage = true
                        }
                    }
                }
            }
        }
    }
    ColumnLayout {
        width: 600
        spacing: 0
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        Image {
            Layout.alignment: Qt.AlignCenter
            source: "image://images/app"
            sourceSize.width: 100
            sourceSize.height: 100
        }
        Header {
            Layout.fillWidth: true
            implicitWidth: childrenRect.width
            bold: true
            header: qsTr("Bitcoin Core App")
            headerSize: 36
            headerMargin: 30
            description: qsTr("Be part of the Bitcoin network.")
            descriptionSize: 24
            descriptionMargin: 10
            subtext: qsTr("100% open-source & open-design")
            subtextMargin: 30
        }
        ContinueButton {
            Layout.alignment: Qt.AlignCenter
            Layout.topMargin: 40
            text: "Start"
            onClicked: swipeView.incrementCurrentIndex()
        }
    }
}
