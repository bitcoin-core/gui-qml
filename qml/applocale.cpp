// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/applocale.h>

#include <QCoreApplication>

namespace {
QStringList ScriptModifiers(QLocale::Script script)
{
    if (script == QLocale::LatinScript) return {QStringLiteral("latin"), QStringLiteral("Latn")};
    if (script == QLocale::CyrillicScript) return {QStringLiteral("Cyrl")};
    return {};
}

bool LoadSystemLanguage(QTranslator& translator, const QLocale& system_locale, const QString& name)
{
    const QString directory = QStringLiteral(":/translations");
    for (const QString& ui_language : system_locale.uiLanguages()) {
        const QLocale locale(ui_language);
        // Bundled files name the script after '@', such as sr@latin, which QTranslator's lookup never tries.
        for (const QString& modifier : ScriptModifiers(locale.script())) {
            const QString file = QStringLiteral("%1/%2_%3@%4.qm").arg(directory, name, QLocale::languageToCode(locale.language()), modifier);
            if (translator.load(file)) return true;
        }
        if (translator.load(locale, name, QStringLiteral("_"), directory)) return true;
    }
    return false;
}
} // namespace

void QmlTranslations::install(const QString& language, const QLocale& system_locale)
{
    const auto load = [&](std::unique_ptr<QTranslator>& slot, const QString& name) {
        if (slot) {
            QCoreApplication::removeTranslator(slot.get());
            slot.reset();
        }
        auto translator = std::make_unique<QTranslator>();
        const bool loaded = language.isEmpty()
            ? LoadSystemLanguage(*translator, system_locale, name)
            : translator->load(QStringLiteral(":/translations/%1_%2.qm").arg(name, language));
        if (loaded) {
            QCoreApplication::installTranslator(translator.get());
            slot = std::move(translator);
        }
    };
    load(m_app_translator, QStringLiteral("bitcoin"));
    load(m_qml_translator, QStringLiteral("bitcoin_qml"));
}
