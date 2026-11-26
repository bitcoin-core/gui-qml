// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import "AmountDisplayLayout.js" as AmountLayout

BitcoinAmountDigit {
    id: column
    required property real viewBleed
    required property real viewCellPitch
    required property color viewColor
    required property int viewDirection
    required property real viewElapsed
    required property real viewExitProgress
    required property font viewFont
    required property string viewNumerals
    required property int viewRollDuration
    required property real viewRollProgress
    required property int viewSettleDuration
    required property real viewShiftProgress
    required property Item edgeMaskItem
    required property var modelData
    objectName: "amountColumn_" + modelData.key
    font: column.viewFont
    color: column.viewColor
    numerals: column.viewNumerals
    glyph: modelData.glyph
    fromDigit: modelData.fromDigit
    steps: modelData.steps
    direction: column.viewDirection
    pitch: column.viewCellPitch
    bleed: column.viewBleed
    mask: column.edgeMaskItem
    turn: modelData.leaving ? column.viewExitProgress : column.viewRollProgress
    phase: column.viewElapsed / column.viewRollDuration
    readonly property real arrival: Math.max(0, Math.min(1, (column.viewElapsed - modelData.delay) / column.viewSettleDuration))
    x: modelData.fromX + (modelData.toX - modelData.fromX) * column.viewShiftProgress
    width: modelData.fromWidth + (modelData.toWidth - modelData.fromWidth) * column.viewShiftProgress
    anchors.verticalCenter: parent.verticalCenter
    offset: modelData.fromOffset * (1 - turn) - direction * steps * pitch * turn
    opacity: modelData.leaving ? modelData.fromOpacity * (1 - column.viewExitProgress)
        : modelData.arriving ? AmountLayout.arrivalOpacity(arrival)
        : modelData.fromOpacity + (1 - modelData.fromOpacity) * column.viewShiftProgress
    transform: Scale {
        origin.x: column.width / 2
        xScale: column.modelData.arriving ? 0.55 + 0.45 * column.arrival : 1
    }
    blurAmount: modelData.leaving ? column.viewExitProgress : modelData.arriving ? 1 - arrival
        : Math.min(1, steps / 4) * Math.max(0, phase < 0.15 ? phase / 0.15 : (0.62 - phase) / 0.47)
}
