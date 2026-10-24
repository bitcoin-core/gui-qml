// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/applocale.h>

#include <QCoreApplication>
#include <QRegularExpression>

namespace {
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
    const QString regional_format = regional.dateFormat(QLocale::LongFormat);
    const QString fields = WithoutWords(regional_format);
    if (kind == QmlWordDate::Long) {
        const bool same_language = names.language() == regional.language() && names.script() == regional.script();
        return same_language ? regional_format : fields;
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
    // Set first so the translators' LanguageChange events see the new names.
    NamesLocale() = language.isEmpty() ? system_locale : LanguageLocale(language);

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
