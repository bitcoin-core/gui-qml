// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/integration/runner/test_cases.h>
#include <test/integration/fixture/test_util.h>
#include <qml/models/core_settings_model.h>
#include <qml/models/desktopwindowbehaviormodel.h>

#include <QSettings>
#include <QDesktopServices>
#include <QScopeGuard>
#include <QUrl>

#include <tuple>

class SettingsIntegrationTests : public QObject
{
    Q_OBJECT
    BitcoinQmlApplication& m_app;
    QList<QUrl> m_opened_urls;

    QObject* openProxySettings()
    {
        if (!qmltest::SelectSettings(m_app, "connection") || !qmltest::Click(m_app, "proxySettingsRow")) return nullptr;
        return qmltest::WaitForVisible(m_app, "proxySettingsPage");
    }

    QString persistentProxy(const std::string& key)
    {
        const auto value = qmltest::Call(m_app, &interfaces::Node::getPersistentSetting, key);
        return value.isStr() ? QString::fromStdString(value.get_str()) : QString{};
    }

public:
    explicit SettingsIntegrationTests(BitcoinQmlApplication& app) : m_app{app} {}

public Q_SLOTS:
    void recordOpenedUrl(const QUrl& url) { m_opened_urls.append(url); }

private Q_SLOTS:
    void aboutLinkConfirmationCancelsAndReopensWithoutNavigation()
    {
        // Catch accidental launches without opening a real browser.
        QDesktopServices::setUrlHandler(QStringLiteral("https"), this, "recordOpenedUrl");
        const auto restore_handler = qScopeGuard([] { QDesktopServices::unsetUrlHandler(QStringLiteral("https")); });
        QVERIFY(qmltest::SelectSettings(m_app, "about"));
        QPointer<QObject> about{qmltest::WaitForVisible(m_app, "aboutSettingsPage")};
        QVERIFY(about);
        QPointer<QObject> popup;
        for (const auto& [row, link] : {
                 std::pair{"aboutWebsiteRow", "https://bitcoincore.org"},
                 std::pair{"aboutSourceCodeRow", "https://github.com/bitcoin/bitcoin"}}) {
            QVERIFY(qmltest::Click(m_app, row));
            auto* opened = qmltest::WaitForVisible(m_app, "aboutExternalLinkPopup");
            QVERIFY(opened);
            if (popup) QCOMPARE(opened, popup.data());
            popup = opened;
            QTRY_VERIFY(popup->property("opened").toBool());
            QCOMPARE(popup->property("link").toUrl(), QUrl{QString::fromLatin1(link)});
            QVERIFY(qmltest::WaitForVisible(m_app, "externalLinkCancel"));
            QVERIFY(qmltest::WaitForVisible(m_app, "externalLinkConfirm"));
            QVERIFY(qmltest::Click(m_app, "externalLinkCancel"));
            QTRY_VERIFY(!popup->property("visible").toBool());
            QVERIFY(about && about->property("visible").toBool());
            QVERIFY(m_opened_urls.isEmpty());
        }
    }

    void displayControlsUpdateUnitsAndTranslations()
    {
        QVERIFY(qmltest::SelectSettings(m_app, "display"));
        QVERIFY(qmltest::WaitForVisible(m_app, "displaySettingsPage"));
        for (const auto* row : {"displayThemeRow", "displayBlockStatusSizeRow", "displayMoneyFontRow",
                                "displayUnitRow", "displayLanguageRow", "displayTransactionUrlsRow"}) {
            auto* object = qmltest::Find(m_app, row);
            QVERIFY2(object, row);
            QCOMPARE(object->property("description").toString(), QString{});
        }
        QVERIFY(qmltest::Click(m_app, "displayUnitPickerButton"));
        QVERIFY(qmltest::Click(m_app, "displayUnitSAT"));
        auto* picker = qmltest::Find(m_app, "displayUnitPicker");
        QVERIFY(picker);
        QTRY_COMPARE(picker->property("currentValue").toInt(), 3);
        QCOMPARE(picker->property("currentText").toString(), QStringLiteral("sat"));
        QVERIFY(qmltest::Click(m_app, "displayLanguageRow"));
        QVERIFY(qmltest::SetText(m_app, "languageSearch", QString::fromUtf8("español")));
        QVERIFY(qmltest::Click(m_app, "language_es"));
        QVERIFY(qmltest::WaitForVisible(m_app, "displayLanguageRow"));
        QTRY_COMPARE(qmltest::Find(m_app, "displayLanguageRow")->property("title").toString(), QStringLiteral("Idioma"));
        QCOMPARE(qmltest::Find(m_app, "displayUnitRow")->property("title").toString(), QString::fromUtf8("Unidad de visualización"));
        QVERIFY(qmltest::Click(m_app, "displayLanguageRow"));
        QVERIFY(qmltest::SetText(m_app, "languageSearch", ""));
        QVERIFY(qmltest::Click(m_app, "language_"));
        QVERIFY(qmltest::WaitForVisible(m_app, "displayLanguageRow"));
        QTRY_COMPARE(qmltest::Find(m_app, "displayLanguageRow")->property("title").toString(), QStringLiteral("Language"));
    }

    void proxyValidationKeepsInvalidDraftsOutOfCoreSettings_data()
    {
        QTest::addColumn<bool>("tor");
        QTest::newRow("default-proxy") << false;
        QTest::newRow("tor-proxy") << true;
    }

    void proxyValidationKeepsInvalidDraftsOutOfCoreSettings()
    {
        QFETCH(bool, tor);
        auto* page = openProxySettings();
        QVERIFY(page);
        const auto* enable = tor ? "torEnableSwitch" : "proxyEnableSwitch";
        const auto* address = tor ? "torAddressInput" : "proxyAddressInput";
        const auto* error = tor ? "draftTorValidationError" : "draftProxyValidationError";
        const auto key = tor ? "onion" : "proxy";
        const auto original = persistentProxy(key);
        QVERIFY(!qmltest::Find(m_app, enable)->property("checked").toBool());
        QVERIFY(!qmltest::Find(m_app, "proxyRestartNotice")->property("visible").toBool());
        QVERIFY(qmltest::Click(m_app, enable));
        QTRY_VERIFY(qmltest::Find(m_app, address)->property("enabled").toBool());
        QVERIFY(qmltest::SetText(m_app, address, "999.999.999.999:9050"));
        QTRY_VERIFY(!page->property(error).toString().isEmpty());
        QVERIFY(!qmltest::Find(m_app, "proxySettingsSaveButton")->property("enabled").toBool());
        QCOMPARE(persistentProxy(key), original);
        QVERIFY(!qmltest::Find(m_app, "proxyRestartNotice")->property("visible").toBool());

        QVERIFY(qmltest::SetText(m_app, address, "127.0.0.1:9050"));
        QTRY_COMPARE(page->property(error).toString(), QString{});
        QTRY_VERIFY(qmltest::Find(m_app, "proxySettingsSaveButton")->property("enabled").toBool());
        QCOMPARE(persistentProxy(key), original);
        QVERIFY(qmltest::Click(m_app, enable));
        QTRY_VERIFY(!qmltest::Find(m_app, address)->property("enabled").toBool());
    }

    void cancellingBackPreservesProxyDraftAndConfirmingDiscardsIt()
    {
        auto* page = openProxySettings();
        QVERIFY(page);
        const auto original = persistentProxy("proxy");
        QVERIFY(qmltest::Click(m_app, "proxyEnableSwitch"));
        QVERIFY(qmltest::SetText(m_app, "proxyAddressInput", "10.0.0.5:9050"));
        QTRY_VERIFY(page->property("proxyDraftDirty").toBool());
        QVERIFY(qmltest::Click(m_app, "proxySettingsBackButton"));
        QVERIFY(qmltest::Click(m_app, "discardProxyChangesCancelButton"));
        QTRY_VERIFY(!qmltest::Find(m_app, "discardProxyChangesPopup")->property("visible").toBool());
        QCOMPARE(qmltest::Find(m_app, "proxyAddressInput")->property("text").toString(), QStringLiteral("10.0.0.5:9050"));
        QVERIFY(qmltest::Click(m_app, "proxySettingsBackButton"));
        QVERIFY(qmltest::Click(m_app, "discardProxyChangesConfirmButton"));
        QVERIFY(qmltest::WaitForVisible(m_app, "proxySettingsRow"));
        QVERIFY(qmltest::Click(m_app, "proxySettingsRow"));
        QVERIFY(qmltest::WaitForVisible(m_app, "proxySettingsPage"));
        QVERIFY(!qmltest::Find(m_app, "proxyEnableSwitch")->property("checked").toBool());
        QCOMPARE(persistentProxy("proxy"), original);
    }

    void savingProxyDraftCommitsBothEntries()
    {
        auto* page = openProxySettings();
        QVERIFY(page);
        auto* core = qobject_cast<CoreSettingsModel*>(page->property("coreSettingsModel").value<QObject*>());
        QVERIFY(core);
        for (const auto& [enable, address, value] : {
                 std::tuple{"proxyEnableSwitch", "proxyAddressInput", "10.0.0.1:9050"},
                 std::tuple{"torEnableSwitch", "torAddressInput", "127.0.0.1:9150"}}) {
            QVERIFY(qmltest::Click(m_app, enable));
            QVERIFY(qmltest::SetText(m_app, address, value));
        }
        QVERIFY(qmltest::Click(m_app, "proxySettingsSaveButton"));
        QVERIFY(qmltest::WaitForVisible(m_app, "proxySettingsRow"));
        QTRY_COMPARE(persistentProxy("proxy"), QStringLiteral("10.0.0.1:9050"));
        QTRY_COMPARE(persistentProxy("onion"), QStringLiteral("127.0.0.1:9150"));
        QVERIFY(core->values().proxy_enabled);
        QVERIFY(core->values().tor_enabled);
    }

    void windowBehaviorControlsUpdateTheLiveModelAndRetainNavigation()
    {
        auto* behavior = qmltest::Model<DesktopWindowBehaviorModel>(m_app, "desktopWindowBehaviorModel");
        QVERIFY(behavior);
        QVERIFY(qmltest::SelectSettings(m_app, "window-behavior"));
        QVERIFY(qmltest::WaitForVisible(m_app, "windowBehaviorSettingsPage"));
        QVERIFY(qmltest::Find(m_app, "showTrayIconSwitch")->property("checked").toBool());
        QVERIFY(!qmltest::Find(m_app, "minimizeToTraySwitch")->property("checked").toBool());
        QVERIFY(!qmltest::Find(m_app, "minimizeOnCloseSwitch")->property("checked").toBool());
        QVERIFY(qmltest::Click(m_app, "showTrayIconSwitch"));
        QTRY_VERIFY(!behavior->showTrayIcon());
        QVERIFY(qmltest::Click(m_app, "showTrayIconSwitch"));
        QTRY_VERIFY(behavior->showTrayIcon());
        QVERIFY(qmltest::Click(m_app, "minimizeOnCloseSwitch"));
        QTRY_VERIFY(behavior->minimizeOnClose());
        QVERIFY(qmltest::SelectSettings(m_app, "about"));
        QVERIFY(qmltest::WaitForVisible(m_app, "aboutSettingsPage"));
        QVERIFY(qmltest::SelectSettings(m_app, "window-behavior"));
        QVERIFY(qmltest::WaitForVisible(m_app, "windowBehaviorSettingsPage"));
        QVERIFY(qmltest::Find(m_app, "minimizeOnCloseSwitch")->property("checked").toBool());
        // Leave normal window-close semantics for the fixture's shutdown.
        QVERIFY(qmltest::Click(m_app, "minimizeOnCloseSwitch"));
    }
};

std::unique_ptr<QObject> CreateSettingsTests(BitcoinQmlApplication& app, WalletMode)
{
    return std::make_unique<SettingsIntegrationTests>(app);
}

#include <test_settings.moc>
