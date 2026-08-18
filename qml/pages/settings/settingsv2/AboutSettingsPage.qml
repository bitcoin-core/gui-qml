pragma ComponentBehavior: Bound

// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0

import "../../../controls"
import "../../../components"
import ".." as LegacySettings

SettingsPage {
    id: root
    objectName: "settingsv2AboutSettingsPage"
    title: qsTr("About")
    showBackButton: false

    PageHeading {
        Layout.fillWidth: true
        description: qsTr("Bitcoin Core is an open source project. If you find it useful, please contribute.\n\nThis is experimental software.")
    }

    FormSection {
        Layout.fillWidth: true

        LinkRow {
            Layout.fillWidth: true
            title: qsTr("Website")
            value: "bitcoincore.org"
            link: "https://bitcoincore.org"
            onActivated: function(link) { root.openExternalLink(link) }
        }

        LinkRow {
            Layout.fillWidth: true
            title: qsTr("Source code")
            value: "github.com/bitcoin/bitcoin"
            link: "https://github.com/bitcoin/bitcoin"
            onActivated: function(link) { root.openExternalLink(link) }
        }

        LinkRow {
            Layout.fillWidth: true
            title: qsTr("License")
            value: "MIT"
            link: "https://opensource.org/licenses/MIT"
            onActivated: function(link) { root.openExternalLink(link) }
        }

        LinkRow {
            objectName: "settingsv2AboutVersionRow"
            Layout.fillWidth: true
            title: qsTr("Version")
            value: BuildInfo.fullClientVersion
            link: "https://bitcoin.org/en/download"
            linkIconSource: ""
            showsDisclosureIndicator: true
            onActivated: function(link) { root.openExternalLink(link) }
        }

        ListRow {
            Layout.fillWidth: true
            title: qsTr("Developer options")
            description: qsTr("Only use these if you have development experience.")
            showDivider: false
            showsDisclosureIndicator: true
            onClicked: root.StackView.view.push(developerPage)
        }
    }

    function openExternalLink(link) {
        externalLinkPopup.link = link
        externalLinkPopup.open()
    }

    ExternalPopup {
        id: externalLinkPopup
        objectName: "settingsv2AboutExternalLinkPopup"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(450, Math.max(0, parent ? parent.width - 40 : 0))
    }

    Component {
        id: developerPage

        LegacySettings.SettingsDeveloper {
            onBack: root.StackView.view.pop()
        }
    }
}
