// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_QML_MODELS_EXACTTEXTDOCUMENT_H
#define BITCOIN_QML_MODELS_EXACTTEXTDOCUMENT_H
#include <QObject>
#include <QPointer>
#include <QQuickTextDocument>
#include <QString>
/** Necessary bounded native-text handoff. QTextDocument::toPlainText replaces
 * NBSP with ASCII space; signed messages require the document's real content.
 * Native paragraph boundaries represent entered newlines, not rich formatting.
 */
class ExactTextDocument : public QObject {
    Q_OBJECT
    Q_PROPERTY(QQuickTextDocument* document READ document WRITE setDocument NOTIFY documentChanged)
    Q_PROPERTY(QString text READ text NOTIFY changed)
    Q_PROPERTY(bool valid READ valid NOTIFY changed)
public:
    explicit ExactTextDocument(QObject* parent = nullptr) : QObject(parent) {}
    QQuickTextDocument* document() const { return m_document; }
    void setDocument(QQuickTextDocument* document);
    QString text() const { return m_text; }
    bool valid() const { return m_valid; }
Q_SIGNALS:
    void documentChanged();
    void changed();
private:
    void refresh();
    QPointer<QQuickTextDocument> m_document;
    QMetaObject::Connection m_changed, m_destroyed;
    QString m_text;
    bool m_valid{true};
};
#endif // BITCOIN_QML_MODELS_EXACTTEXTDOCUMENT_H
