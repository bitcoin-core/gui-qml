// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/backuppolicyio.h>

#include <util/fs.h>
#include <util/fs_helpers.h>

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QUrl>

#include <algorithm>
#include <system_error>
#include <utility>

namespace {
std::string Utf8(const QString& value)
{
    const auto bytes = value.toUtf8();
    return {bytes.constData(), static_cast<size_t>(bytes.size())};
}
backup::Proposal ReadTarget(const QString& path)
{
    backup::Proposal result;
    result.path = Utf8(path);
    const QFileInfo info{path};
    if (info.isSymLink() || (info.exists() && !info.isFile())) {
        result.error = backup::Error::UnsupportedTarget;
        return result;
    }
    const QFileInfo parent{info.absolutePath()};
    if (!parent.exists() || !parent.isDir()) {
        result.error = backup::Error::Inspection;
        return result;
    }
    result.exists = info.exists();
    if (result.exists) {
        QFile file{path};
        if (!file.open(QIODevice::ReadOnly)) {
            result.error = backup::Error::Inspection;
            return result;
        }
        auto version = backup::ReadVersion(file);
        result.version = std::move(version.digest);
        result.error = version.error;
    }
    return result;
}
} // namespace

backup::FileVersion backup::ReadVersion(QIODevice& device)
{
    QCryptographicHash hash{QCryptographicHash::Sha256};
    if (!hash.addData(&device)) return {{}, Error::Inspection};
    const auto bytes = hash.result();
    return {{bytes.constData(), static_cast<size_t>(bytes.size())}, Error::None};
}

namespace {
backup::Proposal InspectTarget(const std::string& raw, const std::string& suggested_name,
                               const QString& suffix, bool allow_dat)
{
    if (raw.empty() || raw.size() > 32'768 || raw.find('\0') != std::string::npos) return {{}, {}, false, false, backup::Error::InvalidPath};
    QString path = QString::fromUtf8(raw.data(), static_cast<qsizetype>(raw.size()));
    if (path.trimmed().isEmpty()) return {{}, {}, false, false, backup::Error::InvalidPath};
    if (path.startsWith(QStringLiteral("file:"), Qt::CaseInsensitive)) {
        const QUrl url{path, QUrl::TolerantMode};
        if (!url.isValid() || !QDir::isAbsolutePath(url.toLocalFile())) return {{}, {}, false, false, backup::Error::InvalidPath};
        path = url.toLocalFile();
    } else if (path.contains(QStringLiteral("://"))) {
        return {{}, {}, false, false, backup::Error::InvalidPath};
    }
    if (path.contains(QChar{0})) return {{}, {}, false, false, backup::Error::InvalidPath};
    path = QDir::cleanPath(QFileInfo{path}.absoluteFilePath());
    const QString initial{path};
    const QFileInfo selected{path};
    const bool directory = selected.isDir();
    if (directory) {
        QString name = QString::fromUtf8(suggested_name.data(), static_cast<qsizetype>(suggested_name.size()));
        name.replace('/', '_');
        name.replace('\\', '_');
        if (name.isEmpty()) name = QStringLiteral("wallet");
        path = QDir{path}.filePath(name + suffix);
    } else if (!path.endsWith(suffix, Qt::CaseInsensitive)
               && !(allow_dat && path.endsWith(QStringLiteral(".dat"), Qt::CaseInsensitive))) {
        path += suffix;
    }
    auto result = ReadTarget(path);
    // A rewritten file name must itself be approved even when it is new.
    // Directory automation retains its documented wallet-name suggestion.
    result.renamed = !directory && path != initial;
    return result;
}
} // namespace

backup::Proposal backup::Inspect(const std::string& raw, const std::string& wallet_name)
{
    return InspectTarget(raw, wallet_name, QStringLiteral(".bak"), true);
}

backup::Proposal backup::Inspect(const std::string& raw, const std::string& suggested_name, const std::string& suffix)
{
    if (suffix.size() < 2 || suffix.size() > 16 || suffix.front() != '.' ||
        !std::all_of(suffix.begin() + 1, suffix.end(), [](unsigned char value) {
            return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') || (value >= '0' && value <= '9');
        })) return {{}, {}, false, false, Error::InvalidPath};
    return InspectTarget(raw, suggested_name, QString::fromLatin1(suffix.data(), static_cast<qsizetype>(suffix.size())), false);
}

backup::Proposal backup::InspectPng(const std::string& raw)
{
    if (raw.empty() || raw.size() > 32'768 || raw.find('\0') != std::string::npos) return {{}, {}, false, false, Error::InvalidPath};
    const QUrl url{QString::fromUtf8(raw.data(), static_cast<qsizetype>(raw.size())), QUrl::StrictMode};
    if (!url.isValid() || !url.isLocalFile() || !QDir::isAbsolutePath(url.toLocalFile())) return {{}, {}, false, false, Error::InvalidPath};
    QString path = QDir::cleanPath(url.toLocalFile());
    if (path.contains(QChar{0}) || QFileInfo{path}.isDir()) return {{}, {}, false, false, Error::InvalidPath};
    const QString selected = path;
    if (!path.endsWith(QStringLiteral(".png"), Qt::CaseInsensitive)) path += QStringLiteral(".png");
    auto result = ReadTarget(path); result.renamed = path != selected; return result;
}

namespace {
bool IsWalletFile(const std::string& path, const backup::InstallCoordinator& coordinator)
{
    if (!coordinator.wallet_files) return false;
    const fs::path destination{fs::PathFromString(path)};
    std::error_code error;
    for (const auto& file : coordinator.wallet_files()) {
        const fs::path database{fs::PathFromString(file)};
        // SQLite keeps a journal next to the database while it writes.
        for (const fs::path& candidate : {database, fs::PathFromString(file + "-journal")}) {
            if (fs::equivalent(destination, candidate, error)) return true;
        }
    }
    return false;
}
} // namespace

backup::Proposal backup::Screen(Proposal proposal, const InstallCoordinator& coordinator)
{
    if (proposal.error == Error::None && proposal.exists && IsWalletFile(proposal.path, coordinator)) {
        proposal.error = Error::WalletFile;
    }
    return proposal;
}

backup::WriteResult backup::Write(const Proposal& approved, bool destination_approved, const Producer& producer,
                                  InstallCoordinator& coordinator, std::function<bool()> submitted)
{
    if (approved.error != Error::None || approved.path.empty() || ((approved.exists || approved.renamed) && !destination_approved)) return {Error::Changed};
    const QString final_path = QString::fromUtf8(approved.path.data(), static_cast<qsizetype>(approved.path.size()));
    const QString parent = QFileInfo{final_path}.absolutePath();
    QTemporaryDir staging{QDir{parent}.filePath(QStringLiteral(".bitcoin-backup-XXXXXX"))};
    if (!staging.isValid()) return {Error::Staging};
    const QString staged_path = staging.filePath(QStringLiteral("wallet.backup"));
    if (!producer(Utf8(staged_path))) return {Error::CoreBackup};
    const QFileInfo staged_info{staged_path};
    if (!staged_info.isFile() || staged_info.isSymLink()) return {Error::CoreBackup};
    // Serialize the final check/install across application wallets. Core's
    // wallet lock has already been released by its backup call at this point.
    const std::lock_guard lock{coordinator.mutex};
    const auto current = Screen(ReadTarget(final_path), coordinator);
    if (current.error != Error::None) return {current.error};
    if (current.exists != approved.exists || current.version != approved.version) return {Error::Changed};
    // Rendering/staging is not external-effect admission. Request sharing can
    // revoke a source during those stages without modifying the final file.
    if (submitted && !submitted()) return {Error::Changed};
    bool installed{false};
    if (approved.exists) {
        installed = RenameOver(fs::PathFromString(Utf8(staged_path)), fs::PathFromString(approved.path));
    } else {
        // QFile's non-replacing rename also protects against a destination
        // created after inspection; never use an overwriting rename here.
        installed = QFile::rename(staged_path, final_path);
    }
    if (!installed) return {Error::Install};
    DirectoryCommit(fs::PathFromString(Utf8(parent)));
    return {};
}
