// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_DESCRIPTORHIGHLIGHTER_H
#define BITCOIN_QML_DESCRIPTORHIGHLIGHTER_H

#include <QColor>
#include <QPointer>
#include <QQuickTextDocument>
#include <QSyntaxHighlighter>

//! Colors descriptors and extended keys wherever they appear in a text
//! field: script, key origin, key, derivation path and checksum, and the
//! names and values of pasted JSON. A line the parser rejected gets a
//! tinted background.
class DescriptorHighlighter : public QSyntaxHighlighter
{
    Q_OBJECT
    Q_PROPERTY(QQuickTextDocument* textDocument READ textDocument WRITE setTextDocument NOTIFY textDocumentChanged)
    Q_PROPERTY(QColor scriptColor READ scriptColor WRITE setScriptColor NOTIFY colorsChanged)
    Q_PROPERTY(QColor originColor READ originColor WRITE setOriginColor NOTIFY colorsChanged)
    Q_PROPERTY(QColor keyColor READ keyColor WRITE setKeyColor NOTIFY colorsChanged)
    Q_PROPERTY(QColor keyAlternateColor READ keyAlternateColor WRITE setKeyAlternateColor NOTIFY colorsChanged)
    Q_PROPERTY(QColor pathColor READ pathColor WRITE setPathColor NOTIFY colorsChanged)
    Q_PROPERTY(QColor checksumColor READ checksumColor WRITE setChecksumColor NOTIFY colorsChanged)
    Q_PROPERTY(QColor jsonNameColor READ jsonNameColor WRITE setJsonNameColor NOTIFY colorsChanged)
    Q_PROPERTY(QColor jsonValueColor READ jsonValueColor WRITE setJsonValueColor NOTIFY colorsChanged)
    Q_PROPERTY(QColor errorColor READ errorColor WRITE setErrorColor NOTIFY colorsChanged)
    Q_PROPERTY(int errorLine READ errorLine WRITE setErrorLine NOTIFY errorLineChanged)

public:
    explicit DescriptorHighlighter(QObject* parent = nullptr);

    QQuickTextDocument* textDocument() const { return m_text_document; }
    void setTextDocument(QQuickTextDocument* text_document);

    QColor scriptColor() const { return m_script_color; }
    void setScriptColor(const QColor& color) { setColor(m_script_color, color); }
    QColor originColor() const { return m_origin_color; }
    void setOriginColor(const QColor& color) { setColor(m_origin_color, color); }
    QColor keyColor() const { return m_key_color; }
    void setKeyColor(const QColor& color) { setColor(m_key_color, color); }
    QColor keyAlternateColor() const { return m_key_alternate_color; }
    void setKeyAlternateColor(const QColor& color) { setColor(m_key_alternate_color, color); }
    QColor pathColor() const { return m_path_color; }
    void setPathColor(const QColor& color) { setColor(m_path_color, color); }
    QColor checksumColor() const { return m_checksum_color; }
    void setChecksumColor(const QColor& color) { setColor(m_checksum_color, color); }
    QColor jsonNameColor() const { return m_json_name_color; }
    void setJsonNameColor(const QColor& color) { setColor(m_json_name_color, color); }
    QColor jsonValueColor() const { return m_json_value_color; }
    void setJsonValueColor(const QColor& color) { setColor(m_json_value_color, color); }
    QColor errorColor() const { return m_error_color; }
    void setErrorColor(const QColor& color) { setColor(m_error_color, color); }

    //! Zero-based line to mark, or -1.
    int errorLine() const { return m_error_line; }
    void setErrorLine(int line);

Q_SIGNALS:
    void textDocumentChanged();
    void colorsChanged();
    void errorLineChanged();

protected:
    void highlightBlock(const QString& text) override;

private:
    void setColor(QColor& member, const QColor& color);

    QPointer<QQuickTextDocument> m_text_document;
    QColor m_script_color;
    QColor m_origin_color;
    QColor m_key_color;
    QColor m_key_alternate_color;
    QColor m_path_color;
    QColor m_checksum_color;
    QColor m_json_name_color;
    QColor m_json_value_color;
    QColor m_error_color;
    int m_error_line{-1};
};

#endif // BITCOIN_QML_DESCRIPTORHIGHLIGHTER_H
