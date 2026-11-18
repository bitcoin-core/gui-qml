// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_QML_SETTINGSPARSE_H
#define BITCOIN_QML_SETTINGSPARSE_H
#include <QString>
#include <qml/guiconstants.h>
#include <limits>
#include <charconv>
#include <cstdint>
#include <optional>
namespace settingsparse {
inline int StorageGB(int64_t mib)
{
    if(mib<=0)return 0;
    if(mib>int64_t{std::numeric_limits<int>::max()}*static_cast<int64_t>(GB_BYTES)/(1024*1024))return std::numeric_limits<int>::max();
    return (mib*1024*1024+GB_BYTES-1)/GB_BYTES;
}
/** Domain-only exact scalar parsing; no numeric prefixes or floating rounding. */
inline std::optional<int64_t> Integer(const QString& raw,qsizetype budget=32768)
{
    if(raw.size()>budget||raw.contains(QChar::Null))return {};
    const auto text=raw.trimmed().toLatin1();if(text.isEmpty())return {};
    int64_t value{0};const char* begin=text.constData();if(*begin=='+')++begin;
    if(begin==text.constData()+text.size())return {};
    const auto parsed=std::from_chars(begin,text.constData()+text.size(),value);
    if(parsed.ec!=std::errc{}||parsed.ptr!=text.constData()+text.size())return {};
    return value;
}
}
#endif // BITCOIN_QML_SETTINGSPARSE_H
