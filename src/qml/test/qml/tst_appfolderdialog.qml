// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtTest 1.2
import "qrc:/qml/controls"

TestCase {
    name: "AppFolderDialog"
    Component { id: factory; AppFolderDialog {} }
    Component { id: spyFactory; SignalSpy {} }

    function test_compatibilityContract() {
        const dialog = createTemporaryObject(factory, this)
        verify(dialog !== null)
        verify(dialog.selectedFolder !== undefined)
        compare(typeof dialog.open, "function")
        compare(typeof dialog.close, "function")
        const accepted = createTemporaryObject(spyFactory, this, {
            target: dialog, signalName: "accepted"
        })
        verify(accepted.valid)
        dialog.accepted()
        compare(accepted.count, 1)
    }
}
