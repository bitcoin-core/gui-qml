// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/walletidentity.h>
#include <qml/walletsettings.h>
#include <QCoreApplication>
#include <QDir>
#include <QSettings>
#include <stdexcept>

namespace walletidentity {
namespace {
QString Text(const char* text) { return QCoreApplication::translate("WalletCreationModel", text); }
QString Alias(const QVariantMap& payload)
{
    const auto value = payload.value(QStringLiteral("alias"));
    if (value.metaType().id() != QMetaType::QString) throw std::runtime_error("The wallet alias entry is incomplete");
    return value.toString().trimmed();
}
}
QString Canonical(const QString& backend_name, const QString& wallet_directory)
{
    if (backend_name.isEmpty()) return {};
    auto cleaned = QDir::cleanPath(backend_name);
    if (!QDir::isAbsolutePath(cleaned) || wallet_directory.isEmpty()) return cleaned;
    const auto relative_path = QDir(wallet_directory).relativeFilePath(cleaned);
    if (relative_path == QLatin1String("..") || relative_path.startsWith(QLatin1String("../")) || QDir::isAbsolutePath(relative_path)) return cleaned;
    return relative_path == QLatin1String(".") ? QString{} : relative_path;
}
QString Display(const QString& id)
{
    if (id.isEmpty()) return Text("Default wallet");
    if (id == id.trimmed()) return id;
    QString escaped = id; escaped.replace(QLatin1Char(' '), QChar{0x2420}); escaped.replace(QLatin1Char('\t'), QLatin1String("\\t"));
    escaped.replace(QLatin1Char('\n'), QLatin1String("\\n")); escaped.replace(QLatin1Char('\r'), QLatin1String("\\r"));
    return escaped;
}
QString CreationError(const QString& name)
{
    if (name.trimmed().isEmpty()) return Text("Enter a wallet name.");
    if (name.size() > 32768) return Text("The wallet name is too long.");
    if (name != name.trimmed()) return Text("Use a wallet name without leading or trailing whitespace.");
    if (QString::fromUtf8(name.toUtf8()) != name || name.contains(QChar::Null)) return Text("The wallet name contains unsupported characters.");
    if (QDir::isAbsolutePath(name) || name.contains(QLatin1Char('\\')) || Canonical(name) != name || name == QLatin1String(".") || name == QLatin1String("..") || name.startsWith(QLatin1String("../")))
        return Text("Use a relative wallet name without a trailing separator or parent path.");
    return {};
}
QString AliasKey(const QString& id) { return QStringLiteral("walletDisplayNames/%1").arg(id); }
QString ReadAlias(QSettings& settings, const QString& id)
{
    if (const auto value = walletsettings::Read(settings, walletsettings::Area::Aliases, id)) return Alias(*value);
    // Legacy entries remain readable until this identity is first written by
    // v2. A committed v2 tombstone prevents a stale legacy alias resurfacing.
    return settings.value(AliasKey(id)).toString().trimmed();
}
std::map<QString, QString> ReadAliases(QSettings& settings)
{
    std::map<QString, QString> aliases;
    if (settings.contains(AliasKey({}))) aliases.emplace(QString{}, settings.value(AliasKey({})).toString().trimmed());
    settings.beginGroup(QStringLiteral("walletDisplayNames"));
    for (const auto& id : settings.allKeys()) aliases.emplace(id, settings.value(id).toString().trimmed());
    settings.endGroup();
    for (const auto& [id, payload] : walletsettings::ReadAll(settings, walletsettings::Area::Aliases)) aliases[id] = Alias(payload);
    return aliases;
}
void WriteAlias(QSettings& settings, const QString& id, const QString& alias)
{
    const auto value = alias.isEmpty() || alias.trimmed() == id ? QString{} : alias.trimmed();
    walletsettings::Stage(settings, walletsettings::Area::Aliases, id, {{QStringLiteral("alias"), value}});
    // QSettings::remove also deletes descendant wallet identities. An empty
    // legacy string clears this alias without erasing another wallet's value.
    settings.setValue(AliasKey(id), value);
}
} // namespace walletidentity
