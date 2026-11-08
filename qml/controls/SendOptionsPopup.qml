// Copyright (c) 2025-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15

ContextMenu {
    id: root
    objectName: "sendOptionsPopup"

    property alias coinControlEnabled: coinControlToggle.checked
    property alias multipleRecipientsEnabled: multipleRecipientsToggle.checked

    signal openPaymentRequest()
    signal importPsbtFromFileRequested()
    signal clearFormRequested()

    modal: true
    dim: false

    ContextMenuButton {
        objectName: "sendOptionsOpenPaymentRequestButton"
        text: qsTr("Open payment request")
        autoClose: true
        onTriggered: root.openPaymentRequest()
    }

    ContextMenuDivider {}

    ContextMenuToggle {
        id: coinControlToggle
        objectName: "sendOptionsCoinControlToggle"
        text: qsTr("Enable Coin control")
    }

    ContextMenuToggle {
        id: multipleRecipientsToggle
        objectName: "sendOptionsMultipleRecipientsToggle"
        text: qsTr("Multiple Recipients")
    }

    ContextMenuDivider {}

    ContextMenuButton {
        objectName: "sendImportPsbtFromFileButton"
        text: qsTr("Import PSBT from file…")
        iconSource: "qrc:/icons/file"
        onTriggered: root.importPsbtFromFileRequested()
    }

    ContextMenuDivider {}

    ContextMenuButton {
        objectName: "sendClearFormButton"
        text: qsTr("Clear form")
        iconSource: "qrc:/icons/cross"
        onTriggered: root.clearFormRequested()
    }
}
