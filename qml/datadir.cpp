// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/datadir.h>

#include <chainparams.h>
#include <common/args.h>
#include <common/settings.h>
#include <common/system.h>
#include <qml/legacy_settings_migration.h>
#include <qml/models/settings_keys.h>
#include <univalue.h>
#include <util/fs_helpers.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QTemporaryFile>
#include <QUrl>

#include <string>
#include <vector>

namespace {
constexpr const char* RESET_GUI_SETTINGS_KEY{"fReset"};

QString DataDirTr(const char* source_text)
{
    return QCoreApplication::translate("DataDirectory", source_text);
}

QString CleanComparablePath(const QString& path)
{
    const QString normalized = QmlDataDir::NormalizeLocalPath(path);
    return normalized.isEmpty() ? QString{} : QDir::cleanPath(normalized);
}

bool HasReadablePreferencesSource(const QSettings& settings)
{
    bool ini = settings.format() == QSettings::IniFormat;
#if !defined(Q_OS_MACOS) && !defined(Q_OS_WIN)
    ini = ini || settings.format() == QSettings::NativeFormat;
#endif
    if (!ini || !QFileInfo::exists(settings.fileName())) return true;
    // Qt shares a parsed INI cache across instances but does not retain its
    // FormatError in every later instance. Ask Qt's own parser to validate a
    // bounded, uniquely named snapshot so repeated startup reads cannot turn
    // a malformed present preference into an absent/default preference.
    constexpr qint64 MAX_PREFERENCE_BYTES{1024 * 1024};
    QFile source{settings.fileName()};
    if (!source.open(QIODevice::ReadOnly)) return false;
    const auto bytes = source.read(MAX_PREFERENCE_BYTES + 1);
    if (source.error() != QFileDevice::NoError || bytes.size() > MAX_PREFERENCE_BYTES) return false;
    QTemporaryFile snapshot;
    if (!snapshot.open() || snapshot.write(bytes) != bytes.size() || !snapshot.flush()) return false;
    QSettings parsed{snapshot.fileName(), QSettings::IniFormat};
    parsed.setFallbacksEnabled(false);
    (void)parsed.allKeys();
    return parsed.status() == QSettings::NoError;
}
} // namespace

namespace QmlDataDir {

QString DefaultDataDirString()
{
    return QString::fromStdString(GetDefaultDataDir().utf8string());
}

QString DisplayPath(QString path)
{
#ifdef __ANDROID__
    path.replace("content://com.android.externalstorage.documents/tree/primary%3A", "/storage/self/primary/");
#endif
    return path;
}

QString NormalizeLocalPath(QString path)
{
    path = path.trimmed();
    if (path.isEmpty()) return {};
#ifdef __ANDROID__
    if (path.startsWith(QStringLiteral("content://"))) return path;
#endif
    const QUrl url(path);
    if (url.isLocalFile()) path = url.toLocalFile();
    return QDir(path).absolutePath();
}

fs::path QStringToPath(const QString& path)
{
    return fs::u8path(path.toStdString());
}

GuiDataDir ReadGuiDataDirWithSource(const UiPreferencesNamespace* identity)
{
    const auto owner=OpenPreferences(identity?*identity:UiPreferencesNamespace::Capture());QSettings& settings=*owner;
    const QString default_dir = DefaultDataDirString();
    settings.sync();
    if (!HasReadablePreferencesSource(settings) || settings.status() != QSettings::NoError)
        return {{}, GuiDataDirSource::Invalid, DataDirTr("The saved data directory preference could not be read. Its source has been retained; restore access and retry startup.")};
    if (settings.contains(SettingsKeys::DATA_DIR)) {
        const QVariant stored = settings.value(SettingsKeys::DATA_DIR);
        if (stored.metaType().id() != QMetaType::QString || stored.toString().trimmed().isEmpty())
            return {{}, GuiDataDirSource::Invalid, DataDirTr("The saved data directory preference is invalid. Its source has been retained; correct the preference before retrying startup.")};
        const QString data_dir = NormalizeLocalPath(stored.toString());
        if (IsDefaultDataDir(data_dir)) {
            return {default_dir, GuiDataDirSource::Default, {}, true};
        }
        return {data_dir, GuiDataDirSource::Settings, {}, true};
    }

    const QString legacy_data_dir = NormalizeLocalPath(QmlLegacySettings::ReadLegacyGuiDataDir());
    if (!legacy_data_dir.isEmpty()) {
        if (IsDefaultDataDir(legacy_data_dir)) {
            return {default_dir, GuiDataDirSource::Default, {}};
        }
        return {legacy_data_dir, GuiDataDirSource::LegacySettings, {}};
    }

    const QString data_dir = NormalizeLocalPath(settings.value(SettingsKeys::DATA_DIR, default_dir).toString());
    return {data_dir.isEmpty() ? default_dir : data_dir, GuiDataDirSource::Default, {}};
}

QString ReadGuiDataDir()
{
    return ReadGuiDataDirWithSource().path;
}

bool IsDefaultDataDir(const QString& path)
{
    const QString normalized = CleanComparablePath(path);
    return normalized.isEmpty() || normalized == CleanComparablePath(DefaultDataDirString());
}

QString ValidateCustomDataDir(const QString& path)
{
#ifdef __ANDROID__
    Q_UNUSED(path);
    return DataDirTr("Custom data directories are not supported on this platform yet.");
#else
    const QString local_path = NormalizeLocalPath(path);
    if (local_path.isEmpty()) {
        return DataDirTr("Choose a data directory.");
    }

    QFileInfo target_info(local_path);
    if (target_info.exists() && !target_info.isDir()) {
        return DataDirTr("The selected path exists and is not a directory.");
    }
    if (target_info.exists() && !target_info.isWritable()) {
        return DataDirTr("The selected directory is not writable.");
    }

    QString parent_path = target_info.absoluteDir().absolutePath();
    while (!QFileInfo::exists(parent_path)) {
        const QString current = parent_path;
        parent_path = QFileInfo(parent_path).absoluteDir().absolutePath();
        if (parent_path.isEmpty() || parent_path == current) {
            return DataDirTr("The data directory cannot be created here.");
        }
    }
    QFileInfo parent_info(parent_path);
    if (!parent_info.isDir() || !parent_info.isWritable()) {
        return DataDirTr("The parent directory is not writable.");
    }
    return {};
#endif
}

StorageSpaceResult CheckStorageSpace(const QString& path)
{
    StorageSpaceResult result;
    result.path = NormalizeLocalPath(path);
    if (result.path.isEmpty()) {
        result.message = DataDirTr("Choose a data directory.");
        return result;
    }

    const fs::path data_dir = QStringToPath(result.path);
    fs::path parent_dir = data_dir;
    fs::path previous_parent;
    try {
        while (parent_dir.has_parent_path() && !fs::exists(parent_dir)) {
            parent_dir = parent_dir.parent_path();
            if (parent_dir == previous_parent) break;
            previous_parent = parent_dir;
        }

        if (!fs::exists(parent_dir)) {
            result.message = DataDirTr("Cannot create data directory here.");
            return result;
        }

        const auto space_info = fs::space(parent_dir);
        result.checked_path = QString::fromStdString(fs::PathToString(parent_dir));
        result.available_bytes = space_info.available;
        result.capacity_bytes = space_info.capacity;
        result.path_exists = fs::exists(data_dir);
        result.path_is_directory = result.path_exists && fs::is_directory(data_dir);

        if (result.path_exists) {
            if (result.path_is_directory) {
                result.ok = true;
                result.message = DataDirTr("Directory already exists.");
            } else {
                result.message = DataDirTr("Path already exists, and is not a directory.");
            }
        } else {
            result.ok = true;
            result.message = DataDirTr("A new data directory will be created.");
        }
    } catch (const fs::filesystem_error&) {
        result.message = DataDirTr("Cannot create data directory here.");
    }
    return result;
}

bool EnsureDataDir(const QString& path, QString* error)
{
    const QString local_path = NormalizeLocalPath(path);
    const QString validation_error = ValidateCustomDataDir(local_path);
    if (!validation_error.isEmpty()) {
        if (error) *error = validation_error;
        return false;
    }

    try {
        const fs::path data_dir_path = QStringToPath(local_path);
        if (TryCreateDirectories(data_dir_path)) {
            TryCreateDirectories(data_dir_path / "wallets");
        }
    } catch (const fs::filesystem_error&) {
        if (error) *error = DataDirTr("The selected data directory could not be created.");
        return false;
    }
    if (error) error->clear();
    return true;
}

namespace {
bool SaveDataDirSelection(const QString& local_path, QString* error, bool consume_legacy, const UiPreferencesNamespace* identity)
{
    const auto cleanup=consume_legacy?QmlLegacySettings::CaptureLegacyGuiDataDir():nullptr;
    const auto owner=OpenPreferences(identity?*identity:UiPreferencesNamespace::Capture());QSettings& settings=*owner;
    const QVariant old_path = settings.value(SettingsKeys::DATA_DIR);
    const QVariant old_reset = settings.value(RESET_GUI_SETTINGS_KEY);
    // An explicit default remains a valid historical path value. Retain it
    // across restart so obsolete legacy fallback cannot retarget this choice.
    settings.setValue(SettingsKeys::DATA_DIR, local_path);
    settings.setValue(RESET_GUI_SETTINGS_KEY, false);
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        if (old_path.isValid()) settings.setValue(SettingsKeys::DATA_DIR, old_path);
        else settings.remove(SettingsKeys::DATA_DIR);
        if (old_reset.isValid()) settings.setValue(RESET_GUI_SETTINGS_KEY, old_reset);
        else settings.remove(RESET_GUI_SETTINGS_KEY);
        settings.sync();
        if (error) *error = QObject::tr("Unable to save the selected data directory.");
        return false;
    }
    // Known successful replacement remains known even if obsolete-source
    // cleanup cannot be confirmed. Onboarding owns its separate typed receipt.
    if (error) error->clear();
    if (consume_legacy) QmlLegacySettings::CompleteLegacyGuiDataDir(cleanup,error);
    return true;
}
} // namespace

bool PersistGuiDataDirSelection(const QString& path, QString* error, bool consume_legacy, const UiPreferencesNamespace* identity)
{
    const QString local_path = NormalizeLocalPath(path);
    return EnsureDataDir(local_path, error) && SaveDataDirSelection(local_path, error, consume_legacy, identity);
}

bool PersistDefaultDataDirSelection(QString* error, bool consume_legacy, const UiPreferencesNamespace* identity)
{
    return SaveDataDirSelection(DefaultDataDirString(), error, consume_legacy, identity);
}

bool ResetGuiSettings(ArgsManager& args, QString* error)
{
    if (error) error->clear();

    try {
        SelectParams(args.GetChainType());
        args.SelectConfigNetwork(args.GetChainTypeString());
    } catch (const std::exception& e) {
        if (error) *error = QString::fromStdString(e.what());
        return false;
    }

    fs::path settings_path;
    if (args.GetSettingsPath(&settings_path) && fs::exists(settings_path)) {
        std::vector<std::string> settings_errors;
        if (!args.ReadSettingsFile(&settings_errors)) {
            if (error) *error = QString::fromStdString(settings_errors.empty() ? std::string{"Settings file could not be read."} : settings_errors.front());
            return false;
        }
        if (!args.WriteSettingsFile(&settings_errors, /*backup=*/true)) {
            if (error) *error = QString::fromStdString(settings_errors.empty() ? std::string{"Settings file backup could not be written."} : settings_errors.front());
            return false;
        }
        args.LockSettings([](common::Settings& settings) {
            settings.rw_settings.clear();
        });
        settings_errors.clear();
        if (!args.WriteSettingsFile(&settings_errors)) {
            if (error) *error = QString::fromStdString(settings_errors.empty() ? std::string{"Settings file could not be written."} : settings_errors.front());
            return false;
        }
    }
    QSettings settings;
    settings.clear();
    settings.setValue(RESET_GUI_SETTINGS_KEY, false);
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        if (error) *error = DataDirTr("The reset settings were saved, but GUI preference cleanup could not be confirmed.");
        return false;
    }
    return QmlLegacySettings::ClearLegacyGuiSettings(QString::fromStdString(args.GetChainTypeString()), error);
}

bool HasExplicitDataDirArg(const ArgsManager& args)
{
    return args.IsArgSet("-datadir") && !args.GetPathArg("-datadir").empty();
}

bool ShouldShowDataDirChooser(const ArgsManager& args, const QString& data_dir)
{
    QSettings settings;
    const QString validation_error = IsDefaultDataDir(data_dir) ? QString{} : ValidateCustomDataDir(data_dir);
    const QFileInfo data_dir_info(data_dir);
    return !validation_error.isEmpty() ||
           !data_dir_info.exists() ||
           !data_dir_info.isDir() ||
           args.GetBoolArg("-choosedatadir", false) ||
           args.GetBoolArg("-resetguisettings", false) ||
           settings.value(RESET_GUI_SETTINGS_KEY, false).toBool() ||
           QmlLegacySettings::ReadLegacyGuiReset();
}

bool ApplyDataDirArg(ArgsManager& args, const QString& path)
{
    if (HasExplicitDataDirArg(args) || IsDefaultDataDir(path)) return false;

    const fs::path data_dir_path = QStringToPath(NormalizeLocalPath(path));
    const bool applied = args.SoftSetArg("-datadir", fs::PathToString(data_dir_path));
    if (applied) {
        args.ClearPathCache();
    }
    return applied;
}

bool ApplyGuiDataDirSetting(ArgsManager& args)
{
    if (HasExplicitDataDirArg(args)) return false;

    const QString data_dir = ReadGuiDataDir();
    if (IsDefaultDataDir(data_dir)) return false;

    if (!EnsureDataDir(data_dir)) return false;
    return ApplyDataDirArg(args, data_dir);
}

} // namespace QmlDataDir
