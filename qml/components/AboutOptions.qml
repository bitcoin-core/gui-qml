// Copyright (c) 2022-2026 The Bitcoin Core developers
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
    spacing: 0
    Setting {
        id: websiteLink
        objectName: "aboutWebsiteLink"
        Layout.fillWidth: true
        header: qsTr("Website")
        actionItem: ExternalLink {
            objectName: "aboutWebsiteLinkIcon"
            parentState: websiteLink.visualState
            description: "bitcoincore.org"
            link: "https://bitcoincore.org"
        }
        onClicked: loadedItem.requestOpen()
    }
    Separator { Layout.fillWidth: true }
    Setting {
        id: sourceLink
        objectName: "aboutSourceCodeLink"
        Layout.fillWidth: true
        header: qsTr("Source code")
        actionItem: ExternalLink {
            objectName: "aboutSourceCodeLinkIcon"
            parentState: sourceLink.visualState
            description: "github.com/bitcoin/bitcoin"
            link: "https://github.com/bitcoin/bitcoin"
        }
        onClicked: loadedItem.requestOpen()
    }
    Separator { Layout.fillWidth: true }
    Setting {
        id: licenseLink
        objectName: "aboutLicenseLink"
        Layout.fillWidth: true
        header: qsTr("License")
        actionItem: ExternalLink {
            objectName: "aboutLicenseLinkIcon"
            parentState: licenseLink.visualState
            description: "MIT"
            link: "https://opensource.org/licenses/MIT"
        }
        onClicked: loadedItem.requestOpen()
    }
    Separator { Layout.fillWidth: true }
    Setting {
        id: versionLink
        objectName: "aboutVersionLink"
        Layout.fillWidth: true
        header: qsTr("Version")
        actionItem: ExternalLink {
            objectName: "aboutVersionLinkIcon"
            parentState: versionLink.visualState
            description: BuildInfo.fullClientVersion
            link: "https://bitcoin.org/en/download"
        }
        onClicked: loadedItem.requestOpen()
    }
    Separator { Layout.fillWidth: true }
    Setting {
        id: gotoDeveloper
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
}
