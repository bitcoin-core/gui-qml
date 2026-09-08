// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Window 2.15

import org.bitcoincore.qt 1.0

import "../../controls"

PageStack {
    id: root

    readonly property bool isCompact:
        SizeClass.widthClassFor(Window.window ? Window.window.width : root.width) === SizeClass.compact

    readonly property string displayName:
        walletController.selectedWallet ? walletController.selectedWallet.displayName : ""

    readonly property string balance:
        walletController.selectedWallet ? walletController.selectedWallet.balance : "0"

    initialItem: dashboardPage

    Component {
        id: dashboardPage
        Page {
            background: null

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 20
                spacing: 16

                CoreText {
                    Layout.fillWidth: true
                    text: root.displayName
                    horizontalAlignment: root.isCompact ? Text.AlignHCenter : Text.AlignLeft
                    color: Theme.color.neutral9
                    font: Theme.text.subtitle.font
                    wrap: false
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.alignment: root.isCompact ? Qt.AlignHCenter : Qt.AlignLeft
                    Layout.topMargin: 4
                    spacing: 6

                    CoreText {
                        text: "₿"
                        font: Theme.text.display.font
                        color: Theme.color.orange
                        wrap: false
                    }
                    CoreText {
                        text: root.balance
                        font: Theme.text.display.font
                        color: Theme.color.neutral9
                        wrap: false
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.maximumWidth: 600
                    Layout.alignment: root.isCompact ? Qt.AlignHCenter : Qt.AlignLeft
                    Layout.topMargin: 4
                    spacing: 12

                    Button {
                        id: sendBtn
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        Layout.preferredHeight: 48
                        hoverEnabled: AppMode.isDesktop
                        onClicked: root.push(sendPage)

                        background: Rectangle {
                            radius: 10
                            color: sendBtn.pressed ? Qt.darker(Theme.color.orange, 1.15)
                                 : (sendBtn.hovered ? Qt.lighter(Theme.color.orange, 1.05)
                                    : Theme.color.orange)
                            Behavior on color { ColorAnimation { duration: 120 } }
                        }
                        contentItem: CoreText {
                            text: qsTr("Send")
                            color: Theme.color.neutral9
                            font: Theme.text.subheading.font
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            wrap: false
                        }
                    }

                    Button {
                        id: receiveBtn
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        Layout.preferredHeight: 48
                        hoverEnabled: AppMode.isDesktop
                        onClicked: root.push(receivePage)

                        background: Rectangle {
                            radius: 10
                            color: receiveBtn.pressed ? Qt.darker(Theme.color.orange, 1.15)
                                 : (receiveBtn.hovered ? Qt.lighter(Theme.color.orange, 1.05)
                                    : Theme.color.orange)
                            Behavior on color { ColorAnimation { duration: 120 } }
                        }
                        contentItem: CoreText {
                            text: qsTr("Receive")
                            color: Theme.color.neutral9
                            font: Theme.text.subheading.font
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            wrap: false
                        }
                    }
                }

                Activity {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.topMargin: 8
                }
            }
        }
    }

    Component {
        id: sendPage
        Page {
            background: null
            header: NavigationBar2 {
                leftItem: NavButton {
                    iconSource: "image://images/caret-left"
                    iconHeight: 22
                    iconWidth: 22
                    onClicked: root.pop()
                }
            }
            Send {
                anchors.fill: parent
                onTransactionPrepared: root.push(sendReviewPage)
            }
        }
    }

    Component {
        id: receivePage
        Page {
            background: null
            header: NavigationBar2 {
                leftItem: NavButton {
                    iconSource: "image://images/caret-left"
                    iconHeight: 22
                    iconWidth: 22
                    onClicked: root.pop()
                }
            }
            RequestPayment {
                anchors.fill: parent
            }
        }
    }

    function _completeSend() {
        walletController.selectedWallet.recipients.clear()
        root.pop()
        root.pop()
        root.push(sendResultPage)
    }

    Component {
        id: sendReviewPage
        SendReview {
            onBack: root.pop()
            onTransactionSent: root._completeSend()
        }
    }

    Component {
        id: sendResultPage
        SendResult {
            onDone: root.pop(null)
            onViewNewTransaction: root.pop(null)
        }
    }
}
