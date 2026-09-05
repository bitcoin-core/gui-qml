// Copyright (c) 2011-2024 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_GUICONSTANTS_H
#define BITCOIN_QML_GUICONSTANTS_H

#include <cstdint>

#define QAPP_ORG_NAME "BitcoinCore"
#define QAPP_ORG_DOMAIN "bitcoincore.org"
#define QAPP_APP_NAME_DEFAULT "BitcoinCore-App"
#define QAPP_APP_NAME_TESTNET "BitcoinCore-App-testnet"
#define QAPP_APP_NAME_TESTNET4 "BitcoinCore-App-testnet4"
#define QAPP_APP_NAME_SIGNET "BitcoinCore-App-signet"
#define QAPP_APP_NAME_REGTEST "BitcoinCore-App-regtest"

// One gigabyte (GB) in bytes.
static constexpr uint64_t GB_BYTES{1000000000};

// Default prune target displayed in QML settings (GB).
static constexpr int DEFAULT_PRUNE_TARGET_GB{2};

#endif // BITCOIN_QML_GUICONSTANTS_H
