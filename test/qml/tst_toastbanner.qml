// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Window 2.15
import QtTest 1.2
import "../../qml/controls"

TestCase {
    name: "ToastBanner"
    when: windowShown

    Window {
        id: testWindow
        width: 400
        height: 200
        visible: true
    }

    Component {
        id: bannerComponent
        ToastBanner {
            width: 360
            shown: false
            text: "Incorrect password"
        }
    }

    Component {
        id: shownBannerComponent
        ToastBanner {
            width: 360
            text: "Only use HWI from its official releases page."
        }
    }

    function cleanup() {
        Theme.motion.resetMode()
    }

    function test_reshown_while_fading_out_fades_back_in() {
        Theme.motion.mode = "full"
        const banner = createTemporaryObject(bannerComponent, testWindow.contentItem)
        banner.shown = true
        tryCompare(banner, "opacity", 1)

        banner.shown = false
        verify(banner.visible)
        verify(banner.opacity > 0.5)
        banner.shown = true
        verify(banner.opacity > 0.5)
        tryCompare(banner, "opacity", 1)
        wait(Theme.motion.fast * 2)
        compare(banner.opacity, 1)
        compare(banner.visible, true)
    }

    // A banner that is shown from the start, on a page that is already up,
    // draws instead of keeping an empty space, and still auto-dismisses.
    function test_shown_when_created_draws() {
        Theme.motion.mode = "full"
        const banner = createTemporaryObject(shownBannerComponent, testWindow.contentItem)
        verify(banner.visible)
        compare(banner.opacity, 1)

        const dismissing = createTemporaryObject(shownBannerComponent, testWindow.contentItem, { dismissAfter: 1 })
        tryCompare(dismissing, "opacity", 0, 3000)
    }
}
