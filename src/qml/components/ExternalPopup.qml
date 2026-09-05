// Copyright (c) 2023 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0
import "../controls"

Popup {
    id: externalConfirmPopup
    property string link: ""
    modal: true
    padding: 0
    anchors.centerIn: parent

    background: Rectangle {
        color: Theme.color.background
        radius: 10
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        CoreText {
            Layout.fillWidth: true
            Layout.preferredHeight: 55
            text: qsTr("External Link")
            bold: true
            font.pixelSize: 24
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }

        Separator {
            Layout.fillWidth: true
        }

        Header {
            Layout.fillWidth: true
            Layout.margins: 20
            Layout.topMargin: 20
            header: qsTr("Do you want to open the following website in your browser?")
            headerBold: false
            headerSize: 16
            description: ("\"" + externalConfirmPopup.link + "\"")
            descriptionMargin: 8
            descriptionTextFormat: Text.PlainText
        }

        GridLayout {
            Layout.fillWidth: true
            Layout.margins: 20
            Layout.topMargin: 0
            columns: AppMode.isDesktop ? 2 : 1
            columnSpacing: 15
            rowSpacing: 10

            OutlineButton {
                text: qsTr("Cancel")
                Layout.fillWidth: true
                Layout.minimumWidth: 120
                onClicked: externalConfirmPopup.close()
            }

            ContinueButton {
                text: qsTr("Ok")
                Layout.fillWidth: true
                Layout.minimumWidth: 120
                onClicked: {
                    Qt.openUrlExternally(externalConfirmPopup.link)
                    externalConfirmPopup.close()
                }
            }
        }
    }
}
