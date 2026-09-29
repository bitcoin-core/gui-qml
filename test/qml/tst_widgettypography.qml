// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
import QtQuick 2.15
import QtTest 1.2
import "../../qml/controls"
import "../../qml/components/widgets"

TestCase {
    id: testCase
    name: "WidgetTypography"
    when: windowShown
    visible: true
    width: 1100
    height: 1100

    Component { id: frameComponent; WidgetFrame { animatePlacement: false } }

    function firstText(item) {
        if (item.visible && item.font !== undefined && item.text !== undefined) return item
        for (const child of item.children) {
            const text = firstText(child)
            if (text) return text
        }
        return null
    }

    function test_cellResize_data() {
        const cases = []
        for (const file of ["FeeRatesWidget", "MempoolWidget", "MempoolSummaryWidget", "DifficultyPeriodWidget", "HalvingWidget", "BlockClockWidget"]) {
            const sizes = file === "BlockClockWidget" ? [[2, 2], [3, 3]] : [[1, 1], [2, 1], [3, 2]]
            for (const size of sizes) cases.push({tag: file + size.join("x"), file: file, columns: size[0], rows: size[1]})
        }
        return cases
    }

    function test_cellResize(data) {
        const frame = createTemporaryObject(frameComponent, testCase, {
            widgetId: data.file, widgetTitle: data.file,
            widgetSource: "qrc:/qml/components/widgets/" + data.file + ".qml",
            supportedSizes: [], coordinateItem: testCase,
            columnSpan: data.columns, rowSpan: data.rows,
            width: data.columns * 160 + (data.columns - 1) * 12,
            height: data.rows * 160 + (data.rows - 1) * 12
        })
        tryVerify(function() { return frame.widgetContent !== null })
        const widget = frame.widgetContent
        waitForPolish(frame)
        compare(frame.contentPadding, 16)
        const position = widget.mapToItem(frame, 0, 0)
        compare(position.x, 16)
        compare(position.y, 16)
        compare(frame.width - widget.width - position.x, 16)
        compare(frame.height - widget.height - position.y, 16)
        const title = firstText(widget)
        verify(title !== null)
        const originalSize = title.font.pixelSize
        compare(widget.fontScale, 1)
        frame.width += data.columns * 160
        frame.height += data.rows * 160
        waitForPolish(widget)
        compare(widget.fontScale, 2)
        compare(title.font.pixelSize, originalSize * 2)
        tryVerify(function() { return !title.truncated })
        compare(title.font.family, Theme.text.family)
        frame.width -= data.columns * 160
        frame.height -= data.rows * 160
        waitForPolish(widget)
        compare(title.font.pixelSize, originalSize)
    }
}
