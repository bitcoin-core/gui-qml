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
        id: pathSection
        objectName: "externalSignerPathSection"
        Layout.fillWidth: true
        //: Title of the field for the external signer executable, such as HWI.
        title: qsTr("Signer executable")
    }

    // Under the results it acts on, where wallet creation's setup step has
    // its check button.
    PrimaryButton {
        objectName: "externalSignerCheckDeviceButton"
        Layout.alignment: Qt.AlignRight
        horizontalPadding: Theme.space.xl
        text: pathSection.checking
            //: Check device button while the signer command and the device are being checked.
            ? qsTr("Checking…")
            //: Button that saves the signer path and checks the command and the connected device.
            : qsTr("Check device")
        enabled: pathSection.pathError.length === 0 && pathSection.editable && !pathSection.checking
        onClicked: pathSection.check()
    }
}
