// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.


import QtQuick 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0

Item {
    id: root

    property string title: ""
    property var model: []
    property string textRole: "text"
    property string valueRole: "value"
    property string subtitleRole: ""
    property string iconRole: ""
    property string objectNameRole: ""
    property string subtitleObjectNameRole: ""
    property var currentValue
    property url selectionIconSource: "image://images/check"
    property int iconSize: 18
    property int rowHeight: 36
    property int subtitleRowHeight: 52
    readonly property alias titleItem: _title

    signal activated(var value)

    Layout.fillWidth: true
    implicitWidth: _column.implicitWidth
    implicitHeight: _column.implicitHeight

    function _rowText(item) {
        return (typeof item === 'object' && item !== null) ? item[root.textRole] : item
    }
    function _rowValue(item) {
        return (typeof item === 'object' && item !== null) ? item[root.valueRole] : item
    }
    function _rowSubtitle(item) {
        if (root.subtitleRole === "" || typeof item !== 'object' || item === null) return ""
        const v = item[root.subtitleRole]
        return v === undefined || v === null ? "" : v
    }
    function _rowIconSource(item) {
        if (root.iconRole === "" || typeof item !== 'object' || item === null) return ""
        const v = item[root.iconRole]
        return v === undefined || v === null ? "" : v
    }
    function _rowObjectName(item) {
        if (root.objectNameRole === "" || typeof item !== 'object' || item === null) return ""
        const v = item[root.objectNameRole]
        return v === undefined || v === null ? "" : v
    }
    function _rowSubtitleObjectName(item) {
        if (root.subtitleObjectNameRole === "" || typeof item !== 'object' || item === null) return ""
        const v = item[root.subtitleObjectNameRole]
        return v === undefined || v === null ? "" : v
    }

    function itemAtIndex(index) {
        return _repeater.itemAt(index)
    }

    ColumnLayout {
        id: _column
        anchors.left: parent.left
        anchors.right: parent.right
        spacing: 0

        CoreText {
            id: _title
            visible: root.title !== ""
            Layout.fillWidth: true
            Layout.leftMargin: 10
            Layout.rightMargin: 8
            Layout.topMargin: visible ? 8 : 0
            Layout.bottomMargin: visible ? 4 : 0
            text: root.title
            horizontalAlignment: Text.AlignLeft
            font: Theme.text.captionStrong.font
            lineHeight: Theme.text.captionStrong.lineHeight
            lineHeightMode: Text.FixedHeight
            wrap: false
            color: Theme.color.neutral6
        }

        Repeater {
            id: _repeater
            model: root.model

            delegate: ContextMenuButton {
                id: _row
                required property var modelData
                readonly property var rowData: modelData
                property string rowText: root._rowText(rowData)
                property var rowValue: root._rowValue(rowData)
                property string subtitle: root._rowSubtitle(rowData)
                property url rowIconSource: root._rowIconSource(rowData)
                property string subtitleObjectName: root._rowSubtitleObjectName(rowData)
                objectName: root._rowObjectName(rowData)
                readonly property bool selected: root.currentValue === rowValue
                readonly property int _textHeight: subtitle !== "" ? root.subtitleRowHeight : root.rowHeight
                readonly property int _iconHeight: rowIconSource.toString() !== "" ? root.iconSize + 12 : 0
                readonly property int _effectiveHeight: Math.max(_textHeight, _iconHeight)

                Accessible.name: rowText
                Accessible.checkable: true
                Accessible.checked: selected

                autoClose: false
                text: rowText
                Layout.fillWidth: true
                Layout.preferredHeight: _effectiveHeight
                Layout.minimumHeight: _effectiveHeight
                implicitHeight: _effectiveHeight

                onTriggered: root.activated(_row.rowValue)

                contentItem: RowLayout {
                    spacing: 7

                    Item {
                        visible: _row.rowIconSource.toString() !== ""
                        Layout.alignment: Qt.AlignVCenter
                        Layout.preferredWidth: visible ? root.iconSize : 0
                        Layout.preferredHeight: visible ? root.iconSize : 0

                        Icon {
                            anchors.centerIn: parent
                            source: _row.rowIconSource
                            color: _row._highlighted ? _row._hoverColor : _row._idleColor
                            size: root.iconSize
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignVCenter
                        spacing: 2

                        CoreText {
                            Layout.fillWidth: true
                            text: _row.rowText
                            horizontalAlignment: Text.AlignLeft
                            font: Theme.text.menuItem.font
                            lineHeight: Theme.text.menuItem.lineHeight
                            lineHeightMode: Text.FixedHeight
                            wrap: false
                            elide: Text.ElideRight
                            color: _row._highlighted ? _row._hoverColor : _row._idleColor
                        }

                        CoreText {
                            objectName: _row.subtitleObjectName
                            visible: _row.subtitle !== ""
                            Layout.fillWidth: true
                            text: _row.subtitle
                            horizontalAlignment: Text.AlignLeft
                            font: Theme.text.caption.font
                            lineHeight: Theme.text.caption.lineHeight
                            lineHeightMode: Text.FixedHeight
                            wrap: false
                            elide: Text.ElideRight
                            color: Theme.color.neutral6
                        }
                    }

                    Item {
                        Layout.alignment: Qt.AlignVCenter
                        Layout.preferredWidth: 18
                        Layout.preferredHeight: 18

                        Icon {
                            anchors.centerIn: parent
                            visible: _row.selected
                            source: root.selectionIconSource
                            color: Theme.color.orange
                            size: 20
                        }
                    }
                }
            }
        }
    }
}
