// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_PSBTSERIALIZATION_H
#define BITCOIN_QML_PSBTSERIALIZATION_H

#include <QByteArray>

class PartiallySignedTransaction;

namespace psbtwork {
/** Wallet-independent wire encoding of an owned document, performed on a worker. */
QByteArray SerializeRaw(const PartiallySignedTransaction& document);
} // namespace psbtwork

#endif // BITCOIN_QML_PSBTSERIALIZATION_H
