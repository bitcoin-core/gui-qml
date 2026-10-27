// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_APPLOCALE_H
#define BITCOIN_QML_APPLOCALE_H

#include <QLocale>
#include <QObject>
#include <QString>
#include <QTranslator>

#include <memory>

class QmlTranslations
{
public:
    void install(const QString& language, const QLocale& system_locale = QLocale::system());

private:
    std::unique_ptr<QTranslator> m_app_translator;
    std::unique_ptr<QTranslator> m_qml_translator;
};

// Dates written with words follow the app language; numeric dates and times stay with QLocale().
QLocale QmlNamesLocale();

enum class QmlWordDate { Long, MonthYear };

QString QmlWordDateFormat(QmlWordDate kind, const QLocale& names = QmlNamesLocale(), const QLocale& regional = QLocale());

class QmlAppLocale : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;
    Q_INVOKABLE QString wordDateFormat(const QString& names_locale, bool month_year) const;
};

QString QmlLocaleName(const QLocale& locale);

#endif // BITCOIN_QML_APPLOCALE_H
