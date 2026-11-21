// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_LEGACY_SETTINGS_MIGRATION_H
#define BITCOIN_QML_LEGACY_SETTINGS_MIGRATION_H

#include <QString>
#include <util/fs.h>

#include <memory>

class ArgsManager;

namespace QmlLegacySettings {

enum class MigrationMode {
    Preview,
    Persist,
};

struct PendingCleanup;
struct DirectoryCleanup;
std::shared_ptr<DirectoryCleanup> CaptureLegacyGuiDataDir();
bool CompleteLegacyGuiDataDir(const std::shared_ptr<DirectoryCleanup>& cleanup, QString* error = nullptr);
// Native checked file/directory flush; no void DirectoryCommit receipt.
bool CommitTargetFile(const fs::path& target, QString* error = nullptr);

struct MigrationResult {
    bool settings_changed{false};
    QString error;
    // Native-only captured source. Staging never erases recoverable settings.
    std::shared_ptr<PendingCleanup> cleanup;
};

QString ReadLegacyGuiDataDir();
bool ReadLegacyGuiReset();
int ReadLegacyGuiDisplayUnit(const QString& chain, int fallback);
QString ReadLegacyGuiLanguage(const QString& chain);
bool ClearLegacyGuiSettings(const QString& chain, QString* error = nullptr);
bool ClearLegacyGuiDataDir(QString* error = nullptr);
MigrationResult MigrateCoreSettings(ArgsManager& args, MigrationMode mode);
MigrationResult CaptureResetSettings(ArgsManager& args);
// Seal the exact final replacement before its write; later in-memory changes
// must never authorize cleanup of a different replacement. Native only.
void CaptureMigrationTarget(ArgsManager& args, const MigrationResult& migration);
// Call only after the captured target write succeeds. This independently reads
// back the target and compares the sources before removing migrated keys.
bool CompleteCoreSettingsMigration(ArgsManager& args, const MigrationResult& migration, QString* error = nullptr);

} // namespace QmlLegacySettings

#endif // BITCOIN_QML_LEGACY_SETTINGS_MIGRATION_H
