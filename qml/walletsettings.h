// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_QML_WALLETSETTINGS_H
#define BITCOIN_QML_WALLETSETTINGS_H

#include <QString>
#include <QVariantMap>
#include <map>
#include <optional>

class QSettings;

namespace walletsettings {
enum class Area { Aliases, Reservations };

// Versioned app preferences, independent of Core's wallet database. One
// canonical identity occupies one flat key, including the unnamed wallet.
QString EntryKey(Area area, const QString& identity);
std::optional<QVariantMap> Read(QSettings& settings, Area area, const QString& identity);
std::map<QString, QVariantMap> ReadAll(QSettings& settings, Area area);
void Stage(QSettings& settings, Area area, const QString& identity, const QVariantMap& payload);
} // namespace walletsettings
#endif // BITCOIN_QML_WALLETSETTINGS_H
