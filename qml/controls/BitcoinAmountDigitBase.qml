// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15

Item {
    id: root
    required property font font
    required property color color
    required property string numerals
    required property string glyph
    required property int fromDigit
    required property int steps
    required property int direction
    required property real pitch
    required property real bleed
    required property Item mask
    required property real turn
    required property real phase
    required property real offset
    property bool useEffects: false
    property alias sourceItem: sourceItem
    required property real blurAmount
    readonly property bool rolling: fromDigit >= 0 && (steps > 0 || offset !== 0)
    height: pitch + 2 * bleed

    Item {
        id: sourceItem
        anchors.fill: parent
        visible: !root.useEffects
        clip: true
        Repeater {
            // Render only the part of the digit wheel traversed on this turn,
            // plus neighbours for the soft edges and interrupted animations.
            model: root.rolling ? root.steps + 3 : 1
            CoreText {
                required property int index
                width: root.width
                height: root.pitch
                y: root.bleed + (root.rolling ? root.offset + root.direction * (index - 1) * root.pitch : 0)
                text: root.rolling ? root.numerals.charAt(((root.fromDigit + root.direction * (index - 1)) % 10 + 10) % 10) : root.glyph
                opacity: root.rolling && !root.useEffects
                    ? Math.max(0, 1 - Math.abs(y - root.bleed) / root.pitch) : 1
                font: root.font
                color: root.color
                wrap: false
            }
        }
    }
}
