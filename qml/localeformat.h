// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_LOCALEFORMAT_H
#define BITCOIN_QML_LOCALEFORMAT_H

#include <QDateTime>
#include <QLocale>
#include <QObject>
#include <QString>

// Dates use the in-app language for month and day names but always Latin
// digits, matching amounts, addresses and heights.
class LocaleFormat : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString name READ name NOTIFY localeChanged)
    // The locale's short date and time pattern, for formatDate().
    Q_PROPERTY(QString shortDateTimePattern READ shortDateTimePattern NOTIFY localeChanged)

public:
    static LocaleFormat& instance();

    // An empty language falls back to the default QLocale.
    static void setLanguage(const QString& language);
    static QLocale locale();
    static QString latinDigits(QString text, const QLocale& locale);
    static QString format(const QDateTime& date_time, const QString& pattern);
    static QString format(const QDate& date, const QString& pattern);

    QString name() const { return locale().name(); }
    QString shortDateTimePattern() const { return locale().dateTimeFormat(QLocale::ShortFormat); }

    Q_INVOKABLE QString formatDate(const QDateTime& date_time, const QString& pattern) const
    {
        return format(date_time, pattern);
    }
    Q_INVOKABLE QString latinDigits(const QString& text) const
    {
        return latinDigits(text, locale());
    }

Q_SIGNALS:
    void localeChanged();

private:
    LocaleFormat() = default;
    QString m_language;
};

#endif // BITCOIN_QML_LOCALEFORMAT_H
