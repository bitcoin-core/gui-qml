// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0
import "../controls"

ColumnLayout {
    id: root
    signal next
    property bool showDeveloper: true
    spacing: 0
    Setting {
        id: websiteLink
        Layout.fillWidth: true
        header: qsTr("Website")
        actionItem: ExternalLink {
            parentState: websiteLink.visualState
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
            parentState: sourceLink.visualState
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
            parentState: licenseLink.visualState
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
            parentState: versionLink.visualState
            description: BuildInfo.fullClientVersion
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
        visible: root.showDeveloper
        objectName: "gotoDeveloperSetting"
        Layout.fillWidth: true
        header: qsTr("Developer options")
        description: qsTr("Only use these if you have development experience")
        actionItem: RightContentIcon {
            color: gotoDeveloper.stateColor
            source: "image://images/caret-right"
            iconSize: 18
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
