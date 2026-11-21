// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/descriptorhighlighter.h>

#include <QtTest/QtTest>

#include <QTextBlock>
#include <QTextDocument>
#include <QTextLayout>

namespace {

const QString KEY{"tpubDDCB6KXE3XaLkJbG6Z8wtVKSim51d4itMKjVwdMPxm5QyrqSfYkwsW5abwiGENWp9QprevCAiuew2dNfHgRMaL6jv1S6ByPHxxuB62RpkDk"};
const QString DESCRIPTOR{"wpkh([4cf2da4b/84h/1h/0h]" + KEY + "/0/*)#jplfry4u"};

QTextCharFormat FormatAt(const QTextDocument& document, int line, int position)
{
    const QTextBlock block{document.findBlockByNumber(line)};
    for (const QTextLayout::FormatRange& range : block.layout()->formats()) {
        if (position >= range.start && position < range.start + range.length) return range.format;
    }
    return {};
}

QColor ColorAt(const QTextDocument& document, int line, int position)
{
    return FormatAt(document, line, position).foreground().color();
}

void SetColors(DescriptorHighlighter& highlighter)
{
    highlighter.setScriptColor(Qt::red);
    highlighter.setOriginColor(Qt::gray);
    highlighter.setKeyColor(Qt::white);
    highlighter.setKeyAlternateColor(Qt::lightGray);
    highlighter.setPathColor(Qt::blue);
    highlighter.setChecksumColor(Qt::darkGray);
    highlighter.setJsonNameColor(Qt::magenta);
    highlighter.setJsonValueColor(Qt::green);
    highlighter.setErrorColor(Qt::yellow);
}

} // namespace

class DescriptorHighlighterTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void colorsEachPartOfADescriptor();
    void colorsDescriptorsInsideJson();
    void colorsPrettyPrintedJson();
    void marksTheErrorLine();
};

void DescriptorHighlighterTests::colorsEachPartOfADescriptor()
{
    QTextDocument document{DESCRIPTOR};
    DescriptorHighlighter highlighter;
    SetColors(highlighter);
    highlighter.setDocument(&document);
    highlighter.rehighlight();

    const int key{int(DESCRIPTOR.indexOf(KEY))};
    QCOMPARE(ColorAt(document, 0, 0), QColor{Qt::red});
    QCOMPARE(ColorAt(document, 0, 5), QColor{Qt::gray});
    QCOMPARE(ColorAt(document, 0, 14), QColor{Qt::gray});
    QCOMPARE(ColorAt(document, 0, key), QColor{Qt::white});
    QCOMPARE(ColorAt(document, 0, key + 4), QColor{Qt::lightGray});
    QCOMPARE(ColorAt(document, 0, key + int(KEY.size())), QColor{Qt::blue});
    QCOMPARE(ColorAt(document, 0, int(DESCRIPTOR.indexOf(')'))), QColor{Qt::red});
    QCOMPARE(ColorAt(document, 0, int(DESCRIPTOR.indexOf('#'))), QColor{Qt::darkGray});
}

void DescriptorHighlighterTests::colorsDescriptorsInsideJson()
{
    const QString json{R"([{"desc": ")" + DESCRIPTOR + R"(", "timestamp": 0}])"};
    QTextDocument document{json};
    DescriptorHighlighter highlighter;
    SetColors(highlighter);
    highlighter.setDocument(&document);
    highlighter.rehighlight();

    QCOMPARE(ColorAt(document, 0, int(json.indexOf("wpkh("))), QColor{Qt::red});
    QCOMPARE(ColorAt(document, 0, int(json.indexOf(KEY))), QColor{Qt::white});
    QCOMPARE(ColorAt(document, 0, int(json.indexOf("\"desc\""))), QColor{Qt::magenta});
    QCOMPARE(ColorAt(document, 0, int(json.indexOf("\"timestamp\""))), QColor{Qt::magenta});
    QCOMPARE(ColorAt(document, 0, int(json.lastIndexOf('0'))), QColor{Qt::green});
    QVERIFY(!FormatAt(document, 0, int(json.indexOf(':'))).hasProperty(QTextFormat::ForegroundBrush));
}

void DescriptorHighlighterTests::colorsPrettyPrintedJson()
{
    const QStringList lines{
        R"({)",
        R"(  "wallet_name": "hot",)",
        R"(  "descriptors": [)",
        R"(    {)",
        R"(      "desc": ")" + DESCRIPTOR + R"(",)",
        R"(      "active": true,)",
        R"(      "range": [)",
        R"(        0,)",
        R"(        999)",
        R"(      ],)",
        R"(      "timestamp": "now")",
    };
    QTextDocument document{lines.join('\n')};
    DescriptorHighlighter highlighter;
    SetColors(highlighter);
    highlighter.setDocument(&document);
    highlighter.rehighlight();

    QCOMPARE(ColorAt(document, 1, 2), QColor{Qt::magenta});
    QCOMPARE(ColorAt(document, 1, int(lines[1].indexOf("\"hot\""))), QColor{Qt::green});
    QCOMPARE(ColorAt(document, 4, int(lines[4].indexOf("wpkh("))), QColor{Qt::red});
    QCOMPARE(ColorAt(document, 5, int(lines[5].indexOf("true"))), QColor{Qt::green});
    QCOMPARE(ColorAt(document, 7, 8), QColor{Qt::green});
    QCOMPARE(ColorAt(document, 8, 8), QColor{Qt::green});
    QCOMPARE(ColorAt(document, 10, int(lines[10].indexOf("\"now\""))), QColor{Qt::green});
}

void DescriptorHighlighterTests::marksTheErrorLine()
{
    QTextDocument document{DESCRIPTOR + "\nwsh(multi(1," + KEY + "/0/*))\n"};
    DescriptorHighlighter highlighter;
    SetColors(highlighter);
    highlighter.setDocument(&document);
    highlighter.rehighlight();
    QVERIFY(!FormatAt(document, 1, 0).hasProperty(QTextFormat::BackgroundBrush));

    highlighter.setErrorLine(1);
    QCOMPARE(FormatAt(document, 1, 0).background().color(), QColor{Qt::yellow});
    QCOMPARE(FormatAt(document, 1, 0).foreground().color(), QColor{Qt::red});
    QCOMPARE(FormatAt(document, 1, 12).background().color(), QColor{Qt::yellow});
    QVERIFY(!FormatAt(document, 0, 0).hasProperty(QTextFormat::BackgroundBrush));

    highlighter.setErrorLine(-1);
    QVERIFY(!FormatAt(document, 1, 0).hasProperty(QTextFormat::BackgroundBrush));
}

#ifdef BITCOINQML_NO_TEST_MAIN
#include <test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(DescriptorHighlighterTests)
#else
QTEST_MAIN(DescriptorHighlighterTests)
#endif
#include "test_descriptorhighlighter.moc"
