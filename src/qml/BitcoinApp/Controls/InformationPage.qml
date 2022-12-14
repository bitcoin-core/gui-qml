// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.bitcoincore.qt

Page {
    id: root
    property alias bannerItem: banner_loader.sourceComponent
    property alias detailItem: detail_loader.sourceComponent
    property alias navLeftDetail: navbar.leftDetail
    property alias navMiddleDetail: navbar.middleDetail
    property alias navRightDetail: navbar.rightDetail
    property string buttonText: ""
    property bool bannerActive: true
    property bool detailActive: false
    property bool lastPage: false
    property bool bold: false
    property bool center: true
    property int bannerMargin: 20
    required property string headerText
    property int headerMargin: 30
    property int headerSize: 28
    property string description: ""
    property int descriptionMargin: 20
    property int descriptionSize: 18
    property string subtext: ""
    property int subtextMargin: 30
    property int subtextSize: 15
    property real maximumWidth: 600
    property real detailMaximumWidth: 450

    background: null
    clip: true

    header: NavigationBar {
      id: navbar
    }

    ColumnLayout {
        id: information
        width: Math.min(parent.width, maximumWidth)
        anchors.horizontalCenter: parent.horizontalCenter
        spacing: 0
        Loader {
            id: banner_loader
            active: root.bannerActive
            visible: active
            Layout.alignment: Qt.AlignCenter
            Layout.topMargin: root.bannerMargin
            sourceComponent: root.bannerItem
        }
        Header {
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            bold: root.bold
            center: root.center
            header: root.headerText
            headerMargin: root.headerMargin
            headerSize: root.headerSize
            description: root.description
            descriptionMargin: root.descriptionMargin
            descriptionSize: root.descriptionSize
            subtext: root.subtext
            subtextMargin: root.subtextMargin
            subtextSize: root.subtextSize
        }
        Loader {
            id: detail_loader
            active: root.detailActive
            visible: active
            Layout.alignment: Qt.AlignCenter
            Layout.topMargin: 30
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            Layout.fillWidth: true
            Layout.maximumWidth: detailMaximumWidth
            sourceComponent: root.detailItem
        }
    }
    ContinueButton {
        id: continueButton
        visible: root.buttonText.length > 0
        enabled: visible
        width: Math.min(300, parent.width - 2 * anchors.leftMargin)
        anchors.topMargin: 40
        anchors.bottomMargin: 60
        anchors.leftMargin: 20
        anchors.rightMargin: 20
        anchors.horizontalCenter: parent.horizontalCenter
        text: root.buttonText
        onClicked: root.lastPage ? swipeView.finished = true : swipeView.incrementCurrentIndex()
    }

    state: AppMode.state

    states: [
        State {
            name: "MOBILE"
            AnchorChanges {
                target: continueButton
                anchors.top: undefined
                anchors.bottom: continueButton.parent.bottom
            }
        },
        State {
            name: "DESKTOP"
            AnchorChanges {
                target: continueButton
                anchors.top: information.bottom
                anchors.bottom: undefined
            }
        }
    ]
}
