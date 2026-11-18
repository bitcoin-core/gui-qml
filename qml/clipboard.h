// Copyright (c) 2024 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_CLIPBOARD_H
#define BITCOIN_QML_CLIPBOARD_H

#include <QObject>
#include <QClipboard>
#include <QGuiApplication>
#include <QImage>

class Clipboard : public QObject
{
    Q_OBJECT
    Q_PROPERTY(quint64 revision READ revision NOTIFY dataChanged)
    //! The clipboard text, for bindings; text() reads the same value.
    Q_PROPERTY(QString currentText READ text NOTIFY dataChanged)
    Q_PROPERTY(quint64 selectionRevision READ selectionRevision NOTIFY selectionChanged)
    Q_PROPERTY(bool primarySelectionSupported READ primarySelectionSupported CONSTANT)
public:
    explicit Clipboard(QObject *parent = nullptr) : QObject(parent) {
        connect(QGuiApplication::clipboard(), &QClipboard::dataChanged,
                this, [this] { ++m_revision; Q_EMIT dataChanged(); });
        connect(QGuiApplication::clipboard(), &QClipboard::selectionChanged,
                this, [this] { ++m_selection_revision; Q_EMIT selectionChanged(); });
    }
    quint64 revision() const { return m_revision; }
    quint64 selectionRevision() const { return m_selection_revision; }
    bool primarySelectionSupported() const { return QGuiApplication::clipboard()->supportsSelection(); }

Q_SIGNALS:
    void dataChanged();
    void selectionChanged();

public:
    Q_INVOKABLE void setText(const QString &text) {
        QGuiApplication::clipboard()->setText(text);
    }

    Q_INVOKABLE QString text() const {
        return QGuiApplication::clipboard()->text();
    }
    Q_INVOKABLE QString selectionText() const {
        return primarySelectionSupported() ? QGuiApplication::clipboard()->text(QClipboard::Selection) : QString{};
    }

    Q_INVOKABLE bool setImage(const QImage& image) {
        if (image.isNull()) return false;
        QGuiApplication::clipboard()->setImage(image);
        return true;
    }
private:
    quint64 m_revision{0}, m_selection_revision{0};
};

#endif // BITCOIN_QML_CLIPBOARD_H
