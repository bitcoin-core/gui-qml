// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_APPLOCALE_H
#define BITCOIN_QML_APPLOCALE_H

#include <QLocale>
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

#endif // BITCOIN_QML_APPLOCALE_H
