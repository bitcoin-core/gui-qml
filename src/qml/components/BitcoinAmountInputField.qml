import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0

import "../controls"

ColumnLayout {
    id: root

    property var amount
    property string errorText: ""
    property string labelText: qsTr("Amount")
    property bool enabled: true

    signal editingFinished(string value)

    Layout.fillWidth: true
    spacing: 4

    Item {
        id: inputRow
        height: amountInput.height
        Layout.fillWidth: true

        CoreText {
            id: lbl
            width: 110
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            horizontalAlignment: Text.AlignLeft
            text: root.labelText
            font.pixelSize: 18
        }

        TextField {
            id: amountInput
            anchors.left: lbl.right
            anchors.verticalCenter: parent.verticalCenter
            leftPadding: 0
            enabled: root.enabled
            font.family: "Inter"
            font.styleName: "Regular"
            font.pixelSize: 18
            color: Theme.color.neutral9
            placeholderTextColor: enabled ? Theme.color.neutral7 : Theme.color.neutral4
            background: Item {}
            placeholderText: "0.00000000"
            selectByMouse: true

            text: root.amount ? root.amount.display : ""

            onEditingFinished: {
                if (root.amount) {
                    root.amount.display = text
                }
                root.editingFinished(text)
            }

            onActiveFocusChanged: {
                if (!activeFocus && root.amount) {
                    root.amount.display = text
                    root.editingFinished(text)
                }
            }
        }

        Item {
            width: unitLabel.width + flipIcon.width
            height: Math.max(unitLabel.height, flipIcon.height)
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            opacity: root.enabled ? 1.0 : 0.5

            MouseArea {
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                enabled: root.enabled && root.amount
                onClicked: root.amount.flipUnit()
            }

            CoreText {
                id: unitLabel
                anchors.right: flipIcon.left
                anchors.verticalCenter: parent.verticalCenter
                text: root.amount ? root.amount.unitLabel : ""
                font.pixelSize: 18
                color: enabled ? Theme.color.neutral7 : Theme.color.neutral4
            }

            Icon {
                id: flipIcon
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                source: "image://images/flip-vertical"
                icon.color: enabled ? Theme.color.neutral8 : Theme.color.neutral4
                size: 30
            }
        }
    }

    RowLayout {
        id: errorRow
        Layout.fillWidth: true
        visible: root.errorText.length > 0

        Icon {
            source: "image://images/alert-filled"
            size: 22
            color: Theme.color.red
        }

        CoreText {
            text: root.errorText
            font.pixelSize: 15
            color: Theme.color.red
            horizontalAlignment: Text.AlignLeft
            Layout.fillWidth: true
        }
    }
}
