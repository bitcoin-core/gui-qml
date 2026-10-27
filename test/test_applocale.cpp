// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QtTest/QtTest>

#include <qml/applocale.h>

#include <QCoreApplication>
#include <QFile>
#include <QLocale>
#include <QScopeGuard>

class AppLocaleTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void systemDefaultInstallsSystemLanguageTranslations();
    void namesFollowLanguageWithoutChangingRegionalFormats();
    void localeNameIdentifiesScript();
    void scriptModifierTagsSelectScript();
    void wordDatesKeepRegionalOrder();
    void missingScriptFallsBackToEnglish();
};

void AppLocaleTests::systemDefaultInstallsSystemLanguageTranslations()
{
    QVERIFY(QFile::exists(QStringLiteral(":/translations/bitcoin_qml_es.qm")));
    const QLocale spanish_system{QStringLiteral("es_ES")};
    const auto label = [] { return QCoreApplication::translate("SettingsDisplay", "Language"); };

    QmlTranslations translations;
    auto restore = qScopeGuard([&] { translations.install(QString{}); });
    translations.install(QString{}, spanish_system);
    QCOMPARE(label(), QStringLiteral("Idioma"));

    translations.install(QStringLiteral("en"), spanish_system);
    QCOMPARE(label(), QStringLiteral("Language"));

    translations.install(QString{}, spanish_system);
    QCOMPARE(label(), QStringLiteral("Idioma"));
}

void AppLocaleTests::namesFollowLanguageWithoutChangingRegionalFormats()
{
    const QLocale regional;
    const QLocale swiss_system{QStringLiteral("de_CH")};
    QmlTranslations translations;
    auto restore = qScopeGuard([&] { translations.install(QString{}); });

    translations.install(QStringLiteral("es"), swiss_system);
    QCOMPARE(QmlNamesLocale().language(), QLocale::Spanish);
    QCOMPARE(QLocale(), regional);

    translations.install(QStringLiteral("pt_BR"), swiss_system);
    QCOMPARE(QmlNamesLocale().territory(), QLocale::Brazil);
    QCOMPARE(QLocale(), regional);

    translations.install(QString{}, swiss_system);
    QCOMPARE(QmlNamesLocale(), swiss_system);
}

void AppLocaleTests::localeNameIdentifiesScript()
{
    const QLocale latin_system{QStringLiteral("sr_Latn_RS")};
    QmlTranslations translations;
    auto restore = qScopeGuard([&] { translations.install(QString{}); });

    translations.install(QString{}, latin_system);
    QCOMPARE(QmlLocaleName(QmlNamesLocale()), QStringLiteral("sr_Latn_RS"));

    translations.install(QStringLiteral("sr"), latin_system);
    QCOMPARE(QmlLocaleName(QmlNamesLocale()), QStringLiteral("sr_Cyrl_RS"));
    QCOMPARE(QmlNamesLocale().monthName(1), QString::fromUtf8("јануар"));
}

void AppLocaleTests::scriptModifierTagsSelectScript()
{
    QmlTranslations translations;
    auto restore = qScopeGuard([&] { translations.install(QString{}); });

    translations.install(QStringLiteral("sr@latin"));
    QCOMPARE(QmlNamesLocale().monthName(1), QStringLiteral("januar"));
    translations.install(QStringLiteral("sr@ijekavianlatin"));
    QCOMPARE(QmlNamesLocale().monthName(1), QStringLiteral("januar"));
    translations.install(QStringLiteral("uz@Cyrl"));
    QCOMPARE(QmlNamesLocale().script(), QLocale::CyrillicScript);
}

void AppLocaleTests::wordDatesKeepRegionalOrder()
{
    const QDate date(2026, 1, 15);
    const QLocale english{QStringLiteral("en_US")};
    const auto word_date = [&](const char* region, QmlWordDate kind) {
        return english.toString(date, QmlWordDateFormat(kind, english, QLocale{QString::fromLatin1(region)}));
    };
    QCOMPARE(word_date("fr_CA", QmlWordDate::Long), QStringLiteral("Thursday 15 January 2026"));
    QCOMPARE(word_date("es_ES", QmlWordDate::Long), QStringLiteral("Thursday, 15 January 2026"));
    QCOMPARE(word_date("ja_JP", QmlWordDate::Long), QStringLiteral("2026 January 15 Thursday"));
    QCOMPARE(word_date("ja_JP", QmlWordDate::MonthYear), QStringLiteral("2026 January"));
    QCOMPARE(word_date("nl_NL", QmlWordDate::MonthYear), QStringLiteral("January 2026"));

    const QLocale spanish{QStringLiteral("es_ES")};
    QCOMPARE(spanish.toString(date, QmlWordDateFormat(QmlWordDate::Long, spanish, spanish)), QStringLiteral("jueves, 15 de enero de 2026"));
    const QLocale ngiemboon{QStringLiteral("nnh_CM")};
    QCOMPARE(QmlWordDateFormat(QmlWordDate::MonthYear, ngiemboon, ngiemboon), QStringLiteral("MMMM yyyy"));
}

void AppLocaleTests::missingScriptFallsBackToEnglish()
{
    QmlTranslations translations;
    auto restore = qScopeGuard([&] { translations.install(QString{}); });

    translations.install(QStringLiteral("kk@latin"));
    QCOMPARE(QmlNamesLocale().monthName(1), QStringLiteral("January"));
}

#ifdef BITCOINQML_NO_TEST_MAIN
#include <test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(AppLocaleTests)
#else
QTEST_MAIN(AppLocaleTests)
#endif
#include "test_applocale.moc"
