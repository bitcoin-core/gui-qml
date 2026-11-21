// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/signerdiscovery.h>
#include <bitcoin-build-config.h> // IWYU pragma: keep
#include <qml/core_settings.h>
#include <qml/effectivesettings.h>
#include <common/args.h>
#include <common/settings.h>
#include <interfaces/node.h>
#include <univalue.h>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QRegularExpression>
#include <QSet>
#include <exception>
#include <stdexcept>
#include <utility>
namespace signerdiscovery {
namespace {
QString Identity(const QString& command) { return QString::fromLatin1(QCryptographicHash::hash(command.toUtf8(), QCryptographicHash::Sha256).toHex()); }
Configuration Config(QString command, bool editable, QString source)
{
    if (command.size() > 32768) command = QString(32769, QChar::ReplacementCharacter);
    return {command, Identity(command), editable, std::move(source)};
}
QString Command(const common::SettingsValue& value)
{
    // Core owns parsing/allocation of its settings value. Bound the app's
    // conversion before creating UTF-16 data or a GUI publication.
    if (value.isStr() && value.get_str().size() > 131072) return QString(32769, QChar::ReplacementCharacter);
    return QString::fromStdString(SettingToString(value).value_or(""));
}
std::vector<Device> Enumerate(interfaces::Node& node, const Configuration& config)
{
    // The public RPC exposes the fingerprint omitted by ExternalSigner::getName.
    UniValue response;
    try { response = node.executeRpc("enumeratesigners", UniValue{UniValue::VARR}, ""); }
    catch (const UniValue& error) {
        const auto& message = error.find_value("message");
        throw std::runtime_error(message.isStr() ? message.get_str().substr(0, 4096) : "The signer RPC failed.");
    }
    const auto& signers = response.find_value("signers");
    if (!signers.isArray() || signers.size() > 16) throw std::length_error("The signer response exceeds the device budget or is not an array.");
    std::vector<Device> devices;
    QSet<QString> seen;
    for (const auto& value : signers.getValues()) {
        const auto& fp = value.find_value("fingerprint"); const auto& name = value.find_value("name");
        if (!fp.isStr() || !name.isStr() || fp.get_str().size() != 8 || name.get_str().size() > 4096) throw std::invalid_argument("The signer response has invalid identity or name fields.");
        const auto fingerprint = QString::fromStdString(fp.get_str()).toLower();
        static const QRegularExpression hex{QStringLiteral("^[0-9a-f]{8}$")};
        if (!hex.match(fingerprint).hasMatch() || seen.contains(fingerprint)) throw std::invalid_argument("The signer response contains an invalid or duplicate fingerprint.");
        seen.insert(fingerprint);
        devices.push_back({config.id + QLatin1Char(':') + fingerprint, QString::fromStdString(name.get_str()), fingerprint, true});
    }
    return devices;
}
}
Ports PersistentPorts(interfaces::Node& node)
{
    Ports ports;
    ports.configuration = [&node] {
        if (node.isSettingIgnored("signer")) return Config({}, false, QStringLiteral("UnavailableCommandLine"));
        return Config(Command(node.getPersistentSetting("signer")), true, QStringLiteral("Persistent"));
    };
    ports.enumerate = [&node](const Configuration& config) { return Enumerate(node, config); };
    return ports;
}
Ports PublicPorts(interfaces::Node& node, ArgsManager& args)
{
    auto ports = PersistentPorts(node);
#ifndef ENABLE_EXTERNAL_SIGNER
    ports.available = false;
#endif
    ports.configuration = [&node, &args] {
        const auto value = QmlCoreSettings::DisplaySettingValue(node, args, QStringLiteral("signer"));
        const bool editable = QmlCoreSettings::CanEditCoreSetting(args, QStringLiteral("signer"));
        return Config(Command(value), editable, editable ? QStringLiteral("Persistent") : QStringLiteral("CommandLine"));
    };
    ports.save = [&node, &args, configuration = ports.configuration, available = ports.available](const QString& input) -> Result {
        Configuration current;
        try { current = configuration(); } catch (...) { return Failure(Error::Unavailable, Text("The effective signer configuration could not be read. No setting was changed.")); }
        if (!available) return Failure(Error::Unavailable, Text("External signer support is unavailable in this build."), current);
        if (!current.editable) return Failure(Error::Unavailable, Text("The signer is set by command line and cannot be edited here."), current);
        if (input.size() > 32768) return Failure(Error::InvalidPath, Text("The signer command is too long."), current);
        const auto command = NormalizeCommand(input);
        if (const auto error = PathError(command); !error.isEmpty()) return Failure(Error::InvalidPath, error, current);
        const common::SettingsValue value = command.isEmpty() ? common::SettingsValue{} : common::SettingsValue{command.toStdString()};
        QString persisted_command;
        try {
            persisted_command = Command(node.getPersistentSetting("signer"));
        } catch (...) {
            return Failure(Error::Unavailable, Text("The saved signer configuration could not be read. No setting was changed."), current);
        }
        // Share the app writer reservation through runtime handoff/readback.
        // A newer settings batch cannot be followed by this older handoff.
        const auto snapshot = effectivesettings::Read(args);
        if (!snapshot.ready || !snapshot.persistence_available)
            return Failure(Error::Persistence, snapshot.error.isEmpty() ? Text("The saved settings are unavailable or disabled.") : snapshot.error, current);
        bool configuration_changed{false};
        Configuration applied_configuration;
        const auto persistence = QmlCoreSettings::PersistTransaction(args, {QStringLiteral("signer")}, [&] {
            if (configuration().id != current.id) {
                configuration_changed = true;
                return false;
            }
            return QmlCoreSettings::WriteCoreSettingOverride(args, QStringLiteral("signer"), value);
        }, [&] {
            node.forceSetting("signer", value);
            applied_configuration = configuration();
        }, persisted_command == command);
        if (persistence.completed) return {applied_configuration, {}, Error::None, {}, true, true};
        auto result = Failure(configuration_changed ? Error::ConfigurationChanged : Error::Persistence, persistence.error, current);
        result.persisted = persistence.persisted;
        if (persistence.persisted) {
            result.message = Text("The signer setting was saved, but its runtime update could not be confirmed. Refresh the effective configuration before retrying.") + QLatin1Char(' ') + persistence.error;
            result.configuration_known = false;
        }
        try {
            result.configuration = configuration();
            result.configuration_known = true;
        } catch (...) {
            // Retain the known/unknown state established by the persistence result.
        }
        return result;
    };
    return ports;
}
} // namespace signerdiscovery
