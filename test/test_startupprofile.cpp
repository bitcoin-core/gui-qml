// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/bitcoinqmlapplication.h>
#include <qml/models/settings_keys.h>
#include <qml/guiconstants.h>
#include <QtTest/QtTest>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QProcessEnvironment>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>

#include <cstdio>

namespace {
class StartupProbe : public BitcoinQmlApplication {
public:
    explicit StartupProbe(QString expected):m_expected(std::move(expected)){}
    bool diagnostic{false};
protected:
    void qmlEngineCreated(QQmlApplicationEngine& engine) override {
        QTimer::singleShot(0,&engine,[this,&engine]{
            const auto message=engine.rootContext()->contextProperty(QStringLiteral("message")).toString();
            auto* window=engine.rootObjects().isEmpty()?nullptr:qobject_cast<QQuickWindow*>(engine.rootObjects().front());
            diagnostic=window&&window->isVisible()&&message.contains(m_expected);
            const QByteArray marker=QByteArray{"R35_STARTUP_DIAGNOSTIC visible="}+QByteArray::number(window&&window->isVisible()?1:0)
                +QByteArray{" matched="}+QByteArray::number(diagnostic?1:0)+QByteArray{"\n"};
            std::fwrite(marker.constData(),1,static_cast<size_t>(marker.size()),stdout);
            std::fflush(stdout);
            if(window)window->close();
        });
    }
    void windowCreated() override {QTimer::singleShot(0,[this]{requestShutdown();});}
private:
    QString m_expected;
};
}

// Checked before the ordinary test QApplication is constructed. This exercises
// the real application entry point, native Core configuration and error window.
int RunStartupProfileProbe(const QString& mode)
{
    QTemporaryDir root;
    if(!root.isValid())return 81;
    const QString data=root.filePath(QStringLiteral("profile"));
    const QString gui=root.filePath(QStringLiteral("gui"));
    if(!QDir{}.mkpath(data+QStringLiteral("/regtest")))return 82;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,gui);
    QSettings::setPath(QSettings::IniFormat,QSettings::SystemScope,root.filePath(QStringLiteral("system")));
    qputenv("HOME",root.path().toUtf8());qputenv("XDG_CONFIG_HOME",gui.toUtf8());
    QDir::setCurrent(root.path());
    QStringList arguments{QStringLiteral("startup-profile-probe"),QStringLiteral("-regtest"),QStringLiteral("-disablewallet"),QStringLiteral("-lang=en"),QStringLiteral("-listen=0"),QStringLiteral("-connect=0"),QStringLiteral("-datadir=")+data};
    QString expected;
    QString original_config_path;
    QByteArray original_config;
    if(mode==QStringLiteral("cli-networks")){arguments<<QStringLiteral("-testnet");expected=QStringLiteral("Invalid combination");}
    else if(mode==QStringLiteral("config-networks")){
        QFile config{data+QStringLiteral("/bitcoin.conf")};if(!config.open(QIODevice::WriteOnly)||config.write("testnet=1\n")<0)return 83;
        expected=QStringLiteral("Invalid combination");
    }else if(mode==QStringLiteral("missing-absolute")||mode==QStringLiteral("missing-relative")){
        arguments.back()=QStringLiteral("-datadir=")+(mode==QStringLiteral("missing-relative")?QStringLiteral("missing-relative"):root.filePath(QStringLiteral("missing-drive")));
        expected=QStringLiteral("does not exist");
    }else if(mode==QStringLiteral("configured-unavailable")){
        arguments.removeLast();
        original_config_path=root.filePath(QStringLiteral("bitcoin.conf"));
        original_config=QStringLiteral("regtest=1\ndatadir=%1\n").arg(root.filePath(QStringLiteral("unmounted-profile"))).toUtf8();
        QFile config{original_config_path};
        if(!config.open(QIODevice::WriteOnly)||config.write(original_config)!=original_config.size())return 88;
        arguments<<QStringLiteral("-conf=")+original_config_path;
        expected=QStringLiteral("does not exist");
    }else if(mode==QStringLiteral("config-return-error")){
        QFile settings{data+QStringLiteral("/regtest/settings.json")};if(!settings.open(QIODevice::WriteOnly)||settings.write("{\"qml_onboarded\":true}\n")<0)return 84;settings.close();
        if(!QDir{}.mkdir(data+QStringLiteral("/regtest/settings.json.tmp")))return 85;
        expected=QStringLiteral("Settings file could not be written");
    }else{
        arguments.removeLast();
#ifdef Q_OS_MACOS
        const auto organization = QStringLiteral(QAPP_ORG_DOMAIN);
#else
        const auto organization = QStringLiteral(QAPP_ORG_NAME);
#endif
        QSettings preferences{QSettings::IniFormat,QSettings::UserScope,organization,QStringLiteral(QAPP_APP_NAME_REGTEST)};
        const auto stored=mode==QStringLiteral("saved-empty")?QVariant{QStringLiteral("")}:mode==QStringLiteral("saved-wrong-type")?QVariant{42}:QVariant{root.filePath(QStringLiteral("unmounted-profile"))};
        preferences.setValue(SettingsKeys::DATA_DIR,stored);preferences.sync();
        if(preferences.status()!=QSettings::NoError)return 86;
        expected=mode==QStringLiteral("saved-unavailable")?QStringLiteral("unavailable"):QStringLiteral("preference is invalid");
    }
    std::vector<QByteArray> encoded;std::vector<char*> argv;
    encoded.reserve(static_cast<size_t>(arguments.size()));argv.reserve(static_cast<size_t>(arguments.size())+1);
    for(const auto& argument:arguments)encoded.push_back(argument.toUtf8());
    for(auto& argument:encoded)argv.push_back(argument.data());
    argv.push_back(nullptr);
    StartupProbe application{expected};
    const int result=application.run(static_cast<int>(encoded.size()),argv.data());
    if(!original_config_path.isEmpty()){
        QFile config{original_config_path};
        if(!config.open(QIODevice::ReadOnly)||config.readAll()!=original_config)return 89;
        constexpr char RETAINED_CONFIG[]{"R35_CONFIG_SOURCE retained=1\n"};
        std::fwrite(RETAINED_CONFIG,1,sizeof(RETAINED_CONFIG)-1,stdout);
    }
    const QByteArray marker=QByteArray{"R35_STARTUP_EXIT actual="}+QByteArray::number(result)+QByteArray{" diagnostic="}
        +QByteArray::number(application.diagnostic?1:0)+QByteArray{"\n"};
    std::fwrite(marker.constData(),1,static_cast<size_t>(marker.size()),stdout);std::fflush(stdout);
    return application.diagnostic&&result==1?0:87;
}

class StartupProfileTests : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void originalFailureShowsActualErrorAndExitsCleanly_data(){
        QTest::addColumn<QString>("mode");
        for(const auto* mode:{"cli-networks","config-networks","missing-absolute","missing-relative","config-return-error","configured-unavailable","saved-unavailable","saved-empty","saved-wrong-type"})
            QTest::newRow(mode)<<QString::fromLatin1(mode);
    }
    void originalFailureShowsActualErrorAndExitsCleanly(){
        QFETCH(QString,mode);QProcess child;auto env=QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("BITCOIN_QML_STARTUP_PROFILE_CASE"),mode);env.insert(QStringLiteral("QT_QPA_PLATFORM"),QStringLiteral("minimal"));
        env.insert(QStringLiteral("QT_QUICK_BACKEND"),QStringLiteral("software"));env.insert(QStringLiteral("QML_DISABLE_DISK_CACHE"),QStringLiteral("1"));
        child.setProcessEnvironment(env);child.start(QCoreApplication::applicationFilePath(),{});
        QVERIFY(child.waitForStarted());const bool finished=child.waitForFinished(30000);
        if(!finished){child.kill();child.waitForFinished();}
        const auto output=child.readAllStandardOutput();const auto error=child.readAllStandardError();
        const auto evidence=qEnvironmentVariable("BITCOIN_QML_STARTUP_PROBE_DIR");
        if(!evidence.isEmpty()){
            QVERIFY(QDir{}.mkpath(evidence));
            for(const auto& [suffix,bytes]:{std::pair{QStringLiteral("stdout"),output},std::pair{QStringLiteral("stderr"),error},std::pair{QStringLiteral("exit"),QByteArray::number(child.exitCode())}}){
                QFile file{evidence+QLatin1Char('/')+mode+QLatin1Char('.')+suffix};QVERIFY(file.open(QIODevice::WriteOnly));QCOMPARE(file.write(bytes),bytes.size());
            }
        }
        QVERIFY2(finished,error.constData());QCOMPARE(child.exitStatus(),QProcess::NormalExit);
        QVERIFY2(child.exitCode()==0,qPrintable(QString::fromUtf8(output)+QString::fromUtf8(error)));
        QVERIFY(output.contains("R35_STARTUP_DIAGNOSTIC visible=1 matched=1"));
        QVERIFY(output.contains("R35_STARTUP_EXIT actual=1 diagnostic=1"));
        if(mode==QStringLiteral("configured-unavailable"))QVERIFY(output.contains("R35_CONFIG_SOURCE retained=1"));
        QVERIFY(!error.contains("AddressSanitizer"));QVERIFY(!error.contains("runtime error:"));
        QVERIFY(!error.contains("QML "));QVERIFY(!error.contains("ReferenceError"));QVERIFY(!error.contains("TypeError"));
    }
};
#ifdef BITCOINQML_NO_TEST_MAIN
#include <test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(StartupProfileTests)
#else
QTEST_MAIN(StartupProfileTests)
#endif
#include <test_startupprofile.moc>
