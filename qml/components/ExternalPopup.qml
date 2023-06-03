// Copyright (c) 2023 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import Qt.labs.settings 1.0
import org.bitcoincore.qt 1.0
import "../controls"

Popup {
    id: externalConfirmPopup
    property string link: ""
    modal: true
    padding: 0

    background: Rectangle {
        color: Theme.color.background
        radius: 10
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        NavigationBar {
            Layout.preferredHeight: 55
            middleDetail: Header {
                Layout.fillWidth: true
                header: qsTr("External Link")
                headerBold: true
                headerSize: 24
            }
        }

        Separator {
            Layout.fillWidth: true
        }

        ColumnLayout {
            id: popupContent
            Layout.fillWidth: true
            Layout.rightMargin: 20
            Layout.leftMargin: 20
            Layout.topMargin: 20
            Layout.bottomMargin: 20
            spacing: 30
            Header {
                Layout.fillWidth: true
                header: qsTr("Do you want to open the following website in your browser?")
                headerBold: false
                headerSize: 18
                description: ("\"" + externalConfirmPopup.link + "\"")
                descriptionMargin: 3
            }
            Loader {
                id: layoutLoader
                Layout.fillWidth: true
                sourceComponent: AppMode.isDesktop ? desktopLayout : mobileLayout
            }
        }
    }

    Component {
        id: desktopLayout
        RowLayout {
            Layout.fillWidth: true
            spacing: 15
            OutlineButton {
                text: qsTr("Cancel")
                Layout.fillWidth: true
                Layout.minimumWidth: 150
                onClicked: {
                    externalConfirmPopup.close()
                }
            }
            ContinueButton {
                text: qsTr("Ok")
                Layout.fillWidth: true
                Layout.minimumWidth: 150
                onClicked: {
                    Qt.openUrlExternally(externalConfirmPopup.link)
                    externalConfirmPopup.close()
                }
            }
        }
    }

    Component {
        id: mobileLayout
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 15
            OutlineButton {
                text: qsTr("Cancel")
                Layout.fillWidth: true
                Layout.minimumWidth: 150
                onClicked: {
                    externalConfirmPopup.close()
                }
            }
            ContinueButton {
                text: qsTr("Ok")
                Layout.fillWidth: true
                Layout.minimumWidth: 150
                onClicked: {
                    Qt.openUrlExternally(externalConfirmPopup.link)
                    externalConfirmPopup.close()
                }
            }
        }
    }
}
