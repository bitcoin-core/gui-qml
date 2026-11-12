// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtTest 1.2
import "../../qml/components"

TestCase {
    name: "MempoolInformationRows"
    when: windowShown
    width: 640
    height: 360

    Component {
        id: mempoolRowsComponent

        MempoolInformationRows {
            width: 520
        }
    }

    function init() {
        testSetup.failOnWarnings()
        optionsModel.maxMempoolSizeMB = 300
        nodeModel.mempoolTransactionCount = 1234
        nodeModel.mempoolUsageMB = 12.34
        nodeModel.mempoolMaxUsageMB = 300
    }

    function test_mempoolRows_display_node_information() {
        const rows = createTemporaryObject(mempoolRowsComponent, this)
        verify(rows !== null)

        const transactionsRow = findChild(rows, "mempoolTransactionsRow")
        verify(transactionsRow !== null)
        compare(transactionsRow.loadedItem.text,
            Number(nodeModel.mempoolTransactionCount).toLocaleString(Qt.locale(), 'f', 0))

        const memoryRow = findChild(rows, "mempoolMemoryUsedRow")
        verify(memoryRow !== null)
        compare(memoryRow.loadedItem.text,
            qsTr("%1 / %2")
                .arg(rows.formatMegabytes(nodeModel.mempoolUsageMB))
                .arg(rows.formatMegabytes(nodeModel.mempoolMaxUsageMB)))

        const limitRow = findChild(rows, "mempoolSizeLimitRow")
        verify(limitRow !== null)
        compare(limitRow.loadedItem.text, "300")
    }

    function test_mempoolRows_valid_size_updates_options_model() {
        const rows = createTemporaryObject(mempoolRowsComponent, this)
        verify(rows !== null)

        const limitRow = findChild(rows, "mempoolSizeLimitRow")
        verify(limitRow !== null)
        const input = limitRow.loadedItem
        verify(input !== null)

        input.text = "400"
        input.editingFinished()

        compare(optionsModel.maxMempoolSizeMB, 400)
        compare(limitRow.showErrorText, false)
    }

    function test_mempoolRows_empty_size_is_invalid() {
        const rows = createTemporaryObject(mempoolRowsComponent, this)
        verify(rows !== null)

        const limitRow = findChild(rows, "mempoolSizeLimitRow")
        verify(limitRow !== null)
        const input = limitRow.loadedItem
        verify(input !== null)

        input.text = ""
        input.editingFinished()

        compare(optionsModel.maxMempoolSizeMB, 300)
        compare(limitRow.showErrorText, true)
    }

    function test_mempoolRows_out_of_range_size_is_invalid() {
        const rows = createTemporaryObject(mempoolRowsComponent, this)
        verify(rows !== null)

        const limitRow = findChild(rows, "mempoolSizeLimitRow")
        verify(limitRow !== null)
        const input = limitRow.loadedItem
        verify(input !== null)

        input.text = String(optionsModel.minMaxMempoolSizeMB - 1)
        input.editingFinished()

        compare(optionsModel.maxMempoolSizeMB, 300)
        compare(limitRow.showErrorText, true)
    }
}
