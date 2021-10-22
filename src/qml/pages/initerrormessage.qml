// Copyright (c) 2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.12
import QtQuick.Dialogs 1.3

MessageDialog {
    id: messageDialog
    title: "Bitcoin Core TnG"
    icon: StandardIcon.Critical
    text: message
    onAccepted: Qt.quit()
    Component.onCompleted: visible = true
}
