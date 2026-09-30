// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

.pragma library

// Keep the original formatted glyphs. Numeric values are used only to choose
// motion direction; formatting and monetary arithmetic remain with the caller.
function cells(text, decimalPoint, numerals) {
    function digit(ch) {
        const local = numerals.indexOf(ch)
        return local >= 0 ? local : "0123456789".indexOf(ch)
    }
    let first = -1
    let last = -1
    for (let i = 0; i < text.length; ++i) {
        if (digit(text.charAt(i)) >= 0) {
            if (first < 0) first = i
            last = i
        }
    }
    if (first < 0) return []
    let decimal = text.indexOf(decimalPoint, first)
    if (decimal < 0 || decimal > last) decimal = last + 1
    let integers = 0
    for (let i = first; i < decimal; ++i) {
        if (digit(text.charAt(i)) >= 0) ++integers
    }
    const result = []
    function add(key, start, end, number = -1) {
        result.push({key: key, start: start, end: end, glyph: text.substring(start, end), digit: number})
    }
    if (first > 0) add("prefix:" + text.substring(0, first), 0, first)
    let fraction = 0
    for (let i = first; i <= last; ++i) {
        const number = digit(text.charAt(i))
        if (number >= 0) {
            add(i < decimal ? "d" + --integers : "f" + ++fraction, i, i + 1, number)
        } else {
            add(i === decimal ? "decimal" : "group" + integers, i, i + 1)
        }
    }
    if (last + 1 < text.length) add("suffix:" + text.substring(last + 1), last + 1, text.length)
    return result
}

function magnitude(cells) {
    let value = ""
    for (const cell of cells) {
        if (cell.digit >= 0) value += cell.digit
        else if (cell.key === "decimal") value += "."
    }
    return Number(value)
}

function steps(from, to, direction) {
    return ((to - from) * direction % 10 + 10) % 10
}

function arrivalOpacity(progress) {
    if (progress < 0.45) return progress / 0.45 * 0.14
    if (progress < 0.78) return 0.14 + (progress - 0.45) / 0.33 * 0.48
    return 0.62 + (progress - 0.78) / 0.22 * 0.38
}

// Compatibility for model roles that still return a full formatted string.
// Keep currency tokens out of the digit wheel, including the BTC prefix form.
function splitUnit(value) {
    let match = /^(₿|BTC|mBTC|bits|sat|sats)\s+(.+)$/.exec(value)
    if (match) return {amount: match[2], unit: match[1], prefix: true}
    match = /^(.*\S)\s+(₿|BTC|mBTC|bits|sat|sats)$/.exec(value)
    if (match) return {amount: match[1], unit: match[2], prefix: false}
    return {amount: value, unit: "", prefix: false}
}
