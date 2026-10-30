// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0

import "../../controls"
import "../../components"

SettingsPage {
    id: root
    objectName: "aboutSettingsPage"
    title: onboardingModal ? qsTr("About Bitcoin Core") : qsTr("About")
    showBackButton: false
    property bool onboardingModal: false

    PageHeading {
        Layout.fillWidth: true
        description: qsTr("Bitcoin Core is an open source project. If you find it useful, please contribute.\n\nThis is experimental software.")
    }

    FormSection {
        Layout.fillWidth: true
        surfaceLevel: root.onboardingModal ? 1 : 0

        LinkRow {
            Layout.fillWidth: true
            dividerColor: Theme.color.surfaceDivider
            title: qsTr("Website")
            value: "bitcoincore.org"
            link: "https://bitcoincore.org"
            onActivated: function(link) { root.openExternalLink(link) }
        }

        LinkRow {
            Layout.fillWidth: true
            dividerColor: Theme.color.surfaceDivider
            title: qsTr("Source code")
            value: "github.com/bitcoin/bitcoin"
            link: "https://github.com/bitcoin/bitcoin"
            onActivated: function(link) { root.openExternalLink(link) }
        }

        LinkRow {
            Layout.fillWidth: true
            dividerColor: Theme.color.surfaceDivider
            title: qsTr("License")
            value: "MIT"
            link: "https://opensource.org/licenses/MIT"
            onActivated: function(link) { root.openExternalLink(link) }
        }

        LinkRow {
            objectName: "aboutVersionRow"
            Layout.fillWidth: true
            dividerColor: Theme.color.surfaceDivider
            title: qsTr("Version")
            value: BuildInfo.fullClientVersion
            link: "https://bitcoin.org/en/download"
            linkIconSource: ""
            showsDisclosureIndicator: true
            onActivated: function(link) { root.openExternalLink(link) }
        }

        ListRow {
            objectName: "aboutDeveloperRow"
            Layout.fillWidth: true
            title: qsTr("Developer options")
            description: qsTr("Only use these if you have development experience.")
            showDivider: false
            showsDisclosureIndicator: true
            onClicked: root.StackView.view.push(developerPage)
        }
    }

    function openExternalLink(link) {
        externalLinkPopup.openLink(link)
    }

    ExternalPopup {
        id: externalLinkPopup
        objectName: "aboutExternalLinkPopup"
        parent: Overlay.overlay
    }

    Component {
        id: developerPage

        SettingsDeveloper {
            onboarding: root.onboardingModal
            onBack: root.StackView.view.pop()
        }
    }
}
