// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../../controls"
import "../../components"

Page {
    id: root
    signal back
    property bool onboarding: false
    background: null
    PageStack {
        id: stack
        anchors.fill: parent
        initialItem: aboutPage
        Component {
            id: aboutPage
            InformationPage {
                id: about_settings
                bannerActive: false
                bannerMargin: 0
                bold: true
                showHeader: root.onboarding
                headerText: qsTr("About")
                headerMargin: 0
                description: qsTr("Bitcoin Core is an open source project.\nIf you find it useful, please contribute.\n\n This is experimental software.")
                descriptionMargin: 20
                detailActive: true
                detailItem: AboutOptions {
                    onNext: stack.push(developerSettings)
                }

                states: [
                    State {
                        when: root.onboarding
                        PropertyChanges {
                            target: about_settings
                            navLeftDetail: backButton
                            navMiddleDetail: null
                        }
                    },
                    State {
                        when: !root.onboarding
                        PropertyChanges {
                            target: about_settings
                            navLeftDetail: backButton
                            navMiddleDetail: header
                        }
                    }
                ]

                Component {
                    id: backButton
                    NavButton {
                        iconSource: "image://images/caret-left"
                        text: qsTr("Back")
                        onClicked: root.back()
                    }
                }
                Component {
                    id: header
                    Header {
                        headerBold: true
                        headerSize: 18
                        header: qsTr("About")
                    }
                }
            }
        }
        Component {
            id: developerSettings
            SettingsDeveloper {
                onboarding: root.onboarding
                onBack: stack.pop()
            }
        }
    }
}


