// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_DATADIR_H
#define BITCOIN_QML_DATADIR_H

#include <util/fs.h>

#include <QString>

#include <cstdint>

class ArgsManager;

namespace QmlDataDir {

enum class GuiDataDirSource {
    Default,
    Settings,
    LegacySettings,
};

struct GuiDataDir {
    QString path;
    GuiDataDirSource source{GuiDataDirSource::Default};
};

struct StorageSpaceResult {
    QString path;
    QString checked_path;
    QString message;
    uint64_t available_bytes{0};
    uint64_t capacity_bytes{0};
    bool ok{false};
    bool path_exists{false};
    bool path_is_directory{false};
};

QString DefaultDataDirString();
QString NormalizeLocalPath(QString path);
fs::path QStringToPath(const QString& path);

GuiDataDir ReadGuiDataDirWithSource();
QString ReadGuiDataDir();
bool IsDefaultDataDir(const QString& path);
QString ValidateCustomDataDir(const QString& path);
StorageSpaceResult CheckStorageSpace(const QString& path);
bool EnsureDataDir(const QString& path, QString* error = nullptr);
bool PersistGuiDataDirSelection(const QString& path, QString* error = nullptr);
void PersistDefaultDataDirSelection();
bool ResetGuiSettings(ArgsManager& args, QString* error = nullptr);

bool HasExplicitDataDirArg(const ArgsManager& args);
bool ShouldShowDataDirChooser(const ArgsManager& args);
bool ApplyGuiDataDirSetting(ArgsManager& args);
bool ApplyDataDirArg(ArgsManager& args, const QString& path);

} // namespace QmlDataDir

#endif // BITCOIN_QML_DATADIR_H
