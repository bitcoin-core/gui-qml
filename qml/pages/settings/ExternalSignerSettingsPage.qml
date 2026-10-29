// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

import "../../controls"
import "../../components"

SettingsPage {
    id: root
    objectName: "externalSignerSettingsPage"
    title: qsTr("External signer")
    showBackButton: false

    PageHeading {
        objectName: "externalSignerIntroduction"
        Layout.fillWidth: true
        //: Introduction of the external signer settings page.
        description: qsTr("Connect a hardware wallet or another external signing tool. Creating an external signer wallet needs exactly one connected device.")
    }

    ExternalSignerPathSection {
        objectName: "externalSignerPathSection"
        Layout.fillWidth: true
        title: qsTr("Signer path")
    }

    Component.onCompleted: walletController.refreshExternalSignerStatus()
}
