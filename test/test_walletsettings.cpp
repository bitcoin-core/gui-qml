// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/walletidentity.h>
#include <qml/walletsettings.h>
#include <qml/walletreservationdata.h>
#include <test/mocks/mockwallet.h>
#include <test/qt_test_registry.h>

#include <QSettings>
#include <QScopeGuard>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest/QtTest>
#include <set>
#include <stdexcept>

class WalletSettingsTests : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void clearingOneAliasPreservesAnotherWallet_data()
    {
        QTest::addColumn<QString>("parent");
        QTest::addColumn<QString>("child");
        QTest::newRow("nested-address") << QStringLiteral("Ledger") << QStringLiteral("Ledger/address");
        QTest::newRow("nested-invoices") << QStringLiteral("Ledger") << QStringLiteral("Ledger/invoices");
        QTest::newRow("default-wallet") << QString{} << QStringLiteral("Savings");
        QTest::newRow("independent-control") << QStringLiteral("Ledger") << QStringLiteral("Savings");
    }
    void clearingOneAliasPreservesAnotherWallet()
    {
        QFETCH(QString, parent);
        QFETCH(QString, child);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QSettings settings{directory.filePath("wallets.ini"), QSettings::IniFormat};
        walletidentity::WriteAlias(settings, parent, QStringLiteral("First alias"));
        walletidentity::WriteAlias(settings, child, QStringLiteral("Second alias"));
        settings.sync();
        QCOMPARE(settings.status(), QSettings::NoError);
        QCOMPARE(walletidentity::ReadAlias(settings, child), QStringLiteral("Second alias"));

        walletidentity::WriteAlias(settings, parent, {});
        settings.sync();
        QCOMPARE(settings.status(), QSettings::NoError);
        QVERIFY(walletidentity::ReadAlias(settings, parent).isEmpty());
        QCOMPARE(walletidentity::ReadAlias(settings, child), QStringLiteral("Second alias"));
        const auto aliases = walletidentity::ReadAliases(settings);
        const auto found = aliases.find(child);
        QVERIFY(found != aliases.end());
        QCOMPARE(found->second, QStringLiteral("Second alias"));
    }
    void encodedIdentitiesStayDistinctAfterLegacyDeletion()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QStringList identities{{}, "Ledger", "ledger", "Ledger/address", "Ledger%2faddress", " Ledger", "Ledger ", QString::fromUtf8("Caf\xc3\xa9"), QString::fromUtf8("Cafe\xcc\x81")};
        std::set<QString> keys;
        {
            QSettings settings{directory.filePath("wallets.ini"), QSettings::IniFormat};
            for (qsizetype i = 0; i < identities.size(); ++i) {
                const auto key = walletsettings::EntryKey(walletsettings::Area::Aliases, identities[i]);
                QVERIFY(keys.insert(key).second);
                QVERIFY(!key.mid(QStringLiteral("walletSettings/v2/aliases/").size()).contains('/'));
                walletidentity::WriteAlias(settings, identities[i], QStringLiteral("Display %1").arg(i));
            }
            QCOMPARE(walletsettings::EntryKey(walletsettings::Area::Aliases, "Ledger/address"),
                QStringLiteral("walletSettings/v2/aliases/w4c65646765722f61646472657373"));
            settings.sync();
            QCOMPARE(settings.status(), QSettings::NoError);
            settings.remove("walletDisplayNames"); // An older writer's recursive deletion.
            settings.sync();
        }
        QSettings reopened{directory.filePath("wallets.ini"), QSettings::IniFormat};
        const auto aliases = walletidentity::ReadAliases(reopened);
        for (qsizetype i = 0; i < identities.size(); ++i) {
            QCOMPARE(walletidentity::ReadAlias(reopened, identities[i]), QStringLiteral("Display %1").arg(i));
            QCOMPARE(aliases.at(identities[i]), QStringLiteral("Display %1").arg(i));
        }
    }
    void legacyDefaultAliasKeepsItsIndependentChildren()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QSettings settings{directory.filePath("wallets.ini"), QSettings::IniFormat};
        settings.setValue("walletDisplayNames", "Default alias");
        settings.setValue("walletDisplayNames/Child", "Child alias");
        QCOMPARE(walletidentity::ReadAlias(settings, {}), QStringLiteral("Default alias"));
        QCOMPARE(walletidentity::ReadAliases(settings).at(QString{}), QStringLiteral("Default alias"));
        walletidentity::WriteAlias(settings, {}, "Renamed default");
        QCOMPARE(walletidentity::ReadAliases(settings).at(QString{}), QStringLiteral("Renamed default"));
        QCOMPARE(walletidentity::ReadAlias(settings, "Child"), QStringLiteral("Child alias"));
    }
    void aliasMigrationInterruption_data()
    {
        QTest::addColumn<int>("stage");
        QTest::addColumn<QString>("expected");
        QTest::newRow("before-v2-commit") << 0 << QStringLiteral("Legacy");
        QTest::newRow("committed-before-legacy-mirror") << 1 << QStringLiteral("Current");
        QTest::newRow("committed-clear-before-legacy-mirror") << 2 << QString{};
        QTest::newRow("completed-compatibility-mirror") << 3 << QStringLiteral("Current");
    }
    void aliasMigrationInterruption()
    {
        QFETCH(int, stage);
        QFETCH(QString, expected);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        {
            QSettings fixture{directory.filePath("wallets.ini"), QSettings::IniFormat};
            fixture.setValue("walletDisplayNames/Ledger", "Legacy");
            fixture.setValue("walletDisplayNames/Ledger/address", "Independent");
            if (stage) {
                // Golden persisted fixture, independent of the writer/codec.
                fixture.setValue("walletSettings/v2/aliases/w4c6564676572", QVariantMap{
                    {"version", 2}, {"identity", "Ledger"}, {"payload", QVariantMap{{"alias", stage == 2 ? QString{} : QStringLiteral("Current")}}}});
            }
            if (stage == 3) fixture.setValue("walletDisplayNames/Ledger", "Current");
            fixture.sync();
            QCOMPARE(fixture.status(), QSettings::NoError);
        }
        QSettings reopened{directory.filePath("wallets.ini"), QSettings::IniFormat};
        QCOMPARE(walletidentity::ReadAlias(reopened, "Ledger"), expected);
        QCOMPARE(walletidentity::ReadAlias(reopened, "Ledger/address"), QStringLiteral("Independent"));
        QCOMPARE(reopened.contains("walletSettings/v2/aliases/w4c6564676572"), stage != 0);
        walletidentity::WriteAlias(reopened, "Ledger", expected);
        reopened.sync();
        QCOMPARE(reopened.status(), QSettings::NoError);
        QCOMPARE(reopened.value("walletDisplayNames/Ledger").toString(), expected);
        QCOMPARE(reopened.value("walletDisplayNames/Ledger/address").toString(), QStringLiteral("Independent"));
    }
    void incompleteAliasNeverFallsBackToStaleLegacy_data()
    {
        QTest::addColumn<int>("corruption");
        QTest::newRow("unsupported-version") << 0;
        QTest::newRow("different-identity") << 1;
        QTest::newRow("missing-value") << 2;
    }
    void incompleteAliasNeverFallsBackToStaleLegacy()
    {
        QFETCH(int, corruption);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QSettings settings{directory.filePath("wallets.ini"), QSettings::IniFormat};
        settings.setValue("walletDisplayNames/Ledger", "Stale");
        settings.setValue("walletSettings/v2/aliases/w4c6564676572", QVariantMap{
            {"version", corruption == 0 ? 3 : 2}, {"identity", corruption == 1 ? "Another" : "Ledger"},
            {"payload", corruption == 2 ? QVariantMap{} : QVariantMap{{"alias", "Current"}}}});
#if QT_VERSION >= QT_VERSION_CHECK(6, 3, 0)
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, walletidentity::ReadAlias(settings, "Ledger"));
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, walletidentity::ReadAliases(settings));
#else
        QVERIFY_EXCEPTION_THROWN(walletidentity::ReadAlias(settings, "Ledger"), std::runtime_error);
        QVERIFY_EXCEPTION_THROWN(walletidentity::ReadAliases(settings), std::runtime_error);
#endif
        QVERIFY(settings.group().isEmpty());
    }
    void aliasWriteFailureKeepsTheSettingsFailureVisible()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QSettings settings{directory.path(), QSettings::IniFormat}; // A directory cannot be an INI file.
        walletidentity::WriteAlias(settings, "Ledger", "Savings");
        settings.sync();
        QVERIFY(settings.status() != QSettings::NoError);
    }
    void reservationMigrationUsesCapturedCanonicalIdentity_data()
    {
        QTest::addColumn<int>("backend_format");
        QTest::newRow("ini-control") << int(QSettings::IniFormat);
        QTest::newRow("native-settings") << int(QSettings::NativeFormat);
    }
    void reservationMigrationUsesCapturedCanonicalIdentity()
    {
        QFETCH(int, backend_format);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto format = QSettings::defaultFormat();
        const auto organization = QCoreApplication::organizationName();
        const auto domain = QCoreApplication::organizationDomain();
        const auto application = QCoreApplication::applicationName();
        QString file;
        const auto restore = qScopeGuard([&] {
            if (backend_format == int(QSettings::NativeFormat)) {
                QSettings cleanup;
                cleanup.clear(); cleanup.sync();
            }
            if (!file.isEmpty()) QFile::remove(file);
            QSettings::setDefaultFormat(format);
            QCoreApplication::setOrganizationName(organization);
            QCoreApplication::setOrganizationDomain(domain);
            QCoreApplication::setApplicationName(application);
        });
        QSettings::setDefaultFormat(static_cast<QSettings::Format>(backend_format));
        QCoreApplication::setOrganizationName("BitcoinCoreGuiQmlR14");
        QCoreApplication::setOrganizationDomain("r42-reservations-" + QFileInfo(directory.path()).fileName() + ".invalid");
        QCoreApplication::setApplicationName("migration-" + QFileInfo(directory.path()).fileName());
        struct NamedWallet final : StubWallet { std::string getWalletName() override { return "./Ledger"; } } wallet;
        const auto storage = walletrequests::SettingsReservationStorage(QStringLiteral("Ledger"));
        QSettings settings;
        file = settings.fileName();
        settings.setValue("receiveAddressTypes/./Ledger/address", "legacy-address");
        settings.setValue("receiveAddressTypes/./Ledger/retired/paid-address", true);
        settings.sync();
        QCOMPARE(settings.status(), QSettings::NoError);
        QCOMPARE(storage.read(wallet, {}).address, QStringLiteral("legacy-address"));
        QVERIFY(storage.read(wallet, "paid-address").retired);
        QVERIFY(!settings.contains("walletSettings/v2/reservations/w4c6564676572"));
        QVERIFY(storage.write(wallet, "new-address"));
        settings.sync();
        QVERIFY(settings.contains("walletSettings/v2/reservations/w4c6564676572"));
        QCOMPARE(settings.value("receiveAddressTypes/./Ledger/address").toString(), QStringLiteral("new-address"));
        const auto persisted = settings.value("walletSettings/v2/reservations/w4c6564676572").toMap();
        const auto persisted_version = persisted.value("version");
        const auto persisted_retired = persisted.value("payload").toMap().value("retired");
        const QString native_types = QStringLiteral("format=%1 version=%2 retired=%3")
            .arg(backend_format).arg(QString::fromLatin1(persisted_version.metaType().name()))
            .arg(QString::fromLatin1(persisted_retired.metaType().name()));
        qInfo().noquote() << native_types;
        const auto paid_after_write = storage.read(wallet, "paid-address");
        QVERIFY2(paid_after_write.ok, qPrintable(native_types));
        QVERIFY(paid_after_write.retired);
        // Native settings may widen an integer and return a property-list
        // array as QVariantList. Preserve exact version/value semantics.
        for (const QVariant& version : {QVariant{2}, QVariant::fromValue<qlonglong>(2)}) {
            auto native_shape = persisted;
            native_shape["version"] = version;
            auto payload = native_shape.value("payload").toMap();
            payload["retired"] = QVariantList{QStringLiteral("paid-address")};
            native_shape["payload"] = payload;
            settings.setValue("walletSettings/v2/reservations/w4c6564676572", native_shape);
            settings.sync();
            const auto retained = storage.read(wallet, "paid-address");
            QVERIFY(retained.ok); QVERIFY(retained.retired);
            QCOMPARE(storage.read(wallet, {}).address, QStringLiteral("new-address"));
        }
        for (const QVariant& invalid_version : {QVariant{QStringLiteral("2")}, QVariant{2.0}, QVariant{true}}) {
            auto invalid = persisted; invalid["version"] = invalid_version;
            settings.setValue("walletSettings/v2/reservations/w4c6564676572", invalid);
            settings.sync();
            QVERIFY(!storage.read(wallet, {}).ok);
        }
        for (const QVariant& invalid_retired : {QVariant{QVariantList{QStringLiteral("paid-address"), 7}}, QVariant{QStringLiteral("paid-address")}}) {
            auto invalid = persisted; auto payload = invalid.value("payload").toMap();
            payload["retired"] = invalid_retired; invalid["payload"] = payload;
            settings.setValue("walletSettings/v2/reservations/w4c6564676572", invalid);
            settings.sync();
            QVERIFY(!storage.read(wallet, {}).ok);
        }
        settings.setValue("walletSettings/v2/reservations/w4c6564676572", persisted);
        settings.sync();
        settings.setValue("receiveAddressTypes/./Ledger/address", "stale-old-writer");
        settings.sync();
        QCOMPARE(storage.read(wallet, {}).address, QStringLiteral("new-address"));
        QVERIFY(storage.clear(wallet, "another-address"));
        QCOMPARE(storage.read(wallet, {}).address, QStringLiteral("new-address"));
        QVERIFY(storage.clear(wallet, "new-address"));
        QVERIFY(storage.read(wallet, {}).address.isEmpty());
        QVERIFY(storage.read(wallet, "paid-address").retired);
        settings.sync();
        QVERIFY(settings.value("receiveAddressTypes/./Ledger/address").toString().isEmpty());
        const auto committed = settings.value("walletSettings/v2/reservations/w4c6564676572");
        auto corrupt = committed.toMap(); corrupt["version"] = 99;
        settings.setValue("walletSettings/v2/reservations/w4c6564676572", corrupt);
        settings.sync();
        QVERIFY(!storage.read(wallet, {}).ok);
        QVERIFY(!storage.write(wallet, "do-not-overwrite"));
        settings.sync();
        QCOMPARE(settings.value("walletSettings/v2/reservations/w4c6564676572").toMap(), corrupt);
    }
};

#include <test_walletsettings.moc>
BITCOINQML_REGISTER_QT_TEST(WalletSettingsTests)
