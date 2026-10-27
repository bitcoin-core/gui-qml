// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtTest 1.2
import "../../qml/components"

TestCase {
    id: testCase
    name: "MonospaceOutputView"
    when: windowShown
    visible: true
    width: 720
    height: 480

    ListModel {
        id: entries
        ListElement { timestamp: "[12:34:56]"; content: "getblockchaininfo" }
    }
    Component {
        id: outputComponent
        MonospaceOutputView {
            objectName: "consoleOutput"
            width: testCase.width
            height: testCase.height
            listModel: entries
            leftColumnRole: "timestamp"
            leftColumnWidth: 60
        }
    }

    function test_fontChangesKeepTimestampAndContentSeparate() {
        const output = createTemporaryObject(outputComponent, testCase)
        verify(output !== null)
        tryCompare(output, "count", 1)
        const timestamp = findChild(output, "consoleOutput_left_0")
        const content = findChild(output, "consoleOutput_content_0")
        verify(timestamp !== null)
        verify(content !== null)

        for (const size of [12, 24, 36, 12]) {
            output.fontPixelSize = size
            tryVerify(function() {
                return timestamp.width >= timestamp.implicitWidth
                    && content.x >= timestamp.x + timestamp.width
            })
            compare(content.text, "getblockchaininfo")
        }
    }
}
