// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/bitcoinqmlapplication.h>
#include <qml/imageprovider.h>
#include <qml/models/onboardingoptionsmodel.h>
#include <qml/networkstyle.h>
#include <qml/test/integration_test_registry.h>
#include <qml/translationmanager.h>

#include <QDir>
#include <QFile>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

class OnboardingIntegrationTests : public QObject
{
    Q_OBJECT
    BitcoinQmlApplication& m_app;

    static QByteArray ReadFile(const QString& path)
    {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
    }

public:
    explicit OnboardingIntegrationTests(BitcoinQmlApplication& app) : m_app(app) {}

private Q_SLOTS:
    void existingProfilePreviewIsReadOnlyAndHonorsCommandLine()
    {
        QTemporaryDir profile;
        QVERIFY(profile.isValid());
        QVERIFY(QDir(profile.path()).mkdir("regtest"));
        const QByteArray config_data{"regtest=1\nlisten=0\nproxy=127.0.0.1:9050\n"};
        const QByteArray settings_data{"{\"listen\":true,\"prune\":2000}"};
        QFile config(profile.filePath("bitcoin.conf"));
        QVERIFY(config.open(QIODevice::WriteOnly));
        QCOMPARE(config.write(config_data), config_data.size());
        config.close();
        QFile settings(profile.filePath("regtest/settings.json"));
        QVERIFY(settings.open(QIODevice::WriteOnly));
        QCOMPARE(settings.write(settings_data), settings_data.size());
        settings.close();

        const std::vector<std::string> argv{"bitcoin-qt", "-datadir=" + profile.path().toStdString(), "-listen=0", "-choosedatadir"};
        OnboardingOptionsModel model(argv, false);
        QTRY_VERIFY_WITH_TIMEOUT(!model.storageCheckPending(), 5'000);
        QVERIFY2(model.previewError().isEmpty(), qPrintable(model.previewError()));
        QVERIFY(model.existingProfile());
        QVERIFY(!model.listen());
        const auto listen = model.coreSettingStatuses().value("listen").toMap();
        QCOMPARE(listen.value("source").toString(), QString("command_line"));
        QVERIFY(!listen.value("canEdit").toBool());
        model.setListen(true);
        QVERIFY(!model.listen());
        QVERIFY(model.prune());
        QCOMPARE(model.coreSettingStatuses().value("prune").toMap().value("source").toString(), QString("settings_json"));
        const auto proxy = model.coreSettingStatuses().value("proxy").toMap();
        QCOMPARE(proxy.value("source").toString(), QString("bitcoin_conf"));
        QVERIFY(proxy.value("createsGuiOverride").toBool());
        QVERIFY(!model.commitProxyLocation("not a proxy"));
        QCOMPARE(model.proxyAddress(), QString("127.0.0.1:9050"));
        QVERIFY(model.commitProxyLocation("127.0.0.1:9150"));
        QCOMPARE(model.proxyAddress(), QString("127.0.0.1:9150"));
        QCOMPARE(ReadFile(config.fileName()), config_data);
        QCOMPARE(ReadFile(settings.fileName()), settings_data);
    }

    void preinitPagesLoadAndFinishWithoutStartingAnotherNode()
    {
        QTemporaryDir profile;
        QVERIFY(profile.isValid());
        OnboardingOptionsModel model({"bitcoin-qt", "-regtest", "-datadir=" + profile.path().toStdString(), "-choosedatadir", "-nosettings"}, false);
        QTRY_VERIFY_WITH_TIMEOUT(!model.storageCheckPending(), 5'000);
        QVERIFY2(model.canFinish(), qPrintable(model.previewError()));
        const std::unique_ptr<const NetworkStyle> style{NetworkStyle::instantiate(ChainType::REGTEST)};
        QQmlApplicationEngine engine;
        m_app.translations().attachEngine(engine);
        engine.addImageProvider("images", new ImageProvider{style.get()});
        engine.rootContext()->setContextProperty("optionsModel", &model);
        QSignalSpy warnings(&engine, &QQmlEngine::warnings);
        engine.load(QUrl("qrc:/qml/pages/preinit.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto* window = engine.rootObjects().constFirst();
        QSignalSpy finished(window, SIGNAL(finished()));
        for (const auto* name : {"onboardingCover", "onboardingStrengthen", "onboardingBlockclock", "onboardingStorageLocation", "onboardingStorageAmount", "onboardingConnection"}) {
            QTRY_VERIFY_WITH_TIMEOUT(window->findChild<QObject*>(name), 5'000);
            auto* page = window->findChild<QObject*>(name);
            QTRY_VERIFY(page->property("visible").toBool());
            QVERIFY(QMetaObject::invokeMethod(page, "next"));
        }
        QTRY_COMPARE(finished.count(), 1);
        QVERIFY(window->property("completed").toBool());
        QVERIFY(window->property("starting").toBool());
        QCOMPARE(warnings.count(), 0);
        QVERIFY(!QFile::exists(profile.filePath("regtest/settings.json")));
    }
};

BITCOINQML_REGISTER_INTEGRATION_TEST(OnboardingIntegrationTests)
#include <test_onboarding_integration.moc>
