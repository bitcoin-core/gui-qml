// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/bitcoinurimodel.h>
#include <test/qt_test_registry.h>
#include <key_io.h>
#include <QtTest/QtTest>

namespace {
QString Address() { return QString::fromStdString(EncodeDestination(PKHash{uint160{std::vector<unsigned char>(20, 3)}})); }
QString Uri(const QString& label = QStringLiteral("Invoice")) { return "bitcoin:" + Address() + "?amount=0.12345678&label=" + label; }
} // namespace

class BitcoinUriModelTests : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void pastedPaymentUriRequiresTheInsertedClipboard_data() {
        QTest::addColumn<QString>("clipboard"); QTest::addColumn<QString>("previous"); QTest::addColumn<QString>("current");
        QTest::addColumn<bool>("imported");
        QTest::newRow("replaced-address") << Uri() << "old" << Uri() << true;
        QTest::newRow("appended-note") << Uri() << "private" << "private" + Uri() << true;
        QTest::newRow("appended-amount") << Uri() << "12" << "12" + Uri() << true;
        QTest::newRow("long-note") << Uri() << QString(2048, 'x') << QString(2048, 'x') + Uri() << true;
        QTest::newRow("extra-typed-text") << Uri() << "12" << "12extra" + Uri() << false;
        QTest::newRow("typed-uri") << "unrelated" << "12" << "12bitcoin:typed" << false;
        QTest::newRow("ordinary-text") << "ordinary" << "private" << "privateordinary" << false;
        QTest::newRow("unchanged") << Uri() << Uri() << Uri() << false;
    }
    void pastedPaymentUriRequiresTheInsertedClipboard() {
        QFETCH(QString, clipboard); QFETCH(QString, previous); QFETCH(QString, current); QFETCH(bool, imported);
        BitcoinUriModel model;
        QCOMPARE(model.pastedPaymentUri(previous, current, clipboard), imported ? clipboard : QString{});
        QCOMPARE(model.isPaymentUri(clipboard), clipboard.startsWith("bitcoin:"));
    }
};
BITCOINQML_REGISTER_QT_TEST(BitcoinUriModelTests)
#include <test_bitcoinurimodel.moc>
