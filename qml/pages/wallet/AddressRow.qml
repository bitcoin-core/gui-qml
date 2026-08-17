// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0

import "../../controls"
import "../../components"

AbstractButton {
    id: root
    objectName: "addressRow"

    required property string address
    required property string label
    required property string category
    required property string displayAmount
    required property bool hasAmount
    required property string scriptType
    required property bool isUsed
    required property bool canEditLabel
    required property int index

    property bool showDivider: true
    property string pendingLabel: root.label

    signal noteDraftEdited(string address, string label)
    signal noteDraftDiscarded(string address)
    signal editLabelRequested(string address, string label)
    signal detailsRequested(string address, string label, string amount, bool hasAmount, string category, string scriptType, bool used)

    function openDetails() {
        root.detailsRequested(root.address, noteField.text, root.displayAmount, root.hasAmount,
            root.category, root.scriptType, root.isUsed)
    }

    hoverEnabled: AppMode.isDesktop
    focusPolicy: Qt.TabFocus
    padding: 0
    implicitHeight: 76
    height: implicitHeight
    Accessible.name: root.label.length > 0 ? root.label : qsTr("Address details")
    Accessible.description: root.address
    Accessible.role: Accessible.ListItem

    onClicked: root.openDetails()
    onPendingLabelChanged: {
        if (!noteField.activeFocus) noteField.text = root.pendingLabel
    }

    HoverHandler {
        cursorShape: Qt.PointingHandCursor
    }

    contentItem: Item {
        id: rowContent

        Column {
            id: addressColumn
            anchors.left: parent.left
            anchors.leftMargin: 8
            anchors.right: detailsButton.left
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            spacing: 2

            AddressLabel {
                objectName: "addressRowAddressText"
                width: parent.width
                address: root.address
                truncateWhenNeeded: true
                textStyle: Theme.text.monoCaption
                leftPadding: 8
                rightPadding: 8
                topPadding: 0
                bottomPadding: 0
            }

            CoreTextField {
                id: noteField
                objectName: "addressRowNoteField"
                property string submittedText: root.label
                width: parent.width
                implicitHeight: 30
                text: root.pendingLabel
                placeholderText: root.canEditLabel
                    ? qsTr("Add a note to self")
                    : qsTr("Change address")
                placeholderTextColor: Theme.color.neutral6
                color: root.canEditLabel ? Theme.color.neutral9 : Theme.color.neutral7
                font: Theme.text.description.font
                readOnly: !root.canEditLabel
                activeFocusOnPress: root.canEditLabel
                focusPolicy: root.canEditLabel ? Qt.StrongFocus : Qt.NoFocus
                selectByMouse: root.canEditLabel
                hoverEnabled: root.canEditLabel && AppMode.isDesktop
                leftPadding: 8
                rightPadding: 8
                topPadding: 4
                bottomPadding: 4

                background: Rectangle {
                    radius: 8
                    color: noteField.activeFocus ? Theme.color.neutral2 : "transparent"

                    FocusBorder {
                        objectName: "addressRowNoteFocusBorder"
                        visible: noteField.activeFocus
                        border.color: Theme.color.orange
                        borderRadius: 10
                        topMargin: -2
                        bottomMargin: -2
                        leftMargin: -2
                        rightMargin: -2
                    }
                }

                onActiveFocusChanged: {
                    if (noteField.activeFocus) noteField.submittedText = root.label
                }
                onTextEdited: root.noteDraftEdited(root.address, noteField.text)
                onAccepted: noteField.focus = false
                onEditingFinished: {
                    if (noteField.text !== root.label
                            && noteField.text !== noteField.submittedText) {
                        noteField.submittedText = noteField.text
                        root.editLabelRequested(root.address, noteField.text)
                    }
                }
                Keys.onEscapePressed: function(event) {
                    root.noteDraftDiscarded(root.address)
                    noteField.text = root.label
                    noteField.focus = false
                    event.accepted = true
                }
            }
        }

        AbstractButton {
            id: detailsButton
            objectName: "addressRowDetailsButton"
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.right: parent.right
            anchors.rightMargin: 8
            width: Math.min(parent.width * 0.5, Math.max(80, Math.ceil(amountMetrics.advanceWidth) + 30))
            hoverEnabled: AppMode.isDesktop
            focusPolicy: Qt.TabFocus
            padding: 0
            Accessible.name: qsTr("Address details")
            onClicked: root.openDetails()

            HoverHandler {
                cursorShape: Qt.PointingHandCursor
            }

            background: null

            contentItem: RowLayout {
                spacing: 8

                CoreText {
                    id: amountText
                    objectName: "addressRowAmountText"
                    Layout.fillWidth: true
                    text: root.displayAmount
                    color: root.hasAmount ? Theme.color.green : Theme.color.neutral7
                    font.family: optionsModel.moneyFont.family
                    font.weight: optionsModel.moneyFont.weight
                    font.pixelSize: Theme.text.description.font.pixelSize
                    lineHeight: Theme.text.description.lineHeight
                    lineHeightMode: Text.FixedHeight
                    horizontalAlignment: Text.AlignRight
                    elide: Text.ElideRight
                    wrap: false
                }

                CaretRightIcon {
                    objectName: "addressRowDisclosureIndicator"
                    size: 14
                    Layout.preferredWidth: 14
                    Layout.preferredHeight: 14
                    Layout.alignment: Qt.AlignVCenter
                    color: Theme.color.neutral7
                }
            }
        }

        TextMetrics {
            id: amountMetrics
            font: amountText.font
            text: root.displayAmount
        }

        Separator {
            objectName: "addressRowDivider"
            visible: root.showDivider
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.leftMargin: 16
            anchors.rightMargin: 16
            color: Theme.color.neutral3
        }
    }

    background: Rectangle {
        radius: 16
        color: "transparent"

        FocusBorder {
            visible: root.visualFocus
            borderRadius: 18
            topMargin: -2
            bottomMargin: -2
            leftMargin: -2
            rightMargin: -2
        }
    }
}
