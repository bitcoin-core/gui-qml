// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Page {
    id: root

    default property alias content: contentLayout.data

    property bool showBackButton: true
    property string backButtonObjectName: ""
    property string backButtonText: ""
    property alias rightItem: settingsHeader.rightItem
    property real maximumContentWidth: 840
    property real contentHorizontalPadding: width >= 900 ? 56 : width >= 640 ? 40 : 24
    property real contentSpacing: 24
    property real contentTopPadding: 20
    property real contentBottomPadding: 40

    readonly property alias pageHeader: settingsHeader
    readonly property alias scrollView: scrollView
    readonly property alias contentLayout: contentLayout

    signal back

    background: null
    padding: 0

    header: SettingsHeader {
        id: settingsHeader
        objectName: "settingsPageHeader"
        title: root.title
        showBackButton: root.showBackButton
        backButtonObjectName: root.backButtonObjectName
        backButtonText: root.backButtonText
        onBack: root.back()
    }

    ScrollView {
        id: scrollView
        objectName: "settingsPageScrollView"
        anchors.fill: parent
        contentWidth: availableWidth
        contentHeight: contentFrame.height
        clip: true
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

        Item {
            id: contentFrame
            width: scrollView.availableWidth
            height: contentLayout.implicitHeight + root.contentTopPadding + root.contentBottomPadding

            ColumnLayout {
                id: contentLayout
                objectName: "settingsPageContentLayout"
                anchors.top: parent.top
                anchors.topMargin: root.contentTopPadding
                anchors.horizontalCenter: parent.horizontalCenter
                width: Math.max(0, Math.min(
                    parent.width - root.contentHorizontalPadding * 2,
                    root.maximumContentWidth))
                spacing: root.contentSpacing
            }
        }
    }
}
