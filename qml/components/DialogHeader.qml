// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

import "../controls"

// Title row of a feature modal: title, optional description and the close button.
RowLayout {
    id: root

    property string title: ""
    property string description: ""
    property string closeButtonObjectName: ""
    property bool showCloseButton: true
    readonly property alias closeButton: closeButton

    signal closeRequested()

    spacing: Theme.space.ml

    ColumnLayout {
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        spacing: Theme.space.s

        CoreText {
            objectName: "dialogHeaderTitle"
            Layout.fillWidth: true
            text: root.title
            font: Theme.text.subtitle.font
            lineHeight: Theme.text.subtitle.lineHeight
            lineHeightMode: Text.FixedHeight
            color: Theme.color.neutral9
            horizontalAlignment: Text.AlignLeft
            wrapMode: Text.WordWrap
        }

        CoreText {
            objectName: "dialogHeaderDescription"
            Layout.fillWidth: true
            visible: text.length > 0
            text: root.description
            font: Theme.text.description.font
            lineHeight: Theme.text.description.lineHeight
            lineHeightMode: Text.FixedHeight
            color: Theme.color.neutral7
            horizontalAlignment: Text.AlignLeft
            wrapMode: Text.WordWrap
        }
    }

    CloseButton {
        id: closeButton
        objectName: root.closeButtonObjectName
        Layout.alignment: Qt.AlignTop
        visible: root.showCloseButton
        onClicked: root.closeRequested()
    }
}
