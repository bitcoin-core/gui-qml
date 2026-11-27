// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_AMOUNTPARSE_H
#define BITCOIN_QML_AMOUNTPARSE_H

#include <consensus/amount.h>

#include <QChar>
#include <QStringView>

namespace amountinput {
enum class State { Empty, Incomplete, Invalid, Valid };
struct Result {
    State state{State::Invalid};
    CAmount satoshi{0};
};
// Bounded exact input normalization. Separators are decimal marks, never grouping.
Result Parse(QStringView text, int decimals, QChar separator);
} // namespace amountinput

#endif // BITCOIN_QML_AMOUNTPARSE_H
