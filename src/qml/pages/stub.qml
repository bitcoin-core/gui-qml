// Copyright (c) 2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQml 2.12
import QtQuick.Controls 2.12

ApplicationWindow {
    id: appWindow
    title: "Bitcoin Core TnG"
    minimumWidth: 750
    minimumHeight: 450
    visible: true

    Component.onCompleted: nodeModel.startNodeInitializionThread();
}
