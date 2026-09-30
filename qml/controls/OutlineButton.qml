// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15

NeutralButton {
    id: root

    // Compatibility for remaining OutlineButton callers.
    property bool bold: true

    buttonSize: NeutralButton.Large
    textStyle: bold ? Theme.text.buttonStrong : Theme.text.button
}
