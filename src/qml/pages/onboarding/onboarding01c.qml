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
