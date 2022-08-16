// Copyright (c) 2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../controls"

ApplicationWindow {
    id: root
    visible: true
    minimumWidth: 500
    minimumHeight: 320
    color: "transparent"
    flags: Qt.FramelessWindowHint
    background: Rectangle {
      color: Theme.color.background
      radius: 10
    }
    menuBar: RowLayout {
        width: parent.width
        height: 55

        DragHandler {
          id: menuHandler
          grabPermissions: TapHandler.CanTakeOverFromAnything
          onActiveChanged: if (active) { root.startSystemMove(); }
          target: null
        }
        Rectangle {
            width: 55
        }
        Header {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignCenter
            bold: true
            header: "Bitcoin Core App"
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

    DragHandler {
      id: moveHandler
      grabPermissions: TapHandler.CanTakeOverFromAnything
      onActiveChanged: if (active) { root.startSystemMove(); }
      target: null
    }
    Rectangle {
        anchors.bottom: root.menuBar.bottom
        width: root.width
        height: 1
        color: Theme.color.neutral5
    }
    ColumnLayout {
        spacing: 30
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter

        Header {
            Layout.topMargin: 30
            Layout.fillWidth: true
            bold: true
            header: "There was a problem starting up."
            headerSize: 24
            description: message
            descriptionSize: 18
            descriptionMargin: 5
        }
        OutlineButton {
            Layout.alignment: Qt.AlignCenter
            text: "Quit"
            onClicked: Qt.quit()
        }
    }
}
