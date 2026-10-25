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
    padding: Theme.space.xs

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
                implicitWidth: Theme.size.iconButton
                implicitHeight: Theme.size.iconButton
                hoverEnabled: AppMode.isDesktop
                focusPolicy: Qt.TabFocus
                Accessible.name: qsTr("Back")
                Accessible.role: Accessible.Button

                onClicked: root.back()

                background: StateBackground {
                    id: backBg
                    radius: 10

                    FocusBorder {
                        visible: compactButton.visualFocus
                        borderRadius: 12
                        topMargin: -Theme.focusRing.offsetDense
                        bottomMargin: -Theme.focusRing.offsetDense
                        leftMargin: -Theme.focusRing.offsetDense
                        rightMargin: -Theme.focusRing.offsetDense
                    }
                }

                contentItem: Item {
                    Icon {
                        objectName: "settingsHeaderBackIcon"
                        anchors.centerIn: parent
                        source: "image://images/caret-left"
                        color: Theme.color.neutral9
                        size: Theme.size.icon.xl
                    }
                }

                HoverHandler {
                    cursorShape: Qt.PointingHandCursor
                }

                states: [
                    State {
                        name: "HOVER"
                        when: compactButton.hovered && !compactButton.pressed
                        PropertyChanges { target: backBg; stateColor: Theme.color.surfaceHover }
                    },
                    State {
                        name: "PRESSED"
                        when: compactButton.pressed
                        PropertyChanges { target: backBg; stateColor: Theme.color.surfacePressed }
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
            textStyle: Theme.text.heading
            color: Theme.color.neutral9

            Behavior on color {
                enabled: Theme.motion.animated
                ColorAnimation { duration: Theme.motion.normal }
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
