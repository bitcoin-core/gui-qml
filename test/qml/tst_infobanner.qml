// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
import QtQuick 2.15
import QtTest 1.2
import "../../qml/controls"

TestCase {
    name: "InfoBanner"
    when: windowShown
    visible: true
    width: 900
    height: 700

    Component {
        id: bannerComponent
        InfoBanner {
            objectName: "testedBanner"
            titleObjectName: "testedTitle"
            messageObjectName: "testedMessage"
            height: implicitHeight
        }
    }
    SignalSpy { id: primarySpy; signalName: "primaryClicked" }
    SignalSpy { id: dismissSpy; signalName: "dismissClicked" }

    function init() {
        testSetup.failOnWarnings()
        primarySpy.clear()
        dismissSpy.clear()
    }
    function test_transient_widths_keep_layout_stable_data() {
        const rows = []
        for (let layout = 0; layout < 2; ++layout) {
            for (let close = 0; close < 2; ++close) {
                for (let buttons = 0; buttons < 5; ++buttons) {
                    rows.push({tag: layout + "-" + close + "-" + buttons,
                               layout: layout, close: close === 1, buttons: buttons})
                }
            }
        }
        return rows
    }
    function test_transient_widths_keep_layout_stable(data) {
        const originalMessage = "A complete diagnostic with wrapped words and Unicode ü. ".repeat(80)
        const banner = createTemporaryObject(bannerComponent, this, {
            width: 0,
            bannerLayout: data.layout === 0 ? InfoBanner.Layout.Horizontal : InfoBanner.Layout.Vertical,
            showsCloseButton: data.close,
            iconSource: "image://images/info",
            title: "A wrapped diagnostic title that remains fully available",
            message: originalMessage,
            primaryButtonText: data.buttons > 0 && data.buttons < 4 ? "Continue" : "",
            primaryButtonOutlined: data.buttons === 2,
            dismissButtonText: data.buttons >= 3 ? "Dismiss" : "",
            textRightMargin: 12
        })
        verify(banner !== null)
        const widths = [0, 1, 24, 59, 320, 760, 0, 760]
        for (let index = 0; index < widths.length; ++index) {
            banner.width = widths[index]
            // Give the real Qt polish/render phases their turn at every size.
            wait(30)
            const content = findChild(banner, "testedBannerContent")
            const title = findChild(banner, "testedTitle")
            const message = findChild(banner, "testedMessage")
            verify(content !== null && title !== null && message !== null)
            verify(content.width >= 0)
            verify(title.width >= 0 && message.width >= 0)
            verify(isFinite(banner.implicitHeight) && banner.implicitHeight > 0)
            compare(message.text, originalMessage)
            compare(title.text, banner.title)
            // The message text style fixes the line height at every width.
            compare(message.lineHeight, banner.messageTextStyle.lineHeight)
            compare(message.lineHeightMode, Text.FixedHeight)
            // Text stops short of the close button when it is shown.
            const closeButton = findChild(banner, banner.objectName + "CloseButton")
            const rightEdge = data.close ? closeButton.x - 8 : banner.width - banner.horizontalContentMargin
            compare(content.width, Math.max(0, rightEdge - banner.horizontalContentMargin))
        }
        // Recover ordinary visible geometry before exercising every action.
        banner.message = "Ready."
        banner.title = "Diagnostic"
        tryVerify(function() { return banner.height < 500 })
        waitForRendering(banner)
        primarySpy.target = banner
        dismissSpy.target = banner
        if (data.buttons > 0 && data.buttons < 4) {
            const primary = findChild(banner, "testedBannerPrimaryButton")
            verify(primary !== null && primary.visible)
            mouseClick(primary)
            compare(primarySpy.count, 1)
        }
        if (data.buttons >= 3) {
            const dismiss = findChild(banner, "testedBannerDismissButton")
            verify(dismiss !== null && dismiss.visible)
            mouseClick(dismiss)
            compare(dismissSpy.count, 1)
        }
        if (data.close) {
            const close = findChild(banner, "testedBannerCloseButton")
            verify(close !== null && close.visible)
            mouseClick(close)
            compare(dismissSpy.count, data.buttons >= 3 ? 2 : 1)
        }
        compare(banner.message, "Ready.")
    }
}
