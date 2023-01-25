// Copyright (c) 2023 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../../controls"

Page {
    background: null

    ColumnLayout {
        anchors.centerIn: parent
        width: parent.width
        spacing: 10
        Button {
            Layout.alignment: Qt.AlignCenter
            Layout.bottomMargin: 20
            background: null
            icon.source: "image://images/shutdown"
            icon.color: Theme.color.neutral9
            icon.width: 60
            icon.height: 60
        }
        Header {
            Layout.alignment: Qt.AlignCenter
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            header: qsTr("Saving...")
            headerBold: true
            description: qsTr("Do not shut down the computer until this is done.")
            descriptionColor: Theme.color.neutral7
        }
    }
}
