// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

import "../controls"

// Every field of one transaction input, output or recipient. The caller
// requests the entry and opens the popup; the document only loads while open.
DialogPopup {
    id: root

    property bool loading: false
    property string errorText: ""
    property var document: null
    property string documentObjectName: "transactionEntryDocument"

    title: qsTr("Complete entry")
    implicitWidth: 720
    implicitHeight: 600

    contentItem: ColumnLayout {
        spacing: Theme.space.ml

        DialogHeader {
            Layout.fillWidth: true
            title: root.title
            onCloseRequested: root.close()
        }

        SpinningIndicator {
            Layout.alignment: Qt.AlignHCenter
            visible: root.loading
            running: visible
        }

        CoreText {
            Layout.fillWidth: true
            visible: text.length > 0
            text: root.errorText
            textStyle: Theme.text.description
            color: Theme.color.dangerText
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignLeft
        }

        ListView {
            id: fields
            objectName: root.documentObjectName
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: root.opened ? root.document : null
            cacheBuffer: 0
            reuseItems: true
            clip: true
            spacing: Theme.space.ml
            ScrollBar.vertical: ScrollBar {}

            delegate: ColumnLayout {
                id: field
                required property var entry
                width: fields.width
                spacing: Theme.space.xs

                CoreText {
                    Layout.fillWidth: true
                    text: field.entry.field
                    textStyle: Theme.text.captionStrong
                    wrapMode: Text.WordWrap
                    horizontalAlignment: Text.AlignLeft
                }

                SelectableText {
                    Layout.fillWidth: true
                    text: field.entry.content
                    textStyle: Theme.text.monoCaption
                    wrapMode: TextEdit.WrapAnywhere
                }
            }
        }
    }
}
