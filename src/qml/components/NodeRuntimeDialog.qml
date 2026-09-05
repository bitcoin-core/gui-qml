// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0

import "../controls"

Popup {
    id: root
    objectName: "nodeRuntimeDialog"
    readonly property int contentMargin: 28

    function runtimeButtonSpecs() {
        return [
            { id: DialogButtonBox.Ok, name: "Ok" },
            { id: DialogButtonBox.Yes, name: "Yes" },
            { id: DialogButtonBox.No, name: "No" },
            { id: DialogButtonBox.Abort, name: "Abort" },
            { id: DialogButtonBox.Retry, name: "Retry" },
            { id: DialogButtonBox.Ignore, name: "Ignore" },
            { id: DialogButtonBox.Close, name: "Close" },
            { id: DialogButtonBox.Cancel, name: "Cancel" },
            { id: DialogButtonBox.Discard, name: "Discard" },
            { id: DialogButtonBox.Help, name: "Help" },
            { id: DialogButtonBox.Apply, name: "Apply" },
            { id: DialogButtonBox.Reset, name: "Reset" }
        ]
    }

    function syncOpenState() {
        if (runtimeDialogModel.runtimeDialogVisible && !opened) {
            open()
        } else if (!runtimeDialogModel.runtimeDialogVisible && opened) {
            close()
        }
    }

    function standardButtonId(button) {
        const specs = runtimeButtonSpecs()
        for (let i = 0; i < specs.length; ++i) {
            if (runtimeButtonBox.standardButton(specs[i].id) === button) {
                return specs[i].id
            }
        }
        return DialogButtonBox.NoButton
    }

    function tagRuntimeButtons() {
        const specs = runtimeButtonSpecs()
        for (let i = 0; i < specs.length; ++i) {
            const button = runtimeButtonBox.standardButton(specs[i].id)
            if (button !== null) {
                button.objectName = "nodeRuntimeDialogButton" + specs[i].name
            }
        }
    }

    Connections {
        target: runtimeDialogModel
        function onRuntimeDialogChanged() {
            root.syncOpenState()
            Qt.callLater(function() { root.tagRuntimeButtons() })
        }
    }

    Component.onCompleted: syncOpenState()
    onOpened: Qt.callLater(function() { root.tagRuntimeButtons() })

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

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 64
            Layout.leftMargin: root.contentMargin
            Layout.rightMargin: root.contentMargin
            spacing: 12

            Icon {
                Layout.preferredWidth: 24
                Layout.preferredHeight: 24
                source: runtimeDialogModel.runtimeDialogIcon
                color: Theme.color.orange
                size: 24
            }

            CoreText {
                objectName: "nodeRuntimeDialogTitle"
                Layout.fillWidth: true
                text: runtimeDialogModel.runtimeDialogTitle
                bold: true
                font.pixelSize: 20
                horizontalAlignment: Text.AlignLeft
            }
        }

        Separator { Layout.fillWidth: true }

        ScrollView {
            id: runtimeMessageScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: root.contentMargin
            contentWidth: availableWidth
            clip: true

            CoreText {
                objectName: "nodeRuntimeDialogMessage"
                width: runtimeMessageScroll.availableWidth
                text: runtimeDialogModel.runtimeDialogMessage
                color: Theme.color.neutral8
                font.pixelSize: 15
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignLeft
            }
        }

        DialogButtonBox {
            id: runtimeButtonBox
            objectName: "nodeRuntimeDialogButtonBox"

            Layout.fillWidth: true
            Layout.margins: root.contentMargin
            Layout.topMargin: 0
            padding: 0
            spacing: 12
            standardButtons: runtimeDialogModel.runtimeDialogButtons
            background: Item {}

            onCountChanged: Qt.callLater(function() { root.tagRuntimeButtons() })
            onStandardButtonsChanged: Qt.callLater(function() { root.tagRuntimeButtons() })
            Component.onCompleted: Qt.callLater(function() { root.tagRuntimeButtons() })
            onClicked: function(button) {
                runtimeDialogModel.answerRuntimeDialog(root.standardButtonId(button))
            }

            delegate: Button {
                id: runtimeDialogButton

                readonly property bool primaryRole: DialogButtonBox.buttonRole === DialogButtonBox.AcceptRole ||
                                                    DialogButtonBox.buttonRole === DialogButtonBox.YesRole ||
                                                    DialogButtonBox.buttonRole === DialogButtonBox.ApplyRole

                hoverEnabled: AppMode.isDesktop
                leftPadding: 20
                rightPadding: 20

                contentItem: CoreText {
                    id: runtimeDialogButtonText

                    text: runtimeDialogButton.text
                    bold: false
                    fontStyleName: Theme.text.button.styleName
                    font.pixelSize: Theme.text.button.pixelSize
                    color: runtimeDialogButton.primaryRole ? Theme.color.white : Theme.color.neutral9
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter

                    states: [
                        State {
                            name: "OUTLINE_PRESSED"; when: !runtimeDialogButton.primaryRole && runtimeDialogButton.pressed
                            PropertyChanges { target: runtimeDialogButtonText; color: Theme.color.neutral9 }
                        },
                        State {
                            name: "OUTLINE_HOVER"; when: !runtimeDialogButton.primaryRole && runtimeDialogButton.hovered
                            PropertyChanges { target: runtimeDialogButtonText; color: Theme.color.neutral9 }
                        }
                    ]
                }

                background: Rectangle {
                    id: runtimeDialogButtonBackground

                    implicitHeight: 46
                    color: runtimeDialogButton.primaryRole ? Theme.color.orange : Theme.color.background
                    radius: 5
                    border.width: runtimeDialogButton.primaryRole ? 0 : 1
                    border.color: runtimeDialogButton.primaryRole ? "transparent" : Theme.color.neutral6

                    states: [
                        State {
                            name: "PRIMARY_PRESSED"; when: runtimeDialogButton.primaryRole && runtimeDialogButton.pressed
                            PropertyChanges { target: runtimeDialogButtonBackground; color: Theme.color.orangeLight2 }
                        },
                        State {
                            name: "PRIMARY_HOVER"; when: runtimeDialogButton.primaryRole && runtimeDialogButton.hovered
                            PropertyChanges { target: runtimeDialogButtonBackground; color: Theme.color.orangeLight1 }
                        },
                        State {
                            name: "OUTLINE_PRESSED"; when: !runtimeDialogButton.primaryRole && runtimeDialogButton.pressed
                            PropertyChanges { target: runtimeDialogButtonBackground; border.color: Theme.color.orangeLight2 }
                        },
                        State {
                            name: "OUTLINE_HOVER"; when: !runtimeDialogButton.primaryRole && runtimeDialogButton.hovered
                            PropertyChanges { target: runtimeDialogButtonBackground; border.color: Theme.color.neutral9 }
                        },
                        State {
                            name: "DISABLED"; when: !runtimeDialogButton.enabled
                            PropertyChanges { target: runtimeDialogButtonBackground; color: Theme.color.neutral2 }
                            PropertyChanges { target: runtimeDialogButtonBackground; border.color: Theme.color.neutral2 }
                        }
                    ]

                    Behavior on color {
                        ColorAnimation { duration: 150 }
                    }

                    FocusBorder {
                        visible: runtimeDialogButton.visualFocus
                    }
                }
            }
        }
    }
}
