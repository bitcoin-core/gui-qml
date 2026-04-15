// Copyright (c) 2024-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

import "../../controls"

Popup {
    id: root
    objectName: "walletSelectPopup"

    property alias model: listView.model
    implicitHeight: layout.height + arrow.height + 11
    implicitWidth: 250
    clip: true

    signal addWallet()

    function closeLoadedWallet(name) {
        walletController.closeWallet(name)
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
            Layout.preferredHeight: Math.min(listView.count * 34, 300)
            id: listView
            interactive: true
            spacing: 2
            ScrollBar.vertical: ScrollBar { }
            model: walletListModel

            delegate: ItemDelegate {
                id: delegate
                required property string name;
                required property string format;
                required property int loadState;

                objectName: "walletSelectItem_" + name.replace(/[^A-Za-z0-9_]/g, "_")
                width: 220
                height: 32
                checked: walletController.selectedWallet.name == name
                ButtonGroup.group: buttonGroup
                leftPadding: 10
                rightPadding: 6
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
                    spacing: 6

                    CoreText {
                        Layout.fillWidth: true
                        text: delegate.name
                        horizontalAlignment: Text.AlignLeft
                        verticalAlignment: Text.AlignVCenter
                        font.pixelSize: 14
                        color: delegate.checked || delegate.hovered ? Theme.color.orange : Theme.color.neutral9
                        elide: Text.ElideRight
                    }

                    IconButton {
                        id: closeButton
                        objectName: "walletSelectClose_" + delegate.name.replace(/[^A-Za-z0-9_]/g, "_")
                        visible: delegate.loadState === 1
                        enabled: visible
                        Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                        Layout.preferredWidth: 20
                        Layout.preferredHeight: 20
                        size: 20
                        iconSource: "image://images/cross"
                        iconColor: delegate.hovered ? Theme.color.orange : Theme.color.neutral7
                        hoverColor: Theme.color.orange
                        activeColor: iconColor

                        onClicked: root.closeLoadedWallet(delegate.name)
                    }
                }

                onClicked: {
                    walletController.setSelectedWallet(name, format)
                    root.close()
                }
            }
        }

        AddWalletButton {
            id: addWallet
            objectName: "walletSelectAddWalletButton"

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
