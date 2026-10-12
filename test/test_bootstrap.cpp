// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/backendexecutor.h>
#include <qml/quithandler.h>
#include <qml/startupdialog.h>

#include <QApplication>
#include <QEventLoop>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QSemaphore>
#include <QSignalSpy>
#include <QTimer>
#include <QtTest/QtTest>

#include <atomic>

namespace {
void SendApplicationQuitEvent()
{
    QEvent quit{QEvent::Quit};
    QCoreApplication::sendEvent(QCoreApplication::instance(), &quit);
}
}

class BootstrapBeforeExecTests : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase() { qApp->setQuitOnLastWindowClosed(false); }
    void dismissedStartupDialogAllowsFinalDrain_data();
    void dismissedStartupDialogAllowsFinalDrain();
    void repeatedNativeQuitAllowsOnboardingExitAndFinalDrain();
};

void BootstrapBeforeExecTests::dismissedStartupDialogAllowsFinalDrain_data()
{
    QTest::addColumn<QString>("action");
    QTest::newRow("native-quit") << QStringLiteral("quit");
    QTest::newRow("qml-close-button") << QStringLiteral("qml");
    QTest::newRow("window-close") << QStringLiteral("close");
}

void BootstrapBeforeExecTests::dismissedStartupDialogAllowsFinalDrain()
{
    QFETCH(QString, action);
    QmlQuitHandler quit_handler;
    QQmlApplicationEngine engine;
    QQuickWindow window;
    window.show();
    QTimer::singleShot(0, this, [&] {
        if (action == "quit") SendApplicationQuitEvent();
        else if (action == "qml") Q_EMIT engine.quit();
        else window.close();
    });
    ExecStartupDialog(engine, window, quit_handler);
    std::atomic_bool work_finished{false};
    BackendExecutor executor;
    executor.submit(this, [&] { work_finished = true; }, [] {});
    QEventLoop drain;
    bool drained{false};
    BackendExecutor::shutdownAll(&drain, [&] { drained = true; drain.quit(); });
    while (!drained) drain.exec();
    QVERIFY(work_finished.load());
    QVERIFY(executor.isDrained());
}

void BootstrapBeforeExecTests::repeatedNativeQuitAllowsOnboardingExitAndFinalDrain()
{
    QmlQuitHandler quit_handler;
    QSignalSpy requested{&quit_handler, &QmlQuitHandler::quitRequested};
    QEventLoop onboarding;
    connect(&quit_handler, &QmlQuitHandler::quitRequested, &onboarding, &QEventLoop::quit);
    QTimer::singleShot(0, this, SendApplicationQuitEvent);
    onboarding.exec();

    QSemaphore release;
    std::atomic_bool work_finished{false};
    BackendExecutor executor;
    executor.submit(this, [&] { release.acquire(); work_finished = true; }, [] {});
    QTimer::singleShot(0, this, [&] {
        SendApplicationQuitEvent();
        QTimer::singleShot(0, this, [&] { release.release(); });
    });
    QEventLoop drain;
    bool drained{false};
    BackendExecutor::shutdownAll(&drain, [&] { drained = true; drain.quit(); });
    while (!drained) drain.exec();
    QVERIFY(executor.isDrained());
    QVERIFY(work_finished.load());
    QCOMPARE(requested.size(), 2);
}

QTEST_MAIN(BootstrapBeforeExecTests)
#include "test_bootstrap.moc"
