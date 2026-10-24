// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QtTest/QtTest>

#include <qml/applocale.h>

#include <QCoreApplication>
#include <QFile>
#include <QLocale>

class AppLocaleTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void systemDefaultInstallsSystemLanguageTranslations();
    void systemDefaultMatchesSystemScript();
};

void AppLocaleTests::systemDefaultInstallsSystemLanguageTranslations()
{
    QVERIFY(QFile::exists(QStringLiteral(":/translations/bitcoin_qml_es.qm")));
    const QLocale spanish_system{QStringLiteral("es_ES")};
    const auto label = [] { return QCoreApplication::translate("SettingsDisplay", "Language"); };

    QmlTranslations translations;
    translations.install(QString{}, spanish_system);
    QCOMPARE(label(), QStringLiteral("Idioma"));

    translations.install(QStringLiteral("en"), spanish_system);
    QCOMPARE(label(), QStringLiteral("Language"));

    translations.install(QString{}, spanish_system);
    QCOMPARE(label(), QStringLiteral("Idioma"));
}

void AppLocaleTests::systemDefaultMatchesSystemScript()
{
    const auto label = [] { return QCoreApplication::translate("AddressBookPage", "Create a new address"); };

    QmlTranslations translations;
    translations.install(QString{}, QLocale{QStringLiteral("sr_Latn_RS")});
    QCOMPARE(label(), QStringLiteral("Kreiraj novu adresu"));

    translations.install(QString{}, QLocale{QStringLiteral("sr_Cyrl_RS")});
    QCOMPARE(label(), QString::fromUtf8("Направи нову адресу"));
}

#ifdef BITCOINQML_NO_TEST_MAIN
#include <test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(AppLocaleTests)
#else
QTEST_MAIN(AppLocaleTests)
#endif
#include "test_applocale.moc"
