// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtTest 1.2
import "../../qml/components"

TestCase {
    id: testCase
    name: "NodePopups"
    when: windowShown
    visible: true
    width: 800
    height: 640

    QtObject {
        id: runtimeDialogModel
        property var warningList: []
    }
    QtObject {
        id: nodeInformationModel
        function nodeInformationRows() { return [] }
    }
    Component {
        id: warningsComponent
        NodeWarningsPopup {}
    }
    Component {
        id: informationComponent
        NodeInformationPopup {}
    }

    function test_warningRowsCanBeReplacedWhileOpen() {
        runtimeDialogModel.warningList = []
        const popup = createTemporaryObject(warningsComponent, testCase)
        verify(popup !== null)
        popup.open()
        tryCompare(popup, "opened", true)
        for (let i = 0; i < 12; ++i) {
            runtimeDialogModel.warningList = ["Long warning ".repeat(30), "Warning " + i]
            tryCompare(popup, "warningCount", 2)
            tryVerify(function() { return popup.firstWarningLineCount > 1 })
            runtimeDialogModel.warningList = ["Replacement " + i]
            tryCompare(popup, "warningCount", 1)
            compare(popup.firstWarningText, "Replacement " + i)
            waitForRendering(testCase)
        }
        runtimeDialogModel.warningList = []
        tryCompare(popup, "warningCount", 0)
        tryCompare(findChild(popup, "nodeNoWarningsText"), "visible", true)
        popup.close()
        tryCompare(popup, "opened", false)
    }

    function test_informationRowsCanBeReplacedWhileOpen() {
        const popup = createTemporaryObject(informationComponent, testCase)
        verify(popup !== null)
        popup.open()
        tryCompare(popup, "opened", true)
        for (let i = 0; i < 12; ++i) {
            popup.rows = [
                {label: "Version", value: "Version " + i},
                {label: "Directory", value: "/a/long/directory/".repeat(30)}
            ]
            tryCompare(popup, "informationRowCount", 2)
            tryVerify(function() { return popup.lastInformationValueLineCount > 1 })
            popup.rows = [{label: "Version", value: "Replacement " + i}]
            tryCompare(popup, "informationRowCount", 1)
            compare(popup.firstInformationValue, "Replacement " + i)
            waitForRendering(testCase)
        }
        popup.close()
        tryCompare(popup, "opened", false)
    }
}
