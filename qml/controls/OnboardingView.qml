// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Page {
    id: root

    property var navigationStack: root.StackView.view
    property bool showBackButton: navigationStack
        ? (navigationStack.canGoBack !== undefined
            ? navigationStack.canGoBack : navigationStack.depth > 1) : false
    property bool showCloseButton: false
    property bool showNavigationBar: true
    property bool usesSharedNavigation: false
    property bool autoNavigateBack: true
    property string heading: ""
    property string subheading: ""
    property string headingObjectName: ""
    property string subheadingObjectName: ""
    property url imageSource: ""
    property real imageSize: 96
    property Component imageView: null
    property Component childView: null
    property string primaryButtonText: qsTr("Continue")
    property string secondaryButtonText: ""
    property bool primaryButtonEnabled: true
    property bool secondaryButtonEnabled: true
    property string primaryButtonObjectName: "onboardingPrimaryButton"
    property string secondaryButtonObjectName: "onboardingSecondaryButton"
    property real maximumContentWidth: 860
    property real contentSidePadding: width >= 640 ? 40 : 24

    readonly property alias scrollView: scrollView
    readonly property alias loadedChildView: childLoader.item
    readonly property alias primaryButton: footerBar.primaryButton
    readonly property alias secondaryButton: footerBar.secondaryButton

    signal backClicked()
    signal closeClicked()
    signal primaryClicked()
    signal secondaryClicked()

    function goBack() {
        backClicked()
        if (!autoNavigateBack || !navigationStack || !showBackButton) return
        if (navigationStack.goBack) navigationStack.goBack()
        else navigationStack.pop()
    }

    padding: 0
    background: null

    header: OnboardingNavigationBar {
        visible: root.showNavigationBar
        height: visible ? implicitHeight : 0
        title: root.title
        contentSidePadding: root.contentSidePadding
        showBackButton: root.showBackButton
        showCloseButton: root.showCloseButton
        onBackClicked: root.goBack()
        onCloseClicked: root.closeClicked()
    }

    ScrollView {
        id: scrollView
        objectName: "onboardingScrollView"
        anchors.fill: parent
        clip: true
        contentWidth: availableWidth
        contentHeight: contentFrame.height
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

        Item {
            id: contentFrame
            width: scrollView.availableWidth
            height: content.implicitHeight + 96

            ColumnLayout {
                id: content
                objectName: "onboardingContent"
                anchors.top: parent.top
                anchors.topMargin: 48
                anchors.horizontalCenter: parent.horizontalCenter
                width: Math.max(0, Math.min(parent.width - root.contentSidePadding * 2,
                                            root.maximumContentWidth))
                spacing: 0

                Image {
                    id: defaultImage
                    visible: root.imageView === null && root.imageSource.toString().length > 0
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: root.imageSize
                    Layout.preferredHeight: root.imageSize
                    sourceSize.width: root.imageSize
                    sourceSize.height: root.imageSize
                    source: root.imageSource
                    fillMode: Image.PreserveAspectFit
                }

                Loader {
                    id: imageLoader
                    active: root.imageView !== null
                    visible: active
                    sourceComponent: root.imageView
                    Layout.alignment: Qt.AlignHCenter
                }

                CoreText {
                    objectName: root.headingObjectName
                    visible: root.heading.length > 0
                    Layout.fillWidth: true
                    Layout.topMargin: defaultImage.visible || imageLoader.visible ? 24 : 0
                    text: root.heading
                    color: Theme.color.neutral9
                    font: Theme.text.headline.font
                    lineHeight: Theme.text.headline.lineHeight
                    lineHeightMode: Text.FixedHeight
                    wrap: true
                    horizontalAlignment: Text.AlignHCenter
                }

                CoreText {
                    objectName: root.subheadingObjectName
                    visible: root.subheading.length > 0
                    Layout.maximumWidth: 560
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignHCenter
                    Layout.topMargin: root.heading.length > 0 ? 16 : 0
                    text: root.subheading
                    color: Theme.color.neutral7
                    font: Theme.text.body.font
                    lineHeight: Theme.text.body.lineHeight
                    lineHeightMode: Text.FixedHeight
                    wrap: true
                    horizontalAlignment: Text.AlignHCenter
                }

                Loader {
                    id: childLoader
                    objectName: "onboardingChildView"
                    active: root.childView !== null
                    visible: active
                    sourceComponent: root.childView
                    Layout.fillWidth: true
                    Layout.topMargin: root.heading.length > 0 || root.subheading.length > 0
                        || defaultImage.visible || imageLoader.visible ? 48 : 0
                }
            }
        }
    }

    footer: OnboardingFooter {
        id: footerBar
        width: root.width
        maximumContentWidth: root.maximumContentWidth
        contentSidePadding: root.contentSidePadding
        primaryText: root.primaryButtonText
        secondaryText: root.secondaryButtonText
        primaryEnabled: root.primaryButtonEnabled
        secondaryEnabled: root.secondaryButtonEnabled
        primaryButtonObjectName: root.primaryButtonObjectName
        secondaryButtonObjectName: root.secondaryButtonObjectName
        onPrimaryClicked: root.primaryClicked()
        onSecondaryClicked: root.secondaryClicked()
    }
}
