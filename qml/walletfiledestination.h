// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_WALLETFILEDESTINATION_H
#define BITCOIN_QML_WALLETFILEDESTINATION_H

#include <QDate>
#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QVariantMap>

//! True for a wallet database of any network.
bool IsWalletDatabaseFile(const QString& path);

//! File name for a backup or watch-only copy saved on date.
QString DefaultWalletFileName(const QString& wallet_name, bool watch_only, const QDate& date);

//! First free path next to path, stamped with now.
QString KeepBothPath(const QString& path, const QDateTime& now);

/**
 * What saving a wallet file to path would do. The map holds status
 * ("available", "walletFile", "otherFile" or "blocked"), path, folder,
 * fileName, keepBothPath, keepBothFileName and error. A folder gets the
 * default file name, and paths inside protected_paths are blocked.
 */
QVariantMap DescribeWalletFileDestination(const QString& path,
                                          bool watch_only,
                                          const QString& wallet_name,
                                          const QStringList& protected_paths,
                                          const QDateTime& now);

#endif // BITCOIN_QML_WALLETFILEDESTINATION_H
