// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Layouts 1.15

Rectangle {
    id: root

    enum Layout {
        Horizontal,
        Vertical
    }

    property url iconSource: ""
    property string title: ""
    property string titleObjectName: ""
    property string message: ""
    property string messageObjectName: ""
    property string primaryButtonText: ""
    property bool primaryButtonOutlined: false
    property string dismissButtonText: ""
    property bool showsCloseButton: false
    property int bannerLayout: InfoBanner.Layout.Horizontal
    property int contentMargin: Theme.space.xl
    property int horizontalContentMargin: contentMargin
    property int verticalContentMargin: contentMargin
    property int contentSpacing: Theme.space.l
    property int iconSize: Theme.size.icon.xl
    property int textRightMargin: 0
    property var titleTextStyle: Theme.text.subheading
    property var messageTextStyle: Theme.text.description
    property color backgroundColor: Theme.color.surface1
    property color iconColor: Theme.color.neutral7
    property color titleColor: Theme.color.neutral9
    property color messageColor: Theme.color.neutral7

    signal primaryClicked()
    signal dismissClicked()

    radius: Theme.radius.card
    color: backgroundColor
    implicitHeight: contentLoader.implicitHeight + 2 * verticalContentMargin

    Loader {
        id: contentLoader
        objectName: root.objectName !== "" ? root.objectName + "Content" : ""
        x: root.horizontalContentMargin
        width: Math.max(0, (root.showsCloseButton ? closeButton.x - 8 : root.width - root.horizontalContentMargin) - x)
        anchors.verticalCenter: parent.verticalCenter
        sourceComponent: root.bannerLayout === InfoBanner.Layout.Vertical
            ? verticalContent : horizontalContent
    }

    CloseButton {
        id: closeButton
        objectName: root.objectName !== "" ? root.objectName + "CloseButton" : ""
        visible: root.showsCloseButton
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.topMargin: Theme.space.m
        anchors.rightMargin: Theme.space.m
        // Banners are tinted, so the button only shows a state fill.
        backgroundColor: "transparent"
        backgroundHoverColor: Theme.color.surfaceHover
        backgroundPressedColor: Theme.color.surfacePressed
        Accessible.name: qsTr("Dismiss")
        onClicked: root.dismissClicked()
    }

    Component {
        id: horizontalContent
        RowLayout {
            spacing: root.contentSpacing

            Icon {
                visible: root.iconSource != ""
                source: root.iconSource
                color: root.iconColor
                size: root.iconSize
                Layout.preferredWidth: root.iconSize
                Layout.preferredHeight: root.iconSize
            }

            Column {
                id: horizontalText
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                Layout.preferredWidth: 0
                Layout.rightMargin: root.textRightMargin
                spacing: Theme.space.xs

                CoreText {
                    objectName: root.titleObjectName
                    visible: root.title !== ""
                    text: root.title
                    textStyle: root.titleTextStyle
                    color: root.titleColor
                    horizontalAlignment: Text.AlignLeft
                    width: horizontalText.width
                }

                CoreText {
                    objectName: root.messageObjectName
                    visible: root.message !== ""
                    text: root.message
                    textStyle: root.messageTextStyle
                    color: root.messageColor
                    horizontalAlignment: Text.AlignLeft
                    width: horizontalText.width
                    wrap: true
                }
            }

            NeutralButton {
                objectName: root.objectName !== "" ? root.objectName + "DismissButton" : ""
                buttonSize: NeutralButton.Large
                visible: root.dismissButtonText !== ""
                text: root.dismissButtonText
                leftPadding: Theme.space.xl
                rightPadding: Theme.space.xl
                onClicked: root.dismissClicked()
            }

            Loader {
                visible: root.primaryButtonText !== ""
                sourceComponent: root.primaryButtonOutlined ? outlinedPrimaryButton : filledPrimaryButton
            }
        }
    }

    Component {
        id: verticalContent
        ColumnLayout {
            spacing: root.contentSpacing

            Icon {
                visible: root.iconSource != ""
                source: root.iconSource
                color: root.iconColor
                size: root.iconSize
                Layout.preferredWidth: root.iconSize
                Layout.preferredHeight: root.iconSize
                Layout.alignment: Qt.AlignHCenter
            }

            CoreText {
                objectName: root.titleObjectName
                visible: root.title !== ""
                text: root.title
                textStyle: root.titleTextStyle
                color: root.titleColor
                horizontalAlignment: Text.AlignHCenter
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                Layout.preferredWidth: 0
            }

            CoreText {
                objectName: root.messageObjectName
                visible: root.message !== ""
                text: root.message
                textStyle: root.messageTextStyle
                color: root.messageColor
                horizontalAlignment: Text.AlignHCenter
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                Layout.preferredWidth: 0
                Layout.bottomMargin: 10
                wrap: true
            }

            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                spacing: Theme.space.l

                NeutralButton {
                    objectName: root.objectName !== "" ? root.objectName + "DismissButton" : ""
                    buttonSize: NeutralButton.Large
                    visible: root.dismissButtonText !== ""
                    text: root.dismissButtonText
                    Layout.maximumWidth: 200
                    onClicked: root.dismissClicked()
                }

                Loader {
                    visible: root.primaryButtonText !== ""
                    Layout.maximumWidth: 200
                    sourceComponent: root.primaryButtonOutlined ? outlinedPrimaryButton : filledPrimaryButton
                }
            }
        }
    }

    Component {
        id: filledPrimaryButton
        PrimaryButton {
            objectName: root.objectName !== "" ? root.objectName + "PrimaryButton" : ""
            text: root.primaryButtonText
            leftPadding: root.bannerLayout === InfoBanner.Layout.Vertical ? 30 : 20
            rightPadding: leftPadding
            onClicked: root.primaryClicked()
        }
    }

    Component {
        id: outlinedPrimaryButton
        NeutralButton {
            objectName: root.objectName !== "" ? root.objectName + "PrimaryButton" : ""
            buttonSize: NeutralButton.Large
            text: root.primaryButtonText
            onClicked: root.primaryClicked()
        }
    }
}
