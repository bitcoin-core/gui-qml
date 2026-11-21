// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/backendexecutor.h>
#include <qml/guipresentation.h>
#include <test/mocks/receiveeditortest.h>
#include <test/qt_test_registry.h>
#include <QtTest/QtTest>
#include <QSemaphore>
#include <QThread>
#include <QQmlEngine>
#include <QQmlContext>

namespace {
using execution::Lane;
struct Fixture {
    ReceiveEditorPorts ports;
    std::unique_ptr<RequestSharingModel> context;
    Fixture() : context(std::make_unique<RequestSharingModel>(17, ports.sharing())) { context->setActive(true); ports.drain(); }
    ~Fixture() { context.reset(); ports.drain(); QVERIFY(ports.execution.quiescent()); }
    std::unique_ptr<RequestImageExportOperation> exportImage()
    {
        auto operation = std::unique_ptr<RequestImageExportOperation>{context->beginQrExport()}; ports.drain(); return operation;
    }
    void toHeldWrite(RequestImageExportOperation& operation)
    {
        QVERIFY(operation.choose(QUrl::fromLocalFile("/controlled/request.png")));
        while (ports.execution.step(Lane::Domain)) {}
        QVERIFY(ports.execution.step(Lane::Core));
        while (ports.execution.step(Lane::Domain)) {}
        QCOMPARE(operation.phase(), RequestImageExportOperation::Writing); QCOMPARE(ports.file_writes, 0);
    }
};
}
class RequestSharingModelTests : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void externallyRetainedOperationOutlivesItsSharingProjection_data()
    {
        QTest::addColumn<bool>("delivered");
        QTest::newRow("closed-before-gui-delivery") << false;
        QTest::newRow("closed-after-confirmed-delivery") << true;
    }
    void externallyRetainedOperationOutlivesItsSharingProjection()
    {
        QFETCH(bool, delivered); ReceiveEditorPorts ports; QQmlEngine engine;
        auto context = std::make_unique<RequestSharingModel>(17, ports.sharing()); context->setActive(true); ports.drain();
        QQmlEngine::setObjectOwnership(context.get(), QQmlEngine::CppOwnership);
        QQmlEngine::setContextForObject(context.get(), engine.rootContext());
        engine.globalObject().setProperty(QStringLiteral("sharing"), engine.newQObject(context.get()));
        QVERIFY(!engine.evaluate(QStringLiteral("var original = sharing.copyUri();")).isError());
        ports.execution.drain(); if (delivered) ports.publish();
        const auto identity = engine.evaluate(QStringLiteral("original.operationId")).toString(); QVERIFY(!identity.isEmpty());
        context.reset(); engine.collectGarbage(); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); ports.publish();
        QCOMPARE(engine.evaluate(QStringLiteral("original.operationId")).toString(), identity);
        QVERIFY(engine.evaluate(QStringLiteral("original.result.present && original.result.outcomeKnown")).toBool());
        QCOMPARE(engine.evaluate(QStringLiteral("original.result.performed")).toBool(), delivered);
        QCOMPARE(ports.clipboard, delivered ? 1 : 0); QVERIFY(ports.execution.quiescent());
        engine.evaluate(QStringLiteral("original = null; sharing = null")); engine.collectGarbage();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); ports.drain();
    }
    void currentCopyOutcomeSurvivesAnIgnoredQmlReturnAndGarbageCollection()
    {
        ReceiveEditorPorts ports; RequestSharingModel context{17, ports.sharing()}; context.setActive(true); ports.drain();
        QQmlEngine engine; QQmlEngine::setObjectOwnership(&context, QQmlEngine::CppOwnership);
        QQmlEngine::setContextForObject(&context, engine.rootContext());
        engine.globalObject().setProperty(QStringLiteral("sharing"), engine.newQObject(&context));
        QVERIFY(!engine.evaluate(QStringLiteral("sharing.copyUri(); undefined")).isError());
        engine.collectGarbage(); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        ports.drain(); QCOMPARE(ports.clipboard, 1); QVERIFY(context.lastOperation());
        QVERIFY(context.lastOperation()->result().performed);
        context.setActive(false); engine.collectGarbage(); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(context.lastOperation()); QVERIFY(context.lastOperation()->result().performed); context.retire(); ports.drain();
    }
    void qrCopyRendersAndDeliversTheExactOriginalOwnedPayload()
    {
        ReceiveEditorPorts ports; auto services = ports.sharing(); QStringList rendered;
        services.render = [&rendered](const QString& payload) { rendered.push_back(payload); return requestshare::RenderQr(payload); };
        RequestSharingModel context{17, services}; context.setActive(true); ports.drain(); QVERIFY(context.canCopyQr());
        auto operation = std::unique_ptr<ShareOperationModel>{context.copyQr()}; ports.drain();
        const QString expected{QStringLiteral("bitcoin:bcrt1q-original?amount=0.000001&message=original%20message")};
        QCOMPARE(rendered, QStringList({expected, expected})); QCOMPARE(ports.clipboard, 1);
        QVERIFY(operation->result().performed); QCOMPARE(ports.clipboard_image, context.image());
        QVERIFY(ports.clipboard_image.width() > 1); context.retire(); ports.drain();
    }
    void freshSavedPayloadCopiesOnlyAfterTheNativeGuiHandoff()
    {
        Fixture f; QVERIFY(f.context->canCopyUri());
        auto operation = std::unique_ptr<ShareOperationModel>{f.context->copyUri()}; f.ports.execution.drain();
        QCOMPARE(f.ports.clipboard, 0); QVERIFY(f.ports.execution.quiescent());
        QCOMPARE(operation->phase(), ShareOperationModel::Authorizing); QVERIFY(!operation->result().admitted);
        f.ports.publish(); QCOMPARE(f.ports.clipboard, 1); QCOMPARE(operation->phase(), ShareOperationModel::Completed);
        QVERIFY(operation->result().admitted); QVERIFY(operation->result().performed);
        QCOMPARE(f.ports.clipboard_text, QStringLiteral("bitcoin:bcrt1q-original?amount=0.000001&message=original%20message"));
    }
    void hiddenViewCannotDeliverAnAlreadyQueuedClipboardJob()
    {
        Fixture f; auto operation = std::unique_ptr<ShareOperationModel>{f.context->copyUri()}; f.ports.execution.drain();
        f.context->setActive(false); f.ports.publish();
        QCOMPARE(f.ports.clipboard, 0); QVERIFY(operation->result().present); QVERIFY(!operation->result().admitted); QVERIFY(!operation->result().performed);
    }
    void reactivatedViewDoesNotRevalidateItsOldGrant()
    {
        Fixture f; auto operation = std::unique_ptr<ShareOperationModel>{f.context->copyQr()}; f.ports.execution.drain();
        f.context->setActive(false); f.context->setActive(true); f.ports.drain();
        QCOMPARE(f.ports.clipboard, 0); QVERIFY(!operation->result().performed); QVERIFY(f.context->canCopyQr());
    }
    void twoViewsHaveIndependentPresentationInterest()
    {
        Fixture f; RequestSharingModel second{17, f.ports.sharing()}; second.setActive(true); f.ports.drain();
        auto first = std::unique_ptr<ShareOperationModel>{f.context->copyUri()}; auto other = std::unique_ptr<ShareOperationModel>{second.copyUri()};
        f.ports.execution.drain(); f.context->setActive(false); f.ports.publish();
        QCOMPARE(f.ports.clipboard, 1); QVERIFY(!first->result().performed); QVERIFY(other->result().performed);
    }
    void nativeUseBeforeGuiPublicationFencesTheOriginalPayload()
    {
        Fixture f; auto operation = std::unique_ptr<ShareOperationModel>{f.context->copyUri()}; f.ports.execution.drain();
        f.ports.useSaved(17); f.ports.publish();
        QCOMPARE(f.ports.clipboard, 0); QVERIFY(!operation->result().performed); QVERIFY(!f.context->canCopyUri());
        f.context->refresh(); f.ports.drain(); QCOMPARE(f.context->error(), requestshare::Error::Used);
    }
    void changedRecordCannotBeSilentlyReboundAtPreparation()
    {
        Fixture f; auto operation = std::unique_ptr<ShareOperationModel>{f.context->copyUri()};
        ++f.ports.records.at(17).revision; f.ports.epoch->advance(); f.ports.drain();
        QCOMPARE(f.ports.clipboard, 0); QCOMPARE(operation->error(), requestshare::Error::Changed);
    }
    void firstInputRevocationPreventsCopyBeforeTheEditedProjectionArrives()
    {
        ReceiveEditorPorts ports; PaymentRequestEditorModel editor{17, 2, false, ports.editors()}; ports.drain();
        RequestSharingModel context{17, ports.sharing(), editor.sharingInputGuard()}; context.setActive(true); ports.drain();
        QVERIFY(context.canCopyUri()); editor.message()->edit("unpublished input"); QVERIFY(!context.canCopyUri());
        auto operation = std::unique_ptr<ShareOperationModel>{context.copyUri()}; ports.drain();
        QCOMPARE(ports.clipboard, 0); QVERIFY(!operation->result().performed); editor.retire(); context.retire(); ports.drain();
    }
    void discardedInputCannotRearmAnOlderQueuedClipboardGrant()
    {
        ReceiveEditorPorts ports; PaymentRequestEditorModel editor{17, 2, false, ports.editors()}; ports.drain();
        RequestSharingModel context{17, ports.sharing(), editor.sharingInputGuard(), nullptr, editor.sharingInputEpoch()};
        context.setActive(true); ports.drain();
        auto original = std::unique_ptr<ShareOperationModel>{context.copyUri()}; ports.execution.drain();
        QCOMPARE(original->phase(), ShareOperationModel::Authorizing); QVERIFY(!ports.gui.empty());
        // Hold only the final native GUI handoff; let independent presentation
        // notifications and the explicit edit/discard complete normally.
        auto handoff = std::move(ports.gui.back()); ports.gui.pop_back();
        editor.message()->edit("temporary input"); ports.drain(); editor.discard(); ports.drain();
        QVERIFY(!editor.dirty()); QVERIFY(context.canCopyUri()); handoff(); ports.publish();
        QCOMPARE(ports.clipboard, 0); QVERIFY(!original->result().performed);
        auto successor = std::unique_ptr<ShareOperationModel>{context.copyUri()}; ports.drain();
        QCOMPARE(ports.clipboard, 1); QVERIFY(successor->result().performed);
        editor.retire(); context.retire(); ports.drain();
    }
    void disappearingProjectionDoesNotKeepAnActionLeaseUntilGuiAcknowledgement()
    {
        Fixture f; auto operation = std::unique_ptr<ShareOperationModel>{f.context->copyUri()}; f.ports.execution.drain();
        QVERIFY(f.ports.execution.quiescent()); operation.reset(); f.ports.publish();
        QCOMPARE(f.ports.clipboard, 0); QVERIFY(f.ports.execution.quiescent());
    }
    void cancellationBeforePreparationHasAnExplicitNoEffectReceipt()
    {
        Fixture f; auto operation = std::unique_ptr<ShareOperationModel>{f.context->copyUri()}; operation->cancel(); f.ports.drain();
        QCOMPARE(operation->phase(), ShareOperationModel::Cancelled); QVERIFY(operation->result().outcomeKnown); QVERIFY(!operation->result().performed);
        QCOMPARE(f.ports.clipboard, 0);
    }
    void clipboardRefusalAndUnknownRemainDifferentOutcomes_data()
    {
        QTest::addColumn<int>("delivery"); QTest::newRow("known-refusal") << int(requestshare::Delivery::Failed);
        QTest::newRow("unconfirmed-effect") << int(requestshare::Delivery::Unknown);
    }
    void clipboardRefusalAndUnknownRemainDifferentOutcomes()
    {
        QFETCH(int, delivery); Fixture f; f.ports.delivery = static_cast<requestshare::Delivery>(delivery);
        auto operation = std::unique_ptr<ShareOperationModel>{f.context->copyUri()}; f.ports.drain();
        QVERIFY(operation->result().admitted); QVERIFY(!operation->result().performed);
        QCOMPARE(operation->result().outcomeKnown, delivery != int(requestshare::Delivery::Unknown));
        f.context->refresh(); f.ports.drain(); QCOMPARE(f.ports.clipboard, 1); // Observation never replays the effect.
    }
    void unencodableQrDoesNotDisableUriCopyOrProduceABlankImage()
    {
        ReceiveEditorPorts ports; auto services = ports.sharing(); services.render = [](const QString&) { return QImage{1, 1, QImage::Format_ARGB32}; };
        RequestSharingModel context{17, services}; context.setActive(true); ports.drain();
        QVERIFY(context.canCopyUri()); QVERIFY(!context.canCopyQr()); QVERIFY(context.image().isNull());
        auto qr = std::unique_ptr<ShareOperationModel>{context.copyQr()}; ports.drain();
        QCOMPARE(qr->error(), requestshare::Error::QrEncoding); QCOMPARE(ports.clipboard, 0);
        auto uri = std::unique_ptr<ShareOperationModel>{context.copyUri()}; ports.drain(); QVERIFY(uri->result().performed); QCOMPARE(ports.clipboard, 1);
    }
    void qrRendererRejectsTheOriginal4250CharacterPayloadAndRecovers()
    {
        const QString long_payload = QStringLiteral("bitcoin:bcrt1q-original?message=") + QString(4250, QLatin1Char('x'));
        QVERIFY(requestshare::RenderQr(long_payload).isNull());
        const auto short_image = requestshare::RenderQr(QStringLiteral("bitcoin:bcrt1q-original?amount=0.000001"));
        QVERIFY(!short_image.isNull()); QVERIFY(short_image.width() > 1); QCOMPARE(short_image.width(), short_image.height());
        QCOMPARE(short_image.pixelColor(0, 0), QColor{Qt::white});
    }
    void viewAndSourceEpochExhaustionFailsClosed()
    {
        auto view = std::make_shared<requestshare::View>(); view->token.store(UINT64_MAX);
        QVERIFY(!view->setActive(true)); QCOMPARE(view->interest.load(), requestshare::View::Interest::Closed);
        auto epoch = std::make_shared<SourceEpoch>(); epoch->value.store(UINT64_MAX); epoch->advance(); QVERIFY(epoch->closed.load());
    }
    void choosingAndOverwriteConsentHoldNoBackgroundLease()
    {
        Fixture f; f.ports.target_exists = true; auto operation = f.exportImage();
        QCOMPARE(operation->phase(), RequestImageExportOperation::Choosing); QVERIFY(f.ports.execution.quiescent());
        QVERIFY(operation->choose(QUrl::fromLocalFile("/controlled/original.png"))); f.ports.drain();
        QVERIFY(operation->confirmationRequired()); QCOMPARE(f.ports.inspections, 1); QCOMPARE(f.ports.file_writes, 0);
        QVERIFY(f.ports.execution.quiescent()); QVERIFY(operation->approve()); f.ports.drain();
        QVERIFY(operation->result().written); QCOMPARE(f.ports.file_writes, 1); QCOMPARE(f.ports.written_path, QStringLiteral("/controlled/original.png"));
    }
    void newFileSelectionAfterRootClosureDoesNotReusePreparationRights()
    {
        Fixture f; auto operation = f.exportImage(); f.ports.execution.scheduler->closeOrdinaryAdmission();
        QVERIFY(!operation->choose(QUrl::fromLocalFile("/controlled/rejected.png"))); f.ports.drain();
        QCOMPARE(operation->error(), RequestImageExportError::Rejected); QCOMPARE(f.ports.inspections, 0); QCOMPARE(f.ports.file_writes, 0);
    }
    void cancellationBeforeNativeInstallRetainsAnExplicitCancelledResult()
    {
        Fixture f; auto operation = f.exportImage(); f.toHeldWrite(*operation); operation->cancel(); f.ports.drain();
        QCOMPARE(operation->phase(), RequestImageExportOperation::Cancelled); QVERIFY(operation->result().present); QVERIFY(!operation->result().written);
        QCOMPARE(f.ports.file_writes, 0);
    }
    void paidSourceBeforeNativeInstallNeverWritesTheTarget()
    {
        Fixture f; auto operation = f.exportImage(); f.toHeldWrite(*operation); f.ports.useSaved(17); f.ports.drain();
        QCOMPARE(operation->error(), RequestImageExportError::Source); QVERIFY(!operation->result().admitted); QCOMPARE(f.ports.file_writes, 0);
    }
    void changedDestinationIsNotMisreportedAsSourceRevocation()
    {
        Fixture f; auto operation = f.exportImage(); f.toHeldWrite(*operation); f.ports.target_changed = true; f.ports.drain();
        QCOMPARE(operation->error(), RequestImageExportError::Changed); QVERIFY(!operation->result().written); QCOMPARE(f.ports.file_writes, 0);
    }
    void chooserFailureIsKnownAndDoesNotBecomeSuccessfulExport()
    {
        Fixture f; auto operation = f.exportImage(); operation->chooserFailed();
        QCOMPARE(operation->phase(), RequestImageExportOperation::Failed); QCOMPARE(operation->error(), RequestImageExportError::Chooser);
        QVERIFY(operation->result().outcomeKnown); QVERIFY(!operation->result().written); QCOMPARE(f.ports.file_writes, 0);
    }
    void writerWithoutAdmissionProofCannotClaimSuccessfulExport()
    {
        ReceiveEditorPorts ports; auto services = ports.sharing(); auto files = std::make_shared<requestimageexport::Services>(*services.files);
        files->write = [](const auto&, bool, const auto&, const auto&) { return backup::WriteResult{}; }; services.files = files;
        RequestSharingModel context{17, services}; context.setActive(true); ports.drain();
        auto operation = std::unique_ptr<RequestImageExportOperation>{context.beginQrExport()}; ports.drain(); QVERIFY(operation->choosing());
        QVERIFY(operation->choose(QUrl::fromLocalFile("/controlled/unproven.png"))); ports.drain();
        QCOMPARE(operation->phase(), RequestImageExportOperation::OutcomeUnknown); QVERIFY(!operation->result().written); QVERIFY(!operation->result().outcomeKnown);
        QCOMPARE(ports.file_writes, 0); QVERIFY(!operation->approve());
    }
    void writerThrowAfterAdmissionRetainsUnknownWithoutReplay()
    {
        ReceiveEditorPorts ports; auto services = ports.sharing(); auto files = std::make_shared<requestimageexport::Services>(*services.files);
        files->write = [&ports](const auto&, bool, const auto&, const std::shared_ptr<requestshare::Grant>& grant) -> backup::WriteResult {
            if (!grant->admit()) return {backup::Error::Changed};
            ++ports.file_writes; throw std::runtime_error{"installation receipt lost"};
        }; services.files = files;
        RequestSharingModel context{17, services}; context.setActive(true); ports.drain();
        auto operation = std::unique_ptr<RequestImageExportOperation>{context.beginQrExport()}; ports.drain(); QVERIFY(operation->choosing());
        QVERIFY(operation->choose(QUrl::fromLocalFile("/controlled/unknown.png"))); ports.drain();
        QCOMPARE(operation->phase(), RequestImageExportOperation::OutcomeUnknown); QVERIFY(operation->result().admitted); QVERIFY(!operation->result().outcomeKnown);
        QVERIFY(!operation->approve()); context.refresh(); ports.drain(); QCOMPARE(ports.file_writes, 1);
    }
    void originalConsentCannotApproveADifferentOrCompletedTarget()
    {
        Fixture f; f.ports.target_exists = true; auto first = f.exportImage(); auto second = f.exportImage();
        QVERIFY(first->choose(QUrl::fromLocalFile("/controlled/first.png"))); QVERIFY(second->choose(QUrl::fromLocalFile("/controlled/second.png"))); f.ports.drain();
        first->cancel(); QVERIFY(!first->approve()); QVERIFY(second->approve()); f.ports.drain();
        QVERIFY(!first->result().written); QVERIFY(second->result().written); QCOMPARE(f.ports.file_writes, 1);
        QCOMPARE(f.ports.written_path, QStringLiteral("/controlled/second.png")); QVERIFY(!second->approve());
    }
    void realThreadCancellationAfterInstallAdmissionPreservesActualWrite()
    {
        BackendExecutor executor; const auto scheduler = executor.scheduler(); const auto gui = QThread::currentThread();
        auto view = std::make_shared<requestshare::View>(); QVERIFY(view->setActive(true));
        QSemaphore admitted, release; std::atomic_int writes{0}; std::atomic<QThread*> writer_thread{nullptr};
        requestshare::Services services; services.execution = {scheduler, presentation::QueuedPost()};
        services.read = [scheduler](int64_t id, auto done) {
            return execution::Submit(*scheduler, [id] { requestshare::Source source; source.request_id = id; source.record_revision = 1;
                source.opening = std::make_shared<const walletopening::Identity>();
                source.payload = "bitcoin:original"; source.current = [] { return true; }; return source; }, std::move(done), {}, {Lane::Domain});
        };
        services.render = [](const QString&) { QImage image{21, 21, QImage::Format_RGB32}; image.fill(Qt::black); return image; };
        auto files = std::make_shared<requestimageexport::Services>();
        files->inspect = [](const QUrl&) { return backup::Proposal{"/controlled/threaded.png", {}, false, false, backup::Error::None}; };
        files->write = [&](const auto&, bool, const auto&, const std::shared_ptr<requestshare::Grant>& grant) {
            writer_thread.store(QThread::currentThread());
            if (!grant->admit()) return backup::WriteResult{backup::Error::Changed};
            admitted.release(); release.acquire(); ++writes; return backup::WriteResult{};
        };
        services.files = std::move(files);
        auto operation = std::make_unique<RequestImageExportOperation>(17, services, view);
        QTRY_VERIFY(operation->choosing()); QVERIFY(operation->choose(QUrl::fromLocalFile("/controlled/threaded.png")));
        const bool entered = admitted.tryAcquire(1, 5000);
        if (!entered) release.release();
        QVERIFY(entered); operation->cancel(); release.release(); QTRY_COMPARE(operation->phase(), RequestImageExportOperation::Completed);
        QCOMPARE(writes.load(), 1); QVERIFY(operation->result().admitted); QVERIFY(operation->result().written); QVERIFY(writer_thread.load() != gui);
        operation.reset(); executor.shutdown(); QTRY_VERIFY(executor.isDrained());
    }
};
BITCOINQML_REGISTER_QT_TEST(RequestSharingModelTests)
#include <test_requestsharingmodel.moc>
