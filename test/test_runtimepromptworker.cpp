// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/runtimepromptworker.h>
#include <qml/models/runtimepromptmodel.h>
#include <test/mocks/manualexecutionenvironment.h>
#include <node/interface_ui.h>
#include <QtTest/QtTest>
#include <QSemaphore>
#include <QScopeGuard>
#include <thread>
#include <deque>

namespace {
using namespace runtimeprompt;
constexpr uint32_t OK{CClientUIInterface::BTN_OK},CANCEL{CClientUIInterface::BTN_CANCEL};
struct Fixture {
    ManualRoutedExecution execution;
    std::deque<std::function<void()>> gui;
    int publications{0},drained{0};bool reject_projection{false};
    std::shared_ptr<Service> service;
    Fixture(){service=std::make_shared<Service>(execution::SnapshotServices{execution.scheduler,{}},[this]{
        if(reject_projection)throw std::runtime_error{"presentation rejected"};
        gui.emplace_back([this]{++publications;});},[this]{++drained;});}
    ~Fixture(){service->close();execution.drain();service.reset();execution.drain();}
    std::shared_ptr<const Request> request(QString summary=QStringLiteral("Original question"),uint32_t buttons=OK|CANCEL){
        return std::make_shared<const Request>(Origin::Runtime,buttons|CClientUIInterface::MODAL,true,std::move(summary));}
    void settle(){execution.drain();while(!gui.empty()){auto turn=std::move(gui.front());gui.pop_front();turn();}}
};
}
class RuntimePromptWorkerTests:public QObject {
    Q_OBJECT
private Q_SLOTS:
    void acceptedAnswerSurvivesCloseBeforeNativeReturn(){
        Fixture f;const auto first=f.request(),second=f.request(QStringLiteral("Second question"));
        std::shared_ptr<Continuation> one,two;QVERIFY(f.service->enqueue(first,true,one));QVERIFY(f.service->enqueue(second,true,two));
        f.settle();const auto id=f.service->projection().active->descriptor.id;
        QVERIFY(f.service->visible(id));f.settle();QVERIFY(f.service->answer(id,OK));f.execution.drain();
        f.service->close(); // Native has not reacquired its answer or acknowledged return.
        QVERIFY(one->wait().affirmative());QVERIFY(!two->wait().affirmative());f.execution.drain();
        // Answer consumption is separate from the original producer's final
        // post-answer work. Neither popup loss nor close invents that receipt.
        QCOMPARE(f.service->projection().state,State::Stopping);QCOMPARE(f.service->projection().pending,size_t{2});QCOMPARE(f.drained,0);
        one.reset();two.reset();f.execution.drain();
        QCOMPARE(f.service->projection().state,State::Stopped);QCOMPARE(f.service->projection().pending,size_t{0});QCOMPARE(f.drained,1);QCOMPARE(f.execution.scheduler->pending(),size_t{0});
    }
    void staleQtHandleCannotAnswerItsSuccessor(){
        Fixture f;std::shared_ptr<Continuation> one,two;const auto first=f.request(),second=f.request(QStringLiteral("Second question"));
        QVERIFY(f.service->enqueue(first,true,one));QVERIFY(f.service->enqueue(second,true,two));f.settle();
        RuntimePromptModel old{f.service->projection().active,f.service,nullptr};QVERIFY(old.presented());f.settle();
        QVERIFY(old.answer(OK));f.execution.drain();QVERIFY(one->wait().affirmative());one.reset();f.settle();
        const auto current=f.service->projection().active;QVERIFY(current);QVERIFY(current->descriptor.id!=old.id());
        RuntimePromptModel next{current,f.service,nullptr};QVERIFY(next.presented());f.settle();
        QVERIFY(old.answer(OK));f.settle();QVERIFY(!second->answer->result());QCOMPARE(f.service->projection().last,Disposition::StaleToken);
        QVERIFY(next.answer(CANCEL));f.execution.drain();QVERIFY(!two->wait().affirmative());two.reset();f.settle();
        QCOMPARE(f.service->projection().state,State::Idle);
    }
    void invalidCompositeAndEarlyAnswersDoNotConsumeQuestion(){
        Fixture f;std::shared_ptr<Continuation> completion;const auto request=f.request();QVERIFY(f.service->enqueue(request,true,completion));f.settle();
        const auto id=f.service->projection().active->descriptor.id;QVERIFY(f.service->answer(id,OK));f.settle();
        QCOMPARE(f.service->projection().last,Disposition::NotReady);QVERIFY(!request->answer->result());
        QVERIFY(f.service->visible(id));f.settle();QVERIFY(f.service->answer(id,OK|CANCEL));f.settle();
        QCOMPARE(f.service->projection().last,Disposition::InvalidButton);QVERIFY(!request->answer->result());
        QVERIFY(f.service->answer(id,CClientUIInterface::BTN_YES));f.settle();QVERIFY(!request->answer->result());
        QVERIFY(f.service->answer(id,OK));f.execution.drain();QVERIFY(completion->wait().affirmative());completion.reset();f.settle();
    }
    void heldGuiDeliveryDoesNotBlockNativeRetirement(){
        Fixture f;std::shared_ptr<Continuation> continuation;const auto request=f.request();QVERIFY(f.service->enqueue(request,true,continuation));f.execution.drain();
        QVERIFY(!f.gui.empty());f.service->close();f.execution.drain();QCOMPARE(f.drained,0);
        QCOMPARE(f.service->projection().state,State::Stopping);QVERIFY(!continuation->wait().affirmative());continuation.reset();f.execution.drain();
        QCOMPARE(f.drained,1);QCOMPARE(f.execution.scheduler->pending(),size_t{0});QVERIFY(!f.gui.empty());
    }
    void nonblockingMessageRetiresOnlyAfterExplicitDisposition(){
        Fixture f;const auto request=std::make_shared<const Request>(Origin::Runtime,OK,false,QStringLiteral("Original information"));
        std::shared_ptr<Continuation> unused;QVERIFY(f.service->enqueue(request,false,unused));QVERIFY(!unused);f.settle();
        const auto id=f.service->projection().active->descriptor.id;QVERIFY(!request->answer->result());
        QVERIFY(f.service->unavailable(id));f.settle();QVERIFY(request->answer->result());
        QCOMPARE(f.service->projection().state,State::Idle);QCOMPARE(f.service->projection().pending,size_t{0});
    }
    void presentationRetirementCancelsOnlyItsOriginalWaitingQuestion(){
        Fixture f;const auto first=f.request(),second=f.request(QStringLiteral("Successor"));std::shared_ptr<Continuation> one,two;
        QVERIFY(f.service->enqueue(first,true,one));QVERIFY(f.service->enqueue(second,true,two));f.settle();
        RuntimePromptModel retired{f.service->projection().active,f.service,nullptr};QVERIFY(retired.presented());f.settle();
        QVERIFY(retired.unavailable());f.execution.drain();QVERIFY(!one->wait().affirmative());one.reset();f.settle();
        const auto successor=f.service->projection().active;QVERIFY(successor);QVERIFY(successor->descriptor.id!=retired.id());
        QVERIFY(retired.unavailable());f.settle();QVERIFY(!second->answer->result());
        f.service->close();QVERIFY(!two->wait().affirmative());two.reset();f.execution.drain();QCOMPARE(f.drained,1);
    }
    void fixedIngressBudgetRejectsWithoutWaitingOrReplacingTheFirst(){
        Fixture f;std::vector<std::shared_ptr<Continuation>> continuations;continuations.reserve(32);
        for(int i=0;i<32;++i){std::shared_ptr<Continuation> continuation;QVERIFY(f.service->enqueue(f.request(QString::number(i)),true,continuation));continuations.push_back(std::move(continuation));}
        const auto overflow=f.request(QStringLiteral("Overflow"));std::shared_ptr<Continuation> rejected;
        QVERIFY(!f.service->enqueue(overflow,true,rejected));QVERIFY(!rejected);QVERIFY(overflow->answer->result());f.settle();
        QCOMPARE(f.service->projection().pending,size_t{32});QCOMPARE(f.service->projection().active->request->summary,QStringLiteral("0"));
        f.service->close();for(const auto& continuation:continuations)QVERIFY(!continuation->wait().affirmative());continuations.clear();f.execution.drain();
        QCOMPARE(f.drained,1);QCOMPARE(f.execution.scheduler->pending(),size_t{0});
    }
    void rejectedPublicationRetainsOriginalPromptAndNativeAnswer(){
        Fixture f;f.reject_projection=true;std::shared_ptr<Continuation> continuation;const auto request=f.request();
        QVERIFY(f.service->enqueue(request,true,continuation));f.execution.drain();const auto id=f.service->projection().active->descriptor.id;
        QVERIFY(f.service->visible(id));f.execution.drain();QVERIFY(f.service->answer(id,OK));f.execution.drain();
        QVERIFY(continuation->wait().affirmative());continuation.reset();f.execution.drain();QCOMPARE(f.service->projection().state,State::Idle);QCOMPARE(f.publications,0);
    }
    void nativeWaitUsesOnlyItsOriginalAnswerCell(){
        Fixture f;const auto request=f.request();std::shared_ptr<Continuation> continuation;QVERIFY(f.service->enqueue(request,true,continuation));f.settle();
        QSemaphore entered,returned;bool affirmative{false};std::thread producer{[&,owned=std::move(continuation)]{entered.release();affirmative=owned->wait().affirmative();returned.release();}};
        const auto cleanup=qScopeGuard([&]{f.service->close();f.execution.drain();if(producer.joinable())producer.join();});
        QVERIFY(entered.tryAcquire(1,5000));QCOMPARE(returned.available(),0);
        const auto id=f.service->projection().active->descriptor.id;QVERIFY(f.service->visible(id));f.settle();QVERIFY(f.service->answer(id,OK));f.execution.drain();
        QVERIFY(returned.tryAcquire(1,5000));producer.join();f.settle();QVERIFY(affirmative);QCOMPARE(f.service->projection().state,State::Idle);
    }
    void closedRootRejectsConstructionWithoutAnEmptySuccess(){
        ManualRoutedExecution execution;execution.scheduler->closeOrdinaryAdmission();
        Service service{{execution.scheduler,{}},[] {}};std::shared_ptr<Continuation> continuation;
        const auto request=std::make_shared<const Request>(Origin::Runtime,OK,true,QStringLiteral("Unadmitted"));
        QVERIFY(!service.enqueue(request,true,continuation));QVERIFY(!continuation);QVERIFY(request->answer->result());
        QCOMPARE(service.projection().state,State::Stopped);QCOMPARE(service.projection().last,Disposition::NotReady);execution.drain();
    }
};
#ifdef BITCOINQML_NO_TEST_MAIN
#include <test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(RuntimePromptWorkerTests)
#else
QTEST_MAIN(RuntimePromptWorkerTests)
#endif
#include <test_runtimepromptworker.moc>
