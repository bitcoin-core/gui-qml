// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtTest 1.2
import "../../qml/controls"

TestCase {
    function init() {
        testSetup.failOnWarnings()
    }

    name: "ContextMenuDivider"
    when: windowShown
    width: 400
    height: 200

    Item {
        id: host
        width: parent.width
        height: parent.height
    }

    Component {
        id: dividerComponent

        ColumnLayout {
            width: 280
            property alias divider: _divider

            ContextMenuDivider {
                id: _divider
            }
        }
    }

    function test_default_height_matches_thickness_and_margins() {
        const host_layout = createTemporaryObject(dividerComponent, host)
        verify(host_layout !== null)
        const divider = host_layout.divider
        compare(divider.thickness, 1)
        compare(divider.verticalMargin, 6)
        compare(divider.implicitHeight, divider.thickness + 2 * divider.verticalMargin)
    }

    function test_custom_thickness_and_margin() {
        const host_layout = createTemporaryObject(dividerComponent, host)
        verify(host_layout !== null)
        const divider = host_layout.divider
        divider.thickness = 2
        divider.verticalMargin = 8
        compare(divider.implicitHeight, 18)
    }
}
