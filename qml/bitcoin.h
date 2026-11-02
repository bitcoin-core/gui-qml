// Copyright (c) 2021-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_BITCOIN_H
#define BITCOIN_QML_BITCOIN_H

class QString;

int QmlGuiMain(int argc, char* argv[]);

//! Returns true for benign Qt font fallback warnings ("OpenType support
//! missing for ...") that should be logged at debug level rather than printed.
bool IsBenignQtFontWarning(const QString& msg);

#endif // BITCOIN_QML_BITCOIN_H
