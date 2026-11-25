// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/viewonlyimport.h>

#include <chainparams.h>
#include <interfaces/chain.h>
#include <node/context.h>
#include <script/descriptor.h>
#include <wallet/wallet.h>
#include <wallet/sqlite.h>

#include <QtTest/QtTest>

#include <QStringList>

namespace {

// BIP32 test vectors 1 and 2.
const QString XPUB{"xpub661MyMwAqRbcFtXgS5sYJABqqG9YLmC4Q1Rdap9gSE8NqtwybGhePY2gZ29ESFjqJoCu1Rupje8YtGqsefD265TMg7usUDFdp6W1EGMcet8"};
const QString XPUB2{"xpub661MyMwAqRbcFW31YEwpkMuc5THy2PSt5bDMsktWQcFF8syAmRUapSCGu8ED9W6oDMSgv6Zz8idoc4a6mr8BDzTJY47LJhkJ8UB7WEGuduB"};
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
    void rowsGroupReceiveAndChangeOfOneKey();
    void missingChangeIsExplained();
    void nowIsTheGivenChainTime();
    void savedDescriptorsAreCheckedPerPath();
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
    QVERIFY(ParseViewOnlyImport("[d34db33f/48h/0h/0h/2h]" + XPUB).error.contains("multisig"));
    QVERIFY(ParseViewOnlyImport("[d34db33f/45'/0]" + XPUB, QStringLiteral("bech32")).error.contains("multisig"));
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
    QVERIFY(zpub.warning.contains("origin path says Base58 (Legacy)"));
    QCOMPARE(DescribeViewOnlyImport("[d34db33f/44'/0'/0']" + ZPUB).value("warning").toString(), zpub.warning);
    QVERIFY(ParseViewOnlyImport("[d34db33f/84'/0'/0']" + ZPUB).warning.isEmpty());
    QVERIFY(taproot.warning.isEmpty());

    QVERIFY(ParseViewOnlyImport("[d34db33f/0h]" + XPUB).needs_address_type);

    QString mistyped{XPUB};
    mistyped[mistyped.size() - 1] = QLatin1Char{'9'};
    const ViewOnlyImport typo{ParseViewOnlyImport("[d34db33f/84h/0h/0h]" + mistyped)};
    QCOMPARE(typo.kind, ViewOnlyImport::Kind::ExtendedKey);
    QCOMPARE(typo.error, QStringLiteral("This extended public key is not valid."));
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
    QVERIFY(ParseViewOnlyImport(Desc("wpkh", "<0;1;2>/*")).error.contains("Only two paths"));
    QVERIFY(ParseViewOnlyImport(Desc("wpkh", "<0;1>/*")).error.isEmpty());

    const ViewOnlyImport second{ParseViewOnlyImport("\n\n" + Desc("wpkh", "0/*") + "\n" + rejected[0])};
    QVERIFY2(second.error.startsWith("Line 4: "), qPrintable(second.error));
    QCOMPARE(second.error_line, 3);
    QCOMPARE(DescribeViewOnlyImport("\n\n" + Desc("wpkh", "0/*") + "\n" + rejected[0]).value("errorLine").toInt(), 3);
    QCOMPARE(ParseViewOnlyImport(QStringLiteral("not-a-key")).error, QStringLiteral("Enter descriptors, or an extended public key (xpub)."));
}

void ViewOnlyImportTests::linesNeedARangedDescriptor()
{
    QVERIFY(ParseViewOnlyImport(Desc("wpkh", "0h/*")).error.contains("hardened"));
    QVERIFY(ParseViewOnlyImport(QStringLiteral("wpkh([d34db33f/84h/0h/0h]%1/0/*)").arg(XPUB)).error.isEmpty());
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
        {QStringLiteral(R"j([{"desc": "wpkh(%1)", "timestamp": 0, "active": true}])j").arg(PUBKEY), QStringLiteral("only a ranged descriptor")},
        {QStringLiteral(R"j([{"desc": "wpkh(%1)", "timestamp": 0, "range": 5}, {"desc": "%2", "active": true}])j").arg(PUBKEY, desc), QStringLiteral("not ranged")},
        {QStringLiteral(R"([{"desc": "%1", "timestamp": 0, "active": true, "internal": false}])").arg(Desc("wpkh", "<0;1>/*")), QStringLiteral("\"internal\"")},
        {QStringLiteral(R"([{"desc": "%1", "timestamp": 0, "active": true, "label": "savings"}])").arg(desc), QStringLiteral("label")},
        {QStringLiteral(R"([{"desc": "%1", "timestamp": 0, "active": true, "range": [0, 9], "next_index": 10}])").arg(desc), QStringLiteral("next index")},
        {QStringLiteral(R"([{"desc": "%1", "timestamp": 0, "active": true, "range": [5, 9], "next_index": 4}])").arg(desc), QStringLiteral("next index")},
        {QStringLiteral(R"([{"desc": "%1", "timestamp": 0, "active": true}])").arg(Desc("wpkh", "1h/*")), QStringLiteral("hardened")},
        {QStringLiteral(R"([{"desc": "%1", "timestamp": 0, "active": true, "next_index": 5}])").arg(desc), QStringLiteral("no range")},
        {QStringLiteral(R"([{"desc": "%1", "timestamp": 0, "active": true, "range": [2147000000, 2147483646]}])").arg(desc),
         QStringLiteral("ends after 999999")},
        {QStringLiteral(R"([{"desc": "%1", "timestamp": 0, "active": true, "range": [5, 1000000]}])").arg(desc), QStringLiteral("too long")},
        {QStringLiteral(R"([{"desc": "%1", "timestamp": 0, "active": true}, {"desc": "%2", "timestamp": 0, "active": false}])").arg(desc, WithChecksum(desc)),
         QStringLiteral("Descriptor 2 repeats")},
        {QStringLiteral(R"([{"desc": "%1", "timestamp": 0, "active": true}, {"desc": "%2", "timestamp": 0, "active": true, "internal": true}])").arg(Desc("wpkh", "<0;1>/*"), desc),
         QStringLiteral("Descriptor 2 repeats")},
        {QStringLiteral(R"([{"desc": "%1", "timestamp": 0, "active": true}, {"desc": "%2", "timestamp": 0}])").arg(Desc("wpkh", "0/*").replace(XPUB, "[d34db33f/84h]" + XPUB), Desc("wpkh", "0/*").replace(XPUB, "[d34db33f/84']" + XPUB)),
         QStringLiteral("Descriptor 2 repeats")},
    };
    for (const auto& [json, expected] : cases) {
        const QString error{ParseViewOnlyImport(json).error};
        QVERIFY2(error.contains(expected), qPrintable(json + " -> " + error));
    }
    const QString same_settings{QStringLiteral(R"([{"desc": "%1", "timestamp": 0, "active": true}, {"desc": "%2", "timestamp": 5, "active": true, "internal": true}])")
                                    .arg(Desc("wpkh", "<0;1>/*"), Desc("wpkh", "1/*"))};
    QVERIFY(ParseViewOnlyImport(same_settings).error.isEmpty());
}

void ViewOnlyImportTests::rowsGroupReceiveAndChangeOfOneKey()
{
    const QString origin{QStringLiteral("[d34db33f/84h/0h/0h]")};
    const QString json{QStringLiteral(R"([
        {"desc": "%1", "timestamp": 1700000000, "active": true, "internal": false},
        {"desc": "%2", "timestamp": 1690000000, "active": true, "internal": true},
        {"desc": "%3", "timestamp": 1600000000, "active": false}
    ])").arg(Desc("wpkh", "0/*").replace(XPUB, origin + XPUB), Desc("wpkh", "1/*").replace(XPUB, origin + XPUB), Desc("tr", "0/*"))};
    const QVariantList rows = DescribeViewOnlyImport(json).value("rows").toList();
    QCOMPARE(rows.size(), 2);
    const QVariantMap segwit{rows[0].toMap()};
    QCOMPARE(segwit.value("addressType").toString(), QStringLiteral("Bech32 (SegWit)"));
    QCOMPARE(segwit.value("key").toString(), origin);
    QVERIFY(segwit.value("receive").toBool() && segwit.value("change").toBool());
    QVERIFY(segwit.value("active").toBool() && segwit.value("ranged").toBool());
    QCOMPARE(segwit.value("createdTime").toLongLong(), 1690000000);
    const QVariantMap taproot{rows[1].toMap()};
    QCOMPARE(taproot.value("key").toString(), XPUB);
    QVERIFY(taproot.value("receive").toBool() && !taproot.value("change").toBool());
    QVERIFY(!taproot.value("active").toBool());

    // Another key with the same origin is not the change of the first.
    const QString mismatched{Desc("wpkh", "0/*").replace(XPUB, origin + XPUB) + "\n" +
                             QStringLiteral("wpkh(%1%2/1/*)").arg(origin, XPUB2)};
    const QVariantList split = DescribeViewOnlyImport(mismatched).value("rows").toList();
    QCOMPARE(split.size(), 2);
    QVERIFY(split[0].toMap().value("receive").toBool() && !split[0].toMap().value("change").toBool());
    QVERIFY(!split[1].toMap().value("receive").toBool() && split[1].toMap().value("change").toBool());

    // Only the last receive descriptor of a type gives out addresses.
    const QVariantList replaced = DescribeViewOnlyImport(Desc("wpkh", "0/*") + "\n" + QStringLiteral("wpkh(%1/0/*)").arg(XPUB2) + "\n" +
                                                         Desc("wpkh", "1/*")).value("rows").toList();
    QCOMPARE(replaced.size(), 3);
    QVERIFY(!replaced[0].toMap().value("active").toBool() && replaced[0].toMap().value("receive").toBool());
    QVERIFY(replaced[1].toMap().value("active").toBool());
    QVERIFY(replaced[2].toMap().value("active").toBool() && replaced[2].toMap().value("change").toBool());

    // A later receive descriptor replaces only the receive branch of a multipath one.
    const QVariantList branch = DescribeViewOnlyImport(Desc("wpkh", "<0;1>/*") + "\n" + QStringLiteral("wpkh(%1/0/*)").arg(XPUB2))
                                  .value("rows").toList();
    QCOMPARE(branch.size(), 3);
    QVERIFY(!branch[0].toMap().value("active").toBool() && branch[0].toMap().value("receive").toBool() &&
            !branch[0].toMap().value("change").toBool());
    QVERIFY(branch[1].toMap().value("active").toBool() && branch[1].toMap().value("change").toBool() &&
            !branch[1].toMap().value("receive").toBool());
    QCOMPARE(branch[1].toMap().value("key").toString(), XPUB);
    QVERIFY(branch[2].toMap().value("active").toBool() && branch[2].toMap().value("receive").toBool());

    const QVariantList lines = DescribeViewOnlyImport(Desc("sh(wpkh", "<0;1>/*") + ")\n" + QStringLiteral("pkh(%1)").arg(PUBKEY))
                                 .value("rows").toList();
    QCOMPARE(lines.size(), 2);
    QVERIFY(lines[0].toMap().value("receive").toBool() && lines[0].toMap().value("change").toBool());
    QCOMPARE(lines[0].toMap().value("createdTime").toLongLong(), -1);
    QCOMPARE(lines[1].toMap().value("key").toString(), PUBKEY);
    QVERIFY(!lines[1].toMap().value("ranged").toBool());
}

void ViewOnlyImportTests::missingChangeIsExplained()
{
    QVERIFY(DescribeViewOnlyImport(Desc("wpkh", "<0;1>/*")).value("warning").toString().isEmpty());
    QVERIFY(DescribeViewOnlyImport(Desc("wpkh", "0/*") + "\n" + Desc("wpkh", "1/*")).value("warning").toString().isEmpty());
    QVERIFY(DescribeViewOnlyImport(Desc("wpkh", "0/*")).value("warning").toString().contains("Nothing here is for change"));
    const QString inactive_change{QStringLiteral(R"([{"desc": "%1", "timestamp": 0, "active": true}, {"desc": "%2", "timestamp": 0, "internal": true}])")
                                      .arg(Desc("wpkh", "0/*"), Desc("wpkh", "1/*"))};
    QVERIFY(DescribeViewOnlyImport(inactive_change).value("warning").toString().contains("No change descriptor is active"));
}

void ViewOnlyImportTests::savedDescriptorsAreCheckedPerPath()
{
    node::NodeContext node_context;
    auto chain{interfaces::MakeChain(node_context)};
    wallet::CWallet wallet{chain.get(), "imported", wallet::MakeInMemoryWalletDatabase()};
    wallet.SetWalletFlag(wallet::WALLET_FLAG_DESCRIPTORS | wallet::WALLET_FLAG_DISABLE_PRIVATE_KEYS);
    const ViewOnlyImport receive{ParseViewOnlyImport(Desc("wpkh", "0/*"))};
    const ViewOnlyImport both{ParseViewOnlyImport(Desc("wpkh", "<0;1>/*"))};
    QVERIFY(!DescriptorsSaved(wallet, receive.requests));
    {
        FlatSigningProvider keys;
        std::string error;
        auto parsed{Parse(receive.requests.front().descriptor, keys, error, /*require_checksum=*/true)};
        QCOMPARE(parsed.size(), size_t{1});
        LOCK(wallet.cs_wallet);
        wallet::WalletDescriptor saved{std::move(parsed.front()), 0, 0, 10, 0};
        QVERIFY(wallet.AddWalletDescriptor(saved, keys, "", false));
    }
    QVERIFY(DescriptorsSaved(wallet, receive.requests));
    // The change path of the multipath descriptor is still missing.
    QVERIFY(!DescriptorsSaved(wallet, both.requests));
}

void ViewOnlyImportTests::nowIsTheGivenChainTime()
{
    std::vector<wallet::ImportDescriptorRequest> requests(3);
    requests[1].timestamp = 1'500'000'000;
    QCOMPARE(ResolveImportTimes(requests, 1'700'000'000), int64_t{1'500'000'000});
    QCOMPARE(requests[0].timestamp, std::optional<int64_t>{1'700'000'000});
    QCOMPARE(requests[2].timestamp, std::optional<int64_t>{1'700'000'000});
    std::vector<wallet::ImportDescriptorRequest> now_only(1);
    // Earlier than the wall clock, like a chain whose median time lags.
    QCOMPARE(ResolveImportTimes(now_only, 1'000), int64_t{1'000});
}

#ifdef BITCOINQML_NO_TEST_MAIN
#include <test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(ViewOnlyImportTests)
#else
QTEST_MAIN(ViewOnlyImportTests)
#endif
#include "test_viewonlyimport.moc"
