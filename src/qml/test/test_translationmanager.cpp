// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <common/args.h>
#include <common/settings.h>
#include <univalue.h>
#include <qml/core_settings.h>
#include <qml/guiargs.h>
#include <qml/guiconstants.h>
#include <qml/models/languagesettingsmodel.h>
#include <qml/models/settings_keys.h>
#include <qml/onboarding_settings.h>
#include <qml/test/mocks/stubnode.h>
#include <qml/translationmanager.h>

#include <QLocale>
#include <QDir>
#include <QFile>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include <memory>

namespace {
class LanguageNode : public StubNode
{
public:
    explicit LanguageNode(ArgsManager& args) : m_args(args) {}
    common::SettingsValue getPersistentSetting(const std::string& name) override { return m_args.GetPersistentSetting(name); }
    void updateRwSetting(const std::string& name, const common::SettingsValue& value) override
    {
        ++writes;
        QmlCoreSettings::SetRwSetting(m_args, QString::fromStdString(name), value);
    }
    int writes{0};
private:
    ArgsManager& m_args;
};
}

class TranslationManagerTests : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void init()
    {
        m_previous_org = QCoreApplication::organizationName();
        m_previous_app = QCoreApplication::applicationName();
        QCoreApplication::setOrganizationName(QStringLiteral(QAPP_ORG_NAME));
        QCoreApplication::setApplicationName(QStringLiteral(QAPP_APP_NAME_DEFAULT));
        m_previous_locale = QLocale();
        m_saved_language = QSettings().value(SettingsKeys::LANGUAGE);
        QSettings().remove(SettingsKeys::LANGUAGE);
        Q_INIT_RESOURCE(qml_gui_translations);
        Q_INIT_RESOURCE(bitcoin_qt_translations);
    }

    void cleanup()
    {
        if (m_saved_language.isValid()) QSettings().setValue(SettingsKeys::LANGUAGE, m_saved_language);
        else QSettings().remove(SettingsKeys::LANGUAGE);
        QLocale::setDefault(m_previous_locale);
        QCoreApplication::setOrganizationName(m_previous_org);
        QCoreApplication::setApplicationName(m_previous_app);
    }

    void startupUsesCorePrecedenceIncludingExplicitDefault()
    {
        ArgsManager args;
        SetupQmlGuiArgs(args);
        QSettings().setValue(SettingsKeys::LANGUAGE, "de");
        QCOMPARE(TranslationManager::ResolveLanguage(args), QString("de"));
        args.LockSettings([](common::Settings& settings) { settings.ro_config[""]["lang"] = {"fr"}; });
        QCOMPARE(TranslationManager::ResolveLanguage(args), QString("fr"));
        args.LockSettings([](common::Settings& settings) { settings.rw_settings["lang"] = "es"; });
        QCOMPARE(TranslationManager::ResolveLanguage(args), QString("es"));
        const char* argv[]{"bitcoin-qt", "-lang="};
        std::string error;
        QVERIFY(args.ParseParameters(2, argv, error));
        QCOMPARE(TranslationManager::ResolveLanguage(args), QString{});
        TranslationManager translations;
        LanguageNode node(args);
        LanguageSettingsModel language(node, args, translations);
        QVERIFY(!language.status().value("canEdit").toBool());
        language.setLanguage("es");
        QCOMPARE(language.language(), QString{});
        QCOMPARE(node.writes, 0);
        QCOMPARE(QSettings().value(SettingsKeys::LANGUAGE).toString(), QString("de"));
    }

    void settingsUseTheSameServiceAndPersistDefault()
    {
        ArgsManager args;
        SetupQmlGuiArgs(args);
        args.LockSettings([](common::Settings& settings) { settings.ro_config[""]["lang"] = {"fr"}; });
        TranslationManager translations;
        LanguageNode node(args);
        LanguageSettingsModel language(node, args, translations);
        QCOMPARE(language.language(), QString("fr"));
        QSignalSpy changed(&language, &LanguageSettingsModel::languageChanged);
        language.setLanguage("es");
        QCOMPARE(language.language(), translations.language());
        QCOMPARE(TranslationManager::ResolveLanguage(args), QString("es"));
        QCOMPARE(QSettings().value(SettingsKeys::LANGUAGE).toString(), QString("es"));
        QCOMPARE(node.writes, 1);
        QCOMPARE(changed.count(), 1);
        language.setLanguage("es");
        QCOMPARE(node.writes, 1);
        language.setLanguage("");
        QCOMPARE(TranslationManager::ResolveLanguage(args), QString{});
        QCOMPARE(QLocale().name(), QLocale::system().name());
        QVERIFY(language.availableLanguages().contains("es"));
        QCOMPARE(language.availableLanguages().first(), QString{});
        TranslationManager restarted;
        LanguageSettingsModel reloaded(node, args, restarted);
        QCOMPARE(reloaded.language(), QString{});
    }

    void bootstrapUsesTheSelectedNetworksSettingsStore()
    {
        ArgsManager args;
        SetupQmlGuiArgs(args);
        args.ForceSetArg("-chain", "regtest");
        QSettings().setValue(SettingsKeys::LANGUAGE, "de");
        QSettings regtest(QSettings::defaultFormat(), QSettings::UserScope,
            QStringLiteral(QAPP_ORG_NAME), QStringLiteral(QAPP_APP_NAME_REGTEST));
        const auto saved = regtest.value(SettingsKeys::LANGUAGE);
        regtest.setValue(SettingsKeys::LANGUAGE, "es");
        const QString language = TranslationManager::ResolveLanguage(args);
        if (saved.isValid()) regtest.setValue(SettingsKeys::LANGUAGE, saved);
        else regtest.remove(SettingsKeys::LANGUAGE);
        QCOMPARE(language, QString("es"));
        QCOMPARE(QSettings().value(SettingsKeys::LANGUAGE).toString(), QString("de"));
    }

    void onboardingPreviewResolvesTheExistingProfileBeforeQml()
    {
        QTemporaryDir profile;
        QVERIFY(profile.isValid());
        QFile config(profile.filePath("bitcoin.conf"));
        QVERIFY(config.open(QIODevice::WriteOnly));
        QVERIFY(config.write("regtest=1\nlang=fr\n") > 0);
        config.close();
        QVERIFY(QDir(profile.path()).mkdir("regtest"));
        QFile settings(profile.filePath("regtest/settings.json"));
        QVERIFY(settings.open(QIODevice::WriteOnly));
        QVERIFY(settings.write("{\"lang\":\"es\"}") > 0);
        settings.close();
        std::vector<std::string> argv{"bitcoin-qt", "-datadir=" + profile.path().toStdString(), "-choosedatadir"};
        auto status = QmlOnboardingSettings::ResolveOnboardingStartupStatus(argv, false);
        QVERIFY2(status.ok, qPrintable(status.error));
        QVERIFY(status.should_show_onboarding);
        QCOMPARE(status.language, QString("es"));
        argv.push_back("-lang=");
        status = QmlOnboardingSettings::ResolveOnboardingStartupStatus(argv, false);
        QVERIFY2(status.ok, qPrintable(status.error));
        QCOMPARE(status.language, QString{});
    }

    void localeInstalledBeforeQmlAndRetranslatesLiveEngines()
    {
        TranslationManager translations;
        translations.setLanguage("es_MX");
        QCOMPARE(QCoreApplication::translate("GlobalMenu", "Menu"), QString::fromUtf8("Menú"));
        QQmlEngine engine;
        translations.attachEngine(engine);
        QQmlComponent component(&engine);
        component.setData("import QtQml 2.15; QtObject { property string caption: qsTr(\"Menu\") }", QUrl("qrc:/GlobalMenu.qml"));
        QTRY_COMPARE(component.status(), QQmlComponent::Ready);
        std::unique_ptr<QObject> object(component.create());
        QVERIFY2(object, qPrintable(component.errorString()));
        QCOMPARE(object->property("caption").toString(), QString("Menú"));
        translations.setLanguage("en");
        QCOMPARE(object->property("caption").toString(), QString("Menu"));
        translations.setLanguage("es");
        QCOMPARE(object->property("caption").toString(), QString("Menú"));
        // Destroying an onboarding engine must leave no stale retranslation target.
        {
            QQmlEngine onboarding;
            translations.attachEngine(onboarding);
        }
        translations.setLanguage("");
        QCOMPARE(QLocale().name(), QLocale::system().name());
        const QString expected = QCoreApplication::translate("GlobalMenu", "Menu");
        QCOMPARE(object->property("caption").toString(), expected);
    }

private:
    QVariant m_saved_language;
    QLocale m_previous_locale;
    QString m_previous_org;
    QString m_previous_app;
};

#ifdef BITCOINQML_NO_TEST_MAIN
#include <qml/test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(TranslationManagerTests)
#else
QTEST_MAIN(TranslationManagerTests)
#endif
#include "test_translationmanager.moc"
