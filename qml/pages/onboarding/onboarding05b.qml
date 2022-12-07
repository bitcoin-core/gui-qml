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
    header: NavigationBar {
        rightDetail: NavButton {
            text: "Done"
            onClicked: {
                storages.decrementCurrentIndex()
                swipeView.inSubPage = false
            }
        }
    }
    OnboardingInfo {
        height: parent.height
        width: Math.min(parent.width, 600)
        anchors.horizontalCenter: parent.horizontalCenter
        bannerActive: false
        bold: true
        header: "Storage settings"
        headerMargin: 0
        detailActive: true
        detailItem: ColumnLayout {
            spacing: 0
            StorageSettings {
                Layout.maximumWidth: 450
                Layout.alignment: Qt.AlignCenter
            }
        }
    }
}
