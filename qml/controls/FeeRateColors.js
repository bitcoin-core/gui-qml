// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

.pragma library

function colorForRate(rate, referenceRate, palette, unavailableColor) {
    if (!(rate > 0) || !isFinite(rate) || !(referenceRate > 0) || !isFinite(referenceRate)) return unavailableColor
    // Half the recent median is cool; each doubling moves one palette stop.
    const level = Math.max(0, Math.min(3, Math.log(rate / referenceRate) / Math.LN2 + 1))
    const lower = Math.floor(level)
    const blend = level - lower
    const a = palette[lower]
    const b = palette[Math.min(lower + 1, 3)]
    return Qt.rgba(a.r + (b.r - a.r) * blend, a.g + (b.g - a.g) * blend,
                   a.b + (b.b - a.b) * blend, 1)
}
