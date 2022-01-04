// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Page {
    id: root
    property var views
    property alias finished: swipeView.finished
    background: null
    header: RowLayout {
        height: 50
        Layout.leftMargin: 10
        Loader {
            active: swipeView.currentIndex > 0 ? true : false
            visible: active
            sourceComponent: TextButton {
                text: "‹ Back"
                onClicked: swipeView.currentIndex -= 1
            }
        }
    }
    SwipeView {
        id: swipeView
        property bool inSubPage: false
        property bool finished: false
        anchors.fill: parent
        interactive: false
        Repeater {
            model: views.length
            Loader {
                source: "../" + views[index]
            }
        }
    }
    footer: Page {
        background: Rectangle {
            color: "black"
        }
        contentItem: RowLayout {
            PageIndicator {
                Layout.alignment: Qt.AlignCenter
                count: swipeView.count
                currentIndex: swipeView.currentIndex
            }
        }
    }
}
