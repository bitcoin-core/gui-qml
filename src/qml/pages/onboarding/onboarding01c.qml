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
                        source: "qrc:/icons/caret-left"
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
                    onClicked: {
                        introductions.decrementCurrentIndex()
                        swipeView.inSubPage = true
                    }
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
            header: "Developer options"
        }
        DeveloperOptions {
            Layout.topMargin: 30
        }
    }
}
