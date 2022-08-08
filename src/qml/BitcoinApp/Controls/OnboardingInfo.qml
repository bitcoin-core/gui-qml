// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Control {
    id: root
    property alias banner: banner_loader.sourceComponent
    required property string buttonText
    property bool bold: false
    property bool center: true
    property int bannerMargin: 20
    required property string header
    property int headerMargin: 30
    property int headerSize: 28
    property string description: ""
    property int descriptionMargin: 20
    property int descriptionSize: 18
    property string subtext: ""
    property int subtextMargin: 30
    property int subtextSize: 15

    implicitWidth: 600

    contentItem: ColumnLayout {
        spacing: 0
        Loader {
            id: banner_loader
            active: true
            visible: active
            Layout.alignment: Qt.AlignCenter
            Layout.topMargin: root.bannerMargin
            sourceComponent: root.actionItem
        }
        Header {
            Layout.fillWidth: true
            bold: root.bold
            center: root.center
            header: root.header
            headerMargin: root.headerMargin
            headerSize: root.headerSize
            description: root.description
            descriptionMargin: root.descriptionMargin
            descriptionSize: root.descriptionSize
            subtext: root.subtext
            subtextMargin: root.subtextMargin
            subtextSize: root.subtextSize
        }
        ContinueButton {
            Layout.alignment: Qt.AlignCenter
            Layout.topMargin: 40
            text: root.buttonText
            onClicked: swipeView.incrementCurrentIndex()
        }
    }
}
