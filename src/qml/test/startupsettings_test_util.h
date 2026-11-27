// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_TEST_STARTUPSETTINGS_TEST_UTIL_H
#define BITCOIN_QML_TEST_STARTUPSETTINGS_TEST_UTIL_H

#include <QtTest/QtTest>
#include <QDir>
#include <QFile>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <chainparams.h>
#include <common/args.h>
#include <common/init.h>
#include <common/settings.h>
#include <init.h>
#include <qml/core_settings.h>
#include <qml/datadir.h>
#include <qml/guiargs.h>
#include <qml/guiconstants.h>
#include <qml/legacy_settings_migration.h>
#include <qml/models/onboardingoptionsmodel.h>
#include <qml/models/settings_keys.h>
#include <qml/onboarding_settings.h>
#include <univalue.h>
#include <util/fs.h>
#include <util/translation.h>
#include <array>
#include <memory>

namespace startupsettingstest {

inline common::SettingsValue MakeInt(int value) { return common::SettingsValue{value}; }

inline std::vector<std::string> TestArgv()
{
    return {std::string{"bitcoinqml"}, std::string{"-regtest"}};
}

inline bool PrepareTestArgs(ArgsManager& args, const std::vector<std::string>& argv, std::string& error)
{
    SetupServerArgs(args, /*can_listen_ipc=*/false);
    SetupQmlGuiArgs(args);
    std::vector<const char*> raw_argv;
    raw_argv.reserve(argv.size());
    for (const std::string& arg : argv) raw_argv.push_back(arg.c_str());
    return args.ParseParameters(static_cast<int>(raw_argv.size()), raw_argv.data(), error);
}

class SavedGuiDataDirSettings
{
public:
    SavedGuiDataDirSettings()
    {
        QSettings settings;
        settings.setFallbacksEnabled(false);
        for (const QString& key : settings.allKeys()) {
            m_values.insert(key, settings.value(key));
        }
    }

    ~SavedGuiDataDirSettings()
    {
        QSettings settings;
        settings.setFallbacksEnabled(false);
        settings.clear();
        for (auto it = m_values.cbegin(); it != m_values.cend(); ++it) {
            settings.setValue(it.key(), it.value());
        }
        settings.sync();
    }

private:
    QVariantMap m_values;
};

class CurrentDirectoryRestorer
{
public:
    CurrentDirectoryRestorer() : m_original{QDir::currentPath()} {}
    ~CurrentDirectoryRestorer() { QDir::setCurrent(m_original); }

private:
    const QString m_original;
};

class SavedSettingsFormat
{
public:
    explicit SavedSettingsFormat(QSettings::Format format)
        : m_format{QSettings::defaultFormat()}
    {
        QSettings::setDefaultFormat(format);
    }

    ~SavedSettingsFormat()
    {
        QSettings::setDefaultFormat(m_format);
    }

private:
    QSettings::Format m_format;
};

class SavedNamedSettings
{
public:
    SavedNamedSettings(const QString& org, const QString& app)
        : m_settings{QSettings::defaultFormat(), QSettings::UserScope, org, app}
    {
        for (const QString& key : m_settings.allKeys()) {
            m_values.insert(key, m_settings.value(key));
        }
        m_settings.clear();
    }

    ~SavedNamedSettings()
    {
        m_settings.clear();
        for (auto it = m_values.cbegin(); it != m_values.cend(); ++it) {
            m_settings.setValue(it.key(), it.value());
        }
        m_settings.sync();
    }

    QSettings& settings() { return m_settings; }

private:
    QSettings m_settings;
    QVariantMap m_values;
};

class SavedRawNamedSettings
{
public:
    SavedRawNamedSettings(const QString& org, const QString& app)
        : m_format{QSettings::defaultFormat()}
        , m_org{org}
        , m_app{app}
    {
        QSettings settings{m_format, QSettings::UserScope, m_org, m_app};
        m_file_name = settings.fileName();
        for (const QString& key : settings.allKeys()) {
            m_values.insert(key, settings.value(key));
        }
        settings.clear();
        settings.sync();
    }

    ~SavedRawNamedSettings()
    {
        QSettings settings{m_format, QSettings::UserScope, m_org, m_app};
        settings.clear();
        for (auto it = m_values.cbegin(); it != m_values.cend(); ++it) {
            settings.setValue(it.key(), it.value());
        }
        settings.sync();
    }

    QString fileName() const { return m_file_name; }

private:
    QSettings::Format m_format;
    QString m_org;
    QString m_app;
    QString m_file_name;
    QVariantMap m_values;
};

inline std::vector<std::string> TestArgvWithDataDir(const QString& data_dir)
{
    return {
        std::string{"bitcoinqml"},
        std::string{"-regtest"},
        "-datadir=" + data_dir.toStdString(),
    };
}

inline void PrepareArgsForDataDir(ArgsManager& args, const QString& data_dir)
{
    std::string parse_error;
    QVERIFY2(PrepareTestArgs(args, TestArgvWithDataDir(data_dir), parse_error), parse_error.c_str());
    SelectParams(args.GetChainType());
    args.SelectConfigNetwork(args.GetChainTypeString());
}

inline void ReadSettingsForDataDir(ArgsManager& args, const QString& data_dir)
{
    PrepareArgsForDataDir(args, data_dir);
    std::vector<std::string> settings_errors;
    QVERIFY2(args.ReadSettingsFile(&settings_errors), settings_errors.empty() ? "" : settings_errors.front().c_str());
}

inline void InitializeAndFinalizeSettings(
    ArgsManager& args,
    const QmlOnboardingSettings::GuiSettingsStore& bootstrap_gui_settings,
    const QmlOnboardingSettings::PendingApply* pending = nullptr,
    QmlOnboardingSettings::FinalizeResult* result = nullptr)
{
    const std::optional<common::ConfigError> init_error{
        common::InitConfig(args, [](const bilingual_str&, const std::vector<std::string>&) {
            return true;
        })
    };
    QVERIFY2(!init_error, init_error ? init_error->message.original.c_str() : "");
    args.SelectConfigNetwork(args.GetChainTypeString());

    QString finalize_error;
    QVERIFY2(
        QmlOnboardingSettings::FinalizeStartupSettings(
            args,
            bootstrap_gui_settings,
            pending,
            result,
            &finalize_error),
        qPrintable(finalize_error));
}

inline void PrepareAndFinalizeModelApply(OnboardingOptionsModel& model, ArgsManager& args)
{
    const QmlOnboardingSettings::GuiSettingsStore bootstrap_gui_settings{
        QmlOnboardingSettings::CurrentGuiSettingsStore()
    };
    QmlOnboardingSettings::PendingApply pending;
    QString apply_error;
    QVERIFY2(model.prepareApplyToArgs(args, pending, &apply_error), qPrintable(apply_error));
    InitializeAndFinalizeSettings(args, bootstrap_gui_settings, &pending);
}

inline void PrepareAndFinalizeApply(
    ArgsManager& args,
    const QmlOnboardingSettings::DataDirSelection& data_dir,
    const QString& resolved_data_dir,
    const QSet<QString>& touched_settings,
    const QmlCoreSettings::Values& values)
{
    const QmlOnboardingSettings::GuiSettingsStore bootstrap_gui_settings{
        QmlOnboardingSettings::CurrentGuiSettingsStore()
    };
    QmlOnboardingSettings::PendingApply pending;
    QString apply_error;
    QVERIFY2(
        QmlOnboardingSettings::PrepareApplyToArgs(
            args,
            data_dir,
            resolved_data_dir,
            touched_settings,
            values,
            /*effective_reset=*/false,
            pending,
            &apply_error),
        qPrintable(apply_error));
    InitializeAndFinalizeSettings(args, bootstrap_gui_settings, &pending);
}

} // namespace startupsettingstest

#endif // BITCOIN_QML_TEST_STARTUPSETTINGS_TEST_UTIL_H
