// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

import "../controls"

DialogPopup {
    id: root
    objectName: "nodeWarningsPopup"

    property NodeStatus status: NodeStatus {}
    readonly property var warnings: status.activeWarnings
    readonly property var dismissed: status.dismissedWarnings
    property bool showDismissed: false

    title: qsTr("Warnings")
    implicitWidth: 520
    onClosed: root.showDismissed = false

    contentItem: ColumnLayout {
        spacing: Theme.space.l

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.space.ml

            DialogHeader {
                Layout.fillWidth: true
                title: root.title
                showCloseButton: false
            }
            LinkButton {
                objectName: "nodeWarningsDismissAll"
                Layout.alignment: Qt.AlignTop
                visible: root.warnings.length > 1
                text: qsTr("Dismiss all")
                onClicked: root.status.dismissWarnings(root.warnings)
            }
            CloseButton {
                objectName: "nodeWarningsCloseButton"
                Layout.alignment: Qt.AlignTop
                onClicked: root.close()
            }
        }

        CoreText {
            objectName: "nodeWarningsEmpty"
            Layout.fillWidth: true
            visible: root.warnings.length === 0
            text: root.dismissed.length > 0
                ? qsTr("No new warnings.")
                : qsTr("Your node has no warnings.")
            textStyle: Theme.text.description
            color: Theme.color.neutral7
            horizontalAlignment: Text.AlignLeft
        }

        ScrollView {
            id: warningsScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredHeight: warningsColumn.implicitHeight
            contentWidth: availableWidth
            clip: true

            // A Column positioner, never a Quick Layout: a Repeater inside one
            // crashes on Qt 6.4.0 to 6.5.0 (QTBUG-111792). See AlertPopup.qml.
            Column {
                id: warningsColumn
                width: warningsScroll.availableWidth
                spacing: Theme.space.m

                Repeater {
                    objectName: "nodeWarningsRepeater"
                    model: root.warnings
                    delegate: WarningCard {
                        width: warningsColumn.width
                        textObjectName: "nodeWarningText_" + index
                        actionObjectName: "nodeWarningDismiss_" + index
                        warning: modelData
                        actionText: qsTr("Dismiss")
                        onActionClicked: root.status.dismissWarnings([modelData])
                    }
                }

                RowLayout {
                    width: warningsColumn.width
                    visible: root.dismissed.length > 0
                    spacing: Theme.space.m
                    LinkButton {
                        objectName: "nodeWarningsShowDismissed"
                        text: (root.dismissed.length === 1 ? qsTr("1 dismissed") : qsTr("%1 dismissed").arg(root.dismissed.length))
                            + " · " + (root.showDismissed ? qsTr("Hide") : qsTr("Show"))
                        textColor: !enabled ? Theme.color.disabledContent
                            : hovered || down ? Theme.color.neutral9
                            : Theme.color.neutral7
                        onClicked: root.showDismissed = !root.showDismissed
                    }
                    Item { Layout.fillWidth: true }
                    LinkButton {
                        objectName: "nodeWarningsRestoreAll"
                        visible: root.showDismissed
                        text: qsTr("Restore all")
                        onClicked: root.status.restoreDismissedWarnings()
                    }
                }

                Repeater {
                    objectName: "nodeWarningsDismissedRepeater"
                    model: root.showDismissed ? root.dismissed : []
                    delegate: WarningCard {
                        width: warningsColumn.width
                        textObjectName: "nodeDismissedWarningText_" + index
                        warning: modelData
                        dimmed: true
                    }
                }
            }
        }
    }

    component WarningCard: Rectangle {
        id: card
        property string warning: ""
        property bool dimmed: false
        property string actionText: ""
        property string textObjectName: ""
        property string actionObjectName: ""
        signal actionClicked()

        readonly property color tone: dimmed ? Theme.color.controlBorder : Theme.color.warning
        implicitHeight: cardContent.implicitHeight + 2 * Theme.space.ml
        radius: Theme.radius.cardInset
        color: dimmed ? Theme.color.surface2 : Theme.color.warningTint
        border.width: 1
        border.color: dimmed ? Theme.color.surfaceDivider : Theme.color.warning

        RowLayout {
            id: cardContent
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: Theme.space.l
            anchors.rightMargin: Theme.space.ml
            spacing: Theme.space.ml

            Rectangle {
                Layout.alignment: Qt.AlignTop
                Layout.topMargin: Theme.space.s
                implicitWidth: Theme.space.m
                implicitHeight: Theme.space.m
                radius: height / 2
                color: card.tone
            }
            CoreText {
                objectName: card.textObjectName
                Layout.fillWidth: true
                text: card.warning
                textStyle: Theme.text.description
                color: card.dimmed ? Theme.color.neutral7 : Theme.color.neutral9
                horizontalAlignment: Text.AlignLeft
                verticalAlignment: Text.AlignTop
            }
            LinkButton {
                objectName: card.actionObjectName
                Layout.alignment: Qt.AlignTop
                visible: card.actionText.length > 0
                text: card.actionText
                onClicked: card.actionClicked()
            }
        }
    }
}
