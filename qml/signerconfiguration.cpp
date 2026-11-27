// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/signerdiscovery.h>
#include <QCoreApplication>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrl>
#include <utility>
namespace signerdiscovery {
QString Text(const char* text) { return QCoreApplication::translate("SignerDiscoveryModel", text); }
Result Failure(Error error, QString message, Configuration config) { const bool known = !config.id.isEmpty(); return {std::move(config), {}, error, std::move(message), false, known}; }
QString NormalizeCommand(const QString& value)
{
    const auto trimmed = value.trimmed(); const QUrl url{trimmed};
    return url.isLocalFile() ? url.toLocalFile() : trimmed;
}
QString PathError(const QString& command)
{
    if (command.size() > 32768) return Text("The signer command is too long.");
    if (command.isEmpty()) return {};
    if (command.contains(QChar{0})) return Text("The signer command contains an invalid character.");
    // Pinned Core splits on spaces/tabs without shell quoting or expansion.
    // Validate the executable Core will actually invoke, including PATH lookup.
    QString executable = command.section(QRegularExpression{QStringLiteral("[ \\t]")}, 0, 0);
    if (executable.startsWith(QLatin1Char('"')) || executable.startsWith(QLatin1Char('\''))) return Text("This Core version does not support a quoted signer executable path.");
    if (!executable.contains(QLatin1Char('/')) && !executable.contains(QLatin1Char('\\'))) executable = QStandardPaths::findExecutable(executable);
    if (executable.isEmpty()) return Text("The signer executable was not found in PATH.");
    const QFileInfo info{executable};
    if (!info.exists()) return QCoreApplication::translate("SettingsModel", "The configured signer path does not exist.");
    if (!info.isFile()) return QCoreApplication::translate("SettingsModel", "The configured signer path is not a file.");
    if (!info.isExecutable()) return QCoreApplication::translate("SettingsModel", "The configured signer path is not executable.");
    return {};
}
} // namespace signerdiscovery
