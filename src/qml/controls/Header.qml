// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

ColumnLayout {
    id: root
    property bool center: true
    required property string header
    property bool showHeader: true
    property int headerMargin
    property int headerSize: 28
    property bool headerBold: false
    property color headerColor: Theme.color.neutral9
    property string description: ""
    property int descriptionMargin: 10
    property int descriptionSize: 18
    property string descriptionColor: Theme.color.neutral8
    property bool descriptionBold: false
    property string subtext: ""
    property int subtextMargin
    property int subtextSize: 15
    property color subtextColor: Theme.color.neutral9
    property bool wrap: true
    property real descriptionLineHeight: 1

    spacing: 0
    Loader {
        Layout.fillWidth: true
        active: root.showHeader && root.header.length > 0
        visible: active
        sourceComponent: Label {
            Layout.fillWidth: true
            topPadding: root.headerMargin
            font.family: "Inter"
            font.styleName: root.headerBold ? "Semi Bold" : "Regular"
            font.pixelSize: root.headerSize
            color: root.headerColor
            text: root.header
            horizontalAlignment: center ? Text.AlignHCenter : Text.AlignLeft
            wrapMode: wrap ? Text.WordWrap : Text.NoWrap

            Behavior on color {
                ColorAnimation { duration: 150 }
            }
        }
    }
    Loader {
        Layout.fillWidth: true
        active: root.description.length > 0
        visible: active
        sourceComponent: Label {
            topPadding: showHeader ? root.descriptionMargin : root.headerMargin
            font.family: "Inter"
            font.styleName: root.descriptionBold ? "Semi Bold" : "Regular"
            font.pixelSize: root.descriptionSize
            color: root.descriptionColor
            text: root.description
            horizontalAlignment: root.center ? Text.AlignHCenter : Text.AlignLeft
            wrapMode: wrap ? Text.WordWrap : Text.NoWrap
            lineHeight: root.descriptionLineHeight

            Behavior on color {
                ColorAnimation { duration: 150 }
            }
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
            color: root.subtextColor
            text: root.subtext
            horizontalAlignment: root.center ? Text.AlignHCenter : Text.AlignLeft
            wrapMode: wrap ? Text.WordWrap : Text.NoWrap

            Behavior on color {
                ColorAnimation { duration: 150 }
            }
        }
    }
}
