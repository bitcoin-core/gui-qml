// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_QML_WALLETIDENTITY_H
#define BITCOIN_QML_WALLETIDENTITY_H
#include <QString>
#include <map>
class QSettings;
namespace walletidentity {
QString Canonical(const QString& backend_name, const QString& wallet_directory = {});
QString Display(const QString& wallet_id);
QString CreationError(const QString& submitted);
QString AliasKey(const QString& wallet_id);
QString ReadAlias(QSettings& settings, const QString& wallet_id);
std::map<QString, QString> ReadAliases(QSettings& settings);
void WriteAlias(QSettings& settings, const QString& wallet_id, const QString& alias);
} // namespace walletidentity
#endif // BITCOIN_QML_WALLETIDENTITY_H
