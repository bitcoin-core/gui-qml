// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import "../components"

Item {
    id: root

    property string primaryText: qsTr("Continue")
    property string secondaryText: ""
    property bool primaryEnabled: true
    property bool secondaryEnabled: true
    property real maximumContentWidth: 860
    property real contentSidePadding: width >= 640 ? 40 : 24
    readonly property bool compact: actions.width < 420
    readonly property alias primaryButton: primaryButton
    readonly property alias secondaryButton: secondaryButton

    signal primaryClicked()
    signal secondaryClicked()

    visible: primaryText.length > 0 || secondaryText.length > 0
    implicitHeight: visible ? separator.height + 24 + actions.height + 24 : 0

    Separator {
        id: separator
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        width: actions.width
    }

    Flow {
        id: actions
        anchors.top: separator.bottom
        anchors.topMargin: 24
        anchors.horizontalCenter: parent.horizontalCenter
        width: Math.max(0, Math.min(root.width - root.contentSidePadding * 2,
                                    root.maximumContentWidth))
        height: childrenRect.height
        layoutDirection: Qt.RightToLeft
        spacing: 12

        ContinueButton {
            id: primaryButton
            objectName: "onboardingPrimaryButton"
            visible: root.primaryText.length > 0
            enabled: root.primaryEnabled
            width: root.compact ? actions.width : Math.min(actions.width, Math.max(180, implicitWidth + 40))
            text: root.primaryText
            onClicked: root.primaryClicked()
        }

        NeutralButton {
            id: secondaryButton
            objectName: "onboardingSecondaryButton"
            visible: root.secondaryText.length > 0
            enabled: root.secondaryEnabled
            buttonSize: NeutralButton.Large
            width: root.compact ? actions.width : Math.min(actions.width, Math.max(140, implicitWidth))
            text: root.secondaryText
            onClicked: root.secondaryClicked()
        }
    }
}
