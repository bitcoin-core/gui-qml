// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Control {
    id: root
    property bool bold: false
    property bool center: true
    required property string header
    property int headerMargin
    property int headerSize: 28
    property string description
    property int descriptionMargin: 10
    property int descriptionSize: 18
    property string subtext
    property int subtextMargin
    property int subtextSize: 15
    property bool wrap: true
    contentItem: ColumnLayout {
        spacing: 0
        Label {
            Layout.fillWidth: true
            Layout.preferredWidth: 0
            topPadding: root.headerMargin
            font.family: "Inter"
            font.styleName: root.bold ? "Semi Bold" : "Regular"
            font.pixelSize: root.headerSize
            color: Theme.color.neutral9
            text: root.header
            horizontalAlignment: center ? Text.AlignHCenter : Text.AlignLeft
            wrapMode: wrap ? Text.WordWrap : Text.NoWrap
        }
        Loader {
            Layout.fillWidth: true
            Layout.preferredWidth: 0
            active: root.description.length > 0
            visible: active
            sourceComponent: Label {
                topPadding: root.descriptionMargin
                font.family: "Inter"
                font.styleName: "Regular"
                font.pixelSize: root.descriptionSize
                color: Theme.color.neutral8
                text: root.description
                horizontalAlignment: root.center ? Text.AlignHCenter : Text.AlignLeft
                wrapMode: wrap ? Text.WordWrap : Text.NoWrap
            }
        }
        Loader {
            Layout.fillWidth: true
            Layout.preferredWidth: 0
            active: root.subtext.length > 0
            visible: active
            sourceComponent: Label {
                topPadding: root.subtextMargin
                font.family: "Inter"
                font.styleName: "Regular"
                font.pixelSize: root.subtextSize
                color: Theme.color.neutral9
                text: root.subtext
                horizontalAlignment: root.center ? Text.AlignHCenter : Text.AlignLeft
                wrapMode: wrap ? Text.WordWrap : Text.NoWrap
            }
        }
    }
}
