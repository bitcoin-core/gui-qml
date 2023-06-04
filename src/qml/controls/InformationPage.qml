// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Page {
    id: root
    property alias bannerItem: banner_loader.sourceComponent
    property alias detailItem: detail_loader.sourceComponent
    property alias loadedDetailItem: detail_loader.item
    property alias navLeftDetail: navbar.leftDetail
    property alias navMiddleDetail: navbar.middleDetail
    property alias navRightDetail: navbar.rightDetail
    property string buttonText: ""
    property int buttonMargin: 40
    property bool bannerActive: true
    property bool detailActive: false
    property int detailTopMargin: 30
    property bool lastPage: false
    property bool bold: false
    property bool center: true
    property bool centerContent: false
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

    ScrollView {
        id: scrollView
        width: parent.width
        height: parent.height
        clip: true
        contentWidth: width

        ColumnLayout {
            id: middleContent
            width: parent.width
            spacing: root.buttonMargin
            ColumnLayout {
                id: information
                width: Math.min(parent.width, 600)
                spacing: 0
                Layout.alignment: Qt.AlignTop
                Loader {
                    id: banner_loader
                    active: root.bannerActive
                    visible: active
                    Layout.alignment: Qt.AlignCenter
                    Layout.topMargin: root.bannerMargin
                    sourceComponent: root.bannerItem
                }
                Header {
                    Layout.leftMargin: 20
                    Layout.rightMargin: 20
                    headerBold: root.bold
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
                    Layout.topMargin: root.detailTopMargin
                    Layout.leftMargin: 20
                    Layout.rightMargin: 20
                    Layout.maximumWidth: detailMaximumWidth
                    sourceComponent: root.detailItem
                }
            }
        }
        ContinueButton {
            id: continueButton
            visible: root.buttonText.length > 0
            enabled: visible
            width: Math.min(300, parent.width - 2 * anchors.leftMargin)
            anchors.topMargin: root.buttonMargin
            anchors.bottomMargin: 60
            anchors.leftMargin: 20
            anchors.rightMargin: 20
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            text: root.buttonText
            onClicked: root.lastPage ? swipeView.finished = true : swipeView.incrementCurrentIndex()
        }
    }

    states: [
        State {
            name: "CENTER"; when: root.centerContent
            AnchorChanges {
                target: continueButton
                anchors.top: undefined
                anchors.bottom: continueButton.parent.bottom
            }
            PropertyChanges {
                target: information
                Layout.alignment: Qt.AlignVCenter
            }
            PropertyChanges {
                target: middleContent
                height: parent.height - (continueButton.height
                    + continueButton.anchors.topMargin
                    + continueButton.anchors.bottomMargin)
            }
        },
        State {
            name: "TOP"; when: !root.centerContent
            AnchorChanges {
                target: continueButton
                anchors.bottom: undefined
            }
            PropertyChanges {
                target: continueButton
                anchors.topMargin: undefined
                anchors.bottomMargin: undefined
                anchors.leftMargin: undefined
                anchors.rightMargin: undefined
                anchors.horizontalCenter: undefined
                Layout.alignment: Qt.AlignHCenter
            }
            ParentChange {
                target: continueButton
                parent: middleContent
            }
        }
    ]
}
