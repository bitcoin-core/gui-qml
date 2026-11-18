// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/runtimeprompt.h>
#include <test/qt_test_registry.h>

#include <node/interface_ui.h>

#include <QScopeGuard>
#include <QtTest/QtTest>

#include <chrono>
#include <future>
#include <memory>
#include <utility>

namespace {
using UI = CClientUIInterface;
using namespace runtimeprompt;

std::shared_ptr<const Request> Question(uint32_t style = UI::MSG_ERROR, QString summary = QStringLiteral("question"),
                                      std::shared_ptr<const QString> details = {})
{
    return std::make_shared<const Request>(Origin::Runtime, style, true, std::move(summary), std::move(details));
}
} // namespace

class RuntimePromptTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void onlyOkIsAffirmative_data()
    {
        QTest::addColumn<unsigned int>("button");
        QTest::addColumn<bool>("affirmative");
        QTest::newRow("ok") << unsigned(UI::BTN_OK) << true;
        QTest::newRow("yes") << unsigned(UI::BTN_YES) << false;
        QTest::newRow("no") << unsigned(UI::BTN_NO) << false;
        QTest::newRow("abort") << unsigned(UI::BTN_ABORT) << false;
        QTest::newRow("retry") << unsigned(UI::BTN_RETRY) << false;
        QTest::newRow("ignore") << unsigned(UI::BTN_IGNORE) << false;
        QTest::newRow("close") << unsigned(UI::BTN_CLOSE) << false;
        QTest::newRow("cancel") << unsigned(UI::BTN_CANCEL) << false;
        QTest::newRow("discard") << unsigned(UI::BTN_DISCARD) << false;
        QTest::newRow("help") << unsigned(UI::BTN_HELP) << false;
        QTest::newRow("apply") << unsigned(UI::BTN_APPLY) << false;
        QTest::newRow("reset") << unsigned(UI::BTN_RESET) << false;
    }

    void onlyOkIsAffirmative()
    {
        QFETCH(unsigned int, button);
        QFETCH(bool, affirmative);
        AnswerCell cell{UI::BTN_MASK};
        QCOMPARE(cell.answer(button), Disposition::Accepted);
        QVERIFY(cell.result());
        QCOMPARE(cell.result()->completion, Completion::Answered);
        QCOMPARE(cell.result()->button, button);
        QCOMPARE(cell.result()->affirmative(), affirmative);
        QVERIFY(!cell.cancel());
        QCOMPARE(cell.answer(button), Disposition::AlreadyCompleted);
        QCOMPARE(cell.result()->affirmative(), affirmative);
    }

    void acceptedOkSurvivesCloseBeforeNativeRead()
    {
        Queue queue;
        const auto request = Question();
        const auto admission = queue.enqueue(request);
        QVERIFY(admission);
        QCOMPARE(queue.visible(admission.id), Disposition::Accepted);
        QCOMPARE(queue.answer(admission.id, UI::BTN_OK), Disposition::Accepted);
        QCOMPARE(queue.state(), State::Completing);
        queue.close();
        queue.close();
        QCOMPARE(queue.state(), State::Stopping);
        QCOMPARE(queue.size(), size_t{1});
        QVERIFY(!queue.active());
        // Native starts consuming after close: no waiter scheduling assumption
        // can hide a shutdown overwrite of the already-latched answer.
        auto native = std::async(std::launch::async, [request] { return request->answer->wait(); });
        const auto finish = qScopeGuard([&] { request->answer->cancel(); if (native.valid()) native.wait(); });
        QVERIFY(native.wait_for(std::chrono::seconds{5}) == std::future_status::ready);
        const auto answer = native.get();
        QCOMPARE(answer.id, admission.id);
        QCOMPARE(answer.completion, Completion::Answered);
        QVERIFY(answer.affirmative());
        QCOMPARE(queue.delivered(admission.id), Disposition::Accepted);
        QCOMPARE(queue.state(), State::Stopped);
        QCOMPARE(queue.retainedCharacters(), size_t{0});
    }

    void closeWakesUnansweredNativeContinuation()
    {
        Queue queue;
        const auto request = Question();
        const auto admission = queue.enqueue(request);
        QVERIFY(admission);
        std::promise<void> entered;
        auto started = entered.get_future();
        auto native = std::async(std::launch::async, [request, &entered] {
            entered.set_value();
            return request->answer->wait();
        });
        const auto finish = qScopeGuard([&] { request->answer->cancel(); if (native.valid()) native.wait(); });
        started.get();
        queue.close();
        QVERIFY(native.wait_for(std::chrono::seconds{5}) == std::future_status::ready);
        const auto answer = native.get();
        QCOMPARE(answer.id, admission.id);
        QCOMPARE(answer.completion, Completion::Cancelled);
        QVERIFY(!answer.affirmative());
        QCOMPARE(queue.answer(admission.id, UI::BTN_OK), Disposition::Closed);
        QCOMPARE(queue.delivered(admission.id), Disposition::Accepted);
        QCOMPARE(queue.state(), State::Stopped);
    }

    void cancelBeforeAnswerRemainsNonAffirmative()
    {
        AnswerCell cell{UI::MSG_ERROR};
        QVERIFY(cell.cancel());
        QVERIFY(!cell.cancel());
        QCOMPARE(cell.answer(UI::BTN_OK), Disposition::AlreadyCompleted);
        QCOMPARE(cell.result()->completion, Completion::Cancelled);
        QCOMPARE(cell.result()->button, uint32_t{UI::BTN_CANCEL});
        QVERIFY(!cell.result()->affirmative());
    }

    void fifoRejectsStaleAnswerWithoutConsumingSuccessor()
    {
        Queue queue;
        const auto first = Question();
        const auto second = Question(UI::MSG_WARNING);
        const auto a = queue.enqueue(first);
        const auto b = queue.enqueue(second);
        QVERIFY(a);
        QVERIFY(b);
        QVERIFY(b.id > a.id);
        QCOMPARE(queue.active()->descriptor.id, a.id);
        QCOMPARE(queue.visible(b.id), Disposition::StaleToken);
        QCOMPARE(queue.delivered(b.id), Disposition::NotReady);
        QCOMPARE(queue.visible(a.id), Disposition::Accepted);
        QCOMPARE(queue.answer(a.id, UI::BTN_OK), Disposition::Accepted);
        QCOMPARE(queue.active()->descriptor.id, a.id);
        QCOMPARE(queue.delivered(a.id), Disposition::Accepted);
        QCOMPARE(queue.active()->descriptor.id, b.id);
        QCOMPARE(queue.answer(a.id, UI::BTN_OK), Disposition::StaleToken);
        QCOMPARE(queue.visible(a.id), Disposition::StaleToken);
        QVERIFY(!second->answer->result());
        QCOMPARE(queue.visible(b.id), Disposition::Accepted);
        QCOMPARE(queue.answer(b.id, UI::BTN_OK), Disposition::Accepted);
        QCOMPARE(queue.delivered(b.id), Disposition::Accepted);
        QCOMPARE(queue.state(), State::Idle);
        QVERIFY(!queue.active());
        QCOMPARE(queue.delivered(a.id), Disposition::StaleToken);
        const auto next = queue.enqueue(Question());
        QVERIFY(next);
        QVERIFY(next.id > b.id);
    }

    void completionRetainsActiveWhileAnotherRequestQueues()
    {
        Queue queue;
        const auto first = Question();
        const auto a = queue.enqueue(first);
        QVERIFY(a);
        QCOMPARE(queue.visible(a.id), Disposition::Accepted);
        QCOMPARE(queue.answer(a.id, UI::BTN_OK), Disposition::Accepted);
        const auto second = Question();
        const auto b = queue.enqueue(second);
        QVERIFY(b);
        QCOMPARE(queue.state(), State::Completing);
        QCOMPARE(queue.active()->descriptor.id, a.id);
        QCOMPARE(queue.answer(b.id, UI::BTN_OK), Disposition::StaleToken);
        QVERIFY(!second->answer->result());
        QCOMPARE(queue.delivered(a.id), Disposition::Accepted);
        QCOMPARE(queue.state(), State::Presenting);
        QCOMPARE(queue.active()->descriptor.id, b.id);
    }

    void invalidButtonKeepsContinuation_data()
    {
        QTest::addColumn<unsigned int>("button");
        QTest::newRow("none") << 0U;
        QTest::newRow("undeclared") << unsigned(UI::BTN_YES);
        QTest::newRow("two-declared") << unsigned(UI::BTN_OK | UI::BTN_ABORT);
        QTest::newRow("button-and-icon") << unsigned(UI::BTN_OK | UI::ICON_WARNING);
        QTest::newRow("unknown-bit") << 0x80000000U;
    }

    void invalidButtonKeepsContinuation()
    {
        QFETCH(unsigned int, button);
        Queue queue;
        const auto request = Question(UI::MSG_ERROR | UI::BTN_ABORT);
        const auto admission = queue.enqueue(request);
        QVERIFY(admission);
        QCOMPARE(queue.visible(admission.id), Disposition::Accepted);
        QCOMPARE(queue.answer(admission.id, button), Disposition::InvalidButton);
        QCOMPARE(queue.state(), State::AwaitingAnswer);
        QCOMPARE(queue.size(), size_t{1});
        QVERIFY(!request->answer->result());
        QCOMPARE(queue.answer(admission.id, UI::BTN_ABORT), Disposition::Accepted);
        QVERIFY(!request->answer->result()->affirmative());
    }

    void emptyButtonMaskDefaultsToOk()
    {
        Queue queue;
        const auto request = Question(UI::ICON_WARNING | UI::MODAL);
        const auto admission = queue.enqueue(request);
        QVERIFY(admission);
        QCOMPARE(queue.active()->descriptor.buttons, uint32_t{UI::BTN_OK});
        QCOMPARE(queue.visible(admission.id), Disposition::Accepted);
        QCOMPARE(queue.answer(admission.id, UI::BTN_YES), Disposition::InvalidButton);
        QCOMPARE(queue.answer(admission.id, UI::BTN_OK), Disposition::Accepted);
        QVERIFY(request->answer->result()->affirmative());
    }

    void presentationMustMatchAndBecomeReachable()
    {
        Queue queue;
        const auto request = Question();
        const auto admission = queue.enqueue(request);
        QVERIFY(admission);
        QCOMPARE(queue.state(), State::Presenting);
        QCOMPARE(queue.answer(admission.id, UI::BTN_OK), Disposition::NotReady);
        QCOMPARE(queue.delivered(admission.id), Disposition::NotReady);
        QVERIFY(!request->answer->result());
        QCOMPARE(queue.visible(admission.id), Disposition::Accepted);
        QCOMPARE(queue.visible(admission.id), Disposition::Accepted);
        QCOMPARE(queue.unavailable(admission.id), Disposition::NotReady);
        QCOMPARE(queue.dismiss(admission.id), Disposition::Accepted);
        QCOMPARE(queue.dismiss(admission.id), Disposition::AlreadyCompleted);
        QCOMPARE(queue.visible(admission.id), Disposition::NotReady);
        QVERIFY(!request->answer->result()->affirmative());
        QCOMPARE(queue.delivered(admission.id), Disposition::Accepted);
    }

    void unavailablePresentationCancelsBeforeVisible()
    {
        Queue queue;
        const auto request = Question();
        const auto admission = queue.enqueue(request);
        QVERIFY(admission);
        QCOMPARE(queue.unavailable(admission.id), Disposition::Accepted);
        QCOMPARE(queue.state(), State::Completing);
        QCOMPARE(request->answer->result()->completion, Completion::Cancelled);
        QCOMPARE(queue.delivered(admission.id), Disposition::Accepted);
        QCOMPARE(queue.state(), State::Idle);
    }

    void overflowAndClosedAdmissionCompleteRejectedCells()
    {
        Queue queue{{1, 128}};
        const auto first = Question();
        const auto a = queue.enqueue(first);
        QVERIFY(a);
        const auto overflow = Question();
        const auto rejected = queue.enqueue(overflow);
        QCOMPARE(rejected.disposition, Disposition::Overflow);
        QCOMPARE(rejected.id, Id{0});
        QCOMPARE(overflow->answer->result()->completion, Completion::Cancelled);
        QVERIFY(!first->answer->result());
        QCOMPARE(queue.size(), size_t{1});
        queue.close();
        const auto closed = Question();
        QCOMPARE(queue.enqueue(closed).disposition, Disposition::Closed);
        QCOMPARE(closed->answer->result()->completion, Completion::Cancelled);
        QCOMPARE(queue.delivered(a.id), Disposition::Accepted);
        QCOMPARE(queue.state(), State::Stopped);
        Queue zero{{0, 128}};
        const auto no_capacity = Question();
        QCOMPARE(zero.enqueue(no_capacity).disposition, Disposition::Overflow);
        QVERIFY(no_capacity->answer->result());
        QCOMPARE(zero.size(), size_t{0});
        zero.close();
        QCOMPARE(zero.state(), State::Stopped);
    }

    void diagnosticsRemainBoundedUntilAcknowledged()
    {
        Queue queue{{2, 10}};
        const auto details = std::make_shared<const QString>(QStringLiteral("detail"));
        const auto request = Question(UI::MSG_ERROR, QStringLiteral("text"), details);
        const auto a = queue.enqueue(request);
        QVERIFY(a);
        QCOMPARE(queue.retainedCharacters(), size_t{10});
        const auto overflow = Question(UI::MSG_ERROR, QStringLiteral("x"));
        QCOMPARE(queue.enqueue(overflow).disposition, Disposition::DiagnosticCapacity);
        QVERIFY(overflow->answer->result());
        QCOMPARE(queue.dismiss(a.id), Disposition::Accepted);
        QCOMPARE(queue.retainedCharacters(), size_t{10});
        QCOMPARE(queue.delivered(a.id), Disposition::Accepted);
        QCOMPARE(queue.retainedCharacters(), size_t{0});
        const auto b = queue.enqueue(Question(UI::MSG_ERROR, QStringLiteral("next")));
        QVERIFY(b);
        QVERIFY(b.id > a.id);
        const auto too_long = Question(UI::MSG_ERROR, QString(static_cast<qsizetype>(MAX_SUMMARY_CHARACTERS + 1), QChar{'x'}));
        QCOMPARE(queue.enqueue(too_long).disposition, Disposition::InvalidRequest);
        QVERIFY(too_long->answer->result());
        QCOMPARE(queue.retainedCharacters(), size_t{4});
    }

    void duplicateAdmissionDoesNotCancelOriginal()
    {
        Queue queue;
        const auto request = Question();
        const auto a = queue.enqueue(request);
        QVERIFY(a);
        QCOMPARE(queue.enqueue(request).disposition, Disposition::InvalidRequest);
        Queue other;
        QCOMPARE(other.enqueue(request).disposition, Disposition::InvalidRequest);
        QVERIFY(!request->answer->result());
        QCOMPARE(queue.size(), size_t{1});
        const auto completed = Question();
        QVERIFY(completed->answer->cancel());
        QCOMPARE(queue.enqueue(completed).disposition, Disposition::InvalidRequest);
        QCOMPARE(queue.enqueue({}).disposition, Disposition::InvalidRequest);
    }

    void producerCancellationRetiresOnlyMatchingQueuedRequest()
    {
        Queue queue;
        const auto first = Question();
        const auto second = Question();
        const auto a = queue.enqueue(first);
        const auto b = queue.enqueue(second);
        QVERIFY(a);
        QVERIFY(b);
        QCOMPARE(queue.visible(a.id), Disposition::Accepted);
        QVERIFY(second->answer->cancel());
        QCOMPARE(queue.delivered(b.id), Disposition::Accepted);
        QCOMPARE(queue.active()->descriptor.id, a.id);
        QCOMPARE(queue.state(), State::AwaitingAnswer);
        QVERIFY(!first->answer->result());
        QCOMPARE(queue.size(), size_t{1});
        QVERIFY(first->answer->cancel());
        QCOMPARE(queue.visible(a.id), Disposition::AlreadyCompleted);
        QCOMPARE(queue.state(), State::Completing);
        QCOMPARE(queue.delivered(a.id), Disposition::Accepted);
        QCOMPARE(queue.state(), State::Idle);
    }

    void closeRetainsEveryOriginalUntilCompletionAcknowledgment()
    {
        Queue queue;
        auto first = Question();
        auto second = Question();
        std::weak_ptr<const Request> first_lifetime = first;
        std::weak_ptr<const Request> second_lifetime = second;
        const auto a = queue.enqueue(first);
        const auto b = queue.enqueue(second);
        QVERIFY(a);
        QVERIFY(b);
        const auto accepted_cell = first->answer;
        const auto canceled_cell = second->answer;
        QCOMPARE(queue.visible(a.id), Disposition::Accepted);
        QCOMPARE(queue.answer(a.id, UI::BTN_OK), Disposition::Accepted);
        first.reset();
        second.reset();
        queue.close();
        QVERIFY(!first_lifetime.expired());
        QVERIFY(!second_lifetime.expired());
        QCOMPARE(queue.size(), size_t{2});
        QCOMPARE(queue.delivered(b.id), Disposition::Accepted);
        QVERIFY(second_lifetime.expired());
        QVERIFY(!first_lifetime.expired());
        QCOMPARE(queue.state(), State::Stopping);
        QCOMPARE(queue.delivered(b.id), Disposition::StaleToken);
        QCOMPARE(queue.delivered(a.id), Disposition::Accepted);
        QVERIFY(first_lifetime.expired());
        QCOMPARE(queue.state(), State::Stopped);
        QVERIFY(accepted_cell->result()->affirmative());
        QCOMPARE(canceled_cell->result()->completion, Completion::Cancelled);
    }

    void immutableDescriptorRetainsCapturedOriginAndDiagnostics()
    {
        Queue queue;
        const auto details = std::make_shared<const QString>(QStringLiteral("full original detail"));
        auto request = std::make_shared<const Request>(Origin::StartupSummary, UI::MSG_WARNING, false,
            QStringLiteral("summary"), details, false);
        const std::weak_ptr<const Request> lifetime = request;
        const auto admission = queue.enqueue(request);
        QVERIFY(admission);
        auto projection = queue.active();
        QCOMPARE(projection->descriptor.id, admission.id);
        QCOMPARE(projection->descriptor.origin, Origin::StartupSummary);
        QCOMPARE(projection->descriptor.style, uint32_t{UI::MSG_WARNING});
        QCOMPARE(projection->descriptor.buttons, uint32_t{UI::BTN_OK});
        QVERIFY(!projection->descriptor.question);
        QVERIFY(!projection->descriptor.diagnostics_complete);
        QCOMPARE(projection->request.get(), request.get());
        QCOMPARE(*projection->request->details, QStringLiteral("full original detail"));
        request.reset();
        QCOMPARE(queue.unavailable(admission.id), Disposition::Accepted);
        QCOMPARE(queue.delivered(admission.id), Disposition::Accepted);
        QCOMPARE(queue.size(), size_t{0});
        QVERIFY(!lifetime.expired());
        QCOMPARE(projection->request->answer->result()->completion, Completion::Cancelled);
        projection.reset();
        QVERIFY(lifetime.expired());
    }

    void ownerDestructionSafelyCancelsUnansweredCell()
    {
        auto queue = std::make_unique<Queue>();
        const auto request = Question();
        const auto admission = queue->enqueue(request);
        QVERIFY(admission);
        queue.reset();
        auto native = std::async(std::launch::async, [request] { return request->answer->wait(); });
        const auto finish = qScopeGuard([&] { request->answer->cancel(); if (native.valid()) native.wait(); });
        QVERIFY(native.wait_for(std::chrono::seconds{5}) == std::future_status::ready);
        const auto answer = native.get();
        QCOMPARE(answer.id, admission.id);
        QCOMPARE(answer.completion, Completion::Cancelled);
        QVERIFY(!answer.affirmative());
        QCOMPARE(request->answer->answer(UI::BTN_OK), Disposition::AlreadyCompleted);
    }
};

#ifdef BITCOINQML_NO_TEST_MAIN
BITCOINQML_REGISTER_QT_TEST(RuntimePromptTests)
#else
QTEST_MAIN(RuntimePromptTests)
#endif

#include <test_runtimeprompt.moc>
