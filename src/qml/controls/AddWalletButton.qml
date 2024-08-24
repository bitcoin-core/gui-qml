// Copyright (c) 2024 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

import org.bitcoincore.qt 1.0

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

    hoverEnabled: AppMode.isDesktop
    implicitHeight: 46
    implicitWidth: 220
    bottomPadding: 10
    topPadding: 0

    contentItem: RowLayout {
        implicitWidth: addIcon.size + addText.width
        implicitHeight: 45
        Icon {
            id: addIcon
            Layout.alignment: Qt.AlignHCenter
            source: "image://images/plus"
            color: Theme.color.neutral8
            size: 14
            topPadding: 5
            bottomPadding: 10
        }
        CoreText {
            id: addText
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("Add Wallet")
            color: Theme.color.neutral9
            font.pixelSize: 15
            topPadding: 5
            bottomPadding: 10
        }
    }

    background: Rectangle {
        id: bg
        height: 30
        width: 220
        radius: 5
        anchors.topMargin: 5
        anchors.bottomMargin: 10
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
        },
        State {
            name: "HOVER"; when: root.hovered
        }
    ]
}
