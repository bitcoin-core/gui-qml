// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15

Item {
    id: root
    property string title: ""
    property bool showBackButton: false
    property bool showCloseButton: false
    property bool isOnSurface: false
    property real contentSidePadding: width >= 640 ? 40 : 24
    property real buttonSidePadding: contentSidePadding
    property int closeButtonSize: CloseButton.Medium
    property bool titleReady: false
    signal backClicked()
    signal closeClicked()

    implicitHeight: 76

    Component.onCompleted: {
        currentTitle.text = root.title
        currentTitle.opacity = root.title.length > 0 ? 1 : 0
        titleReady = true
    }
    onTitleChanged: {
        if (!titleReady || currentTitle.text === title) return
        outgoingTitleFade.stop()
        currentTitleFade.stop()
        outgoingTitle.text = currentTitle.text
        outgoingTitle.opacity = currentTitle.opacity
        currentTitle.text = title
        currentTitle.opacity = 0
        if (outgoingTitle.text.length > 0 && outgoingTitle.opacity > 0)
            outgoingTitleFade.start()
        if (title.length > 0) currentTitleFade.start()
    }

    NumberAnimation {
        id: outgoingTitleFade
        target: outgoingTitle
        property: "opacity"
        to: 0
        duration: 160
        easing.type: Easing.OutCubic
    }
    NumberAnimation {
        id: currentTitleFade
        target: currentTitle
        property: "opacity"
        to: 1
        duration: 160
        easing.type: Easing.OutCubic
    }

    NeutralButton {
        id: backButton
        objectName: "onboardingBackButton"
        anchors.left: parent.left
        anchors.leftMargin: root.buttonSidePadding
        anchors.verticalCenter: parent.verticalCenter
        visible: root.showBackButton || opacity > 0
        enabled: root.showBackButton
        opacity: root.showBackButton ? 1 : 0
        Behavior on opacity {
            NumberAnimation { duration: 160; easing.type: Easing.OutCubic }
        }
        buttonSize: NeutralButton.Medium
        leftPadding: 6
        text: qsTr("Back")
        contentItem: Item {
            implicitWidth: backChevron.width - 3 + backLabel.implicitWidth
            implicitHeight: Math.max(backChevron.height, backLabel.implicitHeight)

            Icon {
                id: backChevron
                source: "image://images/caret-left"
                size: 18
                color: backButton.textColor
                anchors.verticalCenter: parent.verticalCenter
            }
            CoreText {
                id: backLabel
                x: backChevron.width - 3
                anchors.verticalCenter: parent.verticalCenter
                text: backButton.text
                font: Theme.text.captionStrong.font
                color: backButton.textColor
                wrap: false
            }
        }
        onClicked: root.backClicked()
    }

    CoreText {
        id: outgoingTitle
        objectName: "onboardingOutgoingTitle"
        anchors.centerIn: parent
        width: Math.max(0, parent.width - 2 * (root.contentSidePadding + 120))
        opacity: 0
        visible: opacity > 0 && text.length > 0
        color: Theme.color.neutral9
        font: Theme.text.subheading.font
        wrap: false
        elide: Text.ElideRight
        horizontalAlignment: Text.AlignHCenter
    }

    CoreText {
        id: currentTitle
        objectName: "onboardingCurrentTitle"
        anchors.centerIn: parent
        width: Math.max(0, parent.width - 2 * (root.contentSidePadding + 120))
        opacity: 0
        visible: opacity > 0 && text.length > 0
        color: Theme.color.neutral9
        font: Theme.text.subheading.font
        wrap: false
        elide: Text.ElideRight
        horizontalAlignment: Text.AlignHCenter
    }

    CloseButton {
        objectName: "onboardingCloseButton"
        anchors.right: parent.right
        anchors.rightMargin: root.buttonSidePadding
        anchors.verticalCenter: parent.verticalCenter
        visible: root.showCloseButton || opacity > 0
        enabled: root.showCloseButton
        opacity: root.showCloseButton ? 1 : 0
        Behavior on opacity {
            NumberAnimation { duration: 160; easing.type: Easing.OutCubic }
        }
        buttonSize: root.closeButtonSize
        backgroundColor: root.isOnSurface ? Theme.color.neutral2 : Theme.color.background
        onClicked: root.closeClicked()
    }
}
