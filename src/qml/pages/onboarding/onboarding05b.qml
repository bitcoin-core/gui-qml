// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../../controls"
import "../../components"

Page {
    background: null
    Layout.fillWidth: true
    clip: true
    ColumnLayout {
        width: 800
        spacing: 0
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        Header {
            Layout.fillWidth: true
            header: "Storage settings"
        }
        StorageSettings {
            Layout.topMargin: 50
        }
        TextButton {
            Layout.alignment: Qt.AlignCenter
            Layout.topMargin: 30
            text: "Done"
            textSize: 18
            textColor: "#F7931A"
            onClicked: {
                storages.decrementCurrentIndex()
                swipeView.inSubPage = false
            }
        }
    }
}
