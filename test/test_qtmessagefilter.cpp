// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QtTest/QtTest>

#include <qml/bitcoin.h>

class QtMessageFilterTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void benignFontWarning_matches();
    void benignFontWarning_rejectsOther();
};

void QtMessageFilterTests::benignFontWarning_matches()
{
    QVERIFY(IsBenignQtFontWarning(QStringLiteral("OpenType support missing for \"BitcoinCoreSans\", script 18")));
    // The prefix alone is enough; the trailing script details vary.
    QVERIFY(IsBenignQtFontWarning(QStringLiteral("OpenType support missing for")));
}

void QtMessageFilterTests::benignFontWarning_rejectsOther()
{
    QVERIFY(!IsBenignQtFontWarning(QString()));
    QVERIFY(!IsBenignQtFontWarning(QStringLiteral("Some unrelated Qt warning")));
    // Match is case sensitive, mirroring the exact text Qt emits.
    QVERIFY(!IsBenignQtFontWarning(QStringLiteral("opentype support missing for")));
}

#ifdef BITCOINQML_NO_TEST_MAIN
#include <test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(QtMessageFilterTests)
#else
QTEST_MAIN(QtMessageFilterTests)
#endif
#include <test_qtmessagefilter.moc>
