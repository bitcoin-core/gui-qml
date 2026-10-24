// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtTest 1.2
import "../../qml/pages/settings"

TestCase {
    name: "MempoolSettingsPage"
    when: windowShown
    visible: true
    width: 640
    height: 480

    Component {
        id: mempoolPageComponent

        MempoolSettingsPage {
            width: 600
            height: 440
        }
    }

    function init() {
        optionsModel.clearCoreSettingStatusesForTest()
        optionsModel.maxMempoolSizeMB = 300
        nodeModel.mempoolTransactionCount = 1234
        nodeModel.mempoolUsageMB = 12.34
        nodeModel.mempoolMaxUsageMB = 300
    }

    function createPage() {
        const page = createTemporaryObject(mempoolPageComponent, this)
        verify(page !== null)
        return page
    }

    function commitSizeLimit(page, text) {
        const input = findChild(page, "mempoolSizeLimitInput")
        verify(input !== null)
        input.text = text
        input.editingFinished()
    }

    function test_displays_node_information() {
        const page = createPage()

        compare(findChild(page, "mempoolTransactionsRow").value,
            Number(nodeModel.mempoolTransactionCount).toLocaleString(Qt.locale(), 'f', 0))
        compare(findChild(page, "mempoolMemoryUsedRow").value,
            qsTr("%1 / %2")
                .arg(page.formatMegabytes(nodeModel.mempoolUsageMB))
                .arg(page.formatMegabytes(nodeModel.mempoolMaxUsageMB)))
        compare(findChild(page, "mempoolSizeLimitInput").text, "300")
    }

    function test_valid_size_updates_options_model() {
        const page = createPage()
        commitSizeLimit(page, "400")

        compare(optionsModel.maxMempoolSizeMB, 400)
        compare(findChild(page, "mempoolSizeLimitRow").errorText, "")
    }

    function test_empty_size_is_invalid() {
        const page = createPage()
        commitSizeLimit(page, "")

        compare(optionsModel.maxMempoolSizeMB, 300)
        verify(findChild(page, "mempoolSizeLimitRow").errorText.length > 0)
    }

    function test_out_of_range_size_is_invalid() {
        const page = createPage()
        commitSizeLimit(page, String(optionsModel.minMaxMempoolSizeMB - 1))

        compare(optionsModel.maxMempoolSizeMB, 300)
        verify(findChild(page, "mempoolSizeLimitRow").errorText.length > 0)
    }
}
