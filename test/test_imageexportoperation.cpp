// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/backendexecutor.h>
#include <qml/models/imageexportoperation.h>
#include <test/mocks/manualexecutionenvironment.h>
#include <test/qt_test_registry.h>

#include <QFile>
#include <QSemaphore>
#include <QTemporaryDir>
#include <QThread>

#include <stdexcept>
#include <QtTest/QtTest>

namespace {
QImage Image() { QImage image{32, 32, QImage::Format_ARGB32}; image.fill(Qt::green); return image; }
imageexport::Identity Identity() { return {std::make_shared<const int>(1), "17", "bitcoin:original", 2, 3}; }
struct Fixture {
    ManualRoutedExecution env;
    std::shared_ptr<std::atomic_bool> closed{std::make_shared<std::atomic_bool>(false)};
    int writes{0}, outcomes{0}, notifications{0};
    bool original_target{true};
    imageexport::Error write_error{imageexport::Error::None};
    std::shared_ptr<imageexport::Operation> operation;
    void start(QImage image = Image(), QUrl path = QUrl::fromLocalFile("/original.png")) {
        imageexport::Services services{env.scheduler, closed,
            [this](const QImage&, const QUrl& target) { ++writes; original_target = target == QUrl::fromLocalFile("/original.png"); return write_error; },
            [this](const auto&) { ++outcomes; }};
        operation = std::make_shared<imageexport::Operation>(std::move(image), std::move(path), Identity(), std::move(services), [this] { ++notifications; });
        operation->start();
    }
};
}

class ImageExportOperationTests : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void effectAndOutcomeUseSeparateControlledTurns()
    {
        Fixture f; f.start();
        QCOMPARE(f.writes, 0); QVERIFY(!f.operation->result());
        QVERIFY(f.env.step(execution::Lane::Core));
        QCOMPARE(f.writes, 1); QVERIFY(f.original_target); QCOMPARE(f.outcomes, 0); QVERIFY(!f.operation->result());
        f.env.drain();
        const auto result = f.operation->result(); QVERIFY(result); QVERIFY(result->saved);
        QCOMPARE(result->identity.request_id, QStringLiteral("17"));
        QCOMPARE(result->identity.payload, QStringLiteral("bitcoin:original"));
        QCOMPARE(result->identity.generation, 2U); QCOMPARE(result->identity.revision, 3U);
        QCOMPARE(f.outcomes, 1); QCOMPARE(f.notifications, 1);
        f.operation->start(); f.env.drain(); QCOMPARE(f.writes, 1);
    }
    void queuedInterestOrOpeningRetirementPreventsWrite_data()
    {
        QTest::addColumn<bool>("close"); QTest::newRow("interest") << false; QTest::newRow("opening") << true;
    }
    void queuedInterestOrOpeningRetirementPreventsWrite()
    {
        QFETCH(bool, close);
        Fixture f; f.start();
        if (close) f.closed->store(true); else f.operation->retire();
        f.env.drain(); QCOMPARE(f.writes, 0); QCOMPARE(f.outcomes, 1);
        QVERIFY(f.operation->result());
        QCOMPARE(f.operation->result()->error, close ? imageexport::Error::Closed : imageexport::Error::Retired);
    }
    void invalidPayloadAndRejectedAdmissionAreExplicit_data()
    {
        QTest::addColumn<int>("kind");
        QTest::newRow("null") << 0; QTest::newRow("oversized") << 1; QTest::newRow("closed-queue") << 2; QTest::newRow("transport") << 3;
    }
    void invalidPayloadAndRejectedAdmissionAreExplicit()
    {
        QFETCH(int, kind);
        Fixture f;
        if (kind == 2) f.env.scheduler->closeOrdinaryAdmission();
        if (kind == 3) f.env.rejectLane(execution::Lane::Core);
        f.start(kind == 0 ? QImage{} : kind == 1 ? QImage{737, 1, QImage::Format_ARGB32} : Image());
        f.env.drain(); QCOMPARE(f.writes, 0); QCOMPARE(f.outcomes, 1);
        QVERIFY(f.operation->result());
        QCOMPARE(f.operation->result()->error, kind == 2 ? imageexport::Error::Closed : kind == 3 ? imageexport::Error::Rejected : imageexport::Error::Invalid);
    }
    void nativeFileFailureDoesNotClaimSuccess()
    {
        Fixture f; f.start(Image(), QUrl::fromLocalFile("/original.png"));
        // Test the real writer independently of the injected effect oracle.
        QTemporaryDir directory; QVERIFY(directory.isValid());
        QCOMPARE(imageexport::WritePng(Image(), QUrl::fromLocalFile(directory.path() + "/missing/qr.png")), imageexport::Error::Write);
        QCOMPARE(imageexport::WritePng(Image(), QUrl{"https://example.invalid/qr.png"}), imageexport::Error::Invalid);
        f.operation->retire(); f.env.drain(); QVERIFY(!f.operation->result()->saved);
    }
    void rejectedWriterAndExceptionRetainFailure_data()
    {
        QTest::addColumn<bool>("throws"); QTest::newRow("writer") << false; QTest::newRow("exception") << true;
    }
    void rejectedWriterAndExceptionRetainFailure()
    {
        QFETCH(bool, throws);
        ManualRoutedExecution env;
        auto closed = std::make_shared<std::atomic_bool>(false);
        auto operation = std::make_shared<imageexport::Operation>(Image(), QUrl::fromLocalFile("/original.png"), Identity(),
            imageexport::Services{env.scheduler, closed, [throws](const auto&, const auto&) -> imageexport::Error {
                if (throws) throw std::runtime_error("write failed");
                return imageexport::Error::Write;
            }, {}}, [] {});
        operation->start(); env.drain(); QVERIFY(operation->result());
        QVERIFY(!operation->result()->saved); QCOMPARE(operation->result()->error, imageexport::Error::Write);
    }
    void guiProjectionWaitsForItsControlledMailbox()
    {
        ManualRoutedExecution env;
        std::deque<std::function<void()>> gui;
        auto closed = std::make_shared<std::atomic_bool>(false);
        ImageExportOperation operation{Image(), QUrl::fromLocalFile("/original.png"), Identity(),
            {env.scheduler, closed, [](const auto&, const auto&) { return imageexport::Error::None; }, {}},
            [&gui](auto turn) { gui.push_back(std::move(turn)); }};
        operation.start(); env.drain(); QVERIFY(operation.pending());
        QVERIFY(operation.operation()->result()->saved); QCOMPARE(gui.size(), 1U);
        gui.front()(); gui.pop_front(); QVERIFY(!operation.pending()); QVERIFY(operation.saved());
    }
    void physicalWriteSurvivesObserverLossAndRetainsOriginalFile()
    {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const QString path = directory.path() + QStringLiteral("/original.png");
        const auto original = Image();
        BackendExecutor executor;
        QSemaphore writing, release;
        const auto* gui_thread = QThread::currentThread();
        std::atomic_bool native{false}; std::atomic_int outcomes{0};
        std::deque<std::function<void()>> gui;
        std::mutex gui_mutex;
        auto closed = std::make_shared<std::atomic_bool>(false);
        auto projection = std::make_unique<ImageExportOperation>(original, QUrl::fromLocalFile(path), Identity(),
            imageexport::Services{executor.scheduler(), closed, [&](const QImage& image, const QUrl& target) {
                native.store(QThread::currentThread() != gui_thread);
                writing.release(); release.acquire(); return imageexport::WritePng(image, target);
            }, [&](const auto&) { ++outcomes; }},
            [&](auto turn) { std::lock_guard lock{gui_mutex}; gui.push_back(std::move(turn)); });
        auto retained = projection->operation(); projection->start();
        const bool started = writing.tryAcquire(1, 3000);
        if (!started) release.release();
        QVERIFY(started); QVERIFY(native.load()); QVERIFY(!QFile::exists(path));
        projection.reset(); closed->store(true); release.release();
        QVERIFY(QTest::qWaitFor([&] { return retained->result().has_value() && outcomes.load() == 1; }, 3000));
        QVERIFY(retained->result()->saved); QCOMPARE(outcomes.load(), 1);
        const QImage saved{path}; QCOMPARE(saved.size(), original.size()); QCOMPARE(saved.pixel(0, 0), original.pixel(0, 0));
        executor.shutdown(); QVERIFY(QTest::qWaitFor([&] { return executor.isDrained(); }, 3000));
        std::lock_guard lock{gui_mutex}; for (auto& turn : gui) turn();
    }
};

BITCOINQML_REGISTER_QT_TEST(ImageExportOperationTests)
#include <test_imageexportoperation.moc>
