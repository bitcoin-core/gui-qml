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
}
