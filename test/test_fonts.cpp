// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QtTest/QtTest>

#include <qml/fonts.h>

#include <QFont>
#include <QFontDatabase>
#include <QGlyphRun>
#include <QRawFont>
#include <QSet>
#include <QTextLayout>

namespace {
const QString ARABIC_WALLET{QString::fromUtf16(u"\u0645\u062d\u0641\u0638\u0629")};

QTextLine LayOut(QTextLayout& layout)
{
    layout.beginLayout();
    QTextLine line = layout.createLine();
    layout.endLayout();
    return line;
}

QSet<QString> FamiliesUsed(const QString& text, const QFont& font)
{
    QTextLayout layout(text, font);
    LayOut(layout);
    QSet<QString> families;
    for (const QGlyphRun& run : layout.glyphRuns()) families.insert(run.rawFont().familyName());
    return families;
}
} // namespace

class FontsTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void latinTextUsesBitcoinCoreSans();
    void arabicTextFallsBackToBundledNoto();
    void arabicTextSharesTheLatinBaseline();
};

void FontsTests::initTestCase()
{
    Q_INIT_RESOURCE(bitcoin_qml);
    LoadBundledFonts();
    if (!QFontDatabase::families().contains(QStringLiteral("BitcoinCoreSans"))) {
        QSKIP("Platform has no application font support; run with -platform offscreen");
    }
}

void FontsTests::latinTextUsesBitcoinCoreSans()
{
    QCOMPARE(FamiliesUsed(QStringLiteral("Wallet 0.5 BTC"), QFont(QStringLiteral("BitcoinCoreSans"))),
             QSet<QString>{QStringLiteral("BitcoinCoreSans")});
}

void FontsTests::arabicTextFallsBackToBundledNoto()
{
    QCOMPARE(FamiliesUsed(ARABIC_WALLET, QFont(QStringLiteral("BitcoinCoreSans"))),
             QSet<QString>{QStringLiteral("Noto Sans Arabic")});

    QFont semibold(QStringLiteral("BitcoinCoreSans"));
    semibold.setWeight(QFont::DemiBold);
    QTextLayout layout(ARABIC_WALLET, semibold);
    layout.beginLayout();
    layout.createLine();
    layout.endLayout();
    const QList<QGlyphRun> runs = layout.glyphRuns();
    QVERIFY(!runs.isEmpty());
    QCOMPARE(runs.first().rawFont().weight(), int{QFont::DemiBold});
}

void FontsTests::arabicTextSharesTheLatinBaseline()
{
    QFont font(QStringLiteral("BitcoinCoreSans"));
    font.setPixelSize(18);
    QTextLayout latin(QStringLiteral("Website"), font);
    QTextLayout arabic(ARABIC_WALLET, font);
    const QTextLine latin_line = LayOut(latin);
    const QTextLine arabic_line = LayOut(arabic);
    QCOMPARE(arabic_line.ascent(), latin_line.ascent());
    QCOMPARE(arabic_line.descent(), latin_line.descent());
}

#ifdef BITCOINQML_NO_TEST_MAIN
#include <test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(FontsTests)
#else
QTEST_MAIN(FontsTests)
#endif
#include "test_fonts.moc"
