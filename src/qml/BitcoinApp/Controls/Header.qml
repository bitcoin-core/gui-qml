// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: root
    property bool center: true
    required property string header
    property int headerMargin
    property int headerSize: 28
    property bool headerBold: false
    property string description: ""
    property int descriptionMargin: 10
    property int descriptionSize: 18
    property string descriptionColor: Theme.color.neutral8
    property bool descriptionBold: false
    property string subtext: ""
    property int subtextMargin
    property int subtextSize: 15
    property bool wrap: true

    spacing: 0
    Label {
        Layout.fillWidth: true
        topPadding: root.headerMargin
        font.family: "Inter"
        font.styleName: root.headerBold ? "Semi Bold" : "Regular"
        font.pixelSize: root.headerSize
        color: Theme.color.neutral9
        text: root.header
        horizontalAlignment: center ? Text.AlignHCenter : Text.AlignLeft
        wrapMode: wrap ? Text.WordWrap : Text.NoWrap
    }
    Loader {
        Layout.fillWidth: true
        active: root.description.length > 0
        visible: active
        sourceComponent: Label {
            topPadding: root.descriptionMargin
            font.family: "Inter"
            font.styleName: root.descriptionBold ? "Semi Bold" : "Regular"
            font.pixelSize: root.descriptionSize
            color: root.descriptionColor
            text: root.description
            horizontalAlignment: root.center ? Text.AlignHCenter : Text.AlignLeft
            wrapMode: wrap ? Text.WordWrap : Text.NoWrap
        }
    }
    Loader {
        Layout.fillWidth: true
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
