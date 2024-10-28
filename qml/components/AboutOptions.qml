// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../controls"

ColumnLayout {
    id: root
    signal next
    spacing: 4
    Setting {
        id: websiteLink
        Layout.fillWidth: true
        header: qsTr("Website")
        actionItem: ExternalLink {
            parentState: websiteLink.state
            description: "bitcoincore.org"
            link: "https://bitcoincore.org"
        }
        onClicked: openPopup(loadedItem.link)
    }
    Separator { Layout.fillWidth: true }
    Setting {
        id: sourceLink
        Layout.fillWidth: true
        header: qsTr("Source code")
        actionItem: ExternalLink {
            parentState: sourceLink.state
            description: "github.com/bitcoin/bitcoin"
            link: "https://github.com/bitcoin/bitcoin"
        }
        onClicked: openPopup(loadedItem.link)
    }
    Separator { Layout.fillWidth: true }
    Setting {
        id: licenseLink
        Layout.fillWidth: true
        header: qsTr("License")
        actionItem: ExternalLink {
            parentState: licenseLink.state
            description: "MIT"
            link: "https://opensource.org/licenses/MIT"
        }
        onClicked: openPopup(loadedItem.link)
    }
    Separator { Layout.fillWidth: true }
    Setting {
        id: versionLink
        Layout.fillWidth: true
        header: qsTr("Version")
        actionItem: ExternalLink {
            parentState: versionLink.state
            description: nodeModel.fullClientVersion
            link: "https://bitcoin.org/en/download"
            iconSource: "image://images/caret-right"
            iconWidth: 18
            iconHeight: 18
        }
        onClicked: openPopup(loadedItem.link)
    }
    Separator { Layout.fillWidth: true }
    Setting {
        id: gotoDeveloper
        Layout.fillWidth: true
        header: qsTr("Developer options")
        description: qsTr("Only use these if you have development experience")
        actionItem: CaretRightIcon {
            color: gotoDeveloper.stateColor
        }
        onClicked: {
            root.next()
        }
    }
    ExternalPopup {
        id: confirmPopup
        anchors.centerIn: Overlay.overlay
        width: parent.width
    }

    function openPopup(link) {
        confirmPopup.link = link
        confirmPopup.open()
    }
}
