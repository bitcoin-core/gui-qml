// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0

Pane {
    id: root

    property string title: ""
    property bool showBackButton: true
    property string backButtonObjectName: ""
    property string backButtonText: ""
    property alias rightItem: rightSection.contentItem

    signal back()

    background: null
    padding: 4

    contentItem: Item {
        implicitHeight: root.backButtonText.length > 0 ? 46 : 40

        Loader {
            id: backButton
            visible: root.showBackButton
            active: root.showBackButton
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            width: status === Loader.Ready && item ? item.implicitWidth : 0
            height: status === Loader.Ready && item ? item.implicitHeight : 0
            sourceComponent: root.backButtonText.length > 0 ? labeledBackButton : compactBackButton
        }

        Component {
            id: compactBackButton

            AbstractButton {
                id: compactButton
                objectName: root.backButtonObjectName
                implicitWidth: 40
                implicitHeight: 40
                hoverEnabled: AppMode.isDesktop
                focusPolicy: Qt.TabFocus
                Accessible.name: qsTr("Back")
                Accessible.role: Accessible.Button

                onClicked: root.back()

                background: Rectangle {
                    id: backBg
                    radius: 10
                    color: "transparent"

                    FocusBorder {
                        visible: compactButton.visualFocus
                        borderRadius: 12
                        topMargin: -2
                        bottomMargin: -2
                        leftMargin: -2
                        rightMargin: -2
                        border.color: Theme.color.orange
                    }

                    Behavior on color {
                        ColorAnimation { duration: 150 }
                    }
                }

                contentItem: Icon {
                    source: "image://images/caret-left"
                    color: Theme.color.neutral9
                    size: 24
                }

                HoverHandler {
                    cursorShape: Qt.PointingHandCursor
                }

                states: [
                    State {
                        name: "HOVER"
                        when: compactButton.hovered && !compactButton.pressed
                        PropertyChanges { target: backBg; color: Theme.color.neutral2 }
                    },
                    State {
                        name: "PRESSED"
                        when: compactButton.pressed
                        PropertyChanges { target: backBg; color: Theme.color.neutral3 }
                    }
                ]
            }
        }

        Component {
            id: labeledBackButton

            NavButton {
                objectName: root.backButtonObjectName
                iconSource: "image://images/caret-left"
                text: root.backButtonText
                onClicked: root.back()
            }
        }

        CoreText {
            anchors.centerIn: parent
            width: Math.min(
                implicitWidth,
                parent.width
                    - (backButton.visible ? backButton.width + 8 : 0)
                    - (rightSection.visible ? rightSection.width + 8 : 0))
            wrap: false
            elide: Text.ElideRight
            text: root.title
            font.pixelSize: 18
            bold: true
            color: Theme.color.neutral9

            Behavior on color {
                ColorAnimation { duration: 150 }
            }
        }

        Pane {
            id: rightSection
            objectName: "settingsHeaderRightSection"
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            // Gate on the child's existence only. Reading contentItem.visible
            // here returns the child's *effective* visibility, which includes
            // this Pane — a self-referential binding that latches to false and
            // hides the right-side actions (e.g. the debug.log refresh/export
            // buttons). See NavigationBar2, whose right section is never gated.
            visible: contentItem !== null
            background: null
            padding: 0
        }
    }
}
