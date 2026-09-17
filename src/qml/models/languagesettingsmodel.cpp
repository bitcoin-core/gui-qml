// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/languagesettingsmodel.h>

#include <common/args.h>
#include <common/settings.h>
#include <common/system.h>
#include <qml/core_settings.h>
#include <qml/models/settings_keys.h>
#include <qml/translationmanager.h>
#include <univalue.h>

#include <QDir>
#include <QLocale>
#include <QSettings>

LanguageSettingsModel::LanguageSettingsModel(interfaces::Node& node, ArgsManager& args, TranslationManager& translations, QObject* parent)
    : QObject(parent), m_node(node), m_args(args), m_translations(translations)
{
    const auto effective = QmlCoreSettings::DisplaySettingValue(node, args, QStringLiteral("lang"));
    m_translations.setLanguage(effective.isNull() ? TranslationManager::ResolveLanguage(args)
        : QString::fromStdString(SettingToString(effective, "")));
    connect(&translations, &TranslationManager::languageChanged, this, &LanguageSettingsModel::languageChanged);
    m_available_languages << QString{};
    QStringList tags;
    const auto files = QDir(QStringLiteral(":/translations")).entryList({QStringLiteral("bitcoin_*.qm")}, QDir::Files);
    for (const QString& file : files) {
        QString tag = file.mid(8, file.size() - 11);
        if (!tag.startsWith(QStringLiteral("qml_"))) tags.push_back(tag);
    }
    tags.sort(Qt::CaseInsensitive);
    m_available_languages << tags;
}

QString LanguageSettingsModel::language() const { return m_translations.language(); }

QVariantMap LanguageSettingsModel::status() const
{
    return QmlCoreSettings::CoreSettingStatus(m_args, QStringLiteral("lang"));
}

void LanguageSettingsModel::setLanguage(const QString& language)
{
    if (language == this->language() || !QmlCoreSettings::CanEditCoreSetting(m_args, QStringLiteral("lang"))) return;
    QmlCoreSettings::UpdateRwSetting(m_node, QStringLiteral("lang"),
        QmlCoreSettings::GuiOverrideValue(m_args, QStringLiteral("lang"), common::SettingsValue{language.toStdString()}));
    QSettings().setValue(SettingsKeys::LANGUAGE, language);
    m_translations.setLanguage(language);
}

QString LanguageSettingsModel::languageLabel(const QString& locale_tag) const
{
    if (locale_tag.isEmpty()) return tr("System default");
    const QLocale locale(locale_tag);
    QString native = locale.nativeLanguageName();
    if (native.isEmpty()) return locale_tag;
    native[0] = native[0].toUpper();
    QString english = QLocale::languageToString(locale.language());
    if (locale_tag.contains('_')) {
        const QString territory = locale.nativeTerritoryName();
        if (!territory.isEmpty()) native += QStringLiteral(" (%1)").arg(territory);
        english += QStringLiteral(" (%1)").arg(QLocale::territoryToString(locale.territory()));
    }
    return QStringLiteral("%1 \u2014 %2").arg(native, english);
}
