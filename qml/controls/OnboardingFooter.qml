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
    property string primaryButtonObjectName: "onboardingPrimaryButton"
    property string secondaryButtonObjectName: "onboardingSecondaryButton"
    property bool secondaryButtonIsLink: false
    property bool showBackButton: false
    property string backButtonObjectName: "onboardingFooterBackButton"
    property bool isOnSurface: true
    property real maximumContentWidth: 860
    property real contentSidePadding: width >= 640 ? 40 : 24
    property bool fullWidth: false
    readonly property bool compact: actions.width < 420
    readonly property alias primaryButton: primaryButton
    readonly property var secondaryButton: secondaryButtonIsLink ? secondaryLinkButton : secondaryNeutralButton
    readonly property alias backButton: backButton

    signal backClicked()
    signal primaryClicked()
    signal secondaryClicked()

    visible: showBackButton || primaryText.length > 0 || secondaryText.length > 0
    implicitHeight: visible ? separator.height + 24 + actions.height
        + (showBackButton && compact ? 12 + backButton.height : 0) + 24 : 0

    Separator {
        id: separator
        objectName: "onboardingFooterSeparator"
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        width: root.fullWidth ? root.width : actions.width
    }

    Flow {
        id: actions
        objectName: "onboardingFooterActions"
        anchors.top: separator.bottom
        anchors.topMargin: 24
        anchors.horizontalCenter: parent.horizontalCenter
        width: Math.max(0, root.fullWidth
            ? root.width - root.contentSidePadding * 2
            : Math.min(root.width - root.contentSidePadding * 2,
                       root.maximumContentWidth))
        height: childrenRect.height
        layoutDirection: Qt.RightToLeft
        spacing: 12

        ContinueButton {
            id: primaryButton
            objectName: root.primaryButtonObjectName
            visible: root.primaryText.length > 0
            enabled: root.primaryEnabled
            width: root.compact ? actions.width : Math.min(actions.width, Math.max(180, implicitWidth + 40))
            text: root.primaryText
            onClicked: root.primaryClicked()
        }

        NeutralButton {
            id: secondaryNeutralButton
            objectName: root.secondaryButtonIsLink ? "" : root.secondaryButtonObjectName
            visible: root.secondaryText.length > 0 && !root.secondaryButtonIsLink
            enabled: root.secondaryEnabled
            buttonSize: NeutralButton.Large
            isOnSurface: root.isOnSurface
            width: root.compact ? actions.width : Math.min(actions.width, Math.max(140, implicitWidth))
            text: root.secondaryText
            onClicked: root.secondaryClicked()
        }

        LinkButton {
            id: secondaryLinkButton
            objectName: root.secondaryButtonIsLink ? root.secondaryButtonObjectName : ""
            visible: root.secondaryText.length > 0 && root.secondaryButtonIsLink
            enabled: root.secondaryEnabled
            width: root.compact ? actions.width : implicitWidth
            height: root.compact ? implicitHeight : primaryButton.height
            text: root.secondaryText
            onClicked: root.secondaryClicked()
        }
    }

    OnboardingBackButton {
        id: backButton
        objectName: root.backButtonObjectName
        visible: root.showBackButton
        enabled: visible
        isOnSurface: root.isOnSurface
        anchors.left: parent.left
        anchors.leftMargin: root.fullWidth ? root.contentSidePadding
            : Math.max(root.contentSidePadding, (root.width - actions.width) / 2)
        anchors.top: actions.top
        anchors.topMargin: root.compact ? actions.height + 12 : 0
        onClicked: root.backClicked()
    }
}
