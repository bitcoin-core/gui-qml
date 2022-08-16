// Copyright (c) 2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../controls"

ApplicationWindow {
    id: root
    title: "Bitcoin Core App"
    minimumWidth: 640
    minimumHeight: 665
    color: Theme.color.background
    visible: true

    Page {
        id: errorWindow
        width: 400
        height: 240
        anchors.centerIn: parent
        background: Rectangle {
            color: Theme.color.background
            radius: 10
            border {
                width: 1
                color: Theme.color.neutral4
            }
        }
        header: RowLayout {
            width: errorWindow.width
            height: 55
            Rectangle {
                width: 55
            }
            Header {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignCenter
                bold: true
                header: "Error"
                headerSize: 24
            }
            Button {
                id: icon_button
                implicitWidth: 55
                icon.source: "image://images/cross"
                icon.color: Theme.color.neutral9
                icon.width: 20
                background: null
                onClicked: Qt.quit()
            }
        }
        Rectangle {
            anchors.bottom: errorWindow.header.bottom
            width: errorWindow.width
            height: 1
            color: Theme.color.neutral5
        }
        ColumnLayout {
            anchors.fill: parent
            Header {
                Layout.fillWidth: true
                Layout.topMargin: 20
                header: message
                headerSize: 18
            }
            OutlineButton {
                Layout.alignment: Qt.AlignCenter
                Layout.bottomMargin: 20
                text: "Quit"
                onClicked: Qt.quit()
            }
        }
    }
}
