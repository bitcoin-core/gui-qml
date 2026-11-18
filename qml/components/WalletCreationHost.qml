// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15

// The Loader owns an Item so the popup has the containing window immediately.
WalletCreationPort {
    id: root
    modalVisible: modal.visible
    onOpenRequested: modal.open()
    onOnboardingOpenRequested: modal.openForOnboarding()
    function open() { modal.open() }
    function openForOnboarding() { modal.openForOnboarding() }
    WalletCreationModal {
        id: modal
        onFinished: function(openActivity, wallet) { root.finished(openActivity, wallet) }
    }
}
