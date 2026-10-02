// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/backendstage.h>
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
#include <stdexcept>

namespace {
// Native application termination sends Quit to the application even before
// QApplication::exec(). Unlike closing a window, accepting this event stops
// subsequent local event loops from dispatching timers and worker completions.
void RequestNativeQuit()
{
    QEvent quit{QEvent::Quit};
    QCoreApplication::sendEvent(QCoreApplication::instance(), &quit);
}
} // namespace

class BootstrapTests : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase() { qApp->setQuitOnLastWindowClosed(false); }
    void quitDuringBackendStage();
    void quitDuringFailingBackendStage();
    void dismissStartupDialog_data();
    void dismissStartupDialog();
    void quitDuringOnboardingAndFinalDrain();
};

void BootstrapTests::quitDuringBackendStage()
{
    QmlQuitHandler quit_handler;
    QSignalSpy requested{&quit_handler, &QmlQuitHandler::quitRequested};
    QSemaphore release;
    bool heartbeat{false};
    QTimer::singleShot(0, this, [&] {
        RequestNativeQuit();
        QTimer::singleShot(0, this, [&] {
            heartbeat = true;
            release.release();
        });
    });
    const auto result = RunBackendStage([&] { release.acquire(); return 42; });
    QCOMPARE(result, 42);
    QVERIFY(heartbeat);
    QVERIFY(quit_handler.isQuitRequested());
    QCOMPARE(requested.size(), 1);
}

void BootstrapTests::quitDuringFailingBackendStage()
{
    QmlQuitHandler quit_handler;
    QSemaphore release;
    QTimer::singleShot(0, this, [&] {
        RequestNativeQuit();
        QTimer::singleShot(0, this, [&] { release.release(); });
    });
    QVERIFY_EXCEPTION_THROWN(RunBackendStage([&]() -> int {
        release.acquire();
        throw std::runtime_error("bootstrap failed");
    }), std::runtime_error);
    QVERIFY(quit_handler.isQuitRequested());
}

void BootstrapTests::dismissStartupDialog_data()
{
    QTest::addColumn<QString>("action");
    QTest::newRow("native-quit") << QStringLiteral("quit");
    QTest::newRow("qml-close-button") << QStringLiteral("qml");
    QTest::newRow("window-close") << QStringLiteral("close");
}

void BootstrapTests::dismissStartupDialog()
{
    QFETCH(QString, action);
    QmlQuitHandler quit_handler;
    QQmlApplicationEngine engine;
    QQuickWindow window;
    window.show();
    QTimer::singleShot(0, this, [&] {
        if (action == "quit") RequestNativeQuit();
        else if (action == "qml") Q_EMIT engine.quit();
        else window.close();
    });
    ExecStartupDialog(engine, window, quit_handler);
    // Returning from a modal error must still leave worker retirement usable.
    QCOMPARE(RunBackendStage([] { return 42; }), 42);
}

void BootstrapTests::quitDuringOnboardingAndFinalDrain()
{
    QmlQuitHandler quit_handler;
    QSignalSpy requested{&quit_handler, &QmlQuitHandler::quitRequested};
    QEventLoop onboarding;
    connect(&quit_handler, &QmlQuitHandler::quitRequested, &onboarding, &QEventLoop::quit);
    QTimer::singleShot(0, this, RequestNativeQuit);
    onboarding.exec();

    // Accepted work can outlive an early onboarding return. Repeated Quit
    // during that final drain must still allow its completion to be delivered.
    QSemaphore release;
    std::atomic_bool work_finished{false};
    BackendExecutor executor;
    executor.submit(this, [&] { release.acquire(); work_finished = true; }, [] {});
    QTimer::singleShot(0, this, [&] {
        RequestNativeQuit();
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

QTEST_MAIN(BootstrapTests)
#include "test_bootstrap.moc"
