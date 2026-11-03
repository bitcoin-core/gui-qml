// Copyright (c) 2023 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

// The one top bar: an optional back button and left item, a centered
// title (or centerItem), then a right item and an optional close button.
Pane {
    id: root
    // Shown in the center as a heading unless a centerItem replaces it.
    property string title: ""
    property string titleObjectName: ""
    property alias leftItem: left_section.contentItem
    property alias centerItem: center_section.contentItem
    property alias rightItem: right_section.contentItem
    property var navigationStack: null
    property bool showBackButton: navigationStack ? navigationStack.canGoBack : false
    property string backButtonObjectName: ""
    // Empty for an icon-only back button.
    property string backButtonText: qsTr("Back")
    property string backButtonAccessibleName: backButtonText.length > 0 ? backButtonText : qsTr("Back")
    property string backButtonIconObjectName: ""
    property bool showCloseButton: false
    property string closeButtonObjectName: ""
    property int closeButtonSize: CloseButton.Medium
    property string rightSectionObjectName: ""
    readonly property alias closeButton: closeButtonItem

    signal backClicked
    signal closeClicked

    background: null
    padding: Theme.space.xs
    contentItem: RowLayout {
        Div {
            id: left_div
            Layout.preferredWidth: Math.floor(Math.max(left_div.implicitWidth, right_div.implicitWidth))
            contentItem: RowLayout {
                NavButton {
                    objectName: root.backButtonObjectName
                    visible: root.showBackButton
                    iconSource: "image://images/caret-left"
                    iconObjectName: root.backButtonIconObjectName
                    text: root.backButtonText
                    Accessible.name: root.backButtonAccessibleName
                    onClicked: {
                        if (root.navigationStack) {
                            root.navigationStack.goBack()
                        }
                        root.backClicked()
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
            // A title may shrink and elide; other center items keep their implicit width.
            Layout.minimumWidth: root.title.length > 0 ? 0 : implicitWidth
            contentItem: CoreText {
                objectName: root.titleObjectName
                text: root.title
                textStyle: Theme.text.heading
                color: Theme.color.neutral9
                wrap: false
                elide: Text.ElideRight
            }
        }
        Div {
            id: right_div
            Layout.preferredWidth: Math.floor(Math.max(left_div.implicitWidth, right_div.implicitWidth))
            contentItem: RowLayout {
                Spacer {
                }
                Section {
                    id: right_section
                    objectName: root.rightSectionObjectName
                }
                CloseButton {
                    id: closeButtonItem
                    objectName: root.closeButtonObjectName
                    visible: root.showCloseButton
                    buttonSize: root.closeButtonSize
                    Layout.alignment: Qt.AlignVCenter
                    onClicked: root.closeClicked()
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
