// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0

import "../controls"

ColumnLayout {
    id: root

    property var wallet: null
    property var clipboard: Clipboard
    // Descriptors that importdescriptors can't take without private keys.
    property int omittedCount: 0

    // Counting means exporting every descriptor, so only while shown.
    function refresh() {
        if (visible) omittedCount = wallet ? wallet.descriptorImportOmittedCount() : 0
    }

    function copyAll() {
        const json = wallet ? wallet.descriptorImportJson() : ""
        if (json === "") return
        clipboard.setText(json)
        copyAllTimer.restart()
    }

    spacing: Theme.space.l

    Component.onCompleted: refresh()
    onWalletChanged: refresh()
    onVisibleChanged: refresh()

    Connections {
        target: root.wallet && root.wallet.descriptorExportsChanged ? root.wallet : null
        function onDescriptorExportsChanged() { root.refresh() }
    }

    Timer {
        id: copyAllTimer
        interval: 1000
    }

    NeutralButton {
        objectName: "descriptorsCopyAll"
        buttonSize: NeutralButton.Large
        Layout.fillWidth: true
        //: Copies every descriptor as JSON for Bitcoin Core's importdescriptors command.
        text: copyAllTimer.running ? qsTr("Copied") : qsTr("Copy all for Bitcoin Core")
        onClicked: root.copyAll()
    }

    CoreText {
        objectName: "descriptorsCopyAllNote"
        Layout.fillWidth: true
        //: "importdescriptors" is a Bitcoin Core command and is not translated.
        text: qsTr("Copies every descriptor, with when it was created, as JSON for importdescriptors. Import it into a Bitcoin Core wallet without private keys.")
            + (root.omittedCount === 0 ? ""
               : " " + (root.omittedCount === 1
                   ? qsTr("1 older descriptor needs private keys to find its addresses, so it is left out.")
                   //: %1 is a number of descriptors.
                   : qsTr("%1 older descriptors need private keys to find their addresses, so they are left out.").arg(root.omittedCount)))
        textStyle: Theme.text.caption
        color: Theme.color.neutral7
        horizontalAlignment: Text.AlignLeft
    }
}
