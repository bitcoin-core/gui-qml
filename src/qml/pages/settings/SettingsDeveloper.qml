// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../../controls"
import "../../components"

InformationPage {
    id: root
    property bool onboarding: false
    navLeftDetail: NavButton {
        iconSource: "image://images/caret-left"
        text: qsTr("Back")
        onClicked: {
            root.back()
        }
    }
    bannerActive: false
    bold: true
    showHeader: root.onboarding
    headerText: qsTr("Developer options")
    headerMargin: 0
    detailActive: true
    detailItem: DeveloperOptions {}

    states: [
        State {
            when: root.onboarding
            PropertyChanges {
                target: root
                navMiddleDetail: null
            }
        },
        State {
            when: !root.onboarding
            PropertyChanges {
                target: root
                navMiddleDetail: header
            }
        }
    ]

    Component {
        id: header
        Header {
            headerBold: true
            headerSize: 18
            header: qsTr("Developer settings")
        }
    }
}
