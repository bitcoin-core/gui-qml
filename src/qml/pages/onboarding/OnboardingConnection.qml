// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../../controls"
import "../../components"
import "../settings"

Page {
    id: root
    signal back
    signal next
    background: null
    clip: true
    PageStack {
        id: connectionStack
        anchors.fill: parent
        vertical: true
        initialItem: onboardingConnection
        Component {
            id: onboardingConnection
            InformationPage {
                navLeftDetail: NavButton {
                    iconSource: "image://images/caret-left"
                    text: qsTr("Back")
                    onClicked: root.back()
                }
                bannerItem: Image {
                    Layout.topMargin: 20
                    Layout.alignment: Qt.AlignCenter
                    source: Theme.image.storage
                    sourceSize.width: 200
                    sourceSize.height: 200
                }
                bold: true
                headerText: qsTr("Starting initial download")
                headerMargin: 30
                description: qsTr("The application will connect to the Bitcoin network and start downloading and verifying transactions.\n\nThis may take several hours, or even days, based on your connection.")
                descriptionMargin: 10
                detailActive: true
                detailTopMargin: 10
                detailItem: RowLayout {
                    TextButton {
                        Layout.alignment: Qt.AlignCenter
                        text: qsTr("Connection settings")
                        onClicked: connectionStack.push(connectionSettings)
                    }
                }
                lastPage: true
                buttonText: qsTr("Next")
                buttonMargin: 20
                onNext: root.next()
            }
        }
        Component {
            id: connectionSettings
            SettingsConnection {
                onboarding: true
                onBack: connectionStack.pop()
            }
        }
    }
}
