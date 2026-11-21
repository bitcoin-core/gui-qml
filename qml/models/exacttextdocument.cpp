// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/models/exacttextdocument.h>
#include <QTextDocument>
void ExactTextDocument::setDocument(QQuickTextDocument* document)
{
    if (m_document == document) return;
    disconnect(m_changed); disconnect(m_destroyed);
    m_document = document;
    if (document && document->textDocument()) {
        m_changed = connect(document->textDocument(), &QTextDocument::contentsChanged, this, &ExactTextDocument::refresh);
        m_destroyed = connect(document, &QObject::destroyed, this, [this] {
            m_document.clear();
            const QPointer<ExactTextDocument> guard{this};
            refresh(); if (guard) Q_EMIT documentChanged();
        });
    }
    const QPointer<ExactTextDocument> guard{this};
    refresh(); if (guard) Q_EMIT documentChanged();
}
void ExactTextDocument::refresh()
{
    QString text;
    const auto* document = m_document ? m_document->textDocument() : nullptr;
    const bool valid = !document || document->characterCount() <= 32769;
    if (document && valid) {
        text = document->toRawText();
        text.replace(QChar::ParagraphSeparator, QLatin1Char('\n'));
    }
    if (m_text == text && m_valid == valid) return;
    m_text = std::move(text); m_valid = valid; Q_EMIT changed();
}
