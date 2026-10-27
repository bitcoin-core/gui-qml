// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QtTest/QtTest>

#include <chainparams.h>
#include <qml/models/onboardingoptionsmodel.h>
#include <qml/onboarding_settings.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QScopeGuard>
#include <QSemaphore>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>

#include <atomic>
#include <thread>

#ifdef Q_OS_UNIX
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace {
bool WriteFile(const QString& path, const QByteArray& contents)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size();
}

std::vector<std::string> Arguments(const QString& path)
{
    return {"bitcoin-qt", "-regtest", "-datadir=" + path.toStdString()};
}
} // namespace

class OnboardingAsyncTests : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void previewReadsOwnNetworkWithoutChangingGlobalParams();
    void startupStatusReadsOwnNetworkWithoutChangingGlobalParams();
    void reselectCustomDataDirAfterRepair_data();
    void reselectCustomDataDirAfterRepair();
    void reselectCustomDataDirReloadsConfig();
    void reselectDefaultDataDirRefreshesPreview();
    void blockedConfigReadKeepsGuiResponsiveAndRejectsStaleSelection();
};

void OnboardingAsyncTests::previewReadsOwnNetworkWithoutChangingGlobalParams()
{
    SelectParams(ChainType::MAIN);
    const auto* live_params = &Params();
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QVERIFY(QDir(directory.path()).mkpath("regtest"));
    QVERIFY(WriteFile(directory.filePath("settings.json"), R"({"listen":true})"));
    QVERIFY(WriteFile(directory.filePath("regtest/settings.json"), R"({"listen":false})"));
    const auto preview = QmlOnboardingSettings::Preview(Arguments(directory.path()), false, directory.path());
    QVERIFY2(preview.ok, qPrintable(preview.error));
    QVERIFY(!preview.values.listen);
    QVERIFY(preview.profile.has_settings_file);
    QCOMPARE(&Params(), live_params);
    QCOMPARE(Params().GetChainType(), ChainType::MAIN);
}

void OnboardingAsyncTests::startupStatusReadsOwnNetworkWithoutChangingGlobalParams()
{
    SelectParams(ChainType::MAIN);
    const auto* live_params = &Params();
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QVERIFY(QDir(directory.path()).mkpath("regtest"));
    QVERIFY(WriteFile(directory.filePath("settings.json"), R"({"qml_onboarded":false})"));
    QVERIFY(WriteFile(directory.filePath("regtest/settings.json"), R"({"qml_onboarded":true})"));
    const auto status = QmlOnboardingSettings::ResolveOnboardingStartupStatus(Arguments(directory.path()), false);
    QVERIFY2(status.ok, qPrintable(status.error));
    QVERIFY(status.qml_onboarded);
    QCOMPARE(&Params(), live_params);
    QCOMPARE(Params().GetChainType(), ChainType::MAIN);
}

void OnboardingAsyncTests::reselectCustomDataDirAfterRepair_data()
{
    QTest::addColumn<QString>("failure");
    QTest::newRow("permissions") << QStringLiteral("permissions");
    QTest::newRow("file-instead-of-directory") << QStringLiteral("file");
}

void OnboardingAsyncTests::reselectCustomDataDirAfterRepair()
{
    QFETCH(QString, failure);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString selected = directory.filePath("selected");
    const auto writable_permissions = QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner;
    const auto restore_permissions = qScopeGuard([&] {
        if (failure == "permissions") QFile::setPermissions(selected, writable_permissions);
    });
    if (failure == "file") {
        QVERIFY(WriteFile(selected, {}));
    } else {
        QVERIFY(QDir().mkpath(selected));
        if (failure == "permissions") {
#ifdef Q_OS_UNIX
            QVERIFY(QFile::setPermissions(selected, QFileDevice::ReadOwner | QFileDevice::ExeOwner));
            if (QFileInfo(selected).isWritable()) QSKIP("The current user can write to read-only directories.");
#else
            QSKIP("The permission fixture requires POSIX directory permissions.");
#endif
        }
    }

    OnboardingOptionsModel model(Arguments(directory.path()), false);
    QTRY_VERIFY(model.canFinish());
    QSignalSpy finished(&model, &OnboardingOptionsModel::dataDirSelectionFinished);
    QSignalSpy directory_changed(&model, &OnboardingOptionsModel::dataDirChanged);
    QSignalSpy custom_changed(&model, &OnboardingOptionsModel::customDataDirStringChanged);
    QVERIFY(model.selectCustomDataDir(selected));
    QVERIFY(model.validationPending());
    QVERIFY(!model.canFinish());
    QTRY_COMPARE(finished.count(), 1);
    QVERIFY(!finished.at(0).at(0).toBool());
    QVERIFY(!model.previewError().isEmpty());
    QCOMPARE(model.validateCustomDataDir(selected), model.previewError());
    QCOMPARE(model.dataDir(), selected);
    QVERIFY(!model.canFinish());

    if (failure == "permissions") {
        QVERIFY(QFile::setPermissions(selected, writable_permissions));
    } else {
        QVERIFY(QFile::remove(selected));
        QVERIFY(QDir().mkpath(selected));
    }

    // The folder picker supplies a file URL for the same normalized path.
    QVERIFY(model.selectCustomDataDir(QUrl::fromLocalFile(selected).toString()));
    QVERIFY(model.validationPending());
    QVERIFY(!model.canFinish());
    QTRY_COMPARE(finished.count(), 2);
    QVERIFY2(finished.at(1).at(0).toBool(), qPrintable(model.previewError()));
    QVERIFY(model.previewError().isEmpty());
    QVERIFY(model.validateCustomDataDir(selected).isEmpty());
    QTRY_VERIFY(model.canFinish());
    QCOMPARE(directory_changed.count(), 1);
    QCOMPARE(custom_changed.count(), 1);
    QSignalSpy drained(&model, &OnboardingOptionsModel::shutdownFinished);
    model.beginShutdown();
    QTRY_COMPARE(drained.count(), 1);
}

void OnboardingAsyncTests::reselectCustomDataDirReloadsConfig()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString config = directory.filePath("bitcoin.conf");
    QVERIFY(WriteFile(config, "listen=0\n"));
    OnboardingOptionsModel model(Arguments(directory.path()), false);
    QTRY_VERIFY(model.canFinish());
    QVERIFY(model.selectCustomDataDir(directory.path()));
    QTRY_VERIFY(model.canFinish());
    QVERIFY(!model.listen());
    model.setServer(true);

    QVERIFY(WriteFile(config, "listen=1\n"));
    QSignalSpy finished(&model, &OnboardingOptionsModel::dataDirSelectionFinished);
    QSignalSpy directory_changed(&model, &OnboardingOptionsModel::dataDirChanged);
    QVERIFY(model.selectCustomDataDir(directory.path()));
    QVERIFY(model.validationPending());
    QTRY_COMPARE(finished.count(), 1);
    QTRY_VERIFY(model.canFinish());
    QVERIFY(model.listen());
    QVERIFY(model.server()); // Refreshes preserve settings edited during onboarding.
    QCOMPARE(directory_changed.count(), 0);
    QSignalSpy drained(&model, &OnboardingOptionsModel::shutdownFinished);
    model.beginShutdown();
    QTRY_COMPARE(drained.count(), 1);
}

void OnboardingAsyncTests::reselectDefaultDataDirRefreshesPreview()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    OnboardingOptionsModel model(Arguments(directory.path()), false);
    QTRY_VERIFY(model.canFinish());
    model.useDefaultDataDir();
    QTRY_VERIFY(!model.validationPending());

    QSignalSpy finished(&model, &OnboardingOptionsModel::dataDirSelectionFinished);
    QSignalSpy directory_changed(&model, &OnboardingOptionsModel::dataDirChanged);
    model.useDefaultDataDir();
    QVERIFY(model.validationPending());
    QVERIFY(!model.canFinish());
    QTRY_COMPARE(finished.count(), 1);
    QCOMPARE(model.dataDir(), model.getDefaultDataDirString());
    QCOMPARE(directory_changed.count(), 0);
    QSignalSpy drained(&model, &OnboardingOptionsModel::shutdownFinished);
    model.beginShutdown();
    QTRY_COMPARE(drained.count(), 1);
}

void OnboardingAsyncTests::blockedConfigReadKeepsGuiResponsiveAndRejectsStaleSelection()
{
#ifndef Q_OS_UNIX
    QSKIP("The gated filesystem fixture uses a POSIX FIFO.");
#else
    SelectParams(ChainType::MAIN);
    QTemporaryDir first;
    QTemporaryDir latest;
    QVERIFY(first.isValid());
    QVERIFY(latest.isValid());
    const QByteArray pipe = QFile::encodeName(first.filePath("bitcoin.conf"));
    const QByteArray replacement = QFile::encodeName(first.filePath("bitcoin.conf.next"));
    QVERIFY(WriteFile(QString::fromLocal8Bit(replacement), "regtest=1\n"));
    QVERIFY(::mkfifo(pipe.constData(), 0600) == 0);

    QSemaphore entered;
    QSemaphore release;
    std::atomic<bool> stop{false};
    std::thread writer([&] {
        int fd{-1};
        while (!stop && fd < 0) {
            fd = ::open(pipe.constData(), O_WRONLY | O_NONBLOCK);
            if (fd < 0) QThread::msleep(1);
        }
        if (fd < 0) return;
        entered.release();
        // A finite watchdog also releases a regressed synchronous constructor.
        release.tryAcquire(1, 10000);
        ::rename(replacement.constData(), pipe.constData());
        constexpr char config[]{"regtest=1\n"};
        const auto written = ::write(fd, config, sizeof(config) - 1);
        Q_UNUSED(written);
        ::close(fd);
    });
    const auto finish_writer = qScopeGuard([&] {
        stop = true;
        release.release();
        writer.join();
    });
    OnboardingOptionsModel model(Arguments(first.path()), false);
    QVERIFY(model.validationPending());
    QTRY_VERIFY(entered.available() > 0);
    entered.acquire();
    bool heartbeat{false};
    QTimer::singleShot(0, &model, [&] { heartbeat = true; });
    QTRY_VERIFY(heartbeat);
    QVERIFY(!model.canFinish());
    QVERIFY(model.selectCustomDataDir(latest.path()));
    release.release();
    QTRY_VERIFY_WITH_TIMEOUT(!model.validationPending(), 10000);
    QCOMPARE(model.dataDir(), latest.path());
    QCOMPARE(Params().GetChainType(), ChainType::MAIN);
    QSignalSpy drained(&model, &OnboardingOptionsModel::shutdownFinished);
    model.beginShutdown();
    QTRY_COMPARE(drained.count(), 1);
#endif
}

#ifdef BITCOINQML_NO_TEST_MAIN
#include <test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(OnboardingAsyncTests)
#else
QTEST_MAIN(OnboardingAsyncTests)
#endif
#include "test_onboardingasync.moc"
