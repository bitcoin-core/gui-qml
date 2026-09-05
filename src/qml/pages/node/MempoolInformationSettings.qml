// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

import "../../controls"
import "../../components"

InformationPage {
    id: root
    objectName: "mempoolInformationSettingsPage"
    property bool showBackButton: true

    showNavBar: false
    header: SettingsHeader {
        title: qsTr("Mempool information")
        showBackButton: root.showBackButton
        backButtonObjectName: "mempoolInformationBackButton"
        onBack: root.back()
    }

    bannerActive: false
    bold: true
    showHeader: false
    headerText: ""
    headerMargin: 0
    description: ""
    descriptionMargin: 0
    detailActive: true
    detailTopMargin: 0
    detailMaximumWidth: 450
    detailItem: ColumnLayout {
        spacing: 4

        SettingsRestartNotice {
            objectName: "mempoolRestartNotice"
            visible: optionsModel.mempoolSettingsDirty
            Layout.fillWidth: true
            Layout.bottomMargin: visible ? 12 : 0
        }

        MempoolInformationRows {
            id: mempoolInformationRows
            Layout.fillWidth: true
        }
    }

    Component.onCompleted: mempoolModel.mempoolInfoPollingActive = visible
    Component.onDestruction: mempoolModel.mempoolInfoPollingActive = false
    onVisibleChanged: {
        mempoolModel.mempoolInfoPollingActive = visible
    }
}
