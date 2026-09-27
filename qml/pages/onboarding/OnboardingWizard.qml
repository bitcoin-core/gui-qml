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
    property bool reducedMotion: false
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

    Item {
        id: heroLayer
        readonly property var currentPage: pages.currentItem
        anchors.top: navigationBar.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        clip: true
        visible: !!currentPage &&
            (currentPage.objectName === "onboardingCover" ||
             currentPage.objectName === "onboardingStrengthen" ||
             currentPage.objectName === "onboardingBlockchain" ||
             currentPage.objectName === "onboardingBlockclock")

        OnboardingMotion {
            id: motion
            anchors.horizontalCenter: parent.horizontalCenter
            y: 8 - (heroLayer.currentPage && heroLayer.currentPage.scrollView
                ? heroLayer.currentPage.scrollView.contentItem.contentY : 0)
            reducedMotion: root.reducedMotion
        }
    }

    Component {
        id: cover
        OnboardingCover {
            useSharedMotion: true
            showNavigationBar: false
            footerFullWidth: true
            animateContent: !root.reducedMotion
            animateFooterAfterContent: !root.reducedMotion
            contentAnimationDelay: 1800
            onNext: {
                root.push(strengthen)
                motion.toNetwork()
            }
        }
    }
    Component {
        id: strengthen
        OnboardingStrengthen {
            useSharedMotion: true
            showBackButton: pages.depth > 1
            showNavigationBar: false
            footerFullWidth: true
            animateContent: !root.reducedMotion
            contentAnimationDelay: 650
            onBack: {
                root.pop()
                motion.toLogo()
            }
            onNext: {
                root.push(blockchain)
                motion.toBlocks()
            }
        }
    }
    Component {
        id: blockchain
        OnboardingBlockchain {
            useSharedMotion: true
            showBackButton: pages.depth > 1
            showNavigationBar: false
            footerFullWidth: true
            animateContent: !root.reducedMotion
            contentAnimationDelay: 650
            onBack: {
                root.pop()
                motion.backToNetwork()
            }
            onNext: {
                root.push(blockclock)
                motion.toClock()
            }
        }
    }
    Component {
        id: blockclock
        OnboardingBlockclock {
            useSharedMotion: true
            showBackButton: pages.depth > 1
            showNavigationBar: false
            footerFullWidth: true
            animateContent: !root.reducedMotion
            contentAnimationDelay: 650
            onBack: {
                root.pop()
                motion.backToBlocks()
            }
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
