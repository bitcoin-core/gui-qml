// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/walletfiledestination.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QUrl>

namespace {

QString Tr(const char* text)
{
    return QCoreApplication::translate("WalletFileDestination", text);
}

QString CleanPath(const QString& path)
{
    const QFileInfo info{path};
    const QString canonical{info.canonicalFilePath()};
    return canonical.isEmpty() ? QDir::cleanPath(info.absoluteFilePath()) : canonical;
}

bool IsSameOrInside(const QString& path, const QString& root)
{
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
    constexpr Qt::CaseSensitivity sensitivity{Qt::CaseInsensitive};
#else
    constexpr Qt::CaseSensitivity sensitivity{Qt::CaseSensitive};
#endif
    if (root.isEmpty()) return false;
    if (path.compare(root, sensitivity) == 0) return true;
    const QString prefix{root.endsWith('/') ? root : root + '/'};
    return path.startsWith(prefix, sensitivity);
}

QVariantMap Blocked(const QString& error)
{
    return {{QStringLiteral("status"), QStringLiteral("blocked")}, {QStringLiteral("error"), error}};
}

} // namespace

bool IsWalletDatabaseFile(const QString& path)
{
    QFile file{path};
    if (!file.open(QIODevice::ReadOnly)) return false;
    const QByteArray header{file.read(72)};

    if (header.startsWith(QByteArray("SQLite format 3\0", 16))) {
        // Core stores the network magic as the application id.
        return header.size() >= 72 && header.mid(68, 4) != QByteArray(4, '\0');
    }

    // Berkeley DB btree magic, in either byte order, as Core's IsBDBFile.
    if (file.size() < 4096 || header.size() < 16) return false;
    const QByteArray magic{header.mid(12, 4)};
    return magic == QByteArray("\x00\x05\x31\x62", 4) || magic == QByteArray("\x62\x31\x05\x00", 4);
}

QString DefaultWalletFileName(const QString& wallet_name, bool watch_only, const QDate& date)
{
    static const QRegularExpression separators{QStringLiteral("[\\\\/]")};
    static const QRegularExpression reserved{QStringLiteral("[:*?\"<>|]")};
    const QStringList parts{wallet_name.split(separators, Qt::SkipEmptyParts)};
    QString name{parts.isEmpty() ? QString{} : parts.last().trimmed()};
    name.replace(reserved, QStringLiteral("_"));
    if (name.isEmpty()) name = QStringLiteral("wallet");

    const QString stamp{date.toString(QStringLiteral("yyyy-MM-dd"))};
    return watch_only
        ? QStringLiteral("%1-watchonly-%2.dat").arg(name, stamp)
        : QStringLiteral("%1-%2.bak").arg(name, stamp);
}

QString KeepBothPath(const QString& path, const QDateTime& now)
{
    // A stamp left by an earlier save is replaced, not appended to.
    static const QRegularExpression old_stamp{QStringLiteral("-\\d{4}-\\d{2}-\\d{2}(-\\d{4})?(-\\d+)?$")};
    const QFileInfo info{path};
    QString base{info.completeBaseName()};
    base.remove(old_stamp);
    if (base.isEmpty()) base = QStringLiteral("wallet");
    const QString stem{base + '-' + now.toString(QStringLiteral("yyyy-MM-dd-HHmm"))};
    const QString suffix{info.suffix().isEmpty() ? QString{} : '.' + info.suffix()};
    const QDir dir{info.absoluteDir()};

    for (int count{1};; ++count) {
        const QString name{count == 1 ? stem + suffix : QStringLiteral("%1-%2%3").arg(stem).arg(count).arg(suffix)};
        const QString candidate{dir.filePath(name)};
        if (!QFileInfo::exists(candidate)) return candidate;
    }
}

QVariantMap DescribeWalletFileDestination(const QString& path,
                                          bool watch_only,
                                          const QString& wallet_name,
                                          const QStringList& protected_paths,
                                          const QDateTime& now)
{
    const QString trimmed{path.trimmed()};
    if (trimmed.isEmpty()) return Blocked(Tr("Choose where to save the file."));

    const QUrl url{trimmed};
    QString local{QFileInfo{url.isLocalFile() ? url.toLocalFile() : trimmed}.absoluteFilePath()};
    if (QFileInfo{local}.isDir()) {
        local = QDir{local}.filePath(DefaultWalletFileName(wallet_name, watch_only, now.date()));
    } else {
        static const QRegularExpression known_suffix{QStringLiteral("\\.(bak|dat)$"),
                                                     QRegularExpression::CaseInsensitiveOption};
        if (!known_suffix.match(local).hasMatch()) local += watch_only ? QStringLiteral(".dat") : QStringLiteral(".bak");
    }

    const QFileInfo info{local};
    const QString folder{QDir::toNativeSeparators(info.absolutePath())};
    if (!QFileInfo{info.absolutePath()}.isDir()) {
        //: %1 is a folder path.
        return Blocked(Tr("The folder %1 does not exist.").arg(folder));
    }
    if (info.isDir()) return Blocked(Tr("A folder with this name already exists."));

    const QString resolved{CleanPath(info.absolutePath()) + '/' + info.fileName()};
    for (const QString& protected_path : protected_paths) {
        if (!protected_path.isEmpty() && IsSameOrInside(resolved, CleanPath(protected_path))) {
            return Blocked(Tr("Wallet files can't be saved in the wallets folder. Choose another location."));
        }
    }

    QVariantMap result{
        {QStringLiteral("path"), QDir::toNativeSeparators(local)},
        {QStringLiteral("folder"), folder},
        {QStringLiteral("fileName"), info.fileName()},
        {QStringLiteral("error"), QString{}},
    };
    if (!info.exists()) {
        result.insert(QStringLiteral("status"), QStringLiteral("available"));
        return result;
    }
    const QFileInfo keep_both{KeepBothPath(local, now)};
    result.insert(QStringLiteral("status"), IsWalletDatabaseFile(local) ? QStringLiteral("walletFile") : QStringLiteral("otherFile"));
    result.insert(QStringLiteral("keepBothPath"), QDir::toNativeSeparators(keep_both.absoluteFilePath()));
    result.insert(QStringLiteral("keepBothFileName"), keep_both.fileName());
    return result;
}
