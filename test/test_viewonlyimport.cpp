// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/viewonlyimport.h>

#include <chainparams.h>
#include <script/descriptor.h>

#include <QtTest/QtTest>

#include <QStringList>

namespace {

// BIP32 test vector 1.
const QString XPUB{"xpub661MyMwAqRbcFtXgS5sYJABqqG9YLmC4Q1Rdap9gSE8NqtwybGhePY2gZ29ESFjqJoCu1Rupje8YtGqsefD265TMg7usUDFdp6W1EGMcet8"};
const QString XPRV{"xprv9s21ZrQH143K3QTDL4LXw2F7HEK3wJUD2nW2nRk4stbPy6cq3jPPqjiChkVvvNKmPGJxWUtg6LnF5kejMRNNU3TGtRBeJgk33yuGBxrMPHi"};
// The same key with SLIP-132 versions.
const QString YPUB{"ypub6QqdH2c5z7967BioGSfAWFHM1EHzHPBZK7wrND3ZpEWFtzmCqvsD1bgpaE6pSAPkiSKhkuWPCJV6mZTSNMd2tK8xYTcJ48585pZecmSUzWp"};
const QString ZPUB{"zpub6jftahH18ngZxUuv6oSniLNrBCSSE1B4EEU59bwTCEt8x6aS6b2mdfLxbS4QS53g85SWWP6wexqeer516433gYpZQoJie2tcMYdJ1SYYYAL"};
const QString MULTISIG_ZPUB{"Zpub6vZyhw1ShkEwP45J3TumYQietzUhSMreYW7k4sCza1iYaH9LrzR3inCtQ91szWGaMYWVNy74YBE9n1gmPHBzq2wEFGR83SMcFGuAbGkfiwg"};
const QString TPUB{"tpubD6NzVbkrYhZ4XgiXtGrdW5XDAPFCL9h7we1vwNCpn8tGbBcgfVYjXyhWo4E1xkh56hjod1RhGjxbaTLV3X4FyWuejifB9jusQ46QzG87VKp"};
const QString VPUB{"vpub5SLqN2bLY4WeZJ9SmNJHsyzqVKreTXD4ZnPC22MugDNcjhKX5xNX9QiQWcE4SSRzVWyHWUihpKRT7hckDGNzVc69wSX2JPcfGeNiT5c2XZy"};
const QString ZPRV{"zprvAWgYBBk7JR8GjzqSzmunMCS7dAbwpYTCs1YUMDXqduMA5JFHZ3iX5s2UkAR6vBdcCYYa1S5o1fVLrKsrnpCQ4WpUd6aVUWP1bS2Yy5DoaKv"};
const QString PUBKEY{"0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798"};

QString WithChecksum(const QString& descriptor)
{
    return descriptor + "#" + QString::fromStdString(GetDescriptorChecksum(descriptor.toStdString()));
}

QString Desc(const QString& script, const QString& path)
{
    return QStringLiteral("%1(%2/%3)").arg(script, XPUB, path);
}

} // namespace

class ViewOnlyImportTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void blankTextIsEmpty();
    void plainKeyAsksForItsAddressType();
    void slip132KeysCarryTheirScriptType();
    void keyOriginKeepsFingerprintAndPath();
    void privateKeysAreRejected();
    void descriptorLinesTakeTheChainFromThePath();
    void onlySingleKeyDescriptorsAreAccepted();
    void linesNeedARangedDescriptor();
    void listDescriptorsOutputKeepsItsMetadata();
    void importDescriptorsArrayMarksUndatedEntries();
    void malformedJsonIsRejected();
};

void ViewOnlyImportTests::initTestCase()
{
    SelectParams(ChainType::MAIN);
}

void ViewOnlyImportTests::blankTextIsEmpty()
{
    const ViewOnlyImport result{ParseViewOnlyImport(" \n\t ")};
    QCOMPARE(result.kind, ViewOnlyImport::Kind::Empty);
    QVERIFY(result.error.isEmpty());
    QCOMPARE(DescribeViewOnlyImport({}).value("status").toString(), QStringLiteral("empty"));
}

void ViewOnlyImportTests::plainKeyAsksForItsAddressType()
{
    const ViewOnlyImport waiting{ParseViewOnlyImport(" " + XPUB + "\n")};
    QCOMPARE(waiting.kind, ViewOnlyImport::Kind::ExtendedKey);
    QVERIFY(waiting.error.isEmpty());
    QVERIFY(waiting.needs_address_type);
    QVERIFY(waiting.requests.empty());

    QVariantMap description{DescribeViewOnlyImport(XPUB)};
    QCOMPARE(description.value("status").toString(), QStringLiteral("valid"));
    QVERIFY(description.value("needsAddressType").toBool());
    QVERIFY(description.value("needsBirthDate").toBool());
    QStringList ids;
    for (const QVariant& option : description.value("addressTypeOptions").toList()) ids.append(option.toMap().value("id").toString());
    QCOMPARE(ids, (QStringList{"bech32", "bech32m", "p2sh-segwit", "legacy"}));

    const ViewOnlyImport taproot{ParseViewOnlyImport(XPUB, QStringLiteral("bech32m"))};
    QVERIFY(taproot.error.isEmpty());
    QCOMPARE(taproot.requests.size(), size_t{1});
    QCOMPARE(QString::fromStdString(taproot.requests[0].descriptor), WithChecksum(Desc("tr", "<0;1>/*")));
    QVERIFY(taproot.requests[0].active);
    QVERIFY(!taproot.requests[0].internal.has_value());
    QCOMPARE(taproot.undated, std::vector<size_t>{0});

    description = DescribeViewOnlyImport(XPUB, QStringLiteral("bech32m"));
    QVERIFY(description.value("needsAddressType").toBool());
    QCOMPARE(description.value("descriptorCount").toInt(), 2);
    QCOMPARE(description.value("addressTypes").toStringList(), QStringList{QStringLiteral("Bech32m (Taproot)")});
    QVERIFY(!ParseViewOnlyImport(XPUB, QStringLiteral("p2wsh")).error.isEmpty());
}

void ViewOnlyImportTests::slip132KeysCarryTheirScriptType()
{
    const ViewOnlyImport zpub{ParseViewOnlyImport(ZPUB)};
    QVERIFY(!zpub.needs_address_type);
    QCOMPARE(QString::fromStdString(zpub.requests[0].descriptor), WithChecksum(Desc("wpkh", "<0;1>/*")));
    const ViewOnlyImport ypub{ParseViewOnlyImport(YPUB, QStringLiteral("bech32m"))};
    QCOMPARE(QString::fromStdString(ypub.requests[0].descriptor), WithChecksum(Desc("sh(wpkh", "<0;1>/*") + ")"));

    QVERIFY(ParseViewOnlyImport(MULTISIG_ZPUB).error.contains("multisig"));
    QVERIFY(ParseViewOnlyImport(ZPRV).error.contains("private key"));
    QVERIFY(ParseViewOnlyImport(VPUB).error.contains("different network"));
    QVERIFY(ParseViewOnlyImport(TPUB).error.contains("different network"));

    SelectParams(ChainType::REGTEST);
    const ViewOnlyImport vpub{ParseViewOnlyImport(VPUB)};
    SelectParams(ChainType::MAIN);
    QVERIFY(vpub.error.isEmpty());
    QCOMPARE(QString::fromStdString(vpub.requests[0].descriptor),
             QStringLiteral("wpkh(%1/<0;1>/*)#").arg(TPUB) + QString::fromStdString(GetDescriptorChecksum(QStringLiteral("wpkh(%1/<0;1>/*)").arg(TPUB).toStdString())));
}

void ViewOnlyImportTests::keyOriginKeepsFingerprintAndPath()
{
    const ViewOnlyImport taproot{ParseViewOnlyImport("[d34db33f/86h/0h/0h]" + XPUB)};
    QVERIFY(taproot.error.isEmpty());
    QVERIFY(!taproot.needs_address_type);
    QCOMPARE(QString::fromStdString(taproot.requests[0].descriptor),
             WithChecksum(QStringLiteral("tr([d34db33f/86h/0h/0h]%1/<0;1>/*)").arg(XPUB)));

    // The prefix wins over the path.
    const ViewOnlyImport zpub{ParseViewOnlyImport("[d34db33f/44'/0'/0']" + ZPUB)};
    QCOMPARE(QString::fromStdString(zpub.requests[0].descriptor),
             WithChecksum(QStringLiteral("wpkh([d34db33f/44'/0'/0']%1/<0;1>/*)").arg(XPUB)));

    QVERIFY(ParseViewOnlyImport("[d34db33f/0h]" + XPUB).needs_address_type);
}

void ViewOnlyImportTests::privateKeysAreRejected()
{
    QVERIFY(ParseViewOnlyImport(XPRV).error.contains("private key"));
    const QString descriptor{QStringLiteral("wpkh(%1/0/*)").arg(XPRV)};
    QVERIFY(ParseViewOnlyImport(descriptor).error.contains("private key"));
    QVERIFY(ParseViewOnlyImport(QStringLiteral(R"([{"desc": "%1", "timestamp": 0, "active": true}])").arg(descriptor)).error.contains("private key"));
}

void ViewOnlyImportTests::descriptorLinesTakeTheChainFromThePath()
{
    const QString checked{WithChecksum(Desc("pkh", "0/*"))};
    const ViewOnlyImport result{ParseViewOnlyImport(Desc("tr", "0/*") + "\n\n  " + Desc("tr", "1/*") + "  \n" + checked)};
    QVERIFY(result.error.isEmpty());
    QCOMPARE(result.kind, ViewOnlyImport::Kind::Descriptors);
    QCOMPARE(result.requests.size(), size_t{3});
    QCOMPARE(QString::fromStdString(result.requests[0].descriptor), WithChecksum(Desc("tr", "0/*")));
    QCOMPARE(QString::fromStdString(result.requests[2].descriptor), checked);
    QCOMPARE(result.requests[0].internal, std::optional<bool>{false});
    QCOMPARE(result.requests[1].internal, std::optional<bool>{true});
    QCOMPARE(result.requests[2].internal, std::optional<bool>{false});
    QVERIFY(std::ranges::all_of(result.requests, &wallet::ImportDescriptorRequest::active));
    QCOMPARE(result.undated, (std::vector<size_t>{0, 1, 2}));
    QCOMPARE(result.output_types, (std::vector<OutputType>{OutputType::BECH32M, OutputType::LEGACY}));

    QVERIFY(!ParseViewOnlyImport(Desc("wpkh", "0/*") + "#qqqqqqqq").error.isEmpty());
}

void ViewOnlyImportTests::onlySingleKeyDescriptorsAreAccepted()
{
    const QStringList rejected{
        QStringLiteral("wsh(multi(1,%1/0/*,%2))").arg(XPUB, PUBKEY),
        QStringLiteral("tr(%1/0/*,pk(%2))").arg(XPUB, PUBKEY),
        QStringLiteral("wsh(pk(%1/0/*))").arg(XPUB),
        QStringLiteral("combo(%1/0/*)").arg(XPUB),
    };
    for (const QString& descriptor : rejected) {
        QVERIFY2(ParseViewOnlyImport(descriptor).error.contains("single-key"), qPrintable(descriptor));
    }
    QVERIFY(ParseViewOnlyImport(Desc("sh(wpkh", "0/*") + ")").error.isEmpty());

    const ViewOnlyImport second{ParseViewOnlyImport("\n\n" + Desc("wpkh", "0/*") + "\n" + rejected[0])};
    QVERIFY2(second.error.startsWith("Line 4: "), qPrintable(second.error));
    QCOMPARE(second.error_line, 3);
    QCOMPARE(DescribeViewOnlyImport("\n\n" + Desc("wpkh", "0/*") + "\n" + rejected[0]).value("errorLine").toInt(), 3);
    QCOMPARE(ParseViewOnlyImport(QStringLiteral("not-a-key")).error, QStringLiteral("Enter descriptors, or an extended public key (xpub)."));
}

void ViewOnlyImportTests::linesNeedARangedDescriptor()
{
    QVERIFY(ParseViewOnlyImport(QStringLiteral("wpkh(%1)").arg(PUBKEY)).error.contains("ranged"));
    const ViewOnlyImport mixed{ParseViewOnlyImport(QStringLiteral("wpkh(%1)\n").arg(PUBKEY) + Desc("wpkh", "0/*"))};
    QVERIFY(mixed.error.isEmpty());
    QVERIFY(!mixed.requests[0].active);
    QVERIFY(!mixed.requests[0].internal.has_value());
}

void ViewOnlyImportTests::listDescriptorsOutputKeepsItsMetadata()
{
    const QString json{QStringLiteral(R"({"wallet_name": "hot", "descriptors": [
        {"desc": "%1", "timestamp": 1700000000, "active": true, "internal": false, "range": [0, 999], "next": 5, "next_index": 5},
        {"desc": "%2", "timestamp": 1690000000, "active": true, "internal": true, "range": [0, 999], "next_index": 3},
        {"desc": "%3", "timestamp": 1600000000, "active": false, "range": [0, 1999]}
    ]})").arg(WithChecksum(Desc("wpkh", "0/*")), WithChecksum(Desc("wpkh", "1/*")), WithChecksum(Desc("tr", "0/*")))};

    const ViewOnlyImport result{ParseViewOnlyImport(json)};
    QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
    QCOMPARE(result.kind, ViewOnlyImport::Kind::DescriptorJson);
    QCOMPARE(result.requests.size(), size_t{3});
    QVERIFY(result.undated.empty());
    QCOMPARE(result.requests[0].timestamp, std::optional<int64_t>{1700000000});
    QCOMPARE(result.requests[0].next_index, std::optional<int64_t>{5});
    QCOMPARE(result.requests[1].internal, std::optional<bool>{true});
    QCOMPARE(result.requests[1].next_index, std::optional<int64_t>{3});
    QVERIFY(!result.requests[2].active);
    QCOMPARE(result.requests[2].range, (std::optional<std::pair<int64_t, int64_t>>{{0, 1999}}));

    const QVariantMap description{DescribeViewOnlyImport(json)};
    QCOMPARE(description.value("kind").toString(), QStringLiteral("descriptorJson"));
    QVERIFY(!description.value("needsBirthDate").toBool());
    QCOMPARE(description.value("createdTime").toLongLong(), 1600000000);
    QCOMPARE(description.value("addressTypes").toStringList(),
             (QStringList{QStringLiteral("Bech32 (SegWit)"), QStringLiteral("Bech32m (Taproot)")}));
}

void ViewOnlyImportTests::importDescriptorsArrayMarksUndatedEntries()
{
    const QString json{QStringLiteral(R"([{"desc": "%1", "timestamp": "now", "active": true, "range": 99},
                                           {"desc": "%2", "active": true}])")
                           .arg(Desc("wpkh", "0/*"), Desc("wpkh", "1/*"))};
    const ViewOnlyImport result{ParseViewOnlyImport(json)};
    QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
    QVERIFY(!result.requests[0].timestamp.has_value());
    QCOMPARE(result.requests[0].range, (std::optional<std::pair<int64_t, int64_t>>{{0, 99}}));
    QCOMPARE(QString::fromStdString(result.requests[1].descriptor), WithChecksum(Desc("wpkh", "1/*")));
    QCOMPARE(result.undated, std::vector<size_t>{1});
    QVERIFY(DescribeViewOnlyImport(json).value("needsBirthDate").toBool());
}

void ViewOnlyImportTests::malformedJsonIsRejected()
{
    const QString desc{Desc("wpkh", "0/*")};
    const QList<std::pair<QString, QString>> cases{
        {QStringLiteral("[{"), QStringLiteral("not valid JSON")},
        {QStringLiteral("{}"), QStringLiteral("Expected a list")},
        {QStringLiteral("[]"), QStringLiteral("Expected a list")},
        {QStringLiteral(R"([{"timestamp": 0}])"), QStringLiteral("Descriptor 1 has no")},
        {QStringLiteral(R"([{"desc": "%1", "timestamp": -1, "active": true}])").arg(desc), QStringLiteral("invalid timestamp")},
        {QStringLiteral(R"([{"desc": "%1", "timestamp": 0, "active": true, "range": [5, 1]}])").arg(desc), QStringLiteral("Descriptor 1: ")},
        {QStringLiteral(R"([{"desc": "%1", "timestamp": 0, "active": true, "range": "all"}])").arg(desc), QStringLiteral("invalid range")},
        {QStringLiteral(R"([{"desc": "%1", "timestamp": 0, "active": "yes"}])").arg(desc), QStringLiteral("wrong type")},
        {QStringLiteral(R"([{"desc": "%1", "timestamp": 0}])").arg(desc), QStringLiteral("None of these descriptors is active")},
    };
    for (const auto& [json, expected] : cases) {
        const QString error{ParseViewOnlyImport(json).error};
        QVERIFY2(error.contains(expected), qPrintable(json + " -> " + error));
    }
}

#ifdef BITCOINQML_NO_TEST_MAIN
#include <test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(ViewOnlyImportTests)
#else
QTEST_MAIN(ViewOnlyImportTests)
#endif
#include "test_viewonlyimport.moc"
