// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/applocale.h>

#include <QCoreApplication>

void QmlTranslations::install(const QString& language)
{
    const auto load = [&](std::unique_ptr<QTranslator>& slot, const QString& name) {
        if (slot) {
            QCoreApplication::removeTranslator(slot.get());
            slot.reset();
        }
        if (language.isEmpty()) return;
        auto translator = std::make_unique<QTranslator>();
        if (translator->load(QStringLiteral(":/translations/%1_%2.qm").arg(name, language))) {
            QCoreApplication::installTranslator(translator.get());
            slot = std::move(translator);
        }
    };
    load(m_app_translator, QStringLiteral("bitcoin"));
    load(m_qml_translator, QStringLiteral("bitcoin_qml"));
}
