// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Effects

BitcoinAmountDigitBase {
    id: root
    useEffects: GraphicsInfo.api !== GraphicsInfo.Software && GraphicsInfo.api !== GraphicsInfo.Unknown
    MultiEffect {
        anchors.fill: root.sourceItem
        source: root.sourceItem
        visible: root.useEffects
        autoPaddingEnabled: false
        blurEnabled: true
        blurMax: Math.max(2, Math.ceil(root.font.pixelSize * 0.25))
        blur: root.blurAmount
        maskEnabled: root.rolling
        maskSource: root.mask
        maskThresholdMin: 0.5
        maskSpreadAtMin: 1.0
    }
}
