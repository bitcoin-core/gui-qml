// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/applocale.h>

#include <QCoreApplication>
#include <QRegularExpression>

namespace {
QStringList ScriptModifiers(QLocale::Script script)
{
    if (script == QLocale::LatinScript) return {QStringLiteral("latin"), QStringLiteral("Latn")};
    if (script == QLocale::CyrillicScript) return {QStringLiteral("Cyrl")};
    return {};
}

bool LoadSystemLanguage(QTranslator& translator, const QStringList& ui_languages, const QString& name)
{
    const QString directory = QStringLiteral(":/translations");
    for (const QString& ui_language : ui_languages) {
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

// Only the GUI thread reads or writes it, unlike QLocale::setDefault().
QLocale& NamesLocale()
{
    static QLocale locale = QLocale::system();
    return locale;
}

// Translation tags such as sr@latin name the script after '@', which QLocale ignores.
QLocale LanguageLocale(const QString& language)
{
    const qsizetype at = language.indexOf(QLatin1Char('@'));
    const QLocale base(language.left(at));
    if (at < 0) return base;
    const QString modifier = language.mid(at + 1).toLower();
    QLocale::Script script = base.script();
    if (modifier.endsWith(QLatin1String("latin")) || modifier == QLatin1String("latn")) script = QLocale::LatinScript;
    if (modifier == QLatin1String("cyrl")) script = QLocale::CyrillicScript;
    const QLocale locale(base.language(), script, base.territory());
    // Qt lacks some scripts, such as Kazakh in Latin; untranslated text is English, so dates match it.
    return locale.script() == script ? locale : QLocale(QLocale::English);
}

QString WithoutWords(const QString& pattern)
{
    QString result;
    QString punctuation;
    bool quoted = false;
    bool after_word = false;
    for (const QChar c : pattern) {
        const bool field = !quoted && (c == QLatin1Char('d') || c == QLatin1Char('M') || c == QLatin1Char('y'));
        if (c == QLatin1Char('\'') || (!field && (quoted || !(c.isSpace() || c.isPunct() || c.isSymbol())))) {
            if (c == QLatin1Char('\'')) quoted = !quoted;
            // Punctuation touching a word belongs to it, such as 'г'. or ('e').
            punctuation.clear();
            after_word = true;
        } else if (c.isSpace() || field) {
            if (field && after_word && !result.isEmpty() && result.back().isLetter()) result += QLatin1Char(' ');
            result += punctuation + c;
            punctuation.clear();
            after_word = false;
        } else if (!after_word) {
            punctuation += c;
        }
    }
    result = QString(result + punctuation).simplified();
    // Some regions write the month as a number next to a word, such as M月.
    if (!result.contains(QLatin1String("MMM"))) result.replace(QRegularExpression(QStringLiteral("M+")), QStringLiteral("MMMM"));
    return result;
}

QLocale SystemNamesLocale(const QLocale& system_locale, const QTranslator* translator)
{
    // Untranslated text is English, so dates match it.
    if (!translator) return QLocale(QLocale::English);
    const QLocale translated = LanguageLocale(translator->language());
    const bool same_language = translated.language() == system_locale.language() && translated.script() == system_locale.script();
    return same_language ? system_locale : translated;
}
} // namespace

QLocale QmlNamesLocale()
{
    return NamesLocale();
}

QString QmlLocaleName(const QLocale& locale)
{
    return QStringLiteral("%1_%2_%3").arg(QLocale::languageToCode(locale.language()),
                                          QLocale::scriptToCode(locale.script()),
                                          QLocale::territoryToCode(locale.territory()));
}

QString QmlWordDateFormat(QmlWordDate kind, const QLocale& names, const QLocale& regional)
{
    return QmlWordDateFormat(kind, names, regional, regional.dateFormat(QLocale::LongFormat));
}

QString QmlWordDateFormat(QmlWordDate kind, const QLocale& names, const QLocale& regional, const QString& regional_format)
{
    const QString fields = WithoutWords(regional_format);
    if (kind == QmlWordDate::Long) {
        // The system can report one language and take its date pattern from another, such as
        // LANG=es_ES with LC_TIME=ja_JP, so keep the words only when the pattern is the names language's own.
        const QLocale names_in_region(names.language(), names.script(), regional.territory());
        return regional_format == names_in_region.dateFormat(QLocale::LongFormat) ? regional_format : fields;
    }
    return fields.indexOf(QLatin1Char('y')) < fields.indexOf(QLatin1Char('M'))
        ? QStringLiteral("yyyy MMMM")
        : QStringLiteral("MMMM yyyy");
}

QString QmlAppLocale::wordDateFormat(const QString& names_locale, bool month_year) const
{
    return QmlWordDateFormat(month_year ? QmlWordDate::MonthYear : QmlWordDate::Long,
                             names_locale.isEmpty() ? QLocale() : QLocale(names_locale));
}

void QmlTranslations::install(const QString& language, const QLocale& system_locale)
{
    install(language, system_locale, system_locale.uiLanguages());
}

void QmlTranslations::install(const QString& language, const QLocale& system_locale, const QStringList& ui_languages)
{
    const auto load = [&](const QString& name) -> std::unique_ptr<QTranslator> {
        auto translator = std::make_unique<QTranslator>();
        const bool loaded = language.isEmpty()
            ? LoadSystemLanguage(*translator, ui_languages, name)
            : translator->load(QStringLiteral(":/translations/%1_%2.qm").arg(name, language));
        return loaded ? std::move(translator) : nullptr;
    };
    auto app_translator = load(QStringLiteral("bitcoin"));
    auto qml_translator = load(QStringLiteral("bitcoin_qml"));

    // Set first so the translators' LanguageChange events see the new names.
    NamesLocale() = language.isEmpty() ? SystemNamesLocale(system_locale, app_translator.get())
        : (app_translator || qml_translator) ? LanguageLocale(language) : QLocale(QLocale::English);

    const auto replace = [](std::unique_ptr<QTranslator>& slot, std::unique_ptr<QTranslator> translator) {
        if (slot) QCoreApplication::removeTranslator(slot.get());
        slot = std::move(translator);
        if (slot) QCoreApplication::installTranslator(slot.get());
    };
    replace(m_app_translator, std::move(app_translator));
    replace(m_qml_translator, std::move(qml_translator));
}
