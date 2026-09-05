// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/legacy_settings_migration.h>
#include <qml/guiconstants.h>
#include <qml/models/settings_keys.h>
#include <QSettings>

namespace QmlLegacySettings {
QString ReadGuiLanguage(const QString& chain)
{
    const QString suffix = chain == "main" ? QString{} : QStringLiteral("-") + (chain == "test" ? QStringLiteral("testnet") : chain);
    QSettings current(QSettings::defaultFormat(), QSettings::UserScope, QAPP_ORG_NAME, QStringLiteral(QAPP_APP_NAME_DEFAULT) + suffix);
    QSettings legacy(QSettings::defaultFormat(), QSettings::UserScope, QStringLiteral("Bitcoin"), QStringLiteral("Bitcoin-Qt") + suffix);
    QSettings legacy_default(QSettings::defaultFormat(), QSettings::UserScope, QStringLiteral("Bitcoin"), QStringLiteral("Bitcoin-Qt"));
    return current.value(SettingsKeys::LANGUAGE, legacy.value(SettingsKeys::LANGUAGE, legacy_default.value(SettingsKeys::LANGUAGE))).toString();
}
}
