// Copyright (c) 2025-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/bitcoinaddress.h>
#include <QPointer>
#include <algorithm>
#include <limits>

namespace {
constexpr int MAX_BITCOIN_ADDRESS_LENGTH{90};
bool IsBitcoinAddressChar(QChar c) {
    const ushort ch = c.unicode();
    return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z' && ch != 'I' && ch != 'O');
}
}
BitcoinAddress::BitcoinAddress(QObject* parent) : QObject(parent) {}
BitcoinAddress::BitcoinAddress(const QString& address, QObject* parent) : BitcoinAddress(parent) { setAddress(address, 0); }
QString BitcoinAddress::address() const { return m_address; }
bool BitcoinAddress::isEmpty() const { return m_address.isEmpty(); }
QString BitcoinAddress::formattedAddress() const { return m_formattedAddress; }
QString BitcoinAddress::ellipsesAddress() const { return ellipsesAddress(m_address); }
BitcoinAddress::Normalized BitcoinAddress::NormalizeEditorInput(const QString& input, int cursor)
{
    // The scan stops at a full-length address, so its cost is bounded by the
    // leading characters that cannot belong to an address (about 1 ns each).
    Normalized value; value.address.reserve(MAX_BITCOIN_ADDRESS_LENGTH);
    int before{0};
    for (qsizetype i{0}; i < input.size() && value.address.size() < MAX_BITCOIN_ADDRESS_LENGTH; ++i) {
        if (!IsBitcoinAddressChar(input[i])) continue;
        value.address += input[i];
        if (i < std::max(cursor, 0)) ++before;
    }
    value.formatted = formattedAddress(value.address);
    int seen{0};
    while (value.cursor < value.formatted.size() && seen < before) {
        if (value.formatted[value.cursor] != QLatin1Char{' '}) ++seen;
        ++value.cursor;
    }
    return value;
}
int BitcoinAddress::apply(Normalized value, bool force)
{
    const bool changed = value.address != m_address || value.formatted != m_formattedAddress;
    m_address = std::move(value.address); m_formattedAddress = std::move(value.formatted); m_cursor = value.cursor;
    // Stop if a listener destroys this object.
    const QPointer<BitcoinAddress> guard{this};
    if (changed) { Q_EMIT addressChanged(); if (!guard) return value.cursor; }
    if (changed || force) { Q_EMIT formattedAddressChanged(); if (!guard) return value.cursor; }
    if (changed) { Q_EMIT ellipsesAddressChanged(); if (!guard) return value.cursor; }
    Q_EMIT normalizationChanged();
    return value.cursor;
}
void BitcoinAddress::projectAddress(QString address, QString formatted)
{
    if (address.size() > 1024 || formatted.size() > 2048) return;
    const int cursor = static_cast<int>(formatted.size());
    apply({std::move(address), std::move(formatted), cursor}, true);
}
int BitcoinAddress::setAddress(const QString& input, int cursor)
{
    auto value = NormalizeEditorInput(input, cursor);
    const bool force = input != value.formatted;
    return apply(std::move(value), force);
}
int BitcoinAddress::pasteAddress(const QString& text, int start, int end)
{
    start = std::clamp(start, 0, static_cast<int>(m_formattedAddress.size()));
    end = std::clamp(end, start, static_cast<int>(m_formattedAddress.size()));
    const auto cursor = start + static_cast<int>(std::min<qsizetype>(text.size(), std::numeric_limits<int>::max() - start));
    return setAddress(m_formattedAddress.left(start) + text + m_formattedAddress.mid(end), cursor);
}
QString BitcoinAddress::formattedAddress(const QString& address)
{
    QString fmt; fmt.reserve(address.size() + address.size() / 4);
    for (qsizetype i = 0; i < address.size(); ++i) { if (i > 0 && i % 4 == 0) fmt += QLatin1Char{' '}; fmt += address[i]; }
    return fmt;
}
QString BitcoinAddress::ellipsesAddress(const QString& address)
{
    if (address.size() <= 8) return address;
    return address.left(4) + ' ' + address.mid(4, 4) + " ... " + address.mid(address.size() - 8, 4) + ' ' + address.right(4);
}
