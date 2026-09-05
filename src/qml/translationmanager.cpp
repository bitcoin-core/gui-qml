// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/translationmanager.h>

#include <common/args.h>
#include <common/settings.h>
#include <common/system.h>
#include <qml/legacy_settings_migration.h>
#include <univalue.h>

#include <QCoreApplication>
#include <QLocale>
#include <QQmlEngine>

TranslationManager::TranslationManager(QObject* parent) : QObject(parent) {}

TranslationManager::~TranslationManager()
{
    QCoreApplication::removeTranslator(&m_qml_translator);
    QCoreApplication::removeTranslator(&m_core_translator);
}

QString TranslationManager::ResolveLanguage(ArgsManager& args)
{
    // A deliberately empty -lang= overrides the saved preference, too.
    const auto effective = args.GetSetting("-lang");
    if (!effective.isNull()) return QString::fromStdString(SettingToString(effective, ""));
    return QmlLegacySettings::ReadGuiLanguage(QString::fromStdString(args.GetChainTypeString()));
}

void TranslationManager::attachEngine(QQmlEngine& engine)
{
    for (const auto& attached : m_engines) if (attached == &engine) return;
    m_engines.push_back(&engine);
}

void TranslationManager::setLanguage(const QString& language)
{
    if (m_installed && m_language == language) return;
    QCoreApplication::removeTranslator(&m_qml_translator);
    QCoreApplication::removeTranslator(&m_core_translator);
    m_language = language;
    m_installed = true;
    // Empty means the system locale, not an unconditional fallback to English.
    const QLocale locale = language.isEmpty() ? QLocale::system() : QLocale(language);
    QLocale::setDefault(locale);
    if (m_core_translator.load(locale, QStringLiteral("bitcoin"), QStringLiteral("_"), QStringLiteral(":/translations"))) {
        QCoreApplication::installTranslator(&m_core_translator);
    }
    if (m_qml_translator.load(locale, QStringLiteral("bitcoin_qml"), QStringLiteral("_"), QStringLiteral(":/translations"))) {
        QCoreApplication::installTranslator(&m_qml_translator);
    }
    for (auto it = m_engines.begin(); it != m_engines.end();) {
        if (*it) { (*it)->retranslate(); ++it; }
        else it = m_engines.erase(it);
    }
    Q_EMIT languageChanged();
}
