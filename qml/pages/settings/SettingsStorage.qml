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
    property bool customStorage: false
    property bool customStorageAmount
    property bool onboarding: false
    bannerActive: false
    bold: true
    showHeader: root.onboarding
    headerText: qsTr("Storage settings")
    headerMargin: 0
    detailActive: true
    detailItem: StorageSettings {
        id: storageSettings
        onCustomStorageChanged: {
            root.customStorage = storageSettings.customStorage
        }
        onCustomStorageAmountChanged: {
            root.customStorageAmount = storageSettings.customStorageAmount
        }
    }
    states: [
        State {
            when: root.onboarding
            PropertyChanges {
                target: root
                navLeftDetail: null
                navMiddleDetail: null
                navRightDetail: doneButton
            }
        },
        State {
            when: !root.onboarding
            PropertyChanges {
                target: root
                navLeftDetail: backButton
                navMiddleDetail: header
                navRightDetail: null
            }
        }
    ]

    Component {
        id: backButton
        NavButton {
            iconSource: "image://images/caret-left"
            text: qsTr("Back")
            onClicked: root.back()
        }
    }
    Component {
        id: header
        Header {
            headerBold: true
            headerSize: 18
            header: qsTr("Storage Settings")
        }
    }
    Component {
        id: doneButton
        NavButton {
            text: qsTr("Done")
            onClicked: root.back()
        }
    }
}
