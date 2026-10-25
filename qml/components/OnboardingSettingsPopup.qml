// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import "../controls"

DialogPopup {
    id: root
    property Component initialPage: null
    property string closeButtonObjectName: "onboardingSettingsCloseButton"
    readonly property alias pageStack: pages

    sheet: true
    implicitWidth: 532
    implicitHeight: 504
    padding: 0
    horizontalPadding: 0
    closesOnPressOutside: false

    contentItem: Item {
        clip: true

        PageStack {
            id: pages
            anchors.fill: parent
            initialItem: root.initialPage
        }

        CloseButton {
            objectName: root.closeButtonObjectName
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.margins: Theme.space.l
            z: 2
            onClicked: root.close()
        }
    }

    onClosed: if (pages.depth > 1) pages.pop(null, StackView.Immediate)
}
