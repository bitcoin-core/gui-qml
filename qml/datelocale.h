// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_DATELOCALE_H
#define BITCOIN_QML_DATELOCALE_H

#include <QLocale>
#include <QObject>

namespace QmlUtil {

/**
 * Copy of the locale without system overrides, for date patterns. The system
 * locale names months from the user's calendar (Persian, Hebrew, ...) while Qt
 * fills the day and year from the Gregorian one.
 */
inline QLocale GregorianLocale(const QLocale& locale = QLocale())
{
    return QLocale{locale.language(), locale.script(), locale.territory()};
}

} // namespace QmlUtil

//! Gives QML the same date locale as the C++ models.
class DateLocale : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QLocale gregorian READ gregorian CONSTANT)

public:
    explicit DateLocale(QObject* parent = nullptr) : QObject(parent) {}

    QLocale gregorian() const { return QmlUtil::GregorianLocale(); }
};

#endif // BITCOIN_QML_DATELOCALE_H
