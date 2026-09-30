// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
import QtQuick 2.15
import QtQuick.Controls 2.15 as Controls
import QtTest 1.2
import "../../qml/controls"

TestCase {
    name: "Slider"
    when: windowShown
    visible: true

    Slider { id: slider; from: 0; to: 5 }

    function test_continuous_by_default() {
        compare(slider.discrete, false)
        compare(slider.stepSize, 0)
        compare(slider.snapMode, Controls.Slider.NoSnap)
    }

    function test_discrete_mode() {
        slider.discrete = true
        compare(slider.stepSize, 1)
        compare(slider.snapMode, Controls.Slider.SnapAlways)
    }
}
