// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

import "../../controls"
import "../../components"

ColumnLayout {
    id: root
    objectName: "addressDetails"

    property string address
    property string label
    property string amount
    property bool hasAmount
    property string category
    property string scriptType
    property bool used

    signal closeRequested
    signal copyAddressRequested
    signal createPaymentRequestRequested

    spacing: 0

    RowLayout {
        Layout.fillWidth: true
        Layout.bottomMargin: 22

        Header {
            Layout.fillWidth: true
            header: qsTr("Address details")
            headerBold: true
            center: false
        }

        Icon {
            objectName: "addressDetailsCloseButton"
            source: "image://images/cross"
            color: Theme.color.neutral8
            size: 10
            enabled: true
            padding: 0
            onClicked: root.closeRequested()
            HoverHandler {
                cursorShape: Qt.PointingHandCursor
            }
        }
    }

    DetailRow {
        Layout.fillWidth: true
        label: qsTr("Address")
        value: root.address
        actionIcon: "image://images/copy"
        onActionClicked: root.copyAddressRequested()
    }

    DetailRow {
        Layout.fillWidth: true
        label: qsTr("Amount")
        value: root.amount
        valueColor: root.hasAmount ? Theme.color.green : Theme.color.neutral6
    }

    DetailRow {
        Layout.fillWidth: true
        label: qsTr("Label")
        value: root.label === "" ? qsTr("No note") : root.label
    }

    DetailRow {
        Layout.fillWidth: true
        label: qsTr("Type")
        value: root.category === "change" ? qsTr("Change") : qsTr("Single-use (Receive)")
    }

    DetailRow {
        Layout.fillWidth: true
        label: qsTr("Address type")
        value: root.scriptType === "" ? qsTr("Unknown") : root.scriptType
    }

    RowLayout {
        Layout.fillWidth: true
        Layout.topMargin: 24
        spacing: 12

        ContinueButton {
            objectName: "addressDetailsCopyAddressButton"
            Layout.fillWidth: true
            text: qsTr("Copy address")
            backgroundColor: "transparent"
            backgroundHoverColor: Theme.color.neutral2
            backgroundPressedColor: Theme.color.neutral3
            borderColor: Theme.color.neutral4
            textColor: Theme.color.neutral9
            onClicked: root.copyAddressRequested()
        }

        ContinueButton {
            objectName: "addressDetailsCreatePaymentRequestButton"
            Layout.fillWidth: true
            text: qsTr("Create payment request")
            visible: root.category === "single-use" && !root.used
            backgroundColor: Theme.color.orange
            backgroundHoverColor: Theme.color.orangeLight1
            backgroundPressedColor: Theme.color.orangeLight2
            textColor: Theme.color.white
            onClicked: root.createPaymentRequestRequested()
        }
    }

    component DetailRow: Item {
        id: detailSectionRoot

        property string label
        property string value
        property color valueColor: Theme.color.neutral7
        property string actionIcon: ""
        signal actionClicked

        Layout.minimumHeight: sectionContent.height + separator.height + 20
        implicitHeight: sectionContent.height + separator.height + 20

        Column {
            id: sectionContent
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.topMargin: 12
            spacing: 6

            CoreText {
                width: parent.width
                text: detailSectionRoot.label
                color: Theme.color.neutral9
                font: Theme.text.body.font
                horizontalAlignment: Text.AlignLeft
            }

            CoreText {
                width: parent.width - (detailAction.visible ? detailAction.width + 12 : 0)
                text: detailSectionRoot.value
                color: detailSectionRoot.valueColor
                font: Theme.text.body.font
                wrapMode: Text.WrapAnywhere
                horizontalAlignment: Text.AlignLeft
            }
        }

        IconButton {
            id: detailAction
            anchors.right: parent.right
            anchors.top: sectionContent.top
            anchors.topMargin: 24
            visible: detailSectionRoot.actionIcon !== ""
            iconSource: detailSectionRoot.actionIcon
            iconColor: Theme.color.neutral7
            hoverColor: Theme.color.neutral9
            size: 30
            onClicked: detailSectionRoot.actionClicked()
        }

        Separator {
            id: separator
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
        }
    }
}
