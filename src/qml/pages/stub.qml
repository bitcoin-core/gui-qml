// Copyright (c) 2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../components"
import "../controls"

ApplicationWindow {
    id: appWindow
    title: "Bitcoin Core TnG"
    minimumWidth: 750
    minimumHeight: 450
    color: "black"
    visible: true

    Component.onCompleted: nodeModel.startNodeInitializionThread();

    ColumnLayout {
        anchors.centerIn: parent
        spacing: 15
        width: 400
        Image {
            Layout.alignment: Qt.AlignCenter
            source: "image://images/app"
            sourceSize.width: 64
            sourceSize.height: 64
        }
        Header {
            Layout.fillWidth: true
            bold: true
            header: qsTr("Bitcoin Core App")
            headerSize: 36
            headerMargin: 30
            description: qsTr("Be part of the Bitcoin network.")
            descriptionSize: 24
            descriptionMargin: 0
            subtext: qsTr("100% open-source & open-design")
            subtextMargin: 25
        }
        BlockCounter {
            Layout.alignment: Qt.AlignCenter
            blockHeight: nodeModel.blockTipHeight
        }
        ProgressIndicator {
            id: indicator
            Layout.fillWidth: true
            progress: nodeModel.verificationProgress
        }
        ConnectionOptions {
            Layout.preferredWidth: 400
            focus: true
        }
    }
}
