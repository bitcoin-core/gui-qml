// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/walletsettings.h>

#include <QSettings>
#include <QScopeGuard>
#include <stdexcept>

namespace walletsettings {
namespace {
QString Group(Area area)
{
    return area == Area::Aliases ? QStringLiteral("walletSettings/v2/aliases") : QStringLiteral("walletSettings/v2/reservations");
}
QString Component(const QString& identity)
{
    if (identity.size() > 32768 || QString::fromUtf8(identity.toUtf8()) != identity) {
        throw std::runtime_error("The wallet settings identity is invalid");
    }
    // Lowercase hex is injective even on case-insensitive native settings
    // backends. The prefix makes the default wallet's component nonempty.
    return QLatin1Char{'w'} + QString::fromLatin1(identity.toUtf8().toHex());
}
QVariantMap Decode(const QVariant& value, const QString& identity)
{
    if (value.metaType().id() != QMetaType::QVariantMap) throw std::runtime_error("The wallet settings entry is incomplete");
    const auto entry = value.toMap();
    const auto version = entry.value(QStringLiteral("version"));
    const auto version_type = version.metaType().id();
    // CFPreferences may widen a persisted int. This is still integer version2,
    // while bool, floating-point and string conversions are not valid versions.
    if ((version_type != QMetaType::Int && version_type != QMetaType::LongLong) ||
        version.toLongLong() != 2 ||
        entry.value(QStringLiteral("identity")).metaType().id() != QMetaType::QString ||
        entry.value(QStringLiteral("identity")).toString() != identity ||
        entry.value(QStringLiteral("payload")).metaType().id() != QMetaType::QVariantMap) {
        throw std::runtime_error("The wallet settings entry has an unsupported format");
    }
    return entry.value(QStringLiteral("payload")).toMap();
}
}
QString EntryKey(Area area, const QString& identity) { return Group(area) + QLatin1Char{'/'} + Component(identity); }
std::optional<QVariantMap> Read(QSettings& settings, Area area, const QString& identity)
{
    const auto key = EntryKey(area, identity);
    if (!settings.contains(key)) return {};
    return Decode(settings.value(key), identity);
}
std::map<QString, QVariantMap> ReadAll(QSettings& settings, Area area)
{
    std::map<QString, QVariantMap> result;
    settings.beginGroup(Group(area));
    const auto leave = qScopeGuard([&] { settings.endGroup(); });
    if (!settings.childGroups().isEmpty()) throw std::runtime_error("The wallet settings namespace is malformed");
    for (const auto& key : settings.childKeys()) {
        const auto value = settings.value(key);
        const auto identity = value.toMap().value(QStringLiteral("identity")).toString();
        if (key != Component(identity)) throw std::runtime_error("The wallet settings key does not match its identity");
        result.emplace(identity, Decode(value, identity));
    }
    return result;
}
void Stage(QSettings& settings, Area area, const QString& identity, const QVariantMap& payload)
{
    settings.setValue(EntryKey(area, identity), QVariantMap{{QStringLiteral("version"), 2}, {QStringLiteral("identity"), identity}, {QStringLiteral("payload"), payload}});
}
} // namespace walletsettings
