// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_QML_FLOWPRESENTATION_H
#define BITCOIN_QML_FLOWPRESENTATION_H
#include <QVariantMap>
// Run on Domain. QML receives at most16 nodes per side and prepared monetary
// facts; grouping, unknown-value policy and amount scaling are native decisions.
QVariantMap BoundedFlowEntry(QVariantMap entry);
QVariantList FlowEntryDocument(const QVariant& entry);
QVariantMap PrepareTransactionFlowPresentation(const QVariantMap& flow);
#endif // BITCOIN_QML_FLOWPRESENTATION_H
