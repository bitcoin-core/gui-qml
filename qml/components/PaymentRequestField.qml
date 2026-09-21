// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../controls"

FormRow {
    id: root
    property alias label: root.title
    property string value: ""
    property bool modified: false
    property bool editable: true
    property string fieldObjectName: ""
    property alias text: input.text
    property alias placeholderText: input.placeholderText
    property alias validator: input.validator
    property alias maximumLength: input.maximumLength
    property alias inputMethodHints: input.inputMethodHints
    property var fieldTextStyle: Theme.text.description
    property Component unitControl
    signal editingFinished()
    function reset() { modified = false; input.text = value }
    enabled: editable
    showDivider: false
    minimumRowHeight: 56
    titleColor: editable ? Theme.color.neutral7 : Theme.color.neutral4
    onValueChanged: if (!modified) input.text = value
    onEditableChanged: if (!editable) reset()
    Component.onCompleted: reset()
    trailingItem: RowLayout {
        spacing: 4
        TextField {
            id: input
            objectName: root.fieldObjectName
            implicitWidth: Math.max(80, Math.min(260, root.width * 0.53 - (unitLoader.active ? unitLoader.width : 0)))
            implicitHeight: 36
            enabled: root.editable
            font: root.fieldTextStyle.font
            color: root.editable ? Theme.color.neutral9 : Theme.color.neutral4
            placeholderTextColor: Theme.color.neutral5
            horizontalAlignment: Text.AlignRight
            verticalAlignment: TextInput.AlignVCenter
            selectByMouse: true
            padding: 4
            Accessible.name: root.label
            background: FocusBorder { visible: input.activeFocus; border.color: Theme.color.orange; borderRadius: 6 }
            onTextEdited: root.modified = true
            onEditingFinished: root.editingFinished()
        }
        Loader { id: unitLoader; sourceComponent: root.unitControl; active: sourceComponent !== null }
    }
}
