// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtTest 1.2
import "../../qml/components"

TestCase {
    name: "AboutOptions"
    when: windowShown
    width: 520
    height: 600

    Component {
        id: aboutOptionsComponent

        AboutOptions {
            width: 480
        }
    }

    function createOptions() {
        const options = createTemporaryObject(aboutOptionsComponent, this)
        verify(options !== null)
        return options
    }

    // The link owns its confirmation popup, so clicking the icon (the
    // ExternalLink itself, not the surrounding Setting row) opens the dialog
    // rather than the URL. The popup does not exist until first use.
    function test_link_icon_routes_through_confirmation_popup() {
        const options = createOptions()
        const linkIcon = findChild(options, "aboutWebsiteLinkIcon")
        verify(linkIcon !== null)
        compare(linkIcon.popupObjectName, "aboutWebsiteLinkIcon_popup")
        // The link owns its dialog, so assert against the link's own subtree.
        verify(findChild(linkIcon, linkIcon.popupObjectName) === null)

        linkIcon.clicked()

        const popup = findChild(linkIcon, linkIcon.popupObjectName)
        verify(popup !== null)
        tryCompare(popup, "opened", true)
        compare(popup.link, "https://bitcoincore.org")
    }

    // The surrounding row is clickable too, and must reach the same dialog as
    // the icon rather than carrying its own copy of the wiring.
    function test_row_click_opens_the_links_own_popup() {
        const options = createOptions()
        const row = findChild(options, "aboutSourceCodeLink")
        verify(row !== null)

        row.clicked()

        const popup = findChild(row, "aboutSourceCodeLinkIcon_popup")
        verify(popup !== null)
        tryCompare(popup, "opened", true)
        compare(popup.link, "https://github.com/bitcoin/bitcoin")
    }
}
