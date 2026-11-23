// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/localeformat.h>

#include <QList>

LocaleFormat& LocaleFormat::instance()
{
    static LocaleFormat format;
    return format;
}

void LocaleFormat::setLanguage(const QString& language)
{
    LocaleFormat& self = instance();
    if (self.m_language == language) return;
    self.m_language = language;
    Q_EMIT self.localeChanged();
}

QLocale LocaleFormat::locale()
{
    const QString& language = instance().m_language;
    return language.isEmpty() ? QLocale() : QLocale(language);
}

QString LocaleFormat::latinDigits(QString text, const QLocale& locale)
{
    const QList<uint> zero = locale.zeroDigit().toUcs4();
    if (zero.size() != 1 || zero.front() == U'0') return text;
    for (char32_t digit = 0; digit < 10; ++digit) {
        const char32_t native = zero.front() + digit;
        text.replace(QString::fromUcs4(&native, 1), QString(QChar(u'0' + digit)));
    }
    return text;
}

QString LocaleFormat::format(const QDateTime& date_time, const QString& pattern)
{
    const QLocale current = locale();
    return latinDigits(current.toString(date_time, pattern), current);
}

QString LocaleFormat::format(const QDate& date, const QString& pattern)
{
    const QLocale current = locale();
    return latinDigits(current.toString(date, pattern), current);
}
