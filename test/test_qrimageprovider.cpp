// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/backendexecutor.h>
#include <qml/models/uipreferences.h>
#include <qml/qrimageprovider.h>
#include <test/mocks/manualexecutionenvironment.h>
#include <test/qt_test_registry.h>

#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickImageResponse>
#include <QQuickTextureFactory>
#include <QSemaphore>
#include <QSignalSpy>
#include <QScopeGuard>
#include <QThread>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include <deque>
#include <functional>
#include <memory>
#include <stdexcept>

namespace {
QImage Pixel(Qt::GlobalColor color) { QImage image{1, 1, QImage::Format_ARGB32}; image.fill(color); return image; }
void Publish() { QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall); }
}
class QRImageProviderTests : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void renderingOutcomeAndNativePublicationAreSeparateTurns()
    {
        ManualRoutedExecution env; int renders{0}; QString source;
        QRImageProvider provider{env.scheduler, [&](const QString& id) { ++renders; source = id; return Pixel(Qt::red); }};
        std::unique_ptr<QQuickImageResponse> response{provider.requestImageResponse("original", {})};
        QSignalSpy finished{response.get(), &QQuickImageResponse::finished};
        QCOMPARE(renders, 0); QCOMPARE(finished.count(), 0); QVERIFY(!response->textureFactory());
        QVERIFY(env.step(execution::Lane::Domain)); QCOMPARE(renders, 1); QCOMPARE(source, QStringLiteral("original"));
        QCOMPARE(finished.count(), 0); QVERIFY(!response->textureFactory());
        env.drain(); QCOMPARE(finished.count(), 0);
        Publish(); QCOMPARE(finished.count(), 1); QVERIFY(response->errorString().isEmpty());
        std::unique_ptr<QQuickTextureFactory> texture{response->textureFactory()}; QVERIFY(texture);
        QCOMPARE(texture->image().pixelColor(0, 0), QColor{Qt::red}); QVERIFY(env.quiescent());
    }
    void cancelledOriginalDoesNotRenderOrRetargetItsReplacement()
    {
        ManualRoutedExecution env; QStringList rendered;
        QRImageProvider provider{env.scheduler, [&](const QString& id) { rendered.push_back(id); return Pixel(Qt::blue); }};
        std::unique_ptr<QQuickImageResponse> old{provider.requestImageResponse("old", {})};
        QSignalSpy old_finished{old.get(), &QQuickImageResponse::finished};
        old->cancel(); old->cancel(); QCOMPARE(old_finished.count(), 0);
        std::unique_ptr<QQuickImageResponse> current{provider.requestImageResponse("replacement", {})};
        QSignalSpy current_finished{current.get(), &QQuickImageResponse::finished};
        env.drain(); Publish(); QCOMPARE(old_finished.count(), 1); QCOMPARE(current_finished.count(), 1);
        QCOMPARE(rendered, QStringList{QStringLiteral("replacement")});
        QVERIFY(!old->errorString().isEmpty()); QVERIFY(!old->textureFactory());
        QVERIFY(current->errorString().isEmpty()); QVERIFY(env.quiescent());
    }
    void rejectedOrInvalidRequestsFinishWithoutRendering_data()
    {
        QTest::addColumn<int>("kind");
        QTest::newRow("closed") << 0; QTest::newRow("transport") << 1;
        QTest::newRow("oversized") << 2;
    }
    void rejectedOrInvalidRequestsFinishWithoutRendering()
    {
        QFETCH(int, kind);
        ManualRoutedExecution env; int renders{0};
        if (kind == 0) env.scheduler->closeOrdinaryAdmission();
        if (kind == 1) env.rejectLane(execution::Lane::Domain);
        QRImageProvider provider{env.scheduler, [&](const QString&) { ++renders; return Pixel(Qt::red); }};
        std::unique_ptr<QQuickImageResponse> response{provider.requestImageResponse(kind == 2 ? QString(32 * 1024 + 1, 'a') : QStringLiteral("qr"), {})};
        QSignalSpy finished{response.get(), &QQuickImageResponse::finished};
        QCOMPARE(finished.count(), 0); env.drain(); Publish();
        QCOMPARE(finished.count(), 1); QCOMPARE(renders, 0); QVERIFY(!response->errorString().isEmpty());
        QVERIFY(!response->textureFactory()); QVERIFY(env.quiescent());
    }
    void renderFailureAndProviderDestructionRetainOriginalCompletion_data()
    {
        QTest::addColumn<bool>("fail"); QTest::newRow("success") << false; QTest::newRow("failure") << true;
    }
    void renderFailureAndProviderDestructionRetainOriginalCompletion()
    {
        QFETCH(bool, fail); ManualRoutedExecution env;
        auto provider = std::make_unique<QRImageProvider>(env.scheduler, [fail](const QString&) {
            if (fail) throw std::runtime_error("encoder failure");
            return Pixel(Qt::green);
        });
        std::unique_ptr<QQuickImageResponse> response{provider->requestImageResponse("original", {})}; provider.reset();
        QSignalSpy finished{response.get(), &QQuickImageResponse::finished}; env.drain(); Publish();
        QCOMPARE(finished.count(), 1); QCOMPARE(response->errorString().isEmpty(), !fail); QVERIFY(env.quiescent());
    }
    void realRendererPreservesModuleGridAndColors()
    {
        ManualRoutedExecution env; QRImageProvider provider{env.scheduler};
        std::unique_ptr<QQuickImageResponse> response{provider.requestImageResponse("bitcoin%3Aoriginal?fg=%23ff0000&bg=%23ffffff", {})};
        QSignalSpy finished{response.get(), &QQuickImageResponse::finished}; env.drain(); Publish();
        QCOMPARE(finished.count(), 1); QVERIFY(response->errorString().isEmpty());
        std::unique_ptr<QQuickTextureFactory> texture{response->textureFactory()}; QVERIFY(texture);
        const auto image = texture->image();
        if (image.size() == QSize{1, 1}) { QCOMPARE(image.pixelColor(0, 0).alpha(), 0); return; } // QR-disabled configuration.
        QVERIFY(image.width() >= 21 && image.width() <= 177); QCOMPARE(image.width(), image.height());
        bool red{false}, white{false};
        for (int y{0}; y < image.height(); ++y) for (int x{0}; x < image.width(); ++x) {
            const auto pixel = image.pixelColor(x, y);
            QVERIFY(pixel == QColor{Qt::red} || pixel == QColor{Qt::white});
            red |= pixel == QColor{Qt::red}; white |= pixel == QColor{Qt::white};
        }
        QVERIFY(red && white);
    }
    void qmlControlBoundsSourceEncodingBeforeProviderAdmission()
    {
        Q_INIT_RESOURCE(bitcoin_qml);
        static const int preferences_type = qmlRegisterType<AppPreferences>("org.bitcoincore.qt", 1, 0, "AppPreferences");
        Q_UNUSED(preferences_type);
        QTemporaryDir directory; QVERIFY(directory.isValid());
        ManualRoutedExecution preferences_transport;
        std::deque<std::function<void()>> preferences_gui;
        UiPreferencesNamespace identity; identity.file = directory.filePath(QStringLiteral("preferences.ini"));
        UiPreferencesStore preferences{{preferences_transport.scheduler,
            [&](auto turn) { preferences_gui.push_back(std::move(turn)); }}, std::move(identity)};
        const auto settle_preferences = [&] {
            for (int turn = 0; turn < 1024; ++turn) {
                preferences_transport.drain();
                if (preferences_gui.empty()) return true;
                auto publication = std::move(preferences_gui.front()); preferences_gui.pop_front(); publication();
            }
            return false;
        };
        const auto close_preferences = qScopeGuard([&] {
            preferences.close(); QVERIFY(settle_preferences());
            QVERIFY(preferences_transport.quiescent());
        });
        preferences.start(); QVERIFY(settle_preferences()); QVERIFY(preferences.ready());
        ManualRoutedExecution env; int renders{0}; QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("uiPreferencesStore"), &preferences);
        engine.addImageProvider(QStringLiteral("qr"), new QRImageProvider{env.scheduler, [&](const QString&) { ++renders; return Pixel(Qt::red); }});
        QQmlComponent component{&engine, QUrl{QStringLiteral("qrc:/qml/controls/QRImage.qml")}};
        std::unique_ptr<QObject> image{component.create()}; QVERIFY2(image, qPrintable(component.errorString()));
        QVERIFY(image->setProperty("code", QString(1'000'000, 'a')));
        QCOMPARE(image->property("source").toUrl(), QUrl{}); Publish(); env.drain();
        QCOMPARE(renders, 0); QVERIFY(env.quiescent());
        QVERIFY(image->setProperty("code", QString{})); QCOMPARE(image->property("source").toUrl(), QUrl{});
    }
    void actualQmlImageKeepsReplacementWhenOldRenderingFinishes_data()
    {
        QTest::addColumn<bool>("destroy");
        QTest::newRow("source-replaced") << false;
        QTest::newRow("view-and-engine-destroyed") << true;
    }
    void actualQmlImageKeepsReplacementWhenOldRenderingFinishes()
    {
        QFETCH(bool, destroy);
        BackendExecutor backend{execution::Options{}}; QSemaphore entered, release;
        auto engine = std::make_unique<QQmlEngine>(); QList<QQmlError> warnings;
        QObject::connect(engine.get(), &QQmlEngine::warnings, engine.get(), [&](const auto& errors) { warnings.append(errors); });
        engine->addImageProvider(QStringLiteral("qr"), new QRImageProvider{backend.scheduler(), [&](const QString& id) {
            if (id == QStringLiteral("old")) { entered.release(); release.acquire(); }
            const int width = id == QStringLiteral("old") ? 7 : 13;
            QImage image{width, width, QImage::Format_ARGB32}; image.fill(Qt::blue); return image;
        }, engine.get()});
        const auto drain = qScopeGuard([&] { release.release(); backend.shutdown(); QTRY_VERIFY(backend.isDrained()); });
        QQmlComponent component{engine.get()};
        component.setData("import QtQuick 2.15; Image { width: 64; height: 64; cache: false; source: \"image://qr/old\" }", QUrl{});
        std::unique_ptr<QObject> image{component.create()}; QVERIFY2(image, qPrintable(component.errorString()));
        QTRY_VERIFY(entered.available() > 0); QVERIFY(entered.tryAcquire()); QCOMPARE(image->property("status").toInt(), 2); // Loading.
        if (destroy) {
            image.reset(); engine.reset();
            release.release(); backend.shutdown(); QTRY_VERIFY(backend.isDrained());
            QVERIFY(warnings.isEmpty()); return;
        }
        QVERIFY(image->setProperty("source", QUrl{QStringLiteral("image://qr/new")}));
        release.release(); QTRY_COMPARE(image->property("status").toInt(), 1); // Ready.
        QCOMPARE(image->property("sourceSize").toSize(), QSize(13, 13));
        QCOMPARE(image->property("source").toUrl(), QUrl{QStringLiteral("image://qr/new")});
        QVERIFY(warnings.isEmpty());
    }
    void realWorkerCancellationWaitsForExecutingRenderer()
    {
        BackendExecutor backend{execution::Options{}}; QSemaphore entered, release;
        bool off_gui{false}; auto* gui_thread = QThread::currentThread();
        QRImageProvider provider{backend.scheduler(), [&](const QString&) {
            off_gui = QThread::currentThread() != gui_thread; entered.release(); release.acquire(); return Pixel(Qt::green);
        }};
        std::unique_ptr<QQuickImageResponse> response{provider.requestImageResponse("original", {})};
        QSignalSpy finished{response.get(), &QQuickImageResponse::finished};
        const bool executing = entered.tryAcquire(1, 5000);
        const auto unblock = qScopeGuard([&] {
            release.release(); response->cancel(); backend.shutdown(); QTRY_VERIFY(backend.isDrained());
        });
        QVERIFY(executing); response->cancel(); Publish(); QCOMPARE(finished.count(), 0);
        release.release(); QTRY_COMPARE(finished.count(), 1); QVERIFY(off_gui);
        QVERIFY(!response->errorString().isEmpty()); QVERIFY(!response->textureFactory());
        backend.shutdown(); QTRY_VERIFY(backend.isDrained());
    }
};
BITCOINQML_REGISTER_QT_TEST(QRImageProviderTests)
#include <test_qrimageprovider.moc>
