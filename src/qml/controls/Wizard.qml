// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Page {
    id: root
    property var views
    property alias finished: wizard.finished
    background: null
    header: RowLayout {
        height: 50
        Layout.leftMargin: 10
        Loader {
            active: wizard.currentIndex > 0 ? true : false
            visible: active
            sourceComponent: TextButton {
                text: "‹ Back"
                onClicked: wizard.currentIndex -= 1
            }
        }
    }
    SwipeView {
        id: wizard
        property bool inSubPage: false
        property bool finished: false
        anchors.fill: parent
        interactive: false
        Repeater {
            model: views.length
            Loader {
              source: "../pages/" + views[index]
            }
        }
    }
    footer: Rectangle {
        Layout.fillWidth: true
        height: 50
        color: "black"
        Indicator {
            anchors.top: parent.top
            anchors.horizontalCenter: parent.horizontalCenter
            Layout.alignment: Qt.AlignCenter
            count: wizard.count
            currentIndex: wizard.currentIndex
        }
    }
}
