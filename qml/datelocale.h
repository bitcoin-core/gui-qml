// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_DATELOCALE_H
#define BITCOIN_QML_DATELOCALE_H

#include <QLocale>
#include <QObject>
#include <QString>
#include <QtGlobal>

namespace QmlUtil {

/**
 * Locale whose month names the system would use. On Linux that is LC_TIME,
 * which can name a different language than the locale itself. A default set
 * with QLocale::setDefault wins.
 */
inline QLocale TimeLocale()
{
#if defined(Q_OS_UNIX) && !defined(Q_OS_DARWIN) && !defined(Q_OS_ANDROID)
    if (QLocale() == QLocale::system()) {
        for (const char* variable : {"LC_ALL", "LC_TIME", "LANG"}) {
            const QString name{qEnvironmentVariable(variable)};
            if (name.isEmpty()) continue;
            const QLocale locale{name};
            // QLocale ignores the @latin modifier, as in sr_RS@latin.
            if (name.contains(QLatin1String("@latin"))) return QLocale{locale.language(), QLocale::LatinScript, locale.territory()};
            return locale;
        }
    }
#endif
    return QLocale();
}

/**
 * Copy of the locale without system overrides, for date patterns. The system
 * locale names months from the user's calendar (Persian, Hebrew, ...) while Qt
 * fills the day and year from the Gregorian one.
 */
inline QLocale GregorianLocale(const QLocale& locale = TimeLocale())
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
