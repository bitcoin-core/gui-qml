// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

import "../controls"

// The warnings Core reported, styled like Node details. Each warning is an
// amber card, as in the banner, and can be dismissed until its text changes.
// Dismissed warnings stay one click away instead of disappearing for good.
Popup {
    id: root
    objectName: "nodeWarningsPopup"

    property NodeStatus status: NodeStatus {}
    readonly property var warnings: status.activeWarnings
    readonly property var dismissed: status.dismissedWarnings
    property bool showDismissed: false

    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    width: parent ? Math.min(440, parent.width - 48) : 440
    height: parent ? Math.min(implicitHeight, parent.height - 48) : implicitHeight
    padding: 24
    onClosed: root.showDismissed = false

    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 180; easing.type: Easing.OutCubic }
        NumberAnimation { property: "scale"; from: 0.98; to: 1; duration: 180; easing.type: Easing.OutCubic }
    }

    background: Rectangle {
        color: Theme.color.neutral1
        radius: 12
        border.width: 1
        border.color: Theme.color.neutral3
    }

    contentItem: ColumnLayout {
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.bottomMargin: 16
            spacing: 8
            CoreText {
                Layout.fillWidth: true
                text: qsTr("Warnings")
                bold: true
                font.pixelSize: 18
                horizontalAlignment: Text.AlignLeft
            }
            WarningAction {
                objectName: "nodeWarningsDismissAll"
                visible: root.warnings.length > 1
                text: qsTr("Dismiss all")
                onClicked: root.status.dismissWarnings(root.warnings)
            }
            CloseButton {
                objectName: "nodeWarningsCloseButton"
                size: 32
                onClicked: root.close()
            }
        }

        CoreText {
            objectName: "nodeWarningsEmpty"
            Layout.fillWidth: true
            Layout.bottomMargin: 16
            visible: root.warnings.length === 0
            text: root.dismissed.length > 0
                ? qsTr("No new warnings.")
                : qsTr("Your node has no warnings.")
            font.pixelSize: 14
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
                spacing: 8

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

                // Dismissed warnings are out of the way, not gone.
                RowLayout {
                    width: warningsColumn.width
                    visible: root.dismissed.length > 0
                    spacing: 8
                    WarningAction {
                        objectName: "nodeWarningsShowDismissed"
                        text: (root.dismissed.length === 1 ? qsTr("1 dismissed") : qsTr("%1 dismissed").arg(root.dismissed.length))
                            + " · " + (root.showDismissed ? qsTr("Hide") : qsTr("Show"))
                        muted: true
                        onClicked: root.showDismissed = !root.showDismissed
                    }
                    Item { Layout.fillWidth: true }
                    WarningAction {
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

    // A warning in the banner's amber tint. Dimmed cards are dismissed ones.
    component WarningCard: Rectangle {
        id: card
        property string warning: ""
        property bool dimmed: false
        property string actionText: ""
        property string textObjectName: ""
        property string actionObjectName: ""
        signal actionClicked()

        readonly property color tone: dimmed ? Theme.color.neutral5 : Theme.color.amber
        implicitHeight: cardContent.implicitHeight + 24
        radius: 8
        color: Qt.rgba(tone.r, tone.g, tone.b, dimmed ? 0.08 : 0.14)
        border.width: 1
        border.color: dimmed ? Theme.color.neutral3 : Theme.color.amber

        RowLayout {
            id: cardContent
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: 14
            anchors.rightMargin: 10
            spacing: 12

            Rectangle {
                Layout.alignment: Qt.AlignTop
                Layout.topMargin: 6
                implicitWidth: 8
                implicitHeight: 8
                radius: 4
                color: card.tone
            }
            CoreText {
                objectName: card.textObjectName
                Layout.fillWidth: true
                text: card.warning
                font.pixelSize: 14
                lineHeight: 1.45
                color: card.dimmed ? Theme.color.neutral7 : Theme.color.neutral9
                horizontalAlignment: Text.AlignLeft
                verticalAlignment: Text.AlignTop
            }
            WarningAction {
                objectName: card.actionObjectName
                Layout.alignment: Qt.AlignTop
                visible: card.actionText.length > 0
                text: card.actionText
                onClicked: card.actionClicked()
            }
        }
    }

    // A small text action, orange like the app's links, or muted.
    component WarningAction: AbstractButton {
        id: action
        property bool muted: false
        hoverEnabled: true
        focusPolicy: Qt.StrongFocus
        padding: 4
        implicitWidth: actionLabel.implicitWidth + leftPadding + rightPadding
        implicitHeight: actionLabel.implicitHeight + topPadding + bottomPadding
        opacity: down ? 0.7 : hovered ? 0.85 : 1
        Accessible.role: Accessible.Button
        Accessible.name: text

        HoverHandler { cursorShape: Qt.PointingHandCursor }

        contentItem: CoreText {
            id: actionLabel
            text: action.text
            font: Theme.text.captionStrong.font
            wrap: false
            color: action.muted ? Theme.color.neutral7 : Theme.color.orange
        }

        background: FocusBorder {
            visible: action.visualFocus
            borderRadius: 6
        }
    }
}
