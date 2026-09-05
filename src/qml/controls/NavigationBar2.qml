// Copyright (c) 2023 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Pane {
    property alias leftItem: left_section.contentItem
    property alias centerItem: center_section.contentItem
    property alias rightItem: right_section.contentItem
    property var navigationStack: null
    property bool showBackButton: navigationStack ? navigationStack.canGoBack : false
    property string backButtonObjectName: ""
    property string backButtonText: qsTr("Back")

    signal backClicked

    background: null
    padding: 4
    contentItem: RowLayout {
        Div {
            id: left_div
            Layout.preferredWidth: Math.floor(Math.max(left_div.implicitWidth, right_div.implicitWidth))
            contentItem: RowLayout {
                NavButton {
                    objectName: backButtonObjectName
                    visible: showBackButton
                    iconSource: "image://images/caret-left"
                    text: backButtonText
                    onClicked: {
                        if (navigationStack) {
                            navigationStack.goBack()
                        }
                        backClicked()
                    }
                }
                Section {
                    id: left_section
                }
                Spacer {
                }
            }
        }
        Section {
            id: center_section
        }
        Div {
            id: right_div
            Layout.preferredWidth: Math.floor(Math.max(left_div.implicitWidth, right_div.implicitWidth))
            contentItem: RowLayout {
                Spacer {
                }
                Section {
                    id: right_section
                }
            }
        }
    }

    component Div: Pane {
        Layout.alignment: Qt.AlignCenter
        Layout.fillWidth: true
        Layout.minimumWidth: implicitWidth
        background: null
        padding: 0
    }

    component Section: Pane {
        Layout.alignment: Qt.AlignCenter
        Layout.minimumWidth: implicitWidth
        background: null
        padding: 0
    }

    component Spacer: Item {
        Layout.alignment: Qt.AlignCenter
        Layout.fillWidth: true
        height: 1
    }
}
