// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QtTest/QtTest>

#include <qml/models/receiverequestentry.h>
#include <qml/models/receiverequesthistorymodel.h>

#include <chainparams.h>
#include <chainparamsbase.h>
#include <consensus/amount.h>
#include <streams.h>
#include <util/chaintype.h>

#include <string>
#include <vector>

#include <QDateTime>

namespace {
QmlRecentRequestEntry MakeEntry(int64_t id, const std::string& address, CAmount amount,
                                const std::string& label = {}, const std::string& message = {},
                                const std::string& note_self = {})
{
    QmlRecentRequestEntry entry;
    entry.id = id;
    entry.date = QDateTime::fromSecsSinceEpoch(1'700'000'000 + id);
    entry.recipient.address = address;
    entry.recipient.amount = amount;
    entry.recipient.label = label;
    entry.recipient.message = message;
    entry.recipient.noteSelf = note_self;
    return entry;
}
} // namespace

class ReceiveRequestHistoryModelTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void buildUriEmptyAddress();
    void buildUriPlainAddress();
    void buildUriWithAmountLabelMessage();
    void buildUriPreservesBech32Case();
    void buildUriUrlEncodesParams();
    void serializeRoundTripV2();
    void deserializeSkipsMalformed();
    void modelRolesMatchEntry();
    void prependInsertsNewRow();
    void removeByRequestIdRemovesRow();
    void entryByIdLookup();
    void maxIdReturnsHighest();
    void deserializeTruncatedBlob();
    void formatAmountBtcEdgeCases();
    void deserializeV1RecipientBlob();
};

void ReceiveRequestHistoryModelTests::initTestCase()
{
    SelectBaseParams(ChainType::MAIN);
    SelectParams(ChainType::MAIN);
}

void ReceiveRequestHistoryModelTests::buildUriEmptyAddress()
{
    QCOMPARE(ReceiveRequestHistoryModel::BuildBitcoinUri("", 0, "", ""), QString{});
}

void ReceiveRequestHistoryModelTests::buildUriPlainAddress()
{
    const QString addr = "1BvBMSEYstWetqTFn5Au4m4GFg7xJaNVN2";
    QCOMPARE(ReceiveRequestHistoryModel::BuildBitcoinUri(addr, 0, "", ""),
             QStringLiteral("bitcoin:") + addr);
}

void ReceiveRequestHistoryModelTests::buildUriWithAmountLabelMessage()
{
    const QString addr = "1BvBMSEYstWetqTFn5Au4m4GFg7xJaNVN2";
    const QString uri = ReceiveRequestHistoryModel::BuildBitcoinUri(addr, 10000, "Alice", "pizza");
    QCOMPARE(uri, QStringLiteral("bitcoin:1BvBMSEYstWetqTFn5Au4m4GFg7xJaNVN2?amount=0.00010000&label=Alice&message=pizza"));
}

void ReceiveRequestHistoryModelTests::buildUriPreservesBech32Case()
{
    const QString addr = "bc1qar0srrr7xfkvy5l643lydnw9re59gtzzwf5mdq";
    const QString uri = ReceiveRequestHistoryModel::BuildBitcoinUri(addr, 0, "", "");
    QCOMPARE(uri, QStringLiteral("bitcoin:bc1qar0srrr7xfkvy5l643lydnw9re59gtzzwf5mdq"));
}

void ReceiveRequestHistoryModelTests::buildUriUrlEncodesParams()
{
    const QString addr = "1BvBMSEYstWetqTFn5Au4m4GFg7xJaNVN2";
    const QString uri = ReceiveRequestHistoryModel::BuildBitcoinUri(addr, 0, "Al ice & Bob", "hi=hello?x");
    QVERIFY(uri.contains("label=Al%20ice%20%26%20Bob"));
    QVERIFY(uri.contains("message=hi%3Dhello%3Fx"));
}

void ReceiveRequestHistoryModelTests::serializeRoundTripV2()
{
    const auto entry_in = MakeEntry(7, "1BvBMSEYstWetqTFn5Au4m4GFg7xJaNVN2", 50000, "Alice", "lunch", "personal note");
    const std::string blob = ReceiveRequestHistoryModel::SerializeEntry(entry_in);
    const auto entries = ReceiveRequestHistoryModel::DeserializeEntries({blob});
    QCOMPARE(entries.size(), std::size_t{1});
    const auto& out = entries[0];
    QCOMPARE(out.id, int64_t{7});
    QCOMPARE(out.recipient.address, std::string{"1BvBMSEYstWetqTFn5Au4m4GFg7xJaNVN2"});
    QCOMPARE(out.recipient.amount, CAmount{50000});
    QCOMPARE(out.recipient.label, std::string{"Alice"});
    QCOMPARE(out.recipient.message, std::string{"lunch"});
    QCOMPARE(out.recipient.noteSelf, std::string{"personal note"});
    QCOMPARE(out.date.toSecsSinceEpoch(), entry_in.date.toSecsSinceEpoch());
}

void ReceiveRequestHistoryModelTests::deserializeSkipsMalformed()
{
    const auto good = MakeEntry(1, "1BvBMSEYstWetqTFn5Au4m4GFg7xJaNVN2", 1000, "ok", "", "");
    const std::string good_blob = ReceiveRequestHistoryModel::SerializeEntry(good);
    const std::string garbage = "\x01\x02\x03garbage";
    const auto entries = ReceiveRequestHistoryModel::DeserializeEntries({garbage, good_blob, std::string{"\x00\xff", 2}});
    QCOMPARE(entries.size(), std::size_t{1});
    QCOMPARE(entries[0].id, int64_t{1});
}

void ReceiveRequestHistoryModelTests::modelRolesMatchEntry()
{
    ReceiveRequestHistoryModel model;
    std::vector<QmlRecentRequestEntry> entries;
    entries.push_back(MakeEntry(3, "1BvBMSEYstWetqTFn5Au4m4GFg7xJaNVN2", 10000, "Alice", "lunch", "self"));
    model.setEntries(std::move(entries));

    QCOMPARE(model.rowCount(), 1);
    const QModelIndex idx = model.index(0);
    QCOMPARE(model.data(idx, ReceiveRequestHistoryModel::IdRole).toString(), QString{"3"});
    QCOMPARE(model.data(idx, ReceiveRequestHistoryModel::AddressRole).toString(), QString{"1BvBMSEYstWetqTFn5Au4m4GFg7xJaNVN2"});
    QCOMPARE(model.data(idx, ReceiveRequestHistoryModel::LabelRole).toString(), QString{"Alice"});
    QCOMPARE(model.data(idx, ReceiveRequestHistoryModel::MessageRole).toString(), QString{"lunch"});
    QCOMPARE(model.data(idx, ReceiveRequestHistoryModel::NoteSelfRole).toString(), QString{"self"});
    QCOMPARE(model.data(idx, ReceiveRequestHistoryModel::AmountSatRole).toLongLong(), qlonglong{10000});
    QVERIFY(model.data(idx, ReceiveRequestHistoryModel::UriRole).toString().startsWith("bitcoin:"));
}

void ReceiveRequestHistoryModelTests::prependInsertsNewRow()
{
    ReceiveRequestHistoryModel model;
    model.setEntries({MakeEntry(1, "1BvBMSEYstWetqTFn5Au4m4GFg7xJaNVN2", 1000)});
    model.prependOrReplace(MakeEntry(2, "bc1qar0srrr7xfkvy5l643lydnw9re59gtzzwf5mdq", 2000));
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.data(model.index(0), ReceiveRequestHistoryModel::IdRole).toString(), QString{"2"});

    model.prependOrReplace(MakeEntry(2, "bc1qar0srrr7xfkvy5l643lydnw9re59gtzzwf5mdq", 3000));
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.data(model.index(0), ReceiveRequestHistoryModel::AmountSatRole).toLongLong(), qlonglong{3000});
}

void ReceiveRequestHistoryModelTests::removeByRequestIdRemovesRow()
{
    ReceiveRequestHistoryModel model;
    model.setEntries({MakeEntry(1, "1BvBMSEYstWetqTFn5Au4m4GFg7xJaNVN2", 1000),
                      MakeEntry(2, "bc1qar0srrr7xfkvy5l643lydnw9re59gtzzwf5mdq", 2000)});
    QVERIFY(model.removeByRequestId("1"));
    QCOMPARE(model.rowCount(), 1);
    QVERIFY(!model.removeByRequestId("999"));
    QCOMPARE(model.rowCount(), 1);
}

void ReceiveRequestHistoryModelTests::entryByIdLookup()
{
    ReceiveRequestHistoryModel model;
    model.setEntries({MakeEntry(42, "1BvBMSEYstWetqTFn5Au4m4GFg7xJaNVN2", 1000, "lbl", "msg", "self")});
    const auto hit = model.entryById("42");
    QVERIFY(hit.has_value());
    QCOMPARE(hit->recipient.label, std::string{"lbl"});
    QVERIFY(!model.entryById("nope").has_value());
}

void ReceiveRequestHistoryModelTests::maxIdReturnsHighest()
{
    ReceiveRequestHistoryModel model;
    QCOMPARE(model.maxId(), int64_t{0});

    model.setEntries({MakeEntry(5, "1BvBMSEYstWetqTFn5Au4m4GFg7xJaNVN2", 1000),
                      MakeEntry(12, "bc1qar0srrr7xfkvy5l643lydnw9re59gtzzwf5mdq", 2000),
                      MakeEntry(3, "1BvBMSEYstWetqTFn5Au4m4GFg7xJaNVN2", 500)});
    QCOMPARE(model.maxId(), int64_t{12});

    model.prependOrReplace(MakeEntry(20, "1BvBMSEYstWetqTFn5Au4m4GFg7xJaNVN2", 100));
    QCOMPARE(model.maxId(), int64_t{20});
}

void ReceiveRequestHistoryModelTests::deserializeTruncatedBlob()
{
    const auto good = MakeEntry(1, "1BvBMSEYstWetqTFn5Au4m4GFg7xJaNVN2", 1000);
    const std::string good_blob = ReceiveRequestHistoryModel::SerializeEntry(good);
    const std::string truncated = good_blob.substr(0, good_blob.size() / 2);
    const auto entries = ReceiveRequestHistoryModel::DeserializeEntries({truncated, good_blob});
    QCOMPARE(entries.size(), std::size_t{1});
    QCOMPARE(entries[0].id, int64_t{1});
}

void ReceiveRequestHistoryModelTests::formatAmountBtcEdgeCases()
{
    QCOMPARE(ReceiveRequestHistoryModel::FormatAmountBtc(0), QString{});
    QCOMPARE(ReceiveRequestHistoryModel::FormatAmountBtc(-100), QString{});
    QVERIFY(!ReceiveRequestHistoryModel::FormatAmountBtc(2100000000000000).isEmpty());
    QCOMPARE(ReceiveRequestHistoryModel::FormatAmountBtc(1), QStringLiteral("0.00000001"));
    QCOMPARE(ReceiveRequestHistoryModel::FormatAmountBtc(100000000), QStringLiteral("1.00000000"));
}

void ReceiveRequestHistoryModelTests::deserializeV1RecipientBlob()
{
    QmlRecentRequestEntry entry;
    entry.id = 5;
    entry.date = QDateTime::fromSecsSinceEpoch(1'700'000'005);
    entry.recipient.nVersion = 1;
    entry.recipient.address = "1BvBMSEYstWetqTFn5Au4m4GFg7xJaNVN2";
    entry.recipient.amount = 25000;
    entry.recipient.label = "v1label";
    entry.recipient.message = "v1msg";

    DataStream ss{};
    ss << entry;
    const std::string blob = ss.str();

    const auto entries = ReceiveRequestHistoryModel::DeserializeEntries({blob});
    QCOMPARE(entries.size(), std::size_t{1});
    QCOMPARE(entries[0].id, int64_t{5});
    QCOMPARE(entries[0].recipient.address, std::string{"1BvBMSEYstWetqTFn5Au4m4GFg7xJaNVN2"});
    QCOMPARE(entries[0].recipient.amount, CAmount{25000});
    QCOMPARE(entries[0].recipient.label, std::string{"v1label"});
    QCOMPARE(entries[0].recipient.message, std::string{"v1msg"});
    QCOMPARE(entries[0].recipient.noteSelf, std::string{});
    QCOMPARE(entries[0].date.toSecsSinceEpoch(), qint64{1'700'000'005});
}

#ifdef BITCOINQML_NO_TEST_MAIN
#include <test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(ReceiveRequestHistoryModelTests)
#else
QTEST_MAIN(ReceiveRequestHistoryModelTests)
#endif
#include "test_receiverequesthistorymodel.moc"
