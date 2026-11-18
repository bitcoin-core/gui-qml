// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/core_settings.h>
#include <qml/onboarding_settings.h>
#include <common/args.h>
#include <common/settings.h>
#include <init.h>
#include <univalue.h>
#include <test/qt_test_registry.h>
#include <QTemporaryDir>
#include <QFile>
#include <QtTest/QtTest>
#include <stdexcept>
#include <vector>
namespace {
struct Profile {
    QTemporaryDir directory;
    ArgsManager args;
    Profile() {
        if (!directory.isValid()) throw std::runtime_error("Settings fixture unavailable");
        SetupServerArgs(args);
        const std::vector<std::string> values{"r33-test","-regtest","-datadir="+directory.path().toStdString(),"-settings="+directory.filePath("settings.json").toStdString()};
        std::vector<const char*> argv;argv.reserve(values.size()); for (const auto& value:values) argv.push_back(value.c_str()); std::string error;
        if (!args.ParseParameters(int(argv.size()),argv.data(),error)) throw std::runtime_error(error);
        args.SelectConfigNetwork("regtest");
    }
    void config(const std::string& key, const common::SettingsValue& value) { args.LockSettings([&](common::Settings& settings){settings.ro_config["regtest"][key]={value};}); }
};
}
class SettingsPreservationTests : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void invalidEffectiveStartupBatchCannotBeCommitted_data(){QTest::addColumn<int>("scenario");QTest::newRow("bound-address-listen-off")<<0;QTest::newRow("only-onion-without-route")<<1;QTest::newRow("dns-disabled-hostname-proxy")<<2;QTest::newRow("cluster250-mempool9")<<3;QTest::newRow("dns-disabled-hostname-onion")<<4;}
    void invalidEffectiveStartupBatchCannotBeCommitted(){QFETCH(int,scenario);Profile profile;
        if(scenario==0)profile.config("bind",std::string{"127.0.0.1"});
        if(scenario==1){profile.config("onlynet",std::string{"onion"});profile.config("listenonion",false);}
        if(scenario==2||scenario==4)profile.config("dns",false);
        if(scenario==3){profile.config("limitclustersize",int64_t{250});profile.config("maxmempool",int64_t{9});}
        const auto line=scenario==0?QByteArray{"bind=127.0.0.1\n"}:scenario==1?QByteArray{"onlynet=onion\nlistenonion=0\n"}:scenario==2||scenario==4?QByteArray{"dns=0\n"}:QByteArray{"limitclustersize=250\nmaxmempool=9\n"};QFile config{profile.directory.filePath("bitcoin.conf")};QVERIFY(config.open(QIODevice::WriteOnly));QVERIFY(config.write(QByteArray{"[regtest]\n"}+line)>0);config.close();
        QVERIFY(profile.args.WriteSettingsFile());QFile before{profile.directory.filePath("settings.json")};QVERIFY(before.open(QIODevice::ReadOnly));const auto bytes=before.readAll();before.close();
        auto values=QmlCoreSettings::LoadEffectiveValues(profile.args);values.listen=false;QSet<QString> touched{QStringLiteral("listen")};
        if(scenario==2){values.proxy_enabled=true;values.proxy_address=QStringLiteral("no-such-host.invalid:9050");touched.insert(QStringLiteral("proxy"));}
        // #1074: the Tor proxy field has the same next-start name lookup.
        if(scenario==4){values.tor_enabled=true;values.tor_address=QStringLiteral("no-such-host.invalid:9050");touched.insert(QStringLiteral("onion"));}
        QString error;const bool accepted=QmlOnboardingSettings::ApplyToArgs(profile.args,QmlOnboardingSettings::DataDirSelection{profile.directory.path(),QmlOnboardingSettings::DataDirSource::ExplicitArg},touched,values,&error);
        QVERIFY(!accepted);QVERIFY(!error.isEmpty());QFile after{profile.directory.filePath("settings.json")};QVERIFY(after.open(QIODevice::ReadOnly));QCOMPARE(after.readAll(),bytes);QVERIFY(profile.args.GetPersistentSetting("listen").isNull());QVERIFY(!profile.args.GetPersistentSetting("qml_onboarded").isTrue());
    }
    // #594: setup shows a per-network proxy from bitcoin.conf as read-only and
    // cannot negate or broaden it, so Core's routes survive setup unchanged.
    void setupKeepsPerNetworkProxyFromConfiguration_data(){QTest::addColumn<bool>("disable");QTest::newRow("switch-off")<<true;QTest::newRow("plain-address-broadens")<<false;}
    void setupKeepsPerNetworkProxyFromConfiguration(){QFETCH(bool,disable);Profile profile;profile.config("proxy",std::string{"127.0.0.1:9050=onion"});
        QFile config{profile.directory.filePath("bitcoin.conf")};QVERIFY(config.open(QIODevice::WriteOnly));QVERIFY(config.write("[regtest]\nproxy=127.0.0.1:9050=onion\n")>0);config.close();
        QVERIFY(profile.args.WriteSettingsFile());QFile before{profile.directory.filePath("settings.json")};QVERIFY(before.open(QIODevice::ReadOnly));const auto bytes=before.readAll();before.close();const auto native=profile.args.GetArgs("-proxy");
        const auto status=QmlCoreSettings::BuildCoreSettingStatuses(profile.args,QmlCoreSettings::OnboardingCoreSettingNames()).value(QStringLiteral("proxy")).toMap();
        QVERIFY(!status.value(QStringLiteral("canEdit")).toBool());QVERIFY(status.value(QStringLiteral("infoText")).toString().contains(QStringLiteral("127.0.0.1:9050=onion")));
        auto values=QmlCoreSettings::LoadEffectiveValues(profile.args);QmlCoreSettings::Session session{values,QmlCoreSettings::BuildCoreSettingStatuses(profile.args,QmlCoreSettings::OnboardingCoreSettingNames())};
        QVERIFY(!(disable?session.changeProxyEnabled(false):session.changeProxyLocation(QStringLiteral("127.0.0.2:9051"))).accepted);
        if(disable)values.proxy_enabled=false;else{values.proxy_enabled=true;values.proxy_address=QStringLiteral("127.0.0.2:9051");}
        QString error;const bool accepted=QmlOnboardingSettings::ApplyToArgs(profile.args,QmlOnboardingSettings::DataDirSelection{profile.directory.path(),QmlOnboardingSettings::DataDirSource::ExplicitArg},{QStringLiteral("proxy")},values,&error);
        QVERIFY(!accepted);QVERIFY(!error.isEmpty());QFile after{profile.directory.filePath("settings.json")};QVERIFY(after.open(QIODevice::ReadOnly));QCOMPARE(after.readAll(),bytes);
        QCOMPARE(profile.args.GetArgs("-proxy"),native);
    }
    void pruneToggleRoundTripPreservesExactMiB() {
        Profile profile;profile.config("prune",int64_t{550});InitParameterInteraction(profile.args);
        QmlCoreSettings::Session session{QmlCoreSettings::LoadEffectiveValues(profile.args)};
        QVERIFY(session.changePrune(false).accepted);QVERIFY(session.writeToArgs(profile.args,"prune"));
        QVERIFY(session.changePrune(true).accepted);QVERIFY(session.writeToArgs(profile.args,"prune"));
        QCOMPARE(SettingTo<int64_t>(profile.args.GetPersistentSetting("prune")).value_or(-1),int64_t{550});
    }
    void manualPruneIsAnEnabledMode() {
        Profile profile;profile.config("prune",int64_t{1});
        const QmlCoreSettings::Session session{QmlCoreSettings::LoadEffectiveValues(profile.args)};
        QVERIFY(session.values().prune);QCOMPARE(SettingTo<int64_t>(session.settingValue("prune")).value_or(-1),int64_t{1});
    }
    void explicitDefaultDoesNotEraseTheSavedOverride() {
        Profile profile;profile.config("connect",std::string{"0"});QmlCoreSettings::SetRwSetting(profile.args,"natpmp",true);InitParameterInteraction(profile.args);
        QVERIFY(QmlCoreSettings::WriteCoreSettingOverride(profile.args,"natpmp",true));
        QVERIFY(profile.args.GetPersistentSetting("natpmp").isTrue());
    }
    void identicalCommitRetainsExplicitWriteIntent() {
        Profile profile;QmlCoreSettings::Session session{QmlCoreSettings::LoadEffectiveValues(profile.args)};
        const auto change=session.changeProxyLocation(session.values().proxy_address);
        QVERIFY(change.accepted);QVERIFY(change.force_write);QVERIFY(session.isTouched("proxy"));
    }
};
BITCOINQML_REGISTER_QT_TEST(SettingsPreservationTests)
#include <test_settingspreservation.moc>
