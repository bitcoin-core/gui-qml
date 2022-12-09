// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BitcoinApp.Controls
import BitcoinApp.Components

Item {
    property alias navLeftDetail: aboutSwipe.navLeftDetail
    SwipeView {
        id: aboutSwipe
        property alias navLeftDetail: about_settings.navLeftDetail
        anchors.fill: parent
        interactive: false
        orientation: Qt.Horizontal
        InformationPage {
            id: about_settings
            bannerActive: false
            bannerMargin: 0
            bold: true
            headerText: qsTr("About")
            headerMargin: 0
            description: qsTr("Bitcoin Core is an open source project.\nIf you find it useful, please contribute.\n\n This is experimental software.")
            descriptionMargin: 20
            detailActive: true
            detailItem: ColumnLayout {
                spacing: 0
                AboutOptions {
                    Layout.maximumWidth: 450
                    Layout.alignment: Qt.AlignCenter
                }
            }
        }
        SettingsDeveloper {
            navLeftDetail: NavButton {
                iconSource: "image://images/caret-left"
                text: qsTr("Back")
                onClicked: {
                    aboutSwipe.decrementCurrentIndex()
                }
            }
        }
    }
}
