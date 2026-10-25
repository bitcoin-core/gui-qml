// Copyright (c) 2023 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15

import "../controls"

// Confirms opening a website in the browser. Every external link goes through it.
AlertPopup {
    id: root

    property string link: ""

    function openLink(url) {
        link = url
        open()
    }

    title: qsTr("Open link?")
    message: qsTr("This opens the following website in your browser:\n%1").arg(root.link)
    messageObjectName: "externalLinkMessage"
    messageTextFormat: Text.PlainText
    messageWrapMode: Text.WrapAtWordBoundaryOrAnywhere

    AlertAction {
        text: qsTr("Cancel")
        role: AlertAction.Cancel
        buttonObjectName: "externalLinkCancelButton"
    }

    AlertAction {
        text: qsTr("OK")
        buttonObjectName: "externalLinkOpenButton"
        onTriggered: Qt.openUrlExternally(root.link)
    }
}
