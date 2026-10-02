// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Window 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0
import "../../controls"
import ".."

FocusScope {
    id: root
    objectName: "widgetDashboard"
    property bool editing: false
    property bool showWidgetActions: true
    property alias layoutModel: layout
    property alias storageKey: layout.storageKey
    property WidgetRegistry widgetRegistry: DefaultWidgetRegistry {}

    WidgetLayoutModel {
        id: layout
        catalog: root.widgetRegistry.catalog
    }
    function openPicker() {
        editing = true
        picker.open()
    }

    function finishEditing() {
        layout.commitInteraction()
        root.forceActiveFocus()
        editing = false
        picker.close()
    }

    Component.onCompleted: layout.restore()
    onVisibleChanged: if (!visible) { editing = false; picker.close() }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 16
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            visible: root.showWidgetActions
            Item { Layout.fillWidth: true }
            WidgetActionsButton {
                visible: root.showWidgetActions
                dashboard: root
            }
        }
        CoreText {
            Layout.fillWidth: true
            visible: layout.persistenceError.length > 0
            text: layout.persistenceError
            color: Theme.color.red
            horizontalAlignment: Text.AlignLeft
        }
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            WidgetGrid {
                id: grid
                anchors.fill: parent
                layoutModel: layout
                editing: root.editing
                onEditRequested: root.editing = true
            }
            CoreText {
                anchors.bottom: parent.bottom
                anchors.horizontalCenter: parent.horizontalCenter
                width: Math.min(480, parent.width)
                visible: layout.unplacedCount > 0
                //: Some saved widgets do not fit the current fixed-size board and remain available in the picker.
                text: qsTr("Some widgets need more room. Resize or remove widgets, or change the window orientation.")
                color: Theme.color.neutral6
            }
            CoreText {
                anchors.centerIn: parent
                width: Math.min(320, parent.width)
                visible: layout.count === 0
                //: Empty dashboard message; Add Widget in the widget menu opens the picker.
                text: qsTr("Make this space yours. Add a widget to get started.")
                color: Theme.color.neutral6
                font.pixelSize: 16
            }
        }
    }

    WidgetPicker {
        id: picker
        parent: root.Overlay.overlay ? root.Overlay.overlay : root
        layoutModel: layout
    }
    Shortcut {
        sequence: "Escape"
        context: Qt.WindowShortcut
        enabled: root.visible && root.editing
        onActivated: root.finishEditing()
    }
}
