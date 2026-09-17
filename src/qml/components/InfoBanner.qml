// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0

import "../controls"

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
    property string dismissButtonText: ""
    property bool showsCloseButton: false
    property int bannerLayout: InfoBanner.Layout.Horizontal
    property int contentMargin: 30
    property int horizontalContentMargin: contentMargin
    property int verticalContentMargin: contentMargin
    property int contentSpacing: 15
    property int iconSize: 24
    property int textRightMargin: 0
    property int messageFontPixelSize: 13
    property int messageLineHeight: 0
    property color backgroundColor: Qt.rgba(Theme.color.blue.r, Theme.color.blue.g, Theme.color.blue.b, 0.2)
    property color iconColor: Theme.color.neutral7
    property color titleColor: Theme.color.neutral9
    property color messageColor: Theme.color.neutral7

    signal primaryClicked()
    signal dismissClicked()

    radius: 10
    color: backgroundColor
    implicitHeight: contentLoader.item ? contentLoader.item.height + 2 * verticalContentMargin : 2 * verticalContentMargin

    Loader {
        id: contentLoader
        anchors.left: parent.left
        anchors.right: root.showsCloseButton ? closeButton.left : parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: root.horizontalContentMargin
        anchors.rightMargin: root.showsCloseButton ? 8 : root.horizontalContentMargin
        sourceComponent: root.bannerLayout === InfoBanner.Layout.Vertical
            ? verticalContent : horizontalContent
    }

    IconButton {
        id: closeButton
        objectName: root.objectName !== "" ? root.objectName + "CloseButton" : ""
        visible: root.showsCloseButton
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.topMargin: 8
        anchors.rightMargin: 8
        size: 24
        iconSource: "image://images/cross"
        Accessible.name: qsTr("Dismiss")
        Accessible.role: Accessible.Button
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

            ColumnLayout {
                Layout.fillWidth: true
                Layout.rightMargin: root.textRightMargin
                spacing: 4

                CoreText {
                    objectName: root.titleObjectName
                    visible: root.title !== ""
                    text: root.title
                    font.pixelSize: 15
                    bold: true
                    color: root.titleColor
                    horizontalAlignment: Text.AlignLeft
                    Layout.fillWidth: true
                }

                CoreText {
                    objectName: root.messageObjectName
                    visible: root.message !== ""
                    text: root.message
                    font.pixelSize: root.messageFontPixelSize
                    lineHeightMode: root.messageLineHeight > 0 ? Text.FixedHeight : Text.ProportionalHeight
                    lineHeight: root.messageLineHeight > 0 ? root.messageLineHeight : 1.0
                    color: root.messageColor
                    horizontalAlignment: Text.AlignLeft
                    Layout.fillWidth: true
                    wrap: true
                }
            }

            OutlineButton {
                objectName: root.objectName !== "" ? root.objectName + "DismissButton" : ""
                visible: root.dismissButtonText !== ""
                text: root.dismissButtonText
                leftPadding: 20
                rightPadding: 20
                onClicked: root.dismissClicked()
            }

            ContinueButton {
                objectName: root.objectName !== "" ? root.objectName + "PrimaryButton" : ""
                visible: root.primaryButtonText !== ""
                text: root.primaryButtonText
                leftPadding: 20
                rightPadding: 20
                onClicked: root.primaryClicked()
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
                font.pixelSize: 15
                bold: true
                color: root.titleColor
                horizontalAlignment: Text.AlignHCenter
                Layout.fillWidth: true
            }

            CoreText {
                objectName: root.messageObjectName
                visible: root.message !== ""
                text: root.message
                font.pixelSize: root.messageFontPixelSize
                lineHeightMode: root.messageLineHeight > 0 ? Text.FixedHeight : Text.ProportionalHeight
                lineHeight: root.messageLineHeight > 0 ? root.messageLineHeight : 1.0
                color: root.messageColor
                horizontalAlignment: Text.AlignHCenter
                Layout.fillWidth: true
                Layout.bottomMargin: 10
                wrap: true
            }

            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                spacing: 15

                OutlineButton {
                    objectName: root.objectName !== "" ? root.objectName + "DismissButton" : ""
                    visible: root.dismissButtonText !== ""
                    text: root.dismissButtonText
                    Layout.maximumWidth: 200
                    onClicked: root.dismissClicked()
                }

                ContinueButton {
                    objectName: root.objectName !== "" ? root.objectName + "PrimaryButton" : ""
                    visible: root.primaryButtonText !== ""
                    text: root.primaryButtonText
                    Layout.maximumWidth: 200
                    leftPadding: 30
                    rightPadding: 30
                    onClicked: root.primaryClicked()
                }
            }
        }
    }
}
