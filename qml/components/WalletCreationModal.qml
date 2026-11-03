// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import "../controls"
import "../pages/wallet"

DialogPopup {
    id: root
    objectName: "walletCreationModal"
    signal finished(bool openActivity)
    property bool onboardingEntry: false

    function openForOnboarding() {
        onboardingEntry = true
        open()
    }

    onClosed: onboardingEntry = false

    title: qsTr("Add wallet")
    sheet: true
    implicitWidth: 880
    implicitHeight: 820
    padding: 0
    horizontalPadding: 0
    closesOnPressOutside: false
    readonly property var flow: contentItem && contentItem.item ? contentItem.item : null
    busy: !!flow && (flow.creatingWallet || flow.importingWallet || flow.readyPending ||
        (flow.currentItem && flow.currentItem.creatingWallet) ||
        (flow.currentItem && (flow.currentItem.objectName === "walletCreationReadyPage"
            || flow.currentItem.objectName === "externalWalletCreatedPage")))

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
