// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_TEST_MOCKS_RECEIVEREQUESTTEST_H
#define BITCOIN_QML_TEST_MOCKS_RECEIVEREQUESTTEST_H

#include <qml/models/paymentrequesteditormodel.h>
#include <qml/models/receiptreconciliationmodel.h>
#include <qml/models/addressreservationmodel.h>
#include <qml/models/receiverequestoperation.h>
#include <qml/models/walletmodel.h>
#include <qml/models/walletreceivemodel.h>

#include <QPointer>
#include <QtTest/QtTest>

#include <functional>
#include <memory>
#include <optional>

// Existing persistence oracles used the old synchronous bool API. Wait for the
// real terminal outcome before making their original assertions. New timing
// tests retain the operation and drive the manual executors explicitly.
inline bool AwaitReceiveRequest(ReceiveRequestOperation* raw)
{
    const std::unique_ptr<ReceiveRequestOperation> operation{raw};
    if (!operation) return false;
    if (!QTest::qWaitFor([&] { return !operation->pending(); }, 5000)) {
        QTest::qFail("Receive request operation did not reach an outcome", __FILE__, __LINE__);
        return false;
    }
    const bool saved = operation->error() == ReceiveRequestOperation::Error::None || operation->requestSaved() || operation->erased() || operation->labelSaved();
    if (!saved) qInfo() << "Receive request outcome:" << operation->error() << operation->errorText();
    return saved;
}

struct SavedTypedRequest {
    QString id, address;
};

//! Fills and saves the wallet's receive editor, as the Receive page does, then
//! starts a fresh request so later saves get their own address.
inline std::optional<SavedTypedRequest> SaveTypedRequest(WalletModel& wallet,
    const std::function<void(PaymentRequestEditorModel&)>& fill = {}, int timeout_ms = 30'000)
{
    auto* receive = wallet.receive();
    QPointer<PaymentRequestEditorModel> editor;
    // Names the step that stopped the save, so an intermittent CI failure
    // shows its cause.
    const auto fail = [&](const char* step) -> std::optional<SavedTypedRequest> {
        if (!editor) {
            qWarning() << "SaveTypedRequest stopped at" << step << "without an editor";
        } else {
            const auto result = editor->saveResult();
            qWarning() << "SaveTypedRequest stopped at" << step << "busy" << editor->busy() << "canSave" << editor->canSave()
                       << "reconciling" << wallet.receiveRequestReconciliationPending() << "result" << result.present
                       << result.requestSaved << result.error << result.errorText << "editor error" << editor->error().code
                       << editor->error().message;
        }
        return std::nullopt;
    };
    if (!receive) return fail("receive");
    if (!QTest::qWaitFor([&] {
            return receive->editor() && receive->ensureAddress() && receive->reservation()->available() && receive->editor()->canSave();
        }, timeout_ms)) return fail("ready");
    editor = receive->editor();
    if (fill) fill(*editor);
    // The store refuses a save with Checking while it reconciles a wallet
    // notification; the user saves again once it is done.
    for (int attempt{0}; attempt < 5; ++attempt) {
        if (!QTest::qWaitFor([&] { return !editor || (!wallet.receiveRequestReconciliationPending() && editor->canSave()); }, timeout_ms) || !editor) return fail("can save");
        editor->save();
        if (!QTest::qWaitFor([&] { return !editor || (!editor->busy() && (editor->saveResult().requestSaved || !editor->saveResult().errorText.isEmpty() ||
                editor->error().code != TextEditError::None)); }, timeout_ms) || !editor) return fail("save");
        if (editor->saveResult().requestSaved || editor->saveResult().error != ReceiveError::Code::Checking) break;
    }
    if (!editor->saveResult().requestSaved) return fail("saved");
    SavedTypedRequest saved{editor->saveResult().requestId, editor->address()};
    if (!receive->newRequest()) return fail("new request");
    if (!QTest::qWaitFor([&] { return receive->editor() != editor || (editor && editor->closePrompt()); }, timeout_ms)) return fail("replace");
    if (editor && receive->editor() == editor) editor->closePrompt()->answer(TextEditClosePrompt::Discard);
    if (!QTest::qWaitFor([&] { return receive->editor() != editor; }, timeout_ms)) return fail("replaced");
    return saved;
}

//! Saves an open editor's edits, as its Save button does. False when the
//! editor cannot save them or the wallet does not store the request.
inline bool SaveTypedEdit(PaymentRequestEditorModel& editor, int timeout_ms = 30'000)
{
    const QPointer<PaymentRequestEditorModel> guard{&editor};
    if (!QTest::qWaitFor([&] { return !guard || guard->canSave() || guard->error().code != TextEditError::None; }, timeout_ms) ||
        !guard || !guard->canSave()) return false;
    guard->save();
    (void)QTest::qWaitFor([&] { return !guard || (!guard->busy() && (!guard->dirty() || guard->error().code != TextEditError::None)); }, timeout_ms);
    return guard && !guard->busy() && !guard->dirty() && guard->saveResult().requestSaved;
}

//! Opens saved request \p id in its own editor, as a request detail does.
inline QPointer<PaymentRequestEditorModel> OpenTypedRequest(WalletModel& wallet, const QString& id, int timeout_ms = 30'000)
{
    auto* receive = wallet.receive();
    if (!receive) return {};
    QObject owner;
    const QPointer<PaymentRequestOpenOperation> operation{receive->openRequest(id, &owner)};
    if (!operation || !QTest::qWaitFor([&] { return !operation || !operation->pending(); }, timeout_ms) || !operation) return {};
    return operation->state() == PaymentRequestOpenOperation::State::Opened ? operation->editor() : nullptr;
}

#endif // BITCOIN_QML_TEST_MOCKS_RECEIVEREQUESTTEST_H
