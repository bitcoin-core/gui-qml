// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/amountparse.h>

namespace amountinput {
Result Parse(QStringView text, int decimals, QChar separator)
{
    if (text.size() > 128 || decimals < 0 || decimals > 8) return {State::Invalid};
    text = text.trimmed();
    if (text.isEmpty()) return {State::Empty};
    CAmount whole{0}, fraction{0};
    int fraction_digits{0};
    bool decimal_seen{false}, digit_seen{false};
    for (QChar character : text) {
        const int digit = character.digitValue();
        if (digit >= 0) {
            digit_seen = true;
            if (decimal_seen) {
                if (++fraction_digits > decimals) return {State::Invalid};
                fraction = fraction * 10 + digit;
            } else {
                if (whole > (MAX_MONEY - digit) / 10) return {State::Invalid};
                whole = whole * 10 + digit;
            }
        } else if (!decimal_seen && decimals > 0 && (character == u'.' || character == separator)) {
            decimal_seen = true;
        } else {
            return {State::Invalid};
        }
    }
    if (!digit_seen || (decimal_seen && fraction_digits == 0)) return {State::Incomplete};
    CAmount factor{1};
    for (int i{0}; i < decimals; ++i) factor *= 10;
    if (whole > MAX_MONEY / factor) return {State::Invalid};
    for (int i{fraction_digits}; i < decimals; ++i) fraction *= 10;
    const CAmount scaled = whole * factor;
    if (fraction > MAX_MONEY - scaled) return {State::Invalid};
    return {State::Valid, scaled + fraction};
}
} // namespace amountinput
