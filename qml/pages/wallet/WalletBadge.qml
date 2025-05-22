// Copyright (c) 2024 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

import org.bitcoincore.qt 1.0

import "../../controls"

Button {
    id: root

    property color bgActiveColor: Theme.color.neutral2
    property color textColor: Theme.color.neutral7
    property color textHoverColor: Theme.color.orange
    property color textActiveColor: Theme.color.orange
    property color iconColor: "transparent"
    property string iconSource: ""
    property bool showBalance: true
    property bool showIcon: true
    property string balance: "0.0 000 000"
    property bool loading: false

    checkable: true
    hoverEnabled: AppMode.isDesktop
    implicitHeight: 60
    implicitWidth: contentItem.width
    bottomPadding: 0
    topPadding: 0
    clip: true

    contentItem: Item {
        RowLayout {
            visible: root.loading
            anchors.leftMargin: 5
            anchors.rightMargin: 5
            anchors.centerIn: parent
            spacing: 5

            Skeleton {
                Layout.preferredHeight: 30
                Layout.preferredWidth: 30
                loading: root.loading
            }
            ColumnLayout {
                spacing: 2
                Layout.preferredHeight: 30
                Layout.fillWidth: true

                Skeleton {
                    Layout.preferredHeight: 15
                    Layout.preferredWidth: 50
                    loading: root.loading
                }

                Skeleton {
                    Layout.preferredHeight: 15
                    Layout.preferredWidth: 114
                    loading: root.loading
                }
            }
        }

        RowLayout {
            visible: !root.loading

            opacity: visible ? 1 : 0

            Behavior on opacity {
                NumberAnimation { duration: 400 }
            }

            anchors.leftMargin: 5
            anchors.rightMargin: 5
            anchors.centerIn: parent
            clip: true
            spacing: 5
            Icon {
                id: icon
                visible: root.showIcon
                source: "image://images/singlesig-wallet"
                color: Theme.color.neutral8
                size: 30
                Layout.minimumWidth: 30
                Layout.preferredWidth: 30
                Layout.maximumWidth: 30
            }
            ColumnLayout {
                spacing: 2
                CoreText {
                    horizontalAlignment: Text.AlignLeft
                    Layout.fillWidth: true
                    wrap: false
                    id: buttonText
                    font.pixelSize: 13
                    text: root.text
                    color: root.textColor
                    bold: true
                    visible: root.text !== ""
                }
                CoreText {
                    id: balanceText
                    visible: root.showBalance
                    text: "₿ " + root.balance
                    color: Theme.color.neutral7
                }
            }
        }
    }

    background: Rectangle {
        id: bg
        height: root.height
        width: root.width
        radius: 5
        color: Theme.color.neutral3
        visible: root.hovered || root.checked

        FocusBorder {
            visible: root.visualFocus
        }

        Behavior on color {
            ColorAnimation { duration: 150 }
        }
    }

    states: [
        State {
            name: "CHECKED"; when: root.checked
            PropertyChanges { target: buttonText; color: root.textActiveColor }
            PropertyChanges { target: icon; color: root.textActiveColor }
            PropertyChanges { target: balanceText; color: root.textActiveColor }
        },
        State {
            name: "HOVER"; when: root.hovered
            PropertyChanges { target: buttonText; color: root.textHoverColor }
            PropertyChanges { target: icon; color: root.textHoverColor }
            PropertyChanges { target: balanceText; color: root.textHoverColor }
        }
    ]
}
