// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

import "../controls"

Popup {
    id: root
    objectName: "nodeFatalErrorPopup"
    readonly property int contentMargin: 28

    function syncOpenState() {
        if (nodeLifecycleModel.startupError.length > 0 && !opened) {
            open()
        }
    }

    Connections {
        target: nodeLifecycleModel
        function onStartupErrorChanged() {
            root.syncOpenState()
        }
    }

    Component.onCompleted: syncOpenState()

    modal: true
    closePolicy: Popup.NoAutoClose
    padding: 0
    anchors.centerIn: parent
    width: parent ? Math.min(parent.width - (2 * contentMargin), 640) : 640
    height: Math.min(implicitHeight, parent ? parent.height - 80 : 520)
    implicitHeight: columnLayout.implicitHeight

    background: Rectangle {
        color: Theme.color.background
        radius: 8
        border.color: Theme.color.neutral4
        border.width: 1
    }

    ColumnLayout {
        id: columnLayout
        anchors.fill: parent
        spacing: 0

        CoreText {
            Layout.fillWidth: true
            Layout.preferredHeight: 56
            text: qsTr("Fatal node error")
            bold: true
            font.pixelSize: 20
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }

        Separator { Layout.fillWidth: true }

        ScrollView {
            id: fatalErrorScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: root.contentMargin
            contentWidth: availableWidth
            clip: true

            CoreText {
                objectName: "nodeFatalErrorText"
                width: fatalErrorScroll.availableWidth
                text: nodeLifecycleModel.startupError
                color: Theme.color.neutral8
                font.pixelSize: 15
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignLeft
            }
        }

        ContinueButton {
            objectName: "nodeFatalShutdownButton"
            Layout.fillWidth: true
            Layout.leftMargin: root.contentMargin
            Layout.rightMargin: root.contentMargin
            Layout.bottomMargin: root.contentMargin
            text: qsTr("Shut down")
            onClicked: nodeLifecycleModel.requestShutdown()
        }
    }
}
