// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_QMLCACHEPOLICY_H
#define BITCOIN_QML_QMLCACHEPOLICY_H

//! Ignore host-generated QML cache files. Call before constructing a Qt
//! application or QML engine so embedded generated units use the same policy.
void ConfigureQmlCachePolicy();

#endif // BITCOIN_QML_QMLCACHEPOLICY_H
