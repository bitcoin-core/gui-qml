// Copyright (c) 2023 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../../controls"
import "../../components"

Item {
    signal back

    id: root

    PageStack {
        id: displaySettingsView
        anchors.fill: parent

        initialItem: Page {
            id: displaySettings
            background: null
            implicitWidth: 450
            leftPadding: 20
            rightPadding: 20
            topPadding: 30

            header: NavigationBar2 {
                leftItem: NavButton {
                    iconSource: "image://images/caret-left"
                    text: qsTr("Back")
                    onClicked: root.back()
                }
                centerItem: Header {
                    headerBold: true
                    headerSize: 18
                    header: qsTr("Display settings")
                }
            }
            ColumnLayout {
                spacing: 4
                width: Math.min(parent.width, 450)
                anchors.horizontalCenter: parent.horizontalCenter
                Setting {
                    id: gotoTheme
                    Layout.fillWidth: true
                    header: qsTr("Theme")
                    actionItem: CaretRightIcon {
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
                    actionItem: CaretRightIcon {
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
            onBack: {
                nodeSettingsView.pop()
            }
        }
    }
    Component {
        id: blockclocksize_page
        SettingsBlockClockDisplayMode {
            onBack: {
                nodeSettingsView.pop()
            }
        }
    }
}
