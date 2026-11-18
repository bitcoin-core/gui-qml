// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/models/onboardingoptionsmodel.h>
#include <chainparams.h>
#include <test/mocks/manualexecutionenvironment.h>
#include <test/mocks/drainexecutors.h>
#include <QDir>
#include <QScopeGuard>
#include <QSemaphore>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QtTest/QtTest>

#include <atomic>
#include <deque>
#include <future>
#include <optional>
#include <utility>

namespace {
std::vector<std::string> EditableArguments() { return {"bitcoin-core-app", "-regtest", "-resetguisettings"}; }
profileinspection::Result Found(bool found)
{
    profileinspection::Result result;
    result.outcome = profileinspection::Outcome::Succeeded;
    result.has_wallet_data = found;
    return result;
}
struct ManualOwner {
    QTemporaryDir first, second;
    ManualRoutedExecution execution;
    std::deque<std::function<void()>> gui;
    std::vector<profileinspection::Request> requests;
    std::vector<profileinspection::Cancellation> cancellations;
    QString failure;
    std::unique_ptr<OnboardingOptionsModel> model;
    bool initial_selected{false};
    explicit ManualOwner(bool explicit_directory = false, bool initial_wallet_data = true)
    {
        QDir(first.path()).mkpath(QStringLiteral("regtest"));
        QDir(second.path()).mkpath(QStringLiteral("regtest"));
        auto argv=EditableArguments();
        if(explicit_directory)argv.push_back("-datadir="+first.path().toStdString());
        model = std::make_unique<OnboardingOptionsModel>(std::move(argv), false, nullptr,
            execution::SnapshotServices{execution.scheduler, [this](auto turn) { gui.emplace_back(std::move(turn)); }},
            [this,initial_wallet_data](const auto& request, const auto& cancelled) {
                requests.push_back(request); cancellations.push_back(cancelled);
                if (!failure.isEmpty()) {
                    profileinspection::Result result; result.error = failure; return result;
                }
                return Found(initial_wallet_data && requests.size() == 1);
            });
        // Editable chooser tests must not carry an immutable CLI -datadir.
        // Admit the original selection before any GUI startup publication.
        initial_selected=model->selectCustomDataDir(first.path());
    }
    bool publish()
    {
        if (gui.empty()) return false;
        auto turn = std::move(gui.front()); gui.pop_front(); turn(); return true;
    }
    bool firstCapture()
    {
        for (int step = 0; step < 1000 && requests.empty(); ++step) {
            if (execution.stepNext()) continue;
            if (!publish()) return false;
        }
        return initial_selected && requests.size() == 1;
    }
    bool settle()
    {
        for (int step = 0; step < 1000; ++step) {
            if (execution.stepNext()) continue;
            if (publish()) continue;
            return true;
        }
        return false;
    }
    ~ManualOwner() { model.reset(); execution.drain(); while (publish()) execution.drain(); }
};
}
class ProfileInspectionOwnerTests : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void proposedNetworkOverrideAndExplicitWalletDirectoryAreCaptured()
    {
        SelectParams(ChainType::MAIN);
        const auto* parent_network = &Params();
        QTemporaryDir profile, wallets;
        QVERIFY(profile.isValid() && wallets.isValid());
        const std::vector<std::string> argv{"bitcoin-core-app", "-chain=signet", "-signetchallenge=51",
            "-datadir=" + profile.path().toStdString(), "-walletdir=" + wallets.path().toStdString()};
        std::optional<profileinspection::Request> captured;
        std::atomic_bool native{false};
        auto read = std::async(std::launch::async, [&] {
            return QmlOnboardingSettings::Preview(argv, false, profile.path(), [&](const auto& request, const auto&) {
                native = QThread::currentThread() != QCoreApplication::instance()->thread();
                captured = request;
                return Found(false);
            });
        });
        QVERIFY(QTest::qWaitFor([&] { return read.wait_for(std::chrono::milliseconds{0}) == std::future_status::ready; }));
        const auto result = read.get();
        QVERIFY2(result.ok, qPrintable(result.error));
        QVERIFY(native.load());
        QVERIFY(captured);
        QCOMPARE(captured->network, ChainType::SIGNET);
        QCOMPARE(captured->signet_challenge, std::optional<QString>{QStringLiteral("51")});
        QCOMPARE(QDir(captured->directory).canonicalPath(), QDir(wallets.path()).canonicalPath());
        QCOMPARE(&Params(), parent_network);
        QCOMPARE(Params().GetChainType(), ChainType::MAIN);
    }
    void failedInspectionPreservesThePreviousProfileAndRequiresFreshRetry()
    {
        ManualOwner fixture;
        QVERIFY(fixture.settle());
        QVERIFY(fixture.model->existingProfile());
        QVERIFY(fixture.model->previewError().isEmpty());
        fixture.failure = QStringLiteral("Original helper could not classify this directory");
        QVERIFY(fixture.model->selectCustomDataDir(fixture.first.path()));
        QVERIFY(fixture.settle());
        QCOMPARE(fixture.model->previewError(), fixture.failure);
        QVERIFY(!fixture.model->canFinish());
        QVERIFY(!fixture.model->validationPending());
        QVERIFY(fixture.model->existingProfile()); // Failed is not empty.
        fixture.failure.clear();
        QVERIFY(fixture.model->selectCustomDataDir(fixture.second.path()));
        QVERIFY(fixture.settle());
        QVERIFY(fixture.model->previewError().isEmpty());
        QVERIFY(!fixture.model->existingProfile());
        QCOMPARE(fixture.model->dataDir(), fixture.second.path());
        fixture.model->beginShutdown();
        QVERIFY(fixture.settle());
        QVERIFY(fixture.execution.quiescent());
    }
    void successorCancelsOriginalInspectionAndRejectsItsHeldPublication()
    {
        ManualOwner fixture;
        QVERIFY(fixture.first.isValid() && fixture.second.isValid());
        QVERIFY(fixture.firstCapture());
        const auto original = fixture.requests.front();
        const auto cancellation = fixture.cancellations.front();
        QVERIFY(cancellation && !cancellation->load());
        QCOMPARE(original.network, ChainType::REGTEST);
        QVERIFY(fixture.model->selectCustomDataDir(fixture.second.path()));
        QVERIFY(cancellation->load());
        fixture.execution.drain();
        QVERIFY(!fixture.model->existingProfile());
        QVERIFY(!fixture.model->canFinish());
        QVERIFY(fixture.settle());
        QCOMPARE(fixture.model->dataDir(), fixture.second.path());
        QVERIFY(!fixture.model->validationPending());
        QVERIFY2(fixture.model->previewError().isEmpty(), qPrintable(fixture.model->previewError()));
        QVERIFY(!fixture.model->existingProfile());
        QVERIFY(fixture.requests.size() >= 2);
        QVERIFY(fixture.requests.front() == original);
        QCOMPARE(QDir(original.directory).canonicalPath(), QDir(fixture.first.filePath(QStringLiteral("regtest"))).canonicalPath());
        for (size_t i = 1; i < fixture.requests.size(); ++i)
            QCOMPARE(QDir(fixture.requests[i].directory).canonicalPath(), QDir(fixture.second.filePath(QStringLiteral("regtest"))).canonicalPath());
        fixture.model->beginShutdown();
        QVERIFY(fixture.settle());
        QVERIFY(fixture.execution.quiescent());
    }
    void directoryIngressFencesEligibilityAndKeepsTheReentrantSuccessor_data()
    {
        QTest::addColumn<bool>("outer_default");
        QTest::newRow("chooser")<<false;
        QTest::newRow("default")<<true;
    }
    void directoryIngressFencesEligibilityAndKeepsTheReentrantSuccessor()
    {
        QFETCH(bool,outer_default);
        ManualOwner fixture{false,false};
        QVERIFY(fixture.settle());
        QVERIFY2(fixture.model->canFinish(),qPrintable(fixture.model->previewError()));
        auto* settings=qobject_cast<CoreSettingsModel*>(fixture.model->coreSettings());
        QVERIFY(settings);
        bool observed{false},eligible_at_notice{true},pending_at_notice{false},successor_admitted{false};
        const auto connection=connect(settings,&CoreSettingsModel::statusesChanged,this,[&]{
            if(observed)return;
            observed=true;
            eligible_at_notice=fixture.model->canFinish();
            pending_at_notice=fixture.model->validationPending();
            successor_admitted=fixture.model->selectCustomDataDir(fixture.first.path());
        });
        const auto disconnect=qScopeGuard([&]{QObject::disconnect(connection);});
        const auto reads=fixture.requests.size();
        // The outer directory request loses authority to a newer public
        // request during the first bounded settings presentation notification.
        if(outer_default)fixture.model->useDefaultDataDir();
        else QVERIFY(!fixture.model->selectCustomDataDir(fixture.second.path()));
        QVERIFY(observed);
        QVERIFY(!eligible_at_notice);
        QVERIFY(pending_at_notice);
        QVERIFY(successor_admitted);
        QVERIFY(fixture.settle());
        QCOMPARE(fixture.model->dataDir(),fixture.first.path());
        QVERIFY(fixture.model->canFinish());
        QVERIFY(fixture.requests.size()>reads);
        for(size_t i=reads;i<fixture.requests.size();++i)
            QCOMPARE(QDir(fixture.requests[i].directory).canonicalPath(),QDir(fixture.first.filePath(QStringLiteral("regtest"))).canonicalPath());
        fixture.model->beginShutdown();
        QVERIFY(fixture.settle());
        QVERIFY(fixture.execution.quiescent());
    }
    void explicitCommandLineDirectoryRemainsAuthoritativeWhenChooserProjectionChanges()
    {
        ManualOwner fixture{true};
        QVERIFY(fixture.initial_selected);
        QVERIFY(fixture.settle());
        const auto reads=fixture.requests.size();
        QVERIFY(reads>0);
        QVERIFY(fixture.model->selectCustomDataDir(fixture.second.path()));
        QVERIFY(fixture.settle());
        QCOMPARE(fixture.model->dataDir(),fixture.second.path());
        QVERIFY(fixture.requests.size()>reads);
        for(size_t i=reads;i<fixture.requests.size();++i)
            QCOMPARE(QDir(fixture.requests[i].directory).canonicalPath(),QDir(fixture.first.filePath(QStringLiteral("regtest"))).canonicalPath());
        fixture.model->beginShutdown();
        QVERIFY(fixture.settle());
        QVERIFY(fixture.execution.quiescent());
    }
    void retirementReleasesTheOriginalNativeReaderWithGuiDeliveryHeld()
    {
        ManualOwner fixture;
        QVERIFY(fixture.firstCapture());
        const auto cancellation = fixture.cancellations.front();
        QSignalSpy shutdown{fixture.model.get(), &OnboardingOptionsModel::shutdownFinished};
        fixture.model->beginShutdown();
        QVERIFY(cancellation->load());
        fixture.execution.drain();
        QCOMPARE(fixture.requests.size(), size_t{1});
        QVERIFY(fixture.execution.quiescent());
        QCOMPARE(shutdown.size(), 0);
        QVERIFY(!fixture.gui.empty());
        QVERIFY(fixture.settle());
        QCOMPARE(shutdown.size(), 1);
        fixture.model->beginShutdown();
        QVERIFY(fixture.settle());
        QCOMPARE(shutdown.size(), 1);
    }
    void runningOriginalCancellationPreservesPhysicalCompletion_data()
    {
        QTest::addColumn<bool>("shutdown");
        QTest::newRow("successor-directory") << false;
        QTest::newRow("shutdown") << true;
    }
    void runningOriginalCancellationPreservesPhysicalCompletion()
    {
        QFETCH(bool, shutdown);
        QTemporaryDir first, second;
        QVERIFY(first.isValid() && second.isValid());
        QVERIFY(QDir(first.path()).mkpath(QStringLiteral("regtest")));
        QVERIFY(QDir(second.path()).mkpath(QStringLiteral("regtest")));
        QSemaphore entered, release;
        profileinspection::Cancellation original;
        std::atomic_int calls{0};
        std::atomic_bool native_only{true}, successor_target_exact{true};
        const auto successor_directory=QDir(second.filePath(QStringLiteral("regtest"))).canonicalPath();
        bool heartbeat{false};
        const auto drain = DrainExecutorsBeforeNativeOwnerDestruction();
        OnboardingOptionsModel model{EditableArguments(), false, nullptr, {}, [&](const auto& request, const auto& cancelled) {
            if (QThread::currentThread() == QCoreApplication::instance()->thread()) native_only = false;
            const int count = ++calls;
            if (count == 1) { original = cancelled; entered.release(); release.acquire(); }
            else if(QDir(request.directory).canonicalPath()!=successor_directory)successor_target_exact=false;
            if (cancelled && cancelled->load()) {
                profileinspection::Result result; result.outcome = profileinspection::Outcome::Cancelled;
                result.error = QStringLiteral("Original read cancelled after physical completion"); return result;
            }
            Q_UNUSED(request);
            return Found(false);
        }};
        const auto unblock = qScopeGuard([&] { release.release(); });
        QVERIFY(model.selectCustomDataDir(first.path()));
        QSignalSpy stopped{&model, &OnboardingOptionsModel::shutdownFinished};
        QTRY_VERIFY(entered.available() > 0);
        entered.acquire();
        QVERIFY(original && !original->load());
        QTimer::singleShot(0, &model, [&] { heartbeat = true; });
        if (shutdown) model.beginShutdown();
        else QVERIFY(model.selectCustomDataDir(second.path()));
        QVERIFY(original->load());
        QTRY_VERIFY(heartbeat);
        QCOMPARE(stopped.size(), 0);
        QVERIFY(!model.canFinish());
        release.release();
        if (!shutdown) {
            QTRY_VERIFY(!model.validationPending());
            QCOMPARE(model.dataDir(), second.path());
            QVERIFY2(model.previewError().isEmpty(), qPrintable(model.previewError()));
            QVERIFY(!model.existingProfile());
            model.beginShutdown();
        }
        QTRY_COMPARE(stopped.size(), 1);
        QVERIFY(native_only.load());
        QVERIFY(successor_target_exact.load());
        if (shutdown) QCOMPARE(calls.load(), 1);
    }
};
#ifdef BITCOINQML_NO_TEST_MAIN
#include <test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(ProfileInspectionOwnerTests)
#else
QTEST_MAIN(ProfileInspectionOwnerTests)
#endif
#include <test_profileinspectionowner.moc>
