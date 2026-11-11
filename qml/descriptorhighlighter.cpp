// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/descriptorhighlighter.h>

#include <QRegularExpression>
#include <QTextCharFormat>

#include <algorithm>
#include <utility>

namespace {

const QRegularExpression SCRIPT{QStringLiteral(R"(\b(?:sh|wsh|pkh|wpkh|tr|rawtr|pk|combo|multi|sortedmulti|multi_a|sortedmulti_a|addr|raw)\(|\))")};
const QRegularExpression ORIGIN{QStringLiteral(R"(\[[0-9a-fA-F]{8}(?:/[0-9]+['hH]?)*\])")};
const QRegularExpression KEY{QStringLiteral(R"(\b[xtyzuvYZUV](?:pub|prv)[1-9A-HJ-NP-Za-km-z]{100,112})")};
const QRegularExpression PATH{QStringLiteral(R"((?:/(?:[0-9]+['hH]?|<[0-9]+(?:;[0-9]+)+>|\*['hH]?))+)")};
const QRegularExpression CHECKSUM{QStringLiteral(R"(#[0-9a-z]{8}\b)")};

// JSON as listdescriptors prints it, on one line or pretty-printed.
const QRegularExpression JSON_NAME{QStringLiteral(R"(("[^"\\]*")\s*:)")};
const QRegularExpression JSON_VALUE{QStringLiteral(R"(:\s*("[^"\\]*"|-?[0-9]+(?:\.[0-9]+)?|true|false|null))")};
const QRegularExpression JSON_RANGE{QStringLiteral(R"(\[\s*(-?[0-9]+)\s*,\s*(-?[0-9]+)\s*\])")};
const QRegularExpression JSON_LINE_NUMBER{QStringLiteral(R"(^\s*(-?[0-9]+)\s*,?\s*$)")};

// Keys alternate shades every four characters, like the descriptor display.
constexpr int KEY_CHUNK{4};

} // namespace

DescriptorHighlighter::DescriptorHighlighter(QObject* parent)
    : QSyntaxHighlighter(parent)
{
}

void DescriptorHighlighter::setTextDocument(QQuickTextDocument* text_document)
{
    if (m_text_document == text_document) return;
    m_text_document = text_document;
    setDocument(text_document ? text_document->textDocument() : nullptr);
    Q_EMIT textDocumentChanged();
}

void DescriptorHighlighter::setColor(QColor& member, const QColor& color)
{
    if (member == color) return;
    member = color;
    rehighlight();
    Q_EMIT colorsChanged();
}

void DescriptorHighlighter::setErrorLine(int line)
{
    if (m_error_line == line) return;
    m_error_line = line;
    rehighlight();
    Q_EMIT errorLineChanged();
}

void DescriptorHighlighter::highlightBlock(const QString& text)
{
    const bool error_line{m_error_line >= 0 && currentBlock().blockNumber() == m_error_line && m_error_color.isValid()};
    const auto apply = [&](qsizetype start, qsizetype length, const QColor& color) {
        QTextCharFormat format;
        if (color.isValid()) format.setForeground(color);
        if (error_line) format.setBackground(m_error_color);
        setFormat(int(start), int(length), format);
    };
    if (error_line) apply(0, text.size(), {});

    // Later passes win: a descriptor inside a JSON string keeps its own
    // colors, and an origin's path is part of the origin. Patterns with
    // groups color only the groups.
    for (const auto& [pattern, color] : {std::pair{&JSON_NAME, m_json_name_color}, {&JSON_VALUE, m_json_value_color},
                                         {&JSON_RANGE, m_json_value_color}, {&JSON_LINE_NUMBER, m_json_value_color},
                                         {&PATH, m_path_color}, {&ORIGIN, m_origin_color}, {&SCRIPT, m_script_color},
                                         {&CHECKSUM, m_checksum_color}}) {
        for (auto it = pattern->globalMatch(text); it.hasNext();) {
            const QRegularExpressionMatch match{it.next()};
            const int last_group{match.lastCapturedIndex()};
            for (int group = last_group > 0 ? 1 : 0; group <= last_group; ++group) {
                apply(match.capturedStart(group), match.capturedLength(group), color);
            }
        }
    }
    for (auto it = KEY.globalMatch(text); it.hasNext();) {
        const QRegularExpressionMatch match{it.next()};
        for (qsizetype offset = 0; offset < match.capturedLength(); offset += KEY_CHUNK) {
            const QColor& color{(offset / KEY_CHUNK) % 2 == 0 ? m_key_color : m_key_alternate_color};
            apply(match.capturedStart() + offset, std::min<qsizetype>(KEY_CHUNK, match.capturedLength() - offset), color);
        }
    }
}
