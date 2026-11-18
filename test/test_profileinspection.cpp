// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/profileinspection.h>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest/QtTest>

namespace {
profileinspection::Request Original()
{
    return {QDir::root().filePath(QStringLiteral("original profile")), ChainType::REGTEST, {}};
}
QByteArray Reply(const profileinspection::Request& request, bool present)
{
    return QJsonDocument{QJsonObject{{QStringLiteral("protocol"), 1}, {QStringLiteral("request_tag"), profileinspection::Tag(request)},
        {QStringLiteral("ok"), true}, {QStringLiteral("has_wallet_data"), present}}}.toJson(QJsonDocument::Compact);
}
}

class ProfileInspectionTests : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void argumentsPreserveOriginalPathAndOverridePresence()
    {
        auto request = Original();
        request.directory += QString::fromUtf8("/原始 --protocol=other");
        request.network = ChainType::SIGNET;
        QVERIFY(profileinspection::Validate(request).isEmpty());
        const auto automatic = profileinspection::Tag(request);
        request.signet_challenge = QString{};
        const auto explicit_empty = profileinspection::Tag(request);
        QVERIFY(automatic != explicit_empty);
        const auto args = profileinspection::Arguments(request);
        QCOMPARE(args.size(), 10);
        QCOMPARE(args[0], QStringLiteral("--protocol"));
        QCOMPARE(args[2], QStringLiteral("--request-tag"));
        QCOMPARE(args[3], explicit_empty);
        QCOMPARE(args[4], QStringLiteral("--walletdir"));
        QCOMPARE(args[5], request.directory);
        QCOMPARE(args[6], QStringLiteral("--chain"));
        QCOMPARE(args[7], QStringLiteral("signet"));
        QCOMPARE(args[8], QStringLiteral("--signetchallenge"));
        QVERIFY(args[9].isEmpty());
    }
    void everyRequestDependencyChangesTheTag()
    {
        const auto original = Original();
        auto changed = original;
        const auto tag = profileinspection::Tag(original);
        QVERIFY(!tag.isEmpty());
        changed.directory += QStringLiteral("-successor");
        QVERIFY(tag != profileinspection::Tag(changed));
        changed = original;
        changed.network = ChainType::MAIN;
        QVERIFY(tag != profileinspection::Tag(changed));
        changed.network = ChainType::SIGNET;
        const auto automatic = profileinspection::Tag(changed);
        changed.signet_challenge = QStringLiteral("51");
        QVERIFY(automatic != profileinspection::Tag(changed));
        QCOMPARE(profileinspection::Tag(original), tag);
    }
    void invalidRequestsCannotProduceArguments_data()
    {
        QTest::addColumn<QString>("kind");
        for (const auto* kind : {"empty", "relative", "nul", "oversize", "invalid-utf16", "network", "foreign-override", "long-override", "nul-override"})
            QTest::newRow(kind) << QString::fromLatin1(kind);
    }
    void invalidRequestsCannotProduceArguments()
    {
        QFETCH(QString, kind);
        auto request = Original();
        if (kind == QStringLiteral("empty")) request.directory.clear();
        else if (kind == QStringLiteral("relative")) request.directory = QStringLiteral("relative-profile");
        else if (kind == QStringLiteral("nul")) request.directory += QChar{u'\0'};
        else if (kind == QStringLiteral("oversize")) request.directory += QString(32769, QChar{u'x'});
        else if (kind == QStringLiteral("invalid-utf16")) request.directory += QChar{char16_t{0xd800}};
        else if (kind == QStringLiteral("network")) request.network = static_cast<ChainType>(999);
        else if (kind == QStringLiteral("foreign-override")) request.signet_challenge = QStringLiteral("51");
        else {
            request.network = ChainType::SIGNET;
            request.signet_challenge = kind == QStringLiteral("long-override") ? QString(65537, QChar{u'5'}) : QString{QChar{u'\0'}};
        }
        QVERIFY(!profileinspection::Validate(request).isEmpty());
        QVERIFY(profileinspection::Arguments(request).isEmpty());
        QVERIFY(profileinspection::Tag(request).isEmpty());
        const auto result = profileinspection::Decode(request, QByteArray{"{}"});
        QCOMPARE(result.outcome, profileinspection::Outcome::Rejected);
        QVERIFY(!result.error.isEmpty());
        QVERIFY(!result.started && !result.exited);
        QVERIFY(!result.exit_status);
    }
    void classificationRequiresExplicitSuccessfulResponse_data()
    {
        QTest::addColumn<bool>("present");
        QTest::newRow("empty") << false;
        QTest::newRow("recognized-wallet") << true;
    }
    void classificationRequiresExplicitSuccessfulResponse()
    {
        QFETCH(bool, present);
        const auto request = Original();
        const auto result = profileinspection::Decode(request, Reply(request, present));
        QCOMPARE(result.outcome, profileinspection::Outcome::Succeeded);
        QCOMPARE(result.has_wallet_data, present);
        QVERIFY(result.error.isEmpty());
        // Protocol parsing cannot invent any physical process receipt.
        QVERIFY(!result.started && !result.exited);
        QVERIFY(!result.exit_status);
    }
    void malformedOrForeignResponseIsNeverEmptySuccess_data()
    {
        QTest::addColumn<QString>("kind");
        for (const auto* kind : {"empty", "array", "malformed", "trailing", "protocol", "fractional-protocol", "tag", "ok-type", "missing-data", "data-type", "extra", "oversize"})
            QTest::newRow(kind) << QString::fromLatin1(kind);
    }
    void malformedOrForeignResponseIsNeverEmptySuccess()
    {
        QFETCH(QString, kind);
        const auto request = Original();
        auto object = QJsonDocument::fromJson(Reply(request, false)).object();
        QByteArray bytes;
        if (kind == QStringLiteral("empty")) {}
        else if (kind == QStringLiteral("array")) bytes = QByteArray{"[]"};
        else if (kind == QStringLiteral("malformed")) bytes = QByteArray{"{"};
        else if (kind == QStringLiteral("trailing")) bytes = Reply(request, false) + " unexpected-output";
        else if (kind == QStringLiteral("oversize")) bytes = QByteArray(4097, ' ');
        else {
            if (kind == QStringLiteral("protocol")) object[QStringLiteral("protocol")] = 2;
            else if (kind == QStringLiteral("fractional-protocol")) object[QStringLiteral("protocol")] = 1.5;
            else if (kind == QStringLiteral("tag")) {
                auto other = request; other.directory += QStringLiteral("/different");
                object[QStringLiteral("request_tag")] = profileinspection::Tag(other);
            } else if (kind == QStringLiteral("ok-type")) object[QStringLiteral("ok")] = QStringLiteral("true");
            else if (kind == QStringLiteral("missing-data")) object.remove(QStringLiteral("has_wallet_data"));
            else if (kind == QStringLiteral("data-type")) object[QStringLiteral("has_wallet_data")] = 0;
            else object[QStringLiteral("unexpected")] = true;
            bytes = QJsonDocument{object}.toJson(QJsonDocument::Compact);
        }
        const auto result = profileinspection::Decode(request, bytes);
        QCOMPARE(result.outcome, profileinspection::Outcome::Rejected);
        QVERIFY(!result.error.isEmpty());
        QVERIFY(!result.has_wallet_data);
    }
    void nativeFailureKeepsItsOriginalDetail()
    {
        const auto request = Original();
        const QByteArray bytes = QJsonDocument{QJsonObject{{QStringLiteral("protocol"), 1}, {QStringLiteral("request_tag"), profileinspection::Tag(request)},
            {QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("Original configured network is unavailable")}}}.toJson(QJsonDocument::Compact);
        const auto result = profileinspection::Decode(request, bytes);
        QCOMPARE(result.outcome, profileinspection::Outcome::Failed);
        QCOMPARE(result.error, QStringLiteral("Original configured network is unavailable"));
        QVERIFY(!result.has_wallet_data);
    }
};
#ifdef BITCOINQML_NO_TEST_MAIN
#include <test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(ProfileInspectionTests)
#else
QTEST_MAIN(ProfileInspectionTests)
#endif
#include <test_profileinspection.moc>
