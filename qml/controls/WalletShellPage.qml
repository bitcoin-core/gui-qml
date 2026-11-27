// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import org.bitcoincore.qt 1.0

// The main window can address a loaded wallet page without resolving its
// wallet-only descendants in a node-only build. The concrete page owns every
// command handler and its original-wallet guards.
Page {
    property bool modalNavigationBlocked: false

    signal addWallet()
    signal closeWalletRequested()
    signal backupRequested()
    signal uriImportRequested()
    signal signMessageRequested()
    signal verifyMessageRequested()
    signal psbtImportRequested()
    signal nodeRequested()
    signal activityRequested()
    signal sendRequested()
    signal receiveRequested()
    signal consoleRequested()
    signal networkTrafficRequested()
    signal peersRequested()
    signal settingsRequested(string section)
    signal presentWalletRequested(QtObject wallet)
    signal continueImportRequested(DesktopIntentModel mailbox)
}
