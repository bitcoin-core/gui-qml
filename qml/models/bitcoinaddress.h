// Copyright (c) 2025-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_BITCOINADDRESS_H
#define BITCOIN_QML_MODELS_BITCOINADDRESS_H

#include <QObject>
#include <QString>

/** An address being entered: the characters that can belong to an address,
 * and the same text grouped in fours for display. Input is normalized
 * immediately; a scan stops once it has a full-length address.
 */
class BitcoinAddress : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString address READ address NOTIFY addressChanged)
    Q_PROPERTY(QString formattedAddress READ formattedAddress NOTIFY formattedAddressChanged)
    Q_PROPERTY(QString ellipsesAddress READ ellipsesAddress NOTIFY ellipsesAddressChanged)
    Q_PROPERTY(int normalizedCursor READ normalizedCursor NOTIFY normalizationChanged)
public:
    explicit BitcoinAddress(QObject* parent = nullptr);
    BitcoinAddress(const QString& address, QObject* parent = nullptr);
    // The canonical value and formatting were prepared off GUI. This only
    // publishes bounded presentation.
    void projectAddress(QString address, QString formatted);
    int normalizedCursor() const { return m_cursor; }
    /** Replace the characters from start to end of the formatted address
     * with text. Returns the cursor position after the pasted text. */
    Q_INVOKABLE int pasteAddress(const QString& text, int start, int end);
    static QString ellipsesAddress(const QString& address);
    static QString formattedAddress(const QString& address);
    QString address() const;
    bool isEmpty() const;
    QString formattedAddress() const;
    QString ellipsesAddress() const;
    /** Normalize input and return the cursor position in the formatted
     * address. */
    Q_INVOKABLE int setAddress(const QString& address, int cursorPosition = 0);
Q_SIGNALS:
    void addressChanged();
    void formattedAddressChanged();
    void ellipsesAddressChanged();
    void normalizationChanged();
private:
    struct Normalized { QString address, formatted; int cursor{0}; };
    static Normalized NormalizeEditorInput(const QString& input, int cursor);
    int apply(Normalized value, bool force);
    int m_cursor{0};
    QString m_address;
    QString m_formattedAddress;
};

#endif // BITCOIN_QML_MODELS_BITCOINADDRESS_H
