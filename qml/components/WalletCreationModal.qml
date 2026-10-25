// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import "../controls"
import "../pages/wallet"

Popup {
    id: root
    objectName: "walletCreationModal"
    signal finished(bool openActivity)
    property bool onboardingEntry: false

    function openForOnboarding() {
        onboardingEntry = true
        open()
    }

    onClosed: onboardingEntry = false

    parent: Overlay.overlay
    width: Math.min(880, parent ? parent.width - 32 : 880)
    height: Math.min(820, parent ? parent.height - 40 : 820)
    x: parent ? (parent.width - width) / 2 : 0
    y: parent ? (parent.height - height) / 2 : 0
    padding: 0
    modal: true
    dim: true
    focus: true
    readonly property var flow: contentItem && contentItem.item ? contentItem.item : null
    closePolicy: flow && (flow.creatingWallet || flow.importingWallet || flow.readyPending ||
        (flow.currentItem && flow.currentItem.creatingWallet) ||
        (flow.currentItem && (flow.currentItem.objectName === "walletCreationReadyPage"
            || flow.currentItem.objectName === "externalWalletCreatedPage")))
        ? Popup.NoAutoClose : Popup.CloseOnEscape

    background: Rectangle {
        radius: Theme.radius.sheet
        color: Theme.color.neutral1
        border.width: 1
        border.color: Theme.color.neutral3
        SurfaceGradientBorder {
            anchors.fill: parent
            surfaceColor: parent.color
            cornerRadius: parent.radius
        }
    }

    Overlay.modal: Rectangle {
        color: Qt.rgba(0, 0, 0, 0.6)
        Behavior on opacity { enabled: Theme.motion.animated; NumberAnimation { duration: Theme.motion.fade(Theme.motion.moderate) } }
    }
    enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.motion.fade(Theme.motion.moderate); easing.type: Theme.motion.easing.enter } }
    exit: Transition { NumberAnimation { property: "opacity"; to: 0; duration: Theme.motion.normal; easing.type: Theme.motion.easing.exit } }

    contentItem: Loader {
        clip: true
        active: root.visible
        sourceComponent: WalletCreationFlow {
            modalView: true
            onboardingEntry: root.onboardingEntry
            onFinished: function(openActivity) {
                root.close()
                root.finished(openActivity)
            }
        }
    }
}
