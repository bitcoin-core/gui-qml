// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/walletfiledestination.h>

#include <QtTest/QtTest>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QUrl>

namespace {

const QDateTime NOW{QDate{2026, 11, 11}, QTime{14, 32}};

void WriteFile(const QString& path, const QByteArray& data)
{
    QFile file{path};
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(data), data.size());
}

QByteArray SqliteHeader(const QByteArray& application_id)
{
    QByteArray data{QByteArray("SQLite format 3\0", 16)};
    data.append(QByteArray(68 - data.size(), '\0'));
    data.append(application_id);
    data.append(QByteArray(512 - data.size(), '\0'));
    return data;
}

} // namespace

class WalletFileDestinationTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void defaultNamesCarryTypeAndDate();
    void keepBothReplacesAnEarlierStamp();
    void walletDatabasesOfAnyNetworkAreRecognized();
    void missingFileIsAvailable();
    void existingFilesOfferKeepBoth();
    void folderAndBareNameGetCompleted();
    void walletsFolderAndMissingFolderAreBlocked();
};

void WalletFileDestinationTests::defaultNamesCarryTypeAndDate()
{
    const QDate date{2026, 11, 11};
    QCOMPARE(DefaultWalletFileName(QStringLiteral("savings"), false, date), QStringLiteral("savings-2026-11-11.bak"));
    QCOMPARE(DefaultWalletFileName(QStringLiteral("savings"), true, date), QStringLiteral("savings-watchonly-2026-11-11.dat"));
    QCOMPARE(DefaultWalletFileName(QString{}, false, date), QStringLiteral("wallet-2026-11-11.bak"));
    QCOMPARE(DefaultWalletFileName(QStringLiteral("/home/me/wallets/cold/"), false, date), QStringLiteral("cold-2026-11-11.bak"));
    QCOMPARE(DefaultWalletFileName(QStringLiteral("a:b?"), false, date), QStringLiteral("a_b_-2026-11-11.bak"));
}

void WalletFileDestinationTests::keepBothReplacesAnEarlierStamp()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString existing{dir.filePath(QStringLiteral("wallet-2026-11-10.bak"))};
    WriteFile(existing, "old");

    QCOMPARE(KeepBothPath(existing, NOW), dir.filePath(QStringLiteral("wallet-2026-11-11-1432.bak")));
    WriteFile(dir.filePath(QStringLiteral("wallet-2026-11-11-1432.bak")), "taken");
    QCOMPARE(KeepBothPath(existing, NOW), dir.filePath(QStringLiteral("wallet-2026-11-11-1432-2.bak")));
    QCOMPARE(KeepBothPath(dir.filePath(QStringLiteral("mine.dat")), NOW), dir.filePath(QStringLiteral("mine-2026-11-11-1432.dat")));
}

void WalletFileDestinationTests::walletDatabasesOfAnyNetworkAreRecognized()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString mainnet{dir.filePath(QStringLiteral("mainnet.bak"))};
    WriteFile(mainnet, SqliteHeader(QByteArray("\xf9\xbe\xb4\xd9", 4)));
    QVERIFY(IsWalletDatabaseFile(mainnet));

    const QString plain_sqlite{dir.filePath(QStringLiteral("other.db"))};
    WriteFile(plain_sqlite, SqliteHeader(QByteArray(4, '\0')));
    QVERIFY(!IsWalletDatabaseFile(plain_sqlite));

    QByteArray bdb(4096, '\0');
    bdb.replace(12, 4, QByteArray("\x62\x31\x05\x00", 4));
    const QString legacy{dir.filePath(QStringLiteral("legacy.dat"))};
    WriteFile(legacy, bdb);
    QVERIFY(IsWalletDatabaseFile(legacy));

    const QString text{dir.filePath(QStringLiteral("notes.bak"))};
    WriteFile(text, "not a wallet");
    QVERIFY(!IsWalletDatabaseFile(text));
    QVERIFY(!IsWalletDatabaseFile(dir.filePath(QStringLiteral("missing.bak"))));
}

void WalletFileDestinationTests::missingFileIsAvailable()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path{dir.filePath(QStringLiteral("wallet.bak"))};
    const QVariantMap result{DescribeWalletFileDestination(QUrl::fromLocalFile(path).toString(), false, {}, {}, NOW)};
    QCOMPARE(result.value("status").toString(), QStringLiteral("available"));
    QCOMPARE(result.value("path").toString(), QDir::toNativeSeparators(path));
    QCOMPARE(result.value("fileName").toString(), QStringLiteral("wallet.bak"));
    QVERIFY(!result.contains("keepBothPath"));
}

void WalletFileDestinationTests::existingFilesOfferKeepBoth()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString wallet{dir.filePath(QStringLiteral("wallet-2026-11-11.bak"))};
    WriteFile(wallet, SqliteHeader(QByteArray("\xfa\xbf\xb5\xda", 4)));
    QVariantMap result{DescribeWalletFileDestination(wallet, true, {}, {}, NOW)};
    QCOMPARE(result.value("status").toString(), QStringLiteral("walletFile"));
    QCOMPARE(result.value("folder").toString(), QDir::toNativeSeparators(dir.path()));
    QCOMPARE(result.value("keepBothFileName").toString(), QStringLiteral("wallet-2026-11-11-1432.bak"));
    QCOMPARE(result.value("keepBothPath").toString(), QDir::toNativeSeparators(dir.filePath(QStringLiteral("wallet-2026-11-11-1432.bak"))));

    const QString other{dir.filePath(QStringLiteral("notes.dat"))};
    WriteFile(other, "notes");
    result = DescribeWalletFileDestination(other, true, {}, {}, NOW);
    QCOMPARE(result.value("status").toString(), QStringLiteral("otherFile"));
    QCOMPARE(result.value("keepBothFileName").toString(), QStringLiteral("notes-2026-11-11-1432.dat"));
}

void WalletFileDestinationTests::folderAndBareNameGetCompleted()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVariantMap result{DescribeWalletFileDestination(dir.path(), true, QStringLiteral("savings"), {}, NOW)};
    QCOMPARE(result.value("status").toString(), QStringLiteral("available"));
    QCOMPARE(result.value("fileName").toString(), QStringLiteral("savings-watchonly-2026-11-11.dat"));

    result = DescribeWalletFileDestination(dir.filePath(QStringLiteral("copy")), false, {}, {}, NOW);
    QCOMPARE(result.value("fileName").toString(), QStringLiteral("copy.bak"));
    result = DescribeWalletFileDestination(dir.filePath(QStringLiteral("copy.DAT")), false, {}, {}, NOW);
    QCOMPARE(result.value("fileName").toString(), QStringLiteral("copy.DAT"));
}

void WalletFileDestinationTests::walletsFolderAndMissingFolderAreBlocked()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString wallets{dir.filePath(QStringLiteral("wallets"))};
    QVERIFY(QDir{}.mkpath(QDir{wallets}.filePath(QStringLiteral("savings"))));
    const QStringList protected_paths{wallets};

    const QStringList blocked{QDir{wallets}.filePath(QStringLiteral("savings/wallet.dat")),
                              QDir{wallets}.filePath(QStringLiteral("copy.bak")),
                              wallets + QStringLiteral("/savings/../copy.bak")};
    for (const QString& path : blocked) {
        const QVariantMap result{DescribeWalletFileDestination(path, false, {}, protected_paths, NOW)};
        QCOMPARE(result.value("status").toString(), QStringLiteral("blocked"));
        QVERIFY(result.value("error").toString().contains(QStringLiteral("wallets folder")));
    }

    // A sibling that only shares the prefix is fine.
    const QVariantMap sibling{DescribeWalletFileDestination(wallets + QStringLiteral("-backup.bak"), false, {}, protected_paths, NOW)};
    QCOMPARE(sibling.value("status").toString(), QStringLiteral("available"));

    const QVariantMap missing{DescribeWalletFileDestination(dir.filePath(QStringLiteral("nope/copy.bak")), false, {}, {}, NOW)};
    QCOMPARE(missing.value("status").toString(), QStringLiteral("blocked"));
    QVERIFY(missing.value("error").toString().contains(QStringLiteral("does not exist")));
    QCOMPARE(DescribeWalletFileDestination(QStringLiteral("  "), false, {}, {}, NOW).value("status").toString(), QStringLiteral("blocked"));
}

#ifdef BITCOINQML_NO_TEST_MAIN
#include <test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(WalletFileDestinationTests)
#else
QTEST_MAIN(WalletFileDestinationTests)
#endif
#include "test_walletfiledestination.moc"
