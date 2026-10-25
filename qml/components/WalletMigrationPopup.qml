// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15

import "../controls"

AlertPopup {
    id: root

    property string popupObjectName: ""
    property string titleText: qsTr("Wallet update required")
    property string descriptionText: ""
    property string confirmText: qsTr("Update wallet")
    property string busyConfirmText: qsTr("Updating...")
    property string cancelButtonObjectName: ""
    property string confirmButtonObjectName: ""

    signal confirmed()

    objectName: popupObjectName
    title: titleText
    message: descriptionText

    AlertAction {
        text: qsTr("Cancel")
        role: AlertAction.Cancel
        enabled: !root.busy
        buttonObjectName: root.cancelButtonObjectName
    }

    AlertAction {
        text: root.busy ? root.busyConfirmText : root.confirmText
        enabled: !root.busy
        closesPopup: false
        buttonObjectName: root.confirmButtonObjectName
        onTriggered: root.confirmed()
    }
}
