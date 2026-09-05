// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_LEGACY_SETTINGS_MIGRATION_H
#define BITCOIN_QML_LEGACY_SETTINGS_MIGRATION_H

#include <QString>

class ArgsManager;

namespace QmlLegacySettings {

enum class MigrationMode {
    Preview,
    Persist,
};

struct MigrationResult {
    bool settings_changed{false};
    QString error;
};

QString ReadLegacyGuiDataDir();
bool ReadLegacyGuiReset();
int ReadLegacyGuiDisplayUnit(const QString& chain, int fallback);
QString ReadLegacyGuiLanguage(const QString& chain);
QString ReadGuiLanguage(const QString& chain);
void ClearLegacyGuiSettings(const QString& chain);
MigrationResult MigrateCoreSettings(ArgsManager& args, MigrationMode mode);

} // namespace QmlLegacySettings

#endif // BITCOIN_QML_LEGACY_SETTINGS_MIGRATION_H
