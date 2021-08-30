// Copyright (c) 2021-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

import QtQuick
import QtQuick.Dialogs

MessageDialog {
    id: messageDialog
    title: "Bitcoin Core TnG"
    text: message
    onAccepted: Qt.quit()
    Component.onCompleted: visible = true
}
