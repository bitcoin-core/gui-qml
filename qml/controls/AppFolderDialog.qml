// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import org.bitcoincore.qt 1.0

AppFileDialog {
    readonly property url selectedFolder: selectedFile
    fileMode: AppFileDialog.Directory
}
