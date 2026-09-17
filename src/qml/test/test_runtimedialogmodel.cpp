// Copyright (c) 2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/runtimedialogmodel.h>
#include <qml/test/mocks/mocknode.h>
#include <qml/test/qt_test_registry.h>
#include <node/interface_ui.h>
#include <util/translation.h>
#include <QTest>
#include <atomic>
#include <thread>

namespace {
class DialogNode : public MockNode
{
public:
    MessageBoxFn message;
    QuestionFn question;
    NotifyAlertChangedFn alert;
    bilingual_str warning;
    bilingual_str getWarnings() override { return warning; }
    std::unique_ptr<interfaces::Handler> handleMessageBox(MessageBoxFn fn) override
    {
        message = std::move(fn);
        return interfaces::MakeCleanupHandler([this] { message = {}; });
    }
    std::unique_ptr<interfaces::Handler> handleQuestion(QuestionFn fn) override
    {
        question = std::move(fn);
        return interfaces::MakeCleanupHandler([this] { question = {}; });
    }
    std::unique_ptr<interfaces::Handler> handleNotifyAlertChanged(NotifyAlertChangedFn fn) override
    {
        alert = std::move(fn);
        return interfaces::MakeCleanupHandler([this] { alert = {}; });
    }
};

// A failed assertion must also release the Core-style synchronous callback.
struct QuestionCall {
    RuntimeDialogModel& dialogs;
    std::atomic_bool answered{false}, finished{false};
    std::thread worker;
    QuestionCall(DialogNode& node, RuntimeDialogModel& model) : dialogs{model}, worker{[&] {
        answered = node.question(Untranslated("Continue?"), "", CClientUIInterface::BTN_OK | CClientUIInterface::BTN_CANCEL);
        finished = true;
    }} {}
    ~QuestionCall() { dialogs.stop(); worker.join(); }
};
}

class RuntimeDialogModelTests : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void startupWarningsAreDeduplicatedAndNotCurrentWarnings()
    {
        DialogNode node;
        RuntimeDialogModel dialogs{node};
        dialogs.addStartupWarnings({QStringLiteral("startup"), QStringLiteral("startup")});
        QVERIFY(!dialogs.runtimeDialogVisible());
        dialogs.completeInitialization(true);
        QVERIFY(dialogs.runtimeDialogVisible());
        QCOMPARE(dialogs.runtimeDialogMessage(), QStringLiteral("startup"));
        QVERIFY(!dialogs.hasWarnings());
        dialogs.answerRuntimeDialog(CClientUIInterface::BTN_OK);
        QVERIFY(!dialogs.runtimeDialogVisible());
    }

    void warningNotificationsMarshalToGuiThread()
    {
        DialogNode node;
        RuntimeDialogModel dialogs{node};
        node.warning = Untranslated("first<hr/>second");
        std::thread notify{[&] { node.alert(); }};
        notify.join();
        QVERIFY(!dialogs.hasWarnings());
        QTRY_COMPARE(dialogs.warningList().size(), 2);
    }

    void questionsOnlyAcceptAnOfferedButton()
    {
        DialogNode node;
        RuntimeDialogModel dialogs{node};
        dialogs.completeInitialization(true);
        QuestionCall call{node, dialogs};
        QTRY_VERIFY(dialogs.runtimeDialogVisible());
        dialogs.answerRuntimeDialog(CClientUIInterface::BTN_YES);
        QVERIFY(dialogs.runtimeDialogVisible());
        QVERIFY(!call.finished.load());
        dialogs.answerRuntimeDialog(CClientUIInterface::BTN_OK);
        QTRY_VERIFY(call.finished.load());
        QVERIFY(call.answered.load());
    }

    void shutdownCancelsVisibleQuestions()
    {
        DialogNode node;
        RuntimeDialogModel dialogs{node};
        dialogs.completeInitialization(true);
        QuestionCall call{node, dialogs};
        QTRY_VERIFY(dialogs.runtimeDialogVisible());
        dialogs.stop();
        QTRY_VERIFY(call.finished.load());
        QVERIFY(!call.answered.load());
        QVERIFY(!dialogs.runtimeDialogVisible());
    }

    void shutdownAlsoRejectsUndeliveredCallbacks()
    {
        DialogNode node;
        RuntimeDialogModel dialogs{node};
        QuestionCall call{node, dialogs};
        // Do not deliver queued GUI events before stopping.
        dialogs.stop();
        QTRY_VERIFY(call.finished.load());
        QVERIFY(!call.answered.load());
        QVERIFY(!dialogs.runtimeDialogVisible());
    }
};
BITCOINQML_REGISTER_QT_TEST(RuntimeDialogModelTests)
#include <test_runtimedialogmodel.moc>
