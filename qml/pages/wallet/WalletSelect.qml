// Copyright (c) 2024 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

import org.bitcoincore.qt 1.0

import "../../controls"

Popup {
    id: root
    objectName: "walletSelectPopup"

    property alias model: listView.model
    implicitHeight: layout.height + arrow.height + 11
    implicitWidth: 250
    clip: true

    signal addWallet()
    signal closeWalletRequested(string name)

    function closeLoadedWallet(name) {
        root.close()
        root.closeWalletRequested(name)
    }

    Connections {
        target: walletController
        function onWalletLoadSucceeded() {
            root.close()
        }
        function onWalletMigrationRequired() {
            root.close()
        }
        // Intentionally no handler for the LoadError branch of
        // walletLoadStateChanged — the failing row's inline LoadError state
        // stays visible until the user picks again.
    }

    background: Item {
        anchors.fill: parent
        Rectangle {
            id: tooltipBg
            color: Theme.color.neutral0
            border.color: Theme.color.neutral4
            radius: 5
            border.width: 1
            width: parent.width
            height: parent.height - arrow.height - 1
            anchors.top: arrow.bottom
            anchors.horizontalCenter: root.horizontalCenter
            anchors.topMargin: -1
        }
        Image {
            id: arrow
            source: Theme.image.tooltipArrow
            width: 22
            height: 10
            anchors.left: parent.left
            anchors.leftMargin: 10
            anchors.top: parent.top
        }
    }

    ButtonGroup {
        id: buttonGroup
    }

    ColumnLayout {
        id: layout
        width: 220
        anchors.topMargin: arrow.height
        CoreText {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: 220
            Layout.preferredHeight: 30
            id: label
            text: qsTr("Wallets")
            visible: listView.count > 0
            bold: true
            color: Theme.color.neutral9
            font.pixelSize: 14
            topPadding: 10
            bottomPadding: 5
        }

        ListView {
            objectName: "walletSelectList"
            Layout.preferredWidth: 220
            Layout.preferredHeight: Math.min(listView.count * 54, 300)
            id: listView
            interactive: true
            clip: true
            spacing: 2
            ScrollBar.vertical: ScrollBar { }
            model: walletListModel

            delegate: ItemDelegate {
                id: delegate
                required property string name;
                required property string displayName;
                required property string format;
                required property int loadState;
                required property string errorMessage;
                required property string balance;
                required property int keySchemeKind;

                readonly property string iconSource: {
                    const filled = delegate.checked
                    if (keySchemeKind === WalletQmlModel.WatchOnly) {
                        return filled ? "image://images/visible-filled" : "image://images/visible"
                    }
                    if (keySchemeKind === WalletQmlModel.MultiKey) {
                        return filled ? "image://images/two-keys-filled" : "image://images/two-keys"
                    }
                    // Single-key, or Closed (type unknown until loaded).
                    return filled ? "image://images/key-filled" : "image://images/key"
                }
                readonly property real dimmedOpacity:
                    loadState === WalletListModel.Closed ? 0.5 : 1.0

                readonly property string statusText: {
                    switch (loadState) {
                    case WalletListModel.Loading:
                        return qsTr("Loading…")
                    case WalletListModel.LoadError:
                        return qsTr("Failed to open wallet")
                    case WalletListModel.Open:
                        return optionsModel.displayUnit === 0
                            ? optionsModel.displayUnitLabelForAmount(0) + " " + balance
                            : balance + " " + optionsModel.displayUnitLabel
                    case WalletListModel.Closed:
                    default:
                        return qsTr("Closed")
                    }
                }
                readonly property color statusColor: {
                    if (loadState === WalletListModel.LoadError) return Theme.color.red
                    if (loadState === WalletListModel.Open && (delegate.checked || delegate.hovered)) return Theme.color.orange
                    return Theme.color.neutral7
                }

                objectName: "walletSelectItem_" + name.replace(/[^A-Za-z0-9_]/g, "_")
                width: 220
                height: 52
                checked: walletController.selectedWallet.name == name
                enabled: !walletController.walletLoadInProgress
                ButtonGroup.group: buttonGroup
                leftPadding: 10
                rightPadding: 10
                topPadding: 0
                bottomPadding: 0

                background: Rectangle {
                    radius: 5
                    color: delegate.hovered ? Theme.color.neutral2 : "transparent"
                }

                HoverHandler {
                    cursorShape: Qt.PointingHandCursor
                }

                contentItem: RowLayout {
                    spacing: 8

                    Icon {
                        Layout.alignment: Qt.AlignVCenter
                        Layout.preferredWidth: 30
                        Layout.preferredHeight: 30
                        size: 30
                        source: delegate.iconSource
                        color: delegate.checked || delegate.hovered
                            ? Theme.color.orange
                            : Theme.color.neutral8
                        opacity: delegate.dimmedOpacity
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignVCenter
                        spacing: 1
                        opacity: delegate.dimmedOpacity

                        CoreText {
                            id: nameText
                            objectName: "walletSelectName_" + delegate.name.replace(/[^A-Za-z0-9_]/g, "_")
                            Layout.fillWidth: true
                            text: delegate.displayName
                            horizontalAlignment: Text.AlignLeft
                            font.pixelSize: 14
                            bold: true
                            color: delegate.checked || delegate.hovered
                                ? Theme.color.orange
                                : Theme.color.neutral9
                            wrap: false
                            elide: Text.ElideRight

                            ToolTip {
                                id: nameTooltip
                                text: delegate.displayName
                                visible: delegate.hovered && nameText.truncated
                                delay: 500
                                padding: 8
                                background: Rectangle {
                                    color: Theme.color.neutral0
                                    border.color: Theme.color.neutral4
                                    border.width: 1
                                    radius: 5
                                }
                                contentItem: CoreText {
                                    text: nameTooltip.text
                                    color: Theme.color.neutral9
                                    font.pixelSize: 13
                                    horizontalAlignment: Text.AlignLeft
                                    wrapMode: Text.WordWrap
                                    width: Math.min(implicitWidth, 220)
                                }
                            }
                        }

                        CoreText {
                            objectName: "walletSelectStatus_" + delegate.name.replace(/[^A-Za-z0-9_]/g, "_")
                            Layout.fillWidth: true
                            text: delegate.statusText
                            horizontalAlignment: Text.AlignLeft
                            font.pixelSize: 12
                            color: delegate.statusColor
                            wrap: false
                            elide: Text.ElideRight
                            visible: text.length > 0
                        }
                    }

                    IconButton {
                        id: closeButton
                        objectName: "walletSelectClose_" + delegate.name.replace(/[^A-Za-z0-9_]/g, "_")
                        visible: delegate.loadState === WalletListModel.Open
                        enabled: visible && !walletController.walletLoadInProgress
                        Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                        Layout.preferredWidth: 24
                        Layout.preferredHeight: 24
                        size: 24
                        iconSource: "image://images/cross-filled"
                        iconColor: delegate.hovered ? Theme.color.orange : Theme.color.neutral7
                        hoverColor: Theme.color.orange
                        activeColor: iconColor

                        onClicked: root.closeLoadedWallet(delegate.name)
                    }
                }

                onClicked: {
                    if (walletController.walletLoadInProgress) {
                        return
                    }
                    walletController.setSelectedWallet(name, format)
                    if (walletController.isWalletOpen(name)) {
                        root.close()
                    }
                    // Otherwise: stay open until walletLoadSucceeded or
                    // walletMigrationRequired arrives via Connections above.
                }
            }
        }

        AddWalletButton {
            id: addWallet
            objectName: "walletSelectAddWalletButton"
            enabled: !walletController.walletLoadInProgress

            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: 220
            Layout.preferredHeight: 30
            onClicked: {
                root.addWallet()
                root.close()
            }
        }
    }
}
