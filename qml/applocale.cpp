// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/applocale.h>

#include <QCoreApplication>

void QmlTranslations::install(const QString& language, const QLocale& system_locale)
{
    const auto load = [&](std::unique_ptr<QTranslator>& slot, const QString& name) {
        if (slot) {
            QCoreApplication::removeTranslator(slot.get());
            slot.reset();
        }
        auto translator = std::make_unique<QTranslator>();
        const bool loaded = language.isEmpty()
            ? translator->load(system_locale, name, QStringLiteral("_"), QStringLiteral(":/translations"))
            : translator->load(QStringLiteral(":/translations/%1_%2.qm").arg(name, language));
        if (loaded) {
            QCoreApplication::installTranslator(translator.get());
            slot = std::move(translator);
        }
    };
    load(m_app_translator, QStringLiteral("bitcoin"));
    load(m_qml_translator, QStringLiteral("bitcoin_qml"));
}
