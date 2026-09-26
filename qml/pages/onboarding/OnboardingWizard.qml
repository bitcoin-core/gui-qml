// Copyright (c) 2024 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../../controls"

Item {
    id: root
    clip: true

    signal finished()
    property var settingsModel: optionsModel
    property bool preInit: false
    property int assumedBlockchainSize: 0
    property int assumedChainstateSize: 0
    readonly property alias currentItem: pages.currentItem
    readonly property alias depth: pages.depth
    readonly property alias busy: pages.busy

    function push(item) { return pages.push(item, {}, StackView.Immediate) }
    function pop() {
        if (pages.depth > 1)
            return pages.pop(pages.get(pages.depth - 2), StackView.Immediate)
        return null
    }

    OnboardingNavigationBar {
        id: navigationBar
        objectName: "onboardingWizardNavigationBar"
        width: parent.width
        title: root.currentItem ? root.currentItem.title : ""
        showBackButton: false
    }

    PageStack {
        id: pages
        anchors.top: navigationBar.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        clip: true
        initialItem: cover
    }

    Component {
        id: cover
        OnboardingCover {
            showNavigationBar: false
            footerFullWidth: true
            animateContent: true
            onNext: root.push(strengthen)
        }
    }
    Component {
        id: strengthen
        OnboardingStrengthen {
            showBackButton: pages.depth > 1
            showNavigationBar: false
            footerFullWidth: true
            animateContent: true
            onBack: root.pop()
            onNext: root.push(blockclock)
        }
    }
    Component {
        id: blockclock
        OnboardingBlockclock {
            showBackButton: pages.depth > 1
            showNavigationBar: false
            footerFullWidth: true
            animateContent: true
            onBack: root.pop()
            onNext: root.push(storageLocation)
        }
    }
    Component {
        id: storageLocation
        OnboardingStorageLocation {
            showBackButton: pages.depth > 1
            showNavigationBar: false
            footerFullWidth: true
            animateContent: true
            settingsModel: root.settingsModel
            assumedChainstateSize: root.assumedChainstateSize
            onBack: root.pop()
            onNext: root.push(storageAmount)
        }
    }
    Component {
        id: storageAmount
        OnboardingStorageAmount {
            showBackButton: pages.depth > 1
            showNavigationBar: false
            footerFullWidth: true
            animateContent: true
            settingsModel: root.settingsModel
            assumedBlockchainSize: root.assumedBlockchainSize
            assumedChainstateSize: root.assumedChainstateSize
            onBack: root.pop()
            onNext: root.push(connection)
        }
    }
    Component {
        id: connection
        OnboardingConnection {
            showBackButton: pages.depth > 1
            showNavigationBar: false
            footerFullWidth: true
            animateContent: true
            settingsModel: root.settingsModel
            onBack: root.pop()
            onNext: root.finished()
        }
    }
}
