// Copyright (c) 2023 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../../controls"
import "../../components"

Item {
    property alias navLeftDetail: displaySettingsView.navLeftDetail
    property alias navMiddleDetail: displaySettingsView.navMiddleDetail
    StackView {
        id: displaySettingsView
        property alias navLeftDetail: displaySettings.navLeftDetail
        property alias navMiddleDetail: displaySettings.navMiddleDetail
        property bool newcompilebool: false
        anchors.fill: parent


        initialItem: Page {
            id: displaySettings
            property alias navLeftDetail: navbar.leftDetail
            property alias navMiddleDetail: navbar.middleDetail
            background: null
            implicitWidth: 450
            leftPadding: 20
            rightPadding: 20
            topPadding: 30

            header: NavigationBar {
                id: navbar
            }
            ColumnLayout {
                spacing: 4
                width: Math.min(parent.width, 450)
                anchors.horizontalCenter: parent.horizontalCenter
                Setting {
                    id: gotoTheme
                    Layout.fillWidth: true
                    header: qsTr("Theme")
                    actionItem: CaretRightButton {
                        color: gotoTheme.stateColor
                    }
                    onClicked: {
                        nodeSettingsView.push(theme_page)
                    }
                }
                Separator { Layout.fillWidth: true }
                Setting {
                    id: gotoBlockClockSize
                    Layout.fillWidth: true
                    header: qsTr("Block clock display mode")
                    actionItem: CaretRightButton {
                        color: gotoBlockClockSize.stateColor
                    }
                    onClicked: {
                        nodeSettingsView.push(blockclocksize_page)
                    }
                }
            }
        }
    }
    Component {
        id: theme_page
        SettingsTheme {
            navLeftDetail: NavButton {
                iconSource: "image://images/caret-left"
                text: qsTr("Back")
                onClicked: {
                    nodeSettingsView.pop()
                }
            }
            navMiddleDetail: Header {
                headerBold: true
                headerSize: 18
                header: qsTr("Theme")
            }
        }
    }
    Component {
        id: blockclocksize_page
        SettingsBlockClockDisplayMode {
            navLeftDetail: NavButton {
                iconSource: "image://images/caret-left"
                text: qsTr("Back")
                onClicked: {
                    nodeSettingsView.pop()
                }
            }
            navMiddleDetail: Header {
                headerBold: true
                headerSize: 18
                header: qsTr("Block clock display mode")
            }
        }
    }
}
