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
            instanceId: data.file, widgetId: data.file, widgetTitle: data.file,
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
        const expectedStyle = Theme.text.widgetTitle
        compare(originalSize, expectedStyle.pixelSize)
        compare(title.font.styleName, expectedStyle.styleName)
        compare(widget.fontScale, 1)
        const valueStyle = data.columns >= 3 && data.rows >= 2
            ? Theme.text.widgetPrimaryValueLarge : Theme.text.widgetPrimaryValue
        compare(widget.primaryValueFont, valueStyle.font)
        const large = data.columns >= 3 && data.rows >= 2
        compare(widget.largeTypography, large)
        const primaryLabelStyle = large ? Theme.text.widgetPrimaryLabelLarge : Theme.text.widgetPrimaryLabel
        const secondaryValueStyle = large ? Theme.text.widgetSecondaryValueLarge : Theme.text.widgetSecondaryValue
        const footerLabelStyle = large ? Theme.text.widgetFooterLabelLarge : Theme.text.widgetFooterLabel
        const footerValueStyle = large ? Theme.text.widgetFooterValueLarge : Theme.text.widgetFooterValue
        compare(widget.primaryLabelFont, primaryLabelStyle.font)
        compare(widget.secondaryValueFont, secondaryValueStyle.font)
        compare(widget.footerLabelFont, footerLabelStyle.font)
        compare(widget.footerValueFont, footerValueStyle.font)
        const valueNames = {FeeRatesWidget: "feeRatesHeadline", MempoolWidget: "incomingTransactionsPrimaryValue",
            MempoolSummaryWidget: "mempoolSummaryCount", DifficultyPeriodWidget: "difficultyNextChange",
            HalvingWidget: "halvingHeadline", BlockClockWidget: "blockClockPrimaryValue"}
        const value = findChild(widget, valueNames[data.file])
        verify(value !== null)
        compare(value.font.pixelSize, valueStyle.pixelSize)
        compare(value.font.styleName, valueStyle.styleName)
        const clockLabel = findChild(widget, "blockClockSubText")
        const clockDial = findChild(widget, "blockClockDial")
        if (clockLabel) {
            compare(clockLabel.font.styleName, "Semi Bold")
            compare(clockLabel.font.pixelSize, Math.round(clockDial.width * 0.08))
        }
        const footerValue = findChild(widget, "difficultyAverageBlockTime")
        if (footerValue) compare(footerValue.font, widget.footerValueFont)
        frame.width += data.columns * 160
        frame.height += data.rows * 160
        waitForPolish(widget)
        compare(widget.fontScale, 2)
        compare(widget.secondaryValueFont.pixelSize, secondaryValueStyle.pixelSize * 2)
        compare(widget.primaryLabelFont, primaryLabelStyle.font)
        compare(widget.footerLabelFont, footerLabelStyle.font)
        compare(widget.footerValueFont, footerValueStyle.font)
        compare(value.font.pixelSize, valueStyle.pixelSize * 2)
        if (clockLabel) compare(clockLabel.font.pixelSize, Math.round(clockDial.width * 0.08))
        if (footerValue) compare(footerValue.font, widget.footerValueFont)
        compare(title.font.pixelSize, originalSize)
        tryVerify(function() { return !title.truncated })
        compare(title.font.family, Theme.text.family)
        frame.width -= data.columns * 160
        frame.height -= data.rows * 160
        waitForPolish(widget)
        compare(title.font.pixelSize, originalSize)
    }
}
