// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQml 2.15

QtObject {
    enum Role {
        Normal,
        Cancel,
        Destructive
    }

    property string text: ""
    property int role: AlertAction.Normal
    property string buttonObjectName: ""
    property bool closesPopup: true

    signal triggered()
}
