// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/monospaceoutputmodel.h>
#include <qml/executor.h>
#include <test/mocks/manualnodeobservations.h>
#include <test/mocks/manualstartupexecution.h>

#include <QScopeGuard>
#include <QSemaphore>
#include <QGuiApplication>
#include <QClipboard>
#include <QThread>
#include <QtTest/QtTest>

#include <atomic>

namespace {
class Source final : public QAbstractListModel {
public:
    QStringList lines;
    mutable int reads{0};
    void replace(QStringList values) { beginResetModel(); lines = std::move(values); endResetModel(); }
    int rowCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : lines.size(); }
    QVariant data(const QModelIndex& index, int role) const override { ++reads; return index.isValid() && role == Qt::UserRole ? QVariant{lines.at(index.row())} : QVariant{}; }
    QHash<int, QByteArray> roleNames() const override { return {{Qt::UserRole, "content"}}; }
};
class RoleSource final : public QAbstractListModel {
public:
    enum { Text = Qt::UserRole, Left, Right, Category };
    int rowCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : 1; }
    QVariant data(const QModelIndex& index, int role) const override {
        if (!index.isValid() || index.row() != 0) return {};
        switch (role) {
        case Text: return QStringLiteral("alpha\nbeta");
        case Left: return QString(140, QLatin1Char('L'));
        case Right: return QStringLiteral("right");
        case Category: return 7;
        default: return {};
        }
    }
    QHash<int, QByteArray> roleNames() const override { return {{Text, "body"}, {Left, "left"}, {Right, "right"}, {Category, "category"}}; }
};
struct AffinityTrace {
    QThread* gui{QThread::currentThread()};
    std::atomic<int> source_reads{0}, source_off_gui{0}, cleanup_effects{0}, deliveries{0}, cleanup_on_gui{0};
};
class AffinitySource final : public QAbstractListModel {
    const std::shared_ptr<AffinityTrace> m_trace;
public:
    explicit AffinitySource(std::shared_ptr<AffinityTrace> trace) : m_trace(std::move(trace)) {}
    int rowCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : 130; }
    QVariant data(const QModelIndex& index, int role) const override {
        ++m_trace->source_reads;
        if (QThread::currentThread() != m_trace->gui) ++m_trace->source_off_gui;
        return index.isValid() && role == Qt::UserRole ? QVariant{QStringLiteral("worker needle %1").arg(index.row())} : QVariant{};
    }
    QHash<int, QByteArray> roleNames() const override { return {{Qt::UserRole, "content"}}; }
};
struct Ports {
    ManualNodeObservations env;
    std::deque<std::function<void()>> releases, retries;
    bool reject_retirement{false};
    std::shared_ptr<execution::SerialQueue> utility{std::make_shared<execution::SerialQueue>(env.executor().makeQueue(execution::Lane::Utility))};
    std::unique_ptr<PresentationServices> presentation;
    PresentationServices* services() {
        if (!presentation) presentation = std::make_unique<PresentationServices>(ports());
        return presentation.get();
    }
    PresentationServices::Ports ports() {
        return {env.post(),
            [&](auto release) { if (reject_retirement) return false; releases.push_back(std::move(release)); return true; },
            [&](auto retry) { retries.push_back(std::move(retry)); }, utility};
    }
    void releaseAll() { while (!releases.empty()) { auto release = std::move(releases.front()); releases.pop_front(); release(); } }
    void settle() {
        for (int i = 0; i < 1000; ++i) {
            env.drain(); env.publishAll(); releaseAll(); env.publishAll();
            if (env.turnsIdle() && env.publicationsIdle() && releases.empty()) break;
        }
    }
};
}

class MonospaceOutputModelTests : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void realDomainExecutionKeepsSourceAndPresentationOnGui()
    {
        execution::Executor executor{execution::Executor::Config{}};
        const auto trace = std::make_shared<AffinityTrace>();
        const auto utility = std::make_shared<execution::SerialQueue>(executor.makeQueue(execution::Lane::Utility));
        const auto retirement = std::make_shared<execution::SerialQueue>(executor.makeQueue(execution::Lane::Utility));
        AffinitySource source{trace};
        PresentationServices services{{
            [this, trace](auto callback) {
                if (QThread::currentThread() != trace->gui) ++trace->deliveries;
                QMetaObject::invokeMethod(this, std::move(callback), Qt::QueuedConnection);
            },
            [retirement, trace](auto release) {
                return retirement->post([trace, release = std::move(release)] {
                    if (QThread::currentThread() == trace->gui) ++trace->cleanup_on_gui;
                    release();
                    ++trace->cleanup_effects;
                });
            }, {}, utility}};
        MonospaceOutputModel model;
        model.setServices(&services);
        QSemaphore entered, release;
        std::atomic<QThread*> worker_thread{nullptr};
        std::atomic<int> presentation_off_gui{0};
        const auto cleanup = qScopeGuard([&] {
            release.release();
            model.close();
            services.close();
            const auto drained = std::make_shared<std::atomic_bool>(false);
            utility->whenDrained([drained] { *drained = true; });
            const bool retired = QTest::qWaitFor([&] { return drained->load() && services.pendingRetirements() == 0; }, 10'000);
            executor.stopAndJoin();
            QVERIFY(retired);
        });
        QVERIFY(utility->post([&] {
            worker_thread = QThread::currentThread();
            entered.release();
            release.acquire();
        }));
        QVERIFY(entered.tryAcquire(1, 5'000));
        QVERIFY(worker_thread.load() != trace->gui);
        const auto observe_presentation = [&] { if (QThread::currentThread() != trace->gui) ++presentation_off_gui; };
        connect(&model, &MonospaceOutputModel::changed, this, observe_presentation, Qt::DirectConnection);
        connect(&model, &MonospaceOutputModel::countChanged, this, observe_presentation, Qt::DirectConnection);
        connect(&model, &MonospaceOutputModel::searchChanged, this, observe_presentation, Qt::DirectConnection);
        model.setSourceModel(&source);
        QCOMPARE(model.rowCount(), 0);
        release.release();
        QTRY_COMPARE_WITH_TIMEOUT(model.rowCount(), 130, 10000);
        model.setSearchText(QStringLiteral("needle"));
        QTRY_COMPARE_WITH_TIMEOUT(model.searchResultCount(), 130, 10000);
        QVERIFY(!model.searchBusy());
        QCOMPARE(trace->source_reads.load(), 130);
        QCOMPARE(trace->source_off_gui.load(), 0);
        QVERIFY(trace->deliveries.load() > 0);
        QCOMPARE(presentation_off_gui.load(), 0);
        QCOMPARE(model.data(model.index(129, 0), MonospaceOutputModel::PlainContentRole).toString(), QStringLiteral("worker needle 129"));
        model.close();
        QTRY_COMPARE_WITH_TIMEOUT(services.pendingRetirements(), size_t{0}, 10000);
        QVERIFY(trace->cleanup_effects.load() > 0);
        QCOMPARE(trace->cleanup_on_gui.load(), 0);
    }

    void configuredRolesPreserveLineMetadataAndModelBounds()
    {
        Ports ports;
        RoleSource source;
        MonospaceOutputModel model;
        model.setServices(ports.services());
        const auto cleanup = qScopeGuard([&] { model.close(); ports.settle(); ports.services()->retryRetirements(); });
        QSignalSpy source_changed(&model, &MonospaceOutputModel::sourceModelChanged);
        model.setContentRole(QStringLiteral("body"));
        model.setLeftRole(QStringLiteral("left"));
        model.setRightRole(QStringLiteral("right"));
        model.setCategoryRole(QStringLiteral("category"));
        model.setSourceModel(&source);
        QCOMPARE(source_changed.size(), 5);
        model.setContentRole(model.contentRole()); model.setLeftRole(model.leftRole());
        model.setRightRole(model.rightRole()); model.setCategoryRole(model.categoryRole()); model.setSourceModel(&source);
        QCOMPARE(source_changed.size(), 5);
        const QHash<int, QByteArray> expected_roles{{MonospaceOutputModel::ContentRole, "content"},
            {MonospaceOutputModel::PlainContentRole, "plainContent"}, {MonospaceOutputModel::LeftRole, "leftText"},
            {MonospaceOutputModel::RightRole, "rightText"}, {MonospaceOutputModel::CategoryRole, "category"}};
        QCOMPARE(model.roleNames(), expected_roles);
        QCoreApplication::processEvents();
        QCOMPARE(model.rowCount(), 0);
        ports.env.drain(); QCOMPARE(model.rowCount(), 0);
        ports.env.publishAll(); ports.settle();
        QCOMPARE(model.rowCount(), 2);
        const auto first = model.index(0, 0), second = model.index(1, 0);
        QCOMPARE(model.data(first, MonospaceOutputModel::ContentRole).toString(), QStringLiteral("alpha"));
        QCOMPARE(model.data(second, MonospaceOutputModel::PlainContentRole).toString(), QStringLiteral("beta"));
        QCOMPARE(model.data(first, MonospaceOutputModel::LeftRole).toString(), QString(128, QLatin1Char('L')));
        QCOMPARE(model.data(first, MonospaceOutputModel::RightRole).toString(), QStringLiteral("right"));
        QVERIFY(model.data(second, MonospaceOutputModel::LeftRole).toString().isEmpty());
        QVERIFY(model.data(second, MonospaceOutputModel::RightRole).toString().isEmpty());
        QCOMPARE(model.data(first, MonospaceOutputModel::CategoryRole).toInt(), 7);
        QCOMPARE(model.data(second, MonospaceOutputModel::CategoryRole).toInt(), 7);
        QCOMPARE(model.rowCount(first), 0);
        QVERIFY(!model.data({}, MonospaceOutputModel::ContentRole).isValid());
        QVERIFY(!model.data(model.index(2, 0), MonospaceOutputModel::ContentRole).isValid());
        QVERIFY(!model.data(first, Qt::UserRole + 100).isValid());
    }

    void deactivateAtHeldBoundary_data()
    {
        QTest::addColumn<int>("stage");
        QTest::newRow("effect-queued") << 0;
        QTest::newRow("effect-complete") << 1;
        QTest::newRow("presentation-queued") << 2;
        QTest::newRow("rows-visible") << 3;
        QTest::newRow("search-queued") << 4;
    }

    void deactivateAtHeldBoundary()
    {
        QFETCH(int, stage);
        Ports ports;
        Source source; source.lines = {QStringLiteral("needle")};
        MonospaceOutputModel model;
        model.setServices(ports.services()); model.setSourceModel(&source);
        const auto cleanup = qScopeGuard([&] { model.close(); ports.settle(); ports.services()->retryRetirements(); });
        QCoreApplication::processEvents();
        QVERIFY(model.busy()); QCOMPARE(model.rowCount(), 0);
        std::function<void()> held_publication;
        if (stage >= 1) { QVERIFY(ports.env.step(execution::Lane::Utility)); QCOMPARE(model.rowCount(), 0); }
        if (stage == 1) { held_publication = ports.env.takePublicationBack(); QVERIFY(held_publication); }
        if (stage == 2) QVERIFY(!ports.env.publicationsIdle());
        if (stage >= 3) { ports.env.publishAll(); QCOMPARE(model.rowCount(), 1); }
        if (stage == 4) { model.setSearchText(QStringLiteral("needle")); QVERIFY(model.searchBusy()); }
        model.setActive(false);
        QSignalSpy changed(&model, &MonospaceOutputModel::changed);
        QSignalSpy count_changed(&model, &MonospaceOutputModel::countChanged);
        QSignalSpy active_changed(&model, &MonospaceOutputModel::activeChanged);
        model.setActive(false);
        if (held_publication) held_publication();
        ports.settle();
        QVERIFY(!model.active()); QVERIFY(!model.busy()); QVERIFY(!model.searchBusy());
        QCOMPARE(model.rowCount(), 0); QCOMPARE(model.searchResultCount(), 0); QCOMPARE(model.currentSearchRow(), -1);
        QCOMPARE(changed.size(), 0); QCOMPARE(count_changed.size(), 0); QCOMPARE(active_changed.size(), 0);
        model.setActive(true); QCOMPARE(active_changed.size(), 1); QCOMPARE(model.rowCount(), 0);
        QCoreApplication::processEvents();
        ports.settle();
        QCOMPARE(model.rowCount(), 1); QCOMPARE(model.searchResultCount(), stage == 4 ? 1 : 0);
        QVERIFY(!model.busy()); QVERIFY(!model.searchBusy()); QVERIFY(ports.env.turnsIdle() && ports.env.publicationsIdle());
    }

    void multiTurnCaptureSearchNavigationAndSourceReplacement()
    {
        Ports ports;
        Source source;
        for (int i = 0; i < 130; ++i) source.lines.append(QStringLiteral("needle %1 needle").arg(i));
        MonospaceOutputModel model;
        model.setServices(ports.services()); model.setSourceModel(&source);
        const auto cleanup = qScopeGuard([&] { model.close(); ports.settle(); ports.services()->retryRetirements(); });
        QCoreApplication::processEvents();
        QCOMPARE(source.reads, 32); // Each GUI turn captures at most32 source rows.
        ports.env.drain();
        QCOMPARE(source.reads, 32); // Background projection never reads the QObject source.
        QCOMPARE(model.rowCount(), 0); QVERIFY(model.busy());
        ports.env.publishAll(); // One bounded capture continuation; it cannot finish the130-row projection.
        QCOMPARE(source.reads, 64);
        QCOMPARE(model.rowCount(), 0); QVERIFY(model.busy());
        ports.settle();
        QCOMPARE(source.reads, 130);
        QCOMPARE(model.rowCount(), 130); QVERIFY(!model.truncated());
        QCOMPARE(model.data(model.index(0, 0), MonospaceOutputModel::PlainContentRole).toString(), source.lines.front());
        QCOMPARE(model.data(model.index(129, 0), MonospaceOutputModel::PlainContentRole).toString(), source.lines.back());
        model.nextSearchResult(); model.previousSearchResult(); QCOMPARE(model.currentSearchResultIndex(), -1);
        model.setSearchText(QStringLiteral("needle"));
        ports.env.drain(); ports.env.publishAll();
        QCOMPARE(model.searchResultCount(), 0); QVERIFY(model.searchBusy());
        ports.settle();
        QCOMPARE(model.searchResultCount(), 260); QVERIFY(!model.searchBusy()); QVERIFY(!model.searchLimited());
        QCOMPARE(model.currentSearchRow(), 0); QCOMPARE(model.currentSearchStart(), 0); QCOMPARE(model.currentSearchEnd(), 6);
        model.previousSearchResult();
        QCOMPARE(model.currentSearchResultIndex(), 259); QCOMPARE(model.currentSearchRow(), 129);
        QCOMPARE(model.currentSearchStart(), 11); QCOMPARE(model.currentSearchEnd(), 17);
        model.nextSearchResult(); QCOMPARE(model.currentSearchResultIndex(), 0);
        model.nextSearchResult(); QCOMPARE(model.currentSearchRow(), 0); QCOMPARE(model.currentSearchStart(), 9);
        source.replace({QStringLiteral("replacement needle")});
        QCoreApplication::processEvents();
        QCOMPARE(model.rowCount(), 130); QVERIFY(model.busy());
        ports.env.drain(); QCOMPARE(model.rowCount(), 130);
        ports.settle();
        QCOMPARE(model.rowCount(), 1); QCOMPARE(model.searchResultCount(), 1); QCOMPARE(model.currentSearchResultIndex(), 0);
        QCOMPARE(model.currentSearchStart(), 12); QCOMPARE(model.currentSearchEnd(), 18);
        QVERIFY(ports.env.turnsIdle() && ports.env.publicationsIdle());
    }

    void inactiveViewDiscardsHeldPublication_data()
    {
        QTest::addColumn<int>("kind");
        QTest::newRow("rows") << 0;
        QTest::newRow("search") << 1;
        QTest::newRow("copy") << 2;
    }

    void inactiveViewDiscardsHeldPublication()
    {
        QFETCH(int, kind);
        Ports ports;
        Source source; source.lines = {QStringLiteral("needle")};
        MonospaceOutputModel model;
        model.setServices(ports.services()); model.setSourceModel(&source);
        const auto cleanup = qScopeGuard([&] { model.close(); ports.settle(); ports.services()->retryRetirements(); });
        QCoreApplication::processEvents();
        if (kind != 0) { ports.settle(); QCOMPARE(model.rowCount(), 1); }
        QSignalSpy copied(&model, &MonospaceOutputModel::copyFinished);
        QGuiApplication::clipboard()->setText(QStringLiteral("before inactive result"));
        if (kind == 1) model.setSearchText(QStringLiteral("needle"));
        if (kind == 2) model.copyAll();
        ports.env.drain();
        QVERIFY(!ports.env.publicationsIdle());
        model.setActive(false);
        ports.settle();
        QCOMPARE(model.rowCount(), 0); QCOMPARE(model.searchResultCount(), 0); QCOMPARE(copied.size(), 0);
        QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("before inactive result"));
        QCOMPARE(ports.services()->pendingRetirements(), size_t{0});
        QVERIFY(ports.env.turnsIdle() && ports.env.publicationsIdle());
    }

    void retirementRefusalWaitsForManualRetry()
    {
        Ports ports;
        Source source; source.lines = {QStringLiteral("retained output")};
        MonospaceOutputModel model;
        model.setServices(ports.services()); model.setSourceModel(&source);
        const auto cleanup = qScopeGuard([&] { ports.reject_retirement = false; model.close(); ports.settle(); ports.services()->retryRetirements(); });
        QCoreApplication::processEvents(); ports.settle(); QCOMPARE(model.rowCount(), 1);
        ports.reject_retirement = true;
        model.setActive(false); model.setActive(true);
        QCoreApplication::processEvents();
        QCOMPARE(model.rowCount(), 0); QVERIFY(ports.env.turnsIdle());
        QVERIFY(ports.services()->pendingRetirements() > 0); QCOMPARE(ports.retries.size(), size_t{1});
        auto refused = std::move(ports.retries.front()); ports.retries.pop_front(); refused();
        QCoreApplication::processEvents();
        QCOMPARE(model.rowCount(), 0); QVERIFY(ports.env.turnsIdle()); QCOMPARE(ports.retries.size(), size_t{1});
        ports.reject_retirement = false;
        auto admitted = std::move(ports.retries.front()); ports.retries.pop_front(); admitted();
        QVERIFY(ports.retries.empty()); QVERIFY(!ports.releases.empty());
        QVERIFY(ports.services()->pendingRetirements() > 0); // Enqueuing disposal does not release its values.
        ports.releaseAll(); ports.env.publishAll();
        QCOMPARE(ports.services()->pendingRetirements(), size_t{0});
        QCoreApplication::processEvents(); ports.settle();
        QCOMPARE(model.rowCount(), 1); QVERIFY(!model.busy()); QVERIFY(ports.env.turnsIdle() && ports.env.publicationsIdle());
    }

    void projectionAndSearchWaitForWorkerAndPresentation()
    {
        ManualStartupExecution worker;
        Ports ports;
        ports.utility = std::make_shared<execution::SerialQueue>(worker.executor().makeQueue(execution::Lane::Utility));
        Source source; source.lines = {"first line<br>İstanbul SAVINGS", "last .*+?"};
        MonospaceOutputModel model;
        model.setServices(ports.services()); model.setRichText(true); model.setSourceModel(&source);
        const auto settle = [&] {
            worker.drain();
            ports.env.publishAll();
            ports.releaseAll();
            ports.env.publishAll();
        };
        const auto cleanup = qScopeGuard([&] { model.close(); settle(); ports.services()->retryRetirements(); });
        QCoreApplication::processEvents(); // Bounded GUI role capture only.
        QCOMPARE(model.rowCount(), 0);
        QVERIFY(!worker.quiescent());
        QVERIFY(ports.env.turnsIdle());
        const auto core_submissions = worker.coreSubmissions();
        worker.drain();
        QCOMPARE(model.rowCount(), 0);
        ports.env.publishAll();
        QCOMPARE(model.rowCount(), 3);
        QCOMPARE(model.data(model.index(1, 0), MonospaceOutputModel::PlainContentRole).toString(), QStringLiteral("İstanbul SAVINGS"));
        model.setSearchText("savings");
        QVERIFY(model.searchBusy()); QCOMPARE(model.searchResultCount(), 0);
        worker.drain(); QCOMPARE(model.searchResultCount(), 0);
        ports.env.publishAll();
        QCOMPARE(model.searchResultCount(), 1); QCOMPARE(model.currentSearchRow(), 1);
        QCOMPARE(model.currentSearchStart(), 9); QCOMPARE(model.currentSearchEnd(), 16);
        model.setSearchText(".*+?"); settle();
        QCOMPARE(model.searchResultCount(), 1); QCOMPARE(model.currentSearchRow(), 2);
        QCOMPARE(model.currentSearchStart(), 5); QCOMPARE(model.currentSearchEnd(), 9);
        QSignalSpy copied(&model, &MonospaceOutputModel::copyFinished);
        QGuiApplication::clipboard()->setText(QStringLiteral("before"));
        model.copyAll(); worker.drain();
        QCOMPARE(copied.size(), 0);
        QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("before"));
        ports.env.publishAll();
        QCOMPARE(copied.size(), 1); QVERIFY(copied.front().front().toBool());
        QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("first line\nİstanbul SAVINGS\nlast .*+?"));
        QCOMPARE(worker.coreSubmissions(), core_submissions);
        QVERIFY(ports.env.turnsIdle());
    }

    void retirementWaitsForAcceptedUtilityWorkAfterViewClose()
    {
        ManualStartupExecution worker;
        Ports ports;
        ports.utility = std::make_shared<execution::SerialQueue>(worker.executor().makeQueue(execution::Lane::Utility));
        Source source;
        source.lines = {QStringLiteral("original output")};
        MonospaceOutputModel model;
        model.setServices(ports.services());
        model.setSourceModel(&source);
        QCoreApplication::processEvents();
        worker.drain();
        ports.env.publishAll();
        QCOMPARE(model.rowCount(), 1);

        QSignalSpy copied{&model, &MonospaceOutputModel::copyFinished};
        QGuiApplication::clipboard()->setText(QStringLiteral("held clipboard"));
        model.copyAll();
        QVERIFY(!worker.quiescent());
        ports.services()->close();
        QVERIFY(!ports.utility->post([] {}));
        ports.releaseAll();
        ports.env.publishAll();
        QCOMPARE(ports.services()->pendingRetirements(), size_t{0});

        bool retired{false};
        QObject observer;
        ports.services()->whenRetired(&observer, [&] { retired = true; });
        QCoreApplication::processEvents();
        QVERIFY(!retired);
        QVERIFY(!ports.retries.empty());
        worker.drain();
        auto retry = std::move(ports.retries.front());
        ports.retries.pop_front();
        retry();
        ports.env.publishAll();
        QVERIFY(retired);
        QCOMPARE(copied.size(), 0);
        QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("held clipboard"));
    }

    void retirementGateRetainsRejectedHandoffAndWaitsForActualRelease()
    {
        Ports ports;
        Source source; source.lines = {QString(5000, QLatin1Char('x'))};
        auto model = std::make_unique<MonospaceOutputModel>();
        model->setServices(ports.services());
        model->setSourceModel(&source);
        QCoreApplication::processEvents(); ports.settle();
        QCOMPARE(model->rowCount(), 5);
        ports.reject_retirement = true;
        ports.services()->close(); // Closes the model through closed().
        QVERIFY(!ports.services()->retryRetirements());
        QVERIFY(ports.services()->pendingRetirements() > 0);
        bool done{false};
        ports.services()->whenRetired(this, [&] { done = true; });
        QCoreApplication::processEvents();
        QVERIFY(!done); QVERIFY(!ports.retries.empty());
        model.reset(); // The services keep the mailbox with rejected large references.
        ports.reject_retirement = false;
        auto retry = std::move(ports.retries.front()); ports.retries.pop_front(); retry();
        QVERIFY(!done); // Admission alone is not resource retirement.
        QVERIFY(!ports.releases.empty());
        ports.releaseAll();
        ports.env.drain(); // Deliver the closed Utility queue's drain notification.
        QVERIFY(!ports.retries.empty());
        auto next = std::move(ports.retries.front());
        ports.retries.pop_front();
        next();
        ports.env.publishAll();
        QVERIFY(done); QCOMPARE(ports.services()->pendingRetirements(), size_t{0});
    }
};

#ifdef BITCOINQML_NO_TEST_MAIN
#include <test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(MonospaceOutputModelTests)
#else
QTEST_MAIN(MonospaceOutputModelTests)
#endif
#include <test_monospaceoutputmodel.moc>
