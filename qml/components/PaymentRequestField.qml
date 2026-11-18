// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0

import "../controls"

// A payment request detail that keeps the saved value separate from an edited
// draft. Amount fields delegate parsing and draft ownership to BitcoinAmount.
FormRow {
    id: root

    property alias label: root.title
    property string value: ""
    property BitcoinAmount amountEntry: null
    property bool textDirty: false
    readonly property bool modified: amountEntry ? amountEntry.inputDirty : textDirty
    property bool editable: true
    property string fieldObjectName: ""
    readonly property alias field: input
    property alias text: input.text
    property string placeholderText: ""
    property alias validator: input.validator
    property alias maximumLength: input.maximumLength
    property alias inputMethodHints: input.inputMethodHints
    property var fieldTextStyle: Theme.text.description
    property int textAlignment: Text.AlignRight
    property Component unitControl: null
    property bool hidesAmount: false
    readonly property bool amountHidden: hidesAmount && ApplicationModels.optionsModel.hideAmounts
    readonly property real fieldWidth: Math.max(80, Math.min(260, width * 0.53
        - (unitLoader.active ? unitLoader.width + Theme.space.xs : 0)))

    signal editingFinished()

    function reset() {
        if (amountEntry) {
            amountEntry.resetInput()
        } else {
            textDirty = false
            input.text = value
        }
    }

    function acceptEdits() {
        if (amountEntry) amountEntry.format()
        else textDirty = false
    }

    enabled: editable
    showDivider: false
    minimumRowHeight: 56
    titleColor: editable ? Theme.color.neutral7 : Theme.color.disabledContent
    implicitWidth: 320
    onValueChanged: if (!amountEntry && !modified) input.text = value
    onEditableChanged: if (!editable) reset()
    Component.onCompleted: reset()

    trailingItem: RowLayout {
        spacing: Theme.space.xs

        TextField {
            id: input
            objectName: root.fieldObjectName
            Layout.preferredWidth: root.fieldWidth
            implicitHeight: Theme.size.control
            enabled: root.editable
            font: root.fieldTextStyle.font
            color: root.editable ? Theme.color.neutral9 : Theme.color.disabledContent
            placeholderText: root.amountHidden && !activeFocus ? "" : root.placeholderText
            placeholderTextColor: root.editable ? Theme.color.placeholder : Theme.color.disabledContent
            echoMode: root.amountHidden && !activeFocus ? TextInput.NoEcho : TextInput.Normal
            horizontalAlignment: root.textAlignment
            verticalAlignment: TextInput.AlignVCenter
            text: root.amountEntry ? root.amountEntry.inputText : root.value
            selectByMouse: true
            leftPadding: Theme.space.m
            rightPadding: Theme.space.m
            Accessible.name: root.label
            Accessible.description: root.amountHidden && !activeFocus ? qsTr("Hidden amount") : root.description

            background: Rectangle {
                color: "transparent"
                radius: Theme.radius.s

                FieldBorderRings {
                    anchors.fill: parent
                    visible: input.activeFocus
                    cornerRadius: Theme.radius.s
                    ringColor: Theme.color.neutral6
                }
            }

            BitcoinAmountDisplayLabelBase {
                anchors.fill: parent
                anchors.leftMargin: input.leftPadding
                anchors.rightMargin: input.rightPadding
                visible: root.amountHidden && !input.activeFocus
                hidesAmount: true
                textStyle: root.fieldTextStyle
                horizontalAlignment: root.textAlignment
                color: root.editable ? Theme.color.neutral9 : Theme.color.disabledContent
                Accessible.ignored: true
            }

            onTextEdited: {
                if (root.amountEntry) root.amountEntry.editInput(text)
                else root.textDirty = text !== root.value
            }
            onEditingFinished: root.editingFinished()
        }

        Loader {
            id: unitLoader
            active: root.unitControl !== null
            sourceComponent: root.unitControl
        }
    }
}
