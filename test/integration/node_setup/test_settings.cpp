// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/integration/runner/test_cases.h>
#include <test/integration/fixture/test_util.h>
#include <qml/models/core_settings_model.h>
#include <qml/models/settingseditormodel.h>
#include <qml/models/desktopwindowbehaviormodel.h>
#include <qml/models/options_model.h>
#include <qml/models/settings_keys.h>
#include <qml/models/nodemodel.h>
#include <node/caches.h>
#include <common/args.h>
#include <common/settings.h>
#include <util/fs.h>
#include <util/byte_units.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QSignalSpy>
#include <QQuickItem>
#include <QDesktopServices>
#include <QScopeGuard>
#include <QPointer>
#include <QUrl>

#include <stdexcept>
#include <tuple>
#include <map>

class SettingsIntegrationTests : public QObject
{
    Q_OBJECT
    BitcoinQmlApplication& m_app;
    QList<QUrl> m_opened_urls;

    QObject* openProxySettings()
    {
        if (!qmltest::SelectSettings(m_app, "connection")) return nullptr;
        auto* page=qmltest::WaitForVisible(m_app,"connectionSettingsPage");
        auto* editor=settingsEditor();
        if(!editor||!QTest::qWaitFor([&]{return editor->fresh();}))return nullptr;
        return page;
    }
    SettingsEditorModel* settingsEditor()
    {
        auto* view=qmltest::Find(m_app,"settingsView");
        return view?view->property("settingsEditor").value<SettingsEditorModel*>():nullptr;
    }

    // What settings.json holds, without bitcoin.conf values.
    common::SettingsValue savedSetting(const std::string& name)
    {
        fs::path path;std::map<std::string,common::SettingsValue> values;std::vector<std::string> errors;
        if(!gArgs.GetSettingsPath(&path)||!common::ReadSettings(path,values,errors))return {};
        const auto found=values.find(name);return found==values.end()?common::SettingsValue{}:found->second;
    }

    QString persistentProxy(const std::string& key)
    {
        const auto value = qmltest::Call(m_app, &interfaces::Node::getPersistentSetting, key);
        return value.isStr() ? QString::fromStdString(value.get_str()) : QString{};
    }

    // Closes the window and waits until shutdown has visibly started.
    void quitAndWaitForShutdownPage()
    {
        QSignalSpy requested{&m_app.nodeModel(),&NodeModel::requestedShutdown};
        m_app.window()->close();
        QTRY_COMPARE(requested.count(),1);
        QVERIFY(qmltest::WaitForVisible(m_app,"shutdownPage"));
        QTest::qWait(500); // A write admitted by the focus change would land here.
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


    // #1114: quitting with a settings edit nobody confirmed drops it; nothing
    // is written during shutdown. A number field's unsaved draft (the editor
    // asks before its section can be left, so it cannot sit hidden) and a
    // text field that still has the focus when the window closes.
    void quitDropsAnUnsavedNumberSettingDraft()
    {
        auto* options=qmltest::Model<OptionsQmlModel>(m_app,"optionsModel");QVERIFY(options);QTRY_VERIFY(options->settingsReady());QTRY_VERIFY(!options->settingsPending());
        QVERIFY(savedSetting("dbcache").isNull());
        QVERIFY(qmltest::SelectSettings(m_app,"storage"));auto* editor=settingsEditor();QVERIFY(editor);QTRY_VERIFY(editor->fresh());
        QVERIFY(qmltest::SetText(m_app,"dbcacheSettingInput","450"));QTRY_VERIFY(editor->canSave());
        quitAndWaitForShutdownPage();
        QVERIFY(savedSetting("dbcache").isNull());
    }
    void quitDropsAFocusedTextSettingEdit()
    {
        auto* options=qmltest::Model<OptionsQmlModel>(m_app,"optionsModel");QVERIFY(options);QTRY_VERIFY(options->settingsReady());QTRY_VERIFY(!options->settingsPending());
        const auto urls_before=QSettings{}.value(SettingsKeys::THIRD_PARTY_TRANSACTION_URLS);
        QVERIFY(qmltest::SelectSettings(m_app,"display"));QVERIFY(qmltest::WaitForVisible(m_app,"displaySettingsPage"));
        QVERIFY(qmltest::Click(m_app,"displayTransactionUrlsRow"));
        auto* urls=qobject_cast<QQuickItem*>(qmltest::WaitForVisible(m_app,"thirdPartyTransactionUrlsInput"));QVERIFY(urls);
        QVERIFY(qmltest::SetText(m_app,"thirdPartyTransactionUrlsInput","https://typed.example/tx/%s"));
        QVERIFY(urls->hasActiveFocus());
        quitAndWaitForShutdownPage();
        QCOMPARE(QSettings{}.value(SettingsKeys::THIRD_PARTY_TRANSACTION_URLS),urls_before);
        QCOMPARE(options->thirdPartyTransactionUrls(),urls_before.toString());
    }

    void databaseCacheUsesCoreDefaultAndKeepsExplicitOverride()
    {
        auto* options=qmltest::Model<OptionsQmlModel>(m_app,"optionsModel");QVERIFY(options);QTRY_VERIFY(options->settingsReady());QTRY_VERIFY(!options->settingsPending());
        const auto expected=node::GetDefaultDBCache()/1_MiB;
        QCOMPARE(options->dbcacheSizeMiB(),int(expected));QVERIFY(options->maxDbcacheSizeMiB()>=1636);
        QVERIFY(qmltest::Call(m_app,&interfaces::Node::getPersistentSetting,"dbcache").isNull());
        QVERIFY(qmltest::SelectSettings(m_app,"storage"));auto* editor=settingsEditor();QVERIFY(editor);QTRY_VERIFY(editor->fresh());
        QCOMPARE(editor->entry(QStringLiteral("dbcache"))->effectiveText(),QString::number(expected));
        QVERIFY(qmltest::SetText(m_app,"dbcacheSettingInput","450"));QTRY_VERIFY(editor->canSave());
        QVERIFY(qmltest::Click(m_app,"settingsSaveBatchButton"));QTRY_VERIFY(editor->lastSavePersisted()&&!editor->busy());
        const auto saved=qmltest::Call(m_app,&interfaces::Node::getPersistentSetting,"dbcache");
        QCOMPARE(SettingTo<int64_t>(saved).value_or(-1),int64_t{450});
        QCOMPARE(editor->entry(QStringLiteral("dbcache"))->effectiveText(),QString::number(expected));
        QCOMPARE(editor->entry(QStringLiteral("dbcache"))->nextStartText(),QStringLiteral("450"));
        QVERIFY(editor->saveResult().actualValues.contains(QStringLiteral("dbcache = 450")));
    }

    // #1075: prune=550 from bitcoin.conf survives off/on in Settings -> Storage
    // and saves 550, not the 953 of a GB round trip. The restart notice shows
    // only while the saved target differs from the running one, leaving pruning
    // warns about rebuilding first, and Use configuration keeps pruning.
    void configuredPruneTargetSurvivesToggleAndRestartNotice()
    {
        QVERIFY(qmltest::SelectSettings(m_app,"storage"));auto* editor=settingsEditor();QVERIFY(editor);QTRY_VERIFY(editor->fresh());
        auto* prune=editor->entry(QStringLiteral("prune"));QVERIFY(prune);QVERIFY(prune->editable());
        const auto saved_prune=[&]{return savedSetting("prune");};
        const auto save=[&]{return qmltest::Click(m_app,"settingsSaveBatchButton")&&QTest::qWaitFor([&]{return editor->lastSavePersisted()&&!editor->busy();},30'000);};
        QCOMPARE(prune->text(),QStringLiteral("550"));QCOMPARE(prune->effectiveText(),QStringLiteral("550"));QCOMPARE(prune->nextStartText(),QStringLiteral("550"));
        QVERIFY(saved_prune().isNull());QVERIFY(!editor->restartNoticeVisible());
        // Off and on again, through the model's mode switch and the field.
        editor->setPruneMode(SettingsEditorModel::Disabled);QTRY_COMPARE(prune->text(),QStringLiteral("0"));
        editor->setPruneMode(SettingsEditorModel::Automatic);QTRY_COMPARE(prune->text(),QStringLiteral("550"));
        QVERIFY(qmltest::SetText(m_app,"pruneSettingInput","0"));QTRY_COMPARE(prune->text(),QStringLiteral("0"));
        QVERIFY(qmltest::SetText(m_app,"pruneSettingInput","550"));QTRY_VERIFY(editor->canSave());
        QVERIFY(save());
        QCOMPARE(SettingTo<int64_t>(saved_prune()).value_or(-1),int64_t{550});QCOMPARE(prune->nextStartText(),QStringLiteral("550"));
        QVERIFY(!editor->restartRequired());QVERIFY(!editor->restartNoticeVisible());
        // Leaving pruning warns about rebuilding before anything is saved.
        QVERIFY(qmltest::SetText(m_app,"pruneSettingInput","0"));QTRY_VERIFY(editor->canSave());
        QVERIFY(qmltest::Click(m_app,"settingsSaveBatchButton"));QTRY_VERIFY(editor->redownloadWarning()&&!editor->busy());
        QVERIFY(!prune->error().isEmpty());QCOMPARE(SettingTo<int64_t>(saved_prune()).value_or(-1),int64_t{550});QVERIFY(!editor->restartNoticeVisible());
        QVERIFY(qmltest::WaitForVisible(m_app,"settingsConfirmRedownload"));QVERIFY(qmltest::Click(m_app,"settingsConfirmRedownload"));QTRY_VERIFY(prune->error().isEmpty());
        QVERIFY(save());
        QCOMPARE(SettingTo<int64_t>(saved_prune()).value_or(-1),int64_t{0});QCOMPARE(prune->nextStartText(),QStringLiteral("0"));QTRY_VERIFY(editor->restartNoticeVisible());
        // Keep pruning: Use configuration returns to bitcoin.conf's exact target
        // and the notice goes away because nothing differs from the running node.
        QVERIFY(qmltest::WaitForVisible(m_app,"pruneUseConfigurationButton"));QVERIFY(qmltest::Click(m_app,"pruneUseConfigurationButton"));QTRY_VERIFY(editor->canSave());
        QVERIFY(save());
        QVERIFY(saved_prune().isNull());QCOMPARE(prune->nextStartText(),QStringLiteral("550"));QCOMPARE(prune->text(),QStringLiteral("550"));
        QTRY_VERIFY(!editor->restartNoticeVisible());QVERIFY(!editor->restartRequired());
    }

    // #1075: manual pruning (prune=1) shows as 1, not off; off and back keeps 1,
    // and turning it off asks about rebuilding and then saves 0 with a notice.
    void manualPruneIsShownAndTurnsOffOnlyAfterTheRebuildWarning()
    {
        QVERIFY(qmltest::SelectSettings(m_app,"storage"));auto* editor=settingsEditor();QVERIFY(editor);QTRY_VERIFY(editor->fresh());
        auto* prune=editor->entry(QStringLiteral("prune"));QVERIFY(prune);QVERIFY(prune->editable());
        const auto saved_prune=[&]{return savedSetting("prune");};
        const auto save=[&]{return qmltest::Click(m_app,"settingsSaveBatchButton")&&QTest::qWaitFor([&]{return editor->lastSavePersisted()&&!editor->busy();},30'000);};
        QCOMPARE(prune->text(),QStringLiteral("1"));QCOMPARE(prune->effectiveText(),QStringLiteral("1"));QCOMPARE(prune->nextStartText(),QStringLiteral("1"));
        QCOMPARE(qmltest::Find(m_app,"pruneSettingInput")->property("text").toString(),QStringLiteral("1"));
        editor->setPruneMode(SettingsEditorModel::Automatic);QTRY_COMPARE(prune->text(),QStringLiteral("550"));
        editor->setPruneMode(SettingsEditorModel::Manual);QTRY_COMPARE(prune->text(),QStringLiteral("1"));QTRY_VERIFY(editor->canSave());
        QVERIFY(save());
        QCOMPARE(SettingTo<int64_t>(saved_prune()).value_or(-1),int64_t{1});QCOMPARE(prune->nextStartText(),QStringLiteral("1"));QVERIFY(!editor->restartNoticeVisible());
        QVERIFY(qmltest::SetText(m_app,"pruneSettingInput","0"));QTRY_VERIFY(editor->canSave());
        QVERIFY(qmltest::Click(m_app,"settingsSaveBatchButton"));QTRY_VERIFY(editor->redownloadWarning()&&!editor->busy());
        QCOMPARE(SettingTo<int64_t>(saved_prune()).value_or(-1),int64_t{1});QVERIFY(!editor->restartNoticeVisible());
        QVERIFY(qmltest::Click(m_app,"settingsConfirmRedownload"));QTRY_VERIFY(prune->error().isEmpty());
        QVERIFY(save());
        QCOMPARE(SettingTo<int64_t>(saved_prune()).value_or(-1),int64_t{0});QCOMPARE(prune->nextStartText(),QStringLiteral("0"));QTRY_VERIFY(editor->restartNoticeVisible());
    }

    void externalPreferencesRefreshTheOpenDisplayAndWindowModels()
    {
        auto* options=qmltest::Model<OptionsQmlModel>(m_app,"optionsModel");
        auto* behavior=qmltest::Model<DesktopWindowBehaviorModel>(m_app,"desktopWindowBehaviorModel");QVERIFY(options);QVERIFY(behavior);
        QTRY_VERIFY(options->settingsReady());QTRY_VERIFY(behavior->settingsReady());
        QVERIFY(qmltest::SelectSettings(m_app,"display"));auto* picker=qmltest::WaitForVisible(m_app,"displayUnitPicker");QVERIFY(picker);
        {QSettings external;external.setValue(SettingsKeys::DISPLAY_UNIT,2);external.setValue("fHideTrayIcon",true);external.sync();QCOMPARE(external.status(),QSettings::NoError);}
        QTRY_COMPARE(picker->property("currentValue").toInt(),2);QTRY_COMPARE(options->displayUnit(),2);QTRY_VERIFY(!behavior->showTrayIcon());
        QVERIFY(qmltest::SelectSettings(m_app,"window-behavior"));QVERIFY(qmltest::WaitForVisible(m_app,"windowBehaviorSettingsPage"));
        QTRY_VERIFY(!qmltest::Find(m_app,"showTrayIconSwitch")->property("checked").toBool());
        {QSettings external;external.remove("fHideTrayIcon");external.setValue(SettingsKeys::DISPLAY_UNIT,0);external.sync();}
        QTRY_VERIFY(behavior->showTrayIcon());QTRY_COMPARE(options->displayUnit(),0);
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
        QVERIFY(openProxySettings());
        auto* editor=settingsEditor();QVERIFY(editor);
        const auto key=tor?"onion":"proxy";
        const auto address=tor?"onionSettingInput":"proxySettingInput";
        const auto original=persistentProxy(key);
        QVERIFY(!editor->restartRequired());
        QVERIFY(qmltest::SetText(m_app,address,"999.999.999.999:9050"));
        QTRY_VERIFY(editor->dirty());
        QVERIFY(qmltest::Click(m_app,"settingsSaveBatchButton"));
        QTRY_VERIFY(!editor->busy()&&!editor->error().isEmpty());
        QVERIFY(editor->dirty());QCOMPARE(persistentProxy(key),original);
        QVERIFY(!editor->lastSavePersisted());QVERIFY(!editor->restartRequired());
        QCOMPARE(editor->entry(QString::fromLatin1(key))->text(),QStringLiteral("999.999.999.999:9050"));
        QVERIFY(qmltest::SetText(m_app,address,"127.0.0.1:9050"));
        QTRY_VERIFY(editor->canSave());QCOMPARE(persistentProxy(key),original);
        editor->discard();QTRY_VERIFY(!editor->dirty());
        QCOMPARE(persistentProxy(key),original);
    }

    void cancellingBackPreservesProxyDraftAndConfirmingDiscardsIt()
    {
        QVERIFY(openProxySettings());auto* editor=settingsEditor();QVERIFY(editor);
        const auto original=persistentProxy("proxy");
        QVERIFY(qmltest::SetText(m_app,"proxySettingInput","10.0.0.5:9050"));
        QTRY_VERIFY(editor->dirty());
        QVERIFY(qmltest::SelectSettings(m_app,"storage"));
        QTRY_VERIFY(editor->closeQuestion());
        QVERIFY(qmltest::Click(m_app,"settingsCloseKeep"));
        QTRY_VERIFY(!editor->closeQuestion());
        QCOMPARE(qmltest::Find(m_app,"settingsView")->property("selectedSectionId").toString(),QStringLiteral("connection"));
        QCOMPARE(editor->entry(QStringLiteral("proxy"))->text(),QStringLiteral("10.0.0.5:9050"));
        QVERIFY(qmltest::SelectSettings(m_app,"storage"));QTRY_VERIFY(editor->closeQuestion());
        QVERIFY(qmltest::Click(m_app,"settingsCloseDiscard"));
        QTRY_COMPARE(qmltest::Find(m_app,"settingsView")->property("selectedSectionId").toString(),QStringLiteral("storage"));
        QVERIFY(openProxySettings());QTRY_VERIFY(!settingsEditor()->dirty());
        QCOMPARE(settingsEditor()->entry(QStringLiteral("proxy"))->text(),original);
        QCOMPARE(persistentProxy("proxy"),original);
    }

    // #1065: clicking Connection again in the sidebar with an unsaved proxy
    // edit returns to the section's root but must keep the draft.
    void reselectingConnectionKeepsTheProxyDraft()
    {
        QVERIFY(openProxySettings());QPointer<SettingsEditorModel> editor{settingsEditor()};QVERIFY(editor);
        const auto original=persistentProxy("proxy");
        QVERIFY(qmltest::SetText(m_app,"proxySettingInput","10.0.0.7:9050"));
        QTRY_VERIFY(editor->dirty());
        QVERIFY(qmltest::SelectSettings(m_app,"connection"));
        QVERIFY(qmltest::WaitForVisible(m_app,"connectionSettingsPage"));QTRY_VERIFY(qmltest::TransitionsFinished(m_app.window()));
        QCOMPARE(qmltest::Find(m_app,"settingsView")->property("selectedSectionId").toString(),QStringLiteral("connection"));
        QCOMPARE(settingsEditor(),editor.data());QVERIFY(editor->dirty());QVERIFY(!editor->closeQuestion());
        QCOMPARE(editor->entry(QStringLiteral("proxy"))->text(),QStringLiteral("10.0.0.7:9050"));
        QTRY_COMPARE(qmltest::Find(m_app,"proxySettingInput")->property("text").toString(),QStringLiteral("10.0.0.7:9050"));
        QCOMPARE(persistentProxy("proxy"),original);
        editor->discard();QTRY_VERIFY(!editor->dirty());QCOMPARE(persistentProxy("proxy"),original);
    }

    void readOnlyNetworkProxyKeepsOtherSettingsWritable()
    {
        const auto routes=[&]{std::map<std::string,std::string> result;const auto info=qmltest::Call(m_app,&interfaces::Node::executeRpc,"getnetworkinfo",UniValue{UniValue::VARR},"");for(const auto& network:info.find_value("networks").getValues())result.emplace(network.find_value("name").get_str(),network.find_value("proxy").get_str());return result;};
        const auto original=routes();QCOMPARE(original.at("ipv4"),std::string{"127.0.0.1:9050"});QCOMPARE(original.at("onion"),std::string{});
        QVERIFY(openProxySettings());auto* editor=settingsEditor();QVERIFY(editor);QVERIFY(!editor->entry(QStringLiteral("proxy"))->editable());
        const auto running=editor->entry(QStringLiteral("maxmempool"))->effectiveText();
        QVERIFY(qmltest::SelectSettings(m_app,"mempool"));QVERIFY(qmltest::WaitForVisible(m_app,"maxmempoolSettingInput"));QVERIFY(qmltest::SetText(m_app,"maxmempoolSettingInput","400"));QTRY_VERIFY(editor->canSave());
        QVERIFY(qmltest::Click(m_app,"settingsSaveBatchButton"));QTRY_VERIFY(editor->lastSavePersisted()&&!editor->busy());
        const auto saved=qmltest::Call(m_app,&interfaces::Node::getPersistentSetting,"maxmempool");QVERIFY(saved.isNum());QCOMPARE(saved.getInt<int64_t>(),400);QVERIFY(qmltest::Call(m_app,&interfaces::Node::getPersistentSetting,"proxy").isNull());
        QVERIFY(routes()==original);QCOMPARE(editor->entry(QStringLiteral("maxmempool"))->effectiveText(),running);QCOMPARE(editor->entry(QStringLiteral("maxmempool"))->nextStartText(),QStringLiteral("400"));QVERIFY(editor->restartRequired());
    }

    // #594: an onion-only proxy from bitcoin.conf is shown read-only, a direct
    // edit cannot save over it, and saving another setting keeps Core's routes.
    void configuredPerNetworkProxySurvivesSettingsEdits()
    {
        const auto routes=[&]{std::map<std::string,std::string> result;const auto info=qmltest::Call(m_app,&interfaces::Node::executeRpc,"getnetworkinfo",UniValue{UniValue::VARR},"");for(const auto& network:info.find_value("networks").getValues())result.emplace(network.find_value("name").get_str(),network.find_value("proxy").get_str());return result;};
        const auto original=routes();QCOMPARE(original.at("onion"),std::string{"127.0.0.1:9050"});QCOMPARE(original.at("ipv4"),std::string{});
        QVERIFY(openProxySettings());auto* editor=settingsEditor();QVERIFY(editor);auto* proxy=editor->entry(QStringLiteral("proxy"));QVERIFY(proxy);
        QVERIFY(!proxy->editable());QVERIFY(proxy->information().contains(QStringLiteral("127.0.0.1:9050=onion")));QVERIFY(!proxy->overridden());
        auto* input=qmltest::WaitForVisible(m_app,"proxySettingInput");QVERIFY(input);QVERIFY(!input->property("enabled").toBool());
        const auto next=proxy->nextStartText();
        for(const auto& value:{QStringLiteral("0"),QStringLiteral("127.0.0.2:9051")}){
            proxy->editText(value);
            if(editor->canSave()){const auto id=editor->lastSaveId();QVERIFY(qmltest::Click(m_app,"settingsSaveBatchButton"));QTRY_VERIFY(!editor->busy()&&editor->lastSaveId()!=id&&editor->lastSaveKnown());}
            QVERIFY(savedSetting("proxy").isNull());QCOMPARE(proxy->nextStartText(),next);
            if(editor->dirty()){editor->discard();QTRY_VERIFY(!editor->dirty());}
        }
        QVERIFY(qmltest::SelectSettings(m_app,"mempool"));QVERIFY(qmltest::WaitForVisible(m_app,"maxmempoolSettingInput"));QVERIFY(qmltest::SetText(m_app,"maxmempoolSettingInput","400"));QTRY_VERIFY(editor->canSave());
        QVERIFY(qmltest::Click(m_app,"settingsSaveBatchButton"));QTRY_VERIFY(editor->lastSavePersisted()&&!editor->busy());
        QVERIFY(savedSetting("proxy").isNull());QVERIFY(savedSetting("proxy-prev").isNull());QCOMPARE(proxy->nextStartText(),next);QVERIFY(!proxy->editable());QVERIFY(routes()==original);
    }

    void savingProxyDraftCommitsBothEntries()
    {
        QVERIFY(openProxySettings());auto* editor=settingsEditor();QVERIFY(editor);
        for(const auto& [address,value]:{std::pair{"proxySettingInput","10.0.0.1:9050"},std::pair{"onionSettingInput","127.0.0.1:9150"}})QVERIFY(qmltest::SetText(m_app,address,value));
        QTRY_VERIFY(editor->canSave());
        QVERIFY(qmltest::Click(m_app,"settingsSaveBatchButton"));
        QTRY_VERIFY(editor->lastSavePersisted()&&!editor->busy());
        QTRY_COMPARE(persistentProxy("proxy"),QStringLiteral("10.0.0.1:9050"));
        QTRY_COMPARE(persistentProxy("onion"),QStringLiteral("127.0.0.1:9150"));
        const auto result=editor->saveResult();QVERIFY(result.outcomeKnown);QCOMPARE(result.outcome,SettingsSaveResult::Saved);
        QVERIFY(result.actualValues.contains(QStringLiteral("proxy = 10.0.0.1:9050")));
        QVERIFY(result.actualValues.contains(QStringLiteral("onion = 127.0.0.1:9150")));
        QVERIFY(!editor->dirty());QVERIFY(editor->restartRequired());
    }

    // #1093: when the settings file cannot be written, saving both proxies
    // must persist neither, keep both edits for a retry, and say it failed.
    void proxySaveWriteFailureKeepsBothEditsAndPersistsNothing()
    {
        fs::path settings_path;
        QVERIFY(gArgs.GetSettingsPath(&settings_path));
        const auto settings_file = QString::fromStdString(fs::PathToString(settings_path));
        const auto directory = QFileInfo{settings_file}.absolutePath();
        const auto read_file = [&] { QFile file{settings_file}; return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{}; };
        const auto original_proxy = persistentProxy("proxy"), original_onion = persistentProxy("onion"), original_onion_prev = persistentProxy("onion-prev");
        const auto original_bytes = read_file();

        QVERIFY(openProxySettings()); auto* editor = settingsEditor(); QVERIFY(editor);
        QVERIFY(qmltest::SetText(m_app, "proxySettingInput", "127.0.0.1:9151"));
        QVERIFY(qmltest::SetText(m_app, "onionSettingInput", "127.0.0.1:9152"));
        QTRY_VERIFY(editor->canSave());

        // Core writes settings.json.tmp next to settings.json and renames it,
        // so a read-only directory fails the one write the save depends on.
        const auto permissions = QFileInfo{directory}.permissions();
        const auto restore = qScopeGuard([&] { QFile::setPermissions(directory, permissions); });
        QVERIFY(QFile::setPermissions(directory, QFileDevice::ReadOwner | QFileDevice::ExeOwner));
        {
            QFile probe{QDir{directory}.filePath(QStringLiteral("write-probe"))};
            if (probe.open(QIODevice::WriteOnly)) { probe.close(); probe.remove(); QSKIP("Cannot make the data directory read-only here (running as root?)."); }
        }
        QVERIFY(qmltest::Click(m_app, "settingsSaveBatchButton"));
        QTRY_VERIFY_WITH_TIMEOUT(editor->lastSaveKnown() && !editor->busy(), 15'000);
        QVERIFY(QFile::setPermissions(directory, permissions));

        // Reported: the save failed and nothing was persisted.
        QVERIFY(!editor->lastSavePersisted());
        const auto failed = editor->saveResult();
        QVERIFY(failed.outcome != SettingsSaveResult::Saved && failed.outcome != SettingsSaveResult::Partial);
        QVERIFY(!failed.persisted); QVERIFY(!failed.error.isEmpty());
        // The editor stays open with both edits ready for a retry.
        QVERIFY(qmltest::Find(m_app, "connectionSettingsPage")->property("visible").toBool());
        QVERIFY(editor->dirty()); QVERIFY(editor->canSave());
        QCOMPARE(editor->entry(QStringLiteral("proxy"))->text(), QStringLiteral("127.0.0.1:9151"));
        QCOMPARE(editor->entry(QStringLiteral("onion"))->text(), QStringLiteral("127.0.0.1:9152"));
        // Neither the live settings nor the file took half of the change.
        QCOMPARE(persistentProxy("proxy"), original_proxy);
        QCOMPARE(persistentProxy("onion"), original_onion);
        QCOMPARE(persistentProxy("onion-prev"), original_onion_prev);
        QCOMPARE(read_file(), original_bytes);

        // Retrying once the directory is writable saves both.
        QVERIFY(qmltest::Click(m_app, "settingsSaveBatchButton"));
        QTRY_VERIFY(editor->lastSavePersisted() && !editor->busy());
        QCOMPARE(editor->saveResult().outcome, SettingsSaveResult::Saved);
        QCOMPARE(persistentProxy("proxy"), QStringLiteral("127.0.0.1:9151"));
        QCOMPARE(persistentProxy("onion"), QStringLiteral("127.0.0.1:9152"));
        QVERIFY(!editor->dirty());
    }

    void windowBehaviorControlsUpdateTheLiveModelAndRetainNavigation()
    {
        auto* behavior = qmltest::Model<DesktopWindowBehaviorModel>(m_app, "desktopWindowBehaviorModel");
        QVERIFY(behavior);
        QTRY_VERIFY(behavior->settingsReady());
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

QStringList SettingsTestArguments(const QString& selector)
{
    if(selector==QStringLiteral("readOnlyNetworkProxyKeepsOtherSettingsWritable"))return {QStringLiteral("-proxy=127.0.0.1:9050=ipv4")};
    // #1075 starts from bitcoin.conf, not the command line, so settings.json can override it.
    // The child runs in its own profile directory.
    const auto prune=selector==QStringLiteral("configuredPruneTargetSurvivesToggleAndRestartNotice")?"prune=550":selector==QStringLiteral("manualPruneIsShownAndTurnsOffOnlyAfterTheRebuildWarning")?"prune=1":
        selector==QStringLiteral("configuredPerNetworkProxySurvivesSettingsEdits")?"proxy=127.0.0.1:9050=onion":nullptr;
    if(!prune)return {};
    const auto path=QDir::current().absoluteFilePath(QStringLiteral("prune.conf"));QFile config{path};
    if(!config.open(QIODevice::WriteOnly|QIODevice::Truncate)||config.write(QByteArray{prune}+'\n')<=0)throw std::runtime_error("Cannot write the startup configuration");
    return {QStringLiteral("-conf=")+path};
}

#include <test_settings.moc>
