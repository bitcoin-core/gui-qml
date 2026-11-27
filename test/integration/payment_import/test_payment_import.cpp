// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/integration/runner/test_cases.h>
#include <test/integration/fixture/test_wallet.h>
#include <test/mocks/preparationtest.h>
#include <test/mocks/sendtest.h>
#include <qml/models/sendimportmodel.h>

#include <QClipboard>
#include <QFile>
#include <QGuiApplication>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QTemporaryDir>
#include <QUrl>
#include <algorithm>

namespace {
SendImportModel* Imports(QObject* page) { return page ? page->property("imports").value<SendImportModel*>() : nullptr; }
} // namespace

class PaymentImportIntegrationTests : public QObject
{
    Q_OBJECT
    BitcoinQmlApplication& m_app;
    WalletModel* m_wallet{nullptr};
    QString m_address;
    QString importDiagnostic(QObject* original) const {
        auto* page = qmltest::Find(m_app, "walletSendPage");
        QStringList facts{QStringLiteral("Import diagnostic:")};
        if (page) {
            facts << QStringLiteral("active=") + QString::number(Imports(page)->operation() != nullptr)
                  << QStringLiteral("draft=") + m_wallet->sendRecipientList()->revision()
                  << QStringLiteral("importPending=") + QString::number(m_wallet->sendRecipientList()->importPending());
        }
        if (original) for (const auto* name : {"state", "pending", "canApply", "promptToken", "outcome", "error"})
            facts << QString::fromLatin1(name) + QLatin1Char('=') + original->property(name).toString();
        return facts.join(QLatin1Char(' '));
    }

public:
    explicit PaymentImportIntegrationTests(BitcoinQmlApplication& app) : m_app{app} {}

private Q_SLOTS:
    void init()
    {
        QGuiApplication::clipboard()->clear();
        m_wallet = qmltest::CreateWallet(m_app);
        QVERIFY(m_wallet);
        m_address = qmltest::NewAddress(m_app, m_wallet->name());
        QVERIFY(qmltest::Click(m_app, "sendTabButton"));
        QVERIFY(qmltest::WaitForVisible(m_app, "sendPage"));
    }

    void cleanup() { QGuiApplication::clipboard()->clear(); }

    void equalImportedAmountReplacesMaximumIntentWithoutChangingTheRequest()
    {
        QVERIFY(qmltest::FundWallet(m_app, *m_wallet));
        auto* recipient = m_wallet->sendRecipientList()->currentRecipient();
        QVERIFY(qmltest::Click(m_app, "sendUseMaximumButton")); QTRY_COMPARE(m_wallet->maximumRecipient(), recipient);
        QTRY_VERIFY(recipient->amount()->satoshi() > 0);
        const auto requested = recipient->amount()->satoshi();
        const QString uri = "bitcoin:" + m_address + "?amount=" + BitcoinAmount::satsToBtcString(requested) + "&label=Alice&message=Exact";
        QVERIFY(qmltest::Click(m_app, "sendOptionsButton"));
        QVERIFY(qmltest::Click(m_app, "sendOptionsOpenPaymentRequestButton"));
        QVERIFY(qmltest::SetText(m_app, "sendUriImportInput", uri));
        QVERIFY(qmltest::Click(m_app, "sendUriImportApplyButton"));
        auto* replacement = qmltest::Find(m_app, "sendPaymentUriOverwritePopup"); QVERIFY(replacement);
        QTRY_VERIFY(replacement->property("opened").toBool());
        QCOMPARE(m_wallet->maximumRecipient(), recipient); QCOMPARE(recipient->amount()->satoshi(), requested);
        QVERIFY(qmltest::Click(m_app, "sendPaymentUriOverwriteConfirmButton"));
        QTRY_VERIFY(recipient->hasPaymentRequest()); QTRY_COMPARE(m_wallet->maximumRecipient(), nullptr);
        QCOMPARE(recipient->amount()->satoshi(), requested); QCOMPARE(recipient->paymentRequestLabel(), QStringLiteral("Alice"));
        QCOMPARE(recipient->message(), QStringLiteral("Exact"));
        m_wallet->setCustomFeeRate("10"); m_wallet->scheduleFeeEstimates(); QTRY_VERIFY(!m_wallet->feeEstimatePending());
        QCOMPARE(recipient->amount()->satoshi(), requested); QVERIFY(!m_wallet->sendDraftSweepsWallet());
        QCOMPARE(qmltest::Rpc(m_app, "getrawmempool").size(), size_t{0});
    }

    void typedExplicitImportPreservesPrivateNoteAndPublicMetadata() {
        auto* recipient = m_wallet->sendRecipientList()->currentRecipient();
        recipient->editPrivateNote(QStringLiteral("Private bookkeeping"));
        const QString uri = "bitcoin:" + m_address + "?amount=0.01234567&label=Public%20invoice&message=Merchant%20message";
        auto* page = qmltest::Find(m_app, "walletSendPage"); QVERIFY(page);
        QVERIFY(Imports(page)); Imports(page)->importText(uri, SendImportModel::Source::ManualEntry);
        auto* source = Imports(page)->operation(); QVERIFY(source);
        QVERIFY2(QTest::qWaitFor([&] { return recipient->amount()->satoshi() == 1'234'567; }, 5000), qPrintable(importDiagnostic(source)));
        QCOMPARE(recipient->label(), QStringLiteral("Private bookkeeping"));
        QCOMPARE(recipient->paymentRequestLabel(), QStringLiteral("Public invoice"));
        QCOMPARE(recipient->message(), QStringLiteral("Merchant message"));
        QCOMPARE(qmltest::Rpc(m_app, "getrawmempool").size(), 0U);
    }

    // D312: an imported request's public label/message must not replace the address book label when the payment is sent.
    void importedRequestTextLeavesTheExistingAddressBookLabelAfterSend() {
        QVERIFY(qmltest::FundWallet(m_app, *m_wallet));
        qmltest::Rpc(m_app, "setlabel", qmltest::Json("[\"" + m_address.toStdString() + "\",\"Alice\"]"), m_wallet->name());
        auto* recipient = m_wallet->sendRecipientList()->currentRecipient(); QVERIFY(recipient);
        const QString uri = "bitcoin:" + m_address + "?amount=0.001&label=Shop&message=Invoice%2042";
        auto* page = qmltest::Find(m_app, "walletSendPage"); QVERIFY(page);
        QVERIFY(Imports(page)); Imports(page)->importText(uri, SendImportModel::Source::ManualEntry);
        auto* source = Imports(page)->operation(); QVERIFY(source);
        QVERIFY2(QTest::qWaitFor([&] { return recipient->amount()->satoshi() == 100'000 && recipient->hasPaymentRequest(); }, 5000), qPrintable(importDiagnostic(source)));
        QCOMPARE(recipient->paymentRequestLabel(), QStringLiteral("Shop")); QCOMPARE(recipient->message(), QStringLiteral("Invoice 42"));
        m_wallet->setCustomFeeEnabled(true); m_wallet->setCustomFeeRate("1");
        QTRY_VERIFY(!m_wallet->feeEstimatePending()); QVERIFY(AwaitPreparation(*m_wallet)); QVERIFY(AwaitCommittedSend(*m_wallet));
        QTRY_VERIFY(!m_wallet->recipientLabelsPending()); QVERIFY2(m_wallet->recipientLabelsError().isEmpty(), qPrintable(m_wallet->recipientLabelsError()));
        const auto info = qmltest::Rpc(m_app, "getaddressinfo", qmltest::Json("[\"" + m_address.toStdString() + "\"]"), m_wallet->name());
        QCOMPARE(QString::fromStdString(info.find_value("labels")[0].get_str()), QStringLiteral("Alice"));
    }

    void typedPsbtFileCreatesOriginalReviewWithoutSigningOrSubmission_data() {
        QTest::addColumn<QString>("filename");
        QTest::newRow("psbt-extension") << "original.psbt";
        QTest::newRow("text-extension") << "original.txt";
        QTest::newRow("no-extension") << "original";
    }
    void typedPsbtFileCreatesOriginalReviewWithoutSigningOrSubmission() {
        QFETCH(QString, filename);
        QVERIFY(qmltest::FundWallet(m_app, *m_wallet));
        const auto encoded = qmltest::Rpc(m_app, "walletcreatefundedpsbt",
            qmltest::Json("[[],[{\"" + m_address.toStdString() + "\":0.005}],0,{\"fee_rate\":2}]"), m_wallet->name()).find_value("psbt").get_str();
        QTemporaryDir directory; QVERIFY(directory.isValid()); const auto path = directory.filePath(filename);
        { QFile file{path}; QVERIFY(file.open(QIODevice::WriteOnly)); const auto bytes = QByteArray::fromStdString(encoded); QCOMPARE(file.write(bytes), bytes.size()); }
        auto* page = qmltest::Find(m_app, "walletSendPage"); QVERIFY(page);
        QVERIFY(Imports(page)); Imports(page)->importFile(path);
        QTRY_VERIFY(m_wallet->currentTransaction()); QVERIFY(m_wallet->currentTransactionIsImportedPsbt());
        QVERIFY(m_wallet->importedReviewOwner() > 0); QVERIFY(m_wallet->importedReviewChild() > 0);
        QVERIFY(m_wallet->currentTransaction()->feeKnown());
        const auto document = m_wallet->currentTransaction()->assessedPsbt(); QVERIFY(document);
        QVERIFY(qmltest::Call(m_app, [document](interfaces::Node*) {
            return std::all_of(document->inputs.begin(), document->inputs.end(), [](const auto& input) {
                return input.partial_sigs.empty() && input.m_tap_key_sig.empty() && input.m_tap_script_sigs.empty() && input.m_musig2_partial_sigs.empty() &&
                    input.final_script_sig.empty() && input.final_script_witness.IsNull();
            });
        }));
        const auto view = m_wallet->currentTransaction();
        auto* review_popup = qmltest::Find(m_app, "transactionReviewPopup"); QVERIFY(review_popup);
        QTRY_VERIFY(review_popup->property("opened").toBool());
        QCOMPARE(m_wallet->currentTransaction(), view);
        QCOMPARE(qmltest::Rpc(m_app, "getrawmempool").size(), 0U);
    }

    void clipboardPasteRequiresConfirmationBeforeOverwriting()
    {
        QVERIFY(qmltest::SetText(m_app, "sendNoteInput", "My private note"));
        auto* recipient = m_wallet->sendRecipientList()->currentRecipient();
        recipient->amount()->setSatoshi(10'000); // The explicit amount change needs consent; the private note is independent.
        const QString uri = "bitcoin:" + m_address + "?amount=0.01234567&label=Alice&message=Lunch";
        QGuiApplication::clipboard()->setText(uri);
        QCOMPARE(recipient->label(), QStringLiteral("My private note"));
        QVERIFY(qmltest::Click(m_app, "sendAddressInput"));
        auto* field = qmltest::Find(m_app, "sendAddressInput");
        QVERIFY(QMetaObject::invokeMethod(field, "forceActiveFocus"));
        QVERIFY(QMetaObject::invokeMethod(field, "paste"));
        auto* popup = qmltest::Find(m_app, "sendPaymentUriOverwritePopup");
        QVERIFY(popup);
        QTRY_VERIFY(popup->property("opened").toBool());
        QCOMPARE(recipient->label(), QStringLiteral("My private note"));
        QVERIFY(qmltest::Click(m_app, "sendPaymentUriOverwriteCancelButton"));
        QTRY_VERIFY(!popup->property("opened").toBool());
        QCOMPARE(recipient->label(), QStringLiteral("My private note"));
        QVERIFY(QMetaObject::invokeMethod(field, "forceActiveFocus"));
        QVERIFY(QMetaObject::invokeMethod(field, "paste"));
        QTRY_VERIFY(popup->property("opened").toBool());
        QVERIFY(qmltest::Click(m_app, "sendPaymentUriOverwriteConfirmButton"));
        QTRY_VERIFY(!popup->property("opened").toBool());
        auto* source = Imports(qmltest::Find(m_app, "walletSendPage"))->operation();
        QVERIFY2(QTest::qWaitFor([&] { return recipient->amount()->satoshi() == 1'234'567; }, 5000), qPrintable(importDiagnostic(source)));
        QCOMPARE(recipient->paymentRequestLabel(), QStringLiteral("Alice"));
        QCOMPARE(recipient->message(), QStringLiteral("Lunch"));
        QCOMPARE(recipient->label(), QStringLiteral("My private note"));
        QCOMPARE(qmltest::Rpc(m_app, "getrawmempool").size(), 0U);
    }

    void openUriWhileConfirmPasteIsOpen_data()
    {
        QTest::addColumn<bool>("accept");
        QTest::newRow("accepted-uri-retires-the-paste") << true;
        QTest::newRow("cancelled-uri-keeps-the-paste") << false;
    }
    // #1067: accepting File > Open URI while Confirm paste? is open closes that
    // stale confirmation and clears the pending paste; cancelling keeps both.
    void openUriWhileConfirmPasteIsOpen()
    {
        QFETCH(bool, accept);
        auto* recipient = m_wallet->sendRecipientList()->currentRecipient();
        recipient->setAddress(m_address); recipient->amount()->setSatoshi(10'000'000);
        const auto older = qmltest::NewAddress(m_app, m_wallet->name()), newer = qmltest::NewAddress(m_app, m_wallet->name());
        QGuiApplication::clipboard()->setText("bitcoin:" + older + "?amount=0.2&label=older-paste");
        QVERIFY(qmltest::Click(m_app, "sendAddressInput"));
        auto* field = qmltest::Find(m_app, "sendAddressInput"); QVERIFY(field);
        QVERIFY(QMetaObject::invokeMethod(field, "forceActiveFocus")); QTest::keyClick(m_app.window(), Qt::Key_V, Qt::ControlModifier);
        auto* confirm = qmltest::Find(m_app, "sendPaymentUriOverwritePopup"); QVERIFY(confirm);
        QVERIFY2(QTest::qWaitFor([&] { return confirm->property("opened").toBool(); }, 5000), qPrintable(importDiagnostic(nullptr)));
        auto* page = qmltest::Find(m_app, "walletSendPage"); QVERIFY(page);
        QPointer<QObject> pasted{Imports(page)->operation()}; QVERIFY(pasted);
        QTRY_VERIFY(pasted->property("promptToken").toULongLong() != 0);
        QSignalSpy closed{confirm, SIGNAL(aboutToHide())};
        auto* actions = qmltest::Find(m_app, "desktopMenuActions"); QVERIFY(actions);
        auto* open_uri = actions->property("openUri").value<QObject*>(); QVERIFY(open_uri); QVERIFY(open_uri->property("enabled").toBool());
        QVERIFY(QMetaObject::invokeMethod(open_uri, "trigger"));
        auto* dialog = qmltest::WaitForVisible(m_app, "sendUriImportPopup"); QVERIFY(dialog); QTRY_VERIFY(dialog->property("opened").toBool());
        if (!accept) {
            QVERIFY(qmltest::SetText(m_app, "sendUriImportInput", "bitcoin:" + newer + "?amount=0.3&label=newer-import"));
            QTest::keyClick(m_app.window(), Qt::Key_Escape); QTRY_VERIFY(!dialog->property("visible").toBool());
            QCOMPARE(closed.count(), 0); QVERIFY(confirm->property("opened").toBool());
            QCOMPARE(static_cast<QObject*>(Imports(page)->operation()), pasted.data());
            QVERIFY(qmltest::Click(m_app, "sendPaymentUriOverwriteConfirmButton"));
            QTRY_COMPARE(recipient->amount()->satoshi(), 20'000'000);
            QCOMPARE(recipient->address()->address(), older); QCOMPARE(recipient->paymentRequestLabel(), QStringLiteral("older-paste"));
            QCOMPARE(qmltest::Rpc(m_app, "getrawmempool").size(), 0U);
            return;
        }
        QVERIFY(qmltest::SetText(m_app, "sendUriImportInput", "bitcoin:" + newer + "?amount=0.3&label=newer-import"));
        QVERIFY(qmltest::Click(m_app, "sendUriImportApplyButton"));
        QVERIFY2(QTest::qWaitFor([&] { return Imports(page)->operation() != pasted.data(); }, 5000), qPrintable(importDiagnostic(pasted)));
        QCOMPARE(closed.count(), 1);
        QVERIFY(!pasted || !pasted->property("pending").toBool());
        // The newer request asks for its own consent before replacing the draft.
        QTRY_VERIFY(confirm->property("opened").toBool());
        QVERIFY(qmltest::Click(m_app, "sendPaymentUriOverwriteConfirmButton"));
        QTRY_COMPARE(recipient->amount()->satoshi(), 30'000'000);
        QCOMPARE(recipient->address()->address(), newer); QCOMPARE(recipient->paymentRequestLabel(), QStringLiteral("newer-import"));
        QTRY_VERIFY(!confirm->property("opened").toBool());
        // The URI action settles after the import commits, not with the dialog.
        QTRY_VERIFY(!Imports(page)->pending());
        QCOMPARE(field->property("text").toString().remove(QLatin1Char(' ')), newer);
        QCOMPARE(qmltest::Rpc(m_app, "getrawmempool").size(), 0U);
    }

    // RC20: a request opened behind the completion page is a newer draft than
    // the sent one, so closing the completion page must not clear it. The
    // wallet cleared the sent draft at commit, so the import fills an empty
    // draft without asking to replace it.
    void uriImportedBehindTheCompletionPageSurvivesDone_data()
    {
        QTest::addColumn<bool>("import");
        QTest::newRow("sent-draft-is-cleared") << false;
        QTest::newRow("imported-draft-survives") << true;
    }
    void uriImportedBehindTheCompletionPageSurvivesDone()
    {
        QFETCH(bool, import);
        QVERIFY(qmltest::FundWallet(m_app, *m_wallet));
        QVERIFY(qmltest::SetText(m_app, "sendAddressInput", m_address));
        QVERIFY(qmltest::SetText(m_app, "sendAmountInput", "0.01"));
        QTRY_VERIFY(!m_wallet->sendRecipientList()->validationPending() && m_wallet->sendRecipientList()->allValid());
        QTRY_VERIFY(!m_wallet->feeEstimatePending() && !m_wallet->fees()->policyPending());
        QVERIFY(qmltest::Click(m_app, "sendReviewButton")); QVERIFY(qmltest::WaitForVisible(m_app, "sendTransactionReviewPage"));
        QVERIFY(qmltest::Click(m_app, "sendTransactionReviewSendButton"));
        QVERIFY(qmltest::WaitForVisible(m_app, "sendCompletePage"));
        if (!import) {
            QVERIFY(qmltest::Click(m_app, "sendResultDoneButton"));
            QTRY_COMPARE(m_wallet->sendRecipientList()->currentRecipient()->amount()->satoshi(), 0);
            QVERIFY(m_wallet->sendRecipientList()->currentRecipient()->address()->address().isEmpty());
            return;
        }
        const auto newer = qmltest::NewAddress(m_app, m_wallet->name());
        auto* actions = qmltest::Find(m_app, "desktopMenuActions"); QVERIFY(actions);
        auto* open_uri = actions->property("openUri").value<QObject*>(); QVERIFY(open_uri); QVERIFY(open_uri->property("enabled").toBool());
        QVERIFY(QMetaObject::invokeMethod(open_uri, "trigger"));
        auto* dialog = qmltest::WaitForVisible(m_app, "sendUriImportPopup"); QVERIFY(dialog); QTRY_VERIFY(dialog->property("opened").toBool());
        QVERIFY(qmltest::SetText(m_app, "sendUriImportInput", "bitcoin:" + newer + "?amount=0.3&label=newer-import"));
        QVERIFY(qmltest::Click(m_app, "sendUriImportApplyButton"));
        auto* recipient = m_wallet->sendRecipientList()->currentRecipient(); QVERIFY(recipient);
        QTRY_COMPARE(recipient->amount()->satoshi(), 30'000'000); QCOMPARE(recipient->address()->address(), newer);
        auto* confirm = qmltest::Find(m_app, "sendPaymentUriOverwritePopup");
        QVERIFY(!confirm || !confirm->property("opened").toBool());
        QVERIFY(qmltest::Click(m_app, "sendResultDoneButton"));
        QTRY_VERIFY(!qmltest::Find(m_app, "transactionReviewPopup")->property("visible").toBool());
        recipient = m_wallet->sendRecipientList()->currentRecipient(); QVERIFY(recipient);
        QCOMPARE(recipient->address()->address(), newer); QCOMPARE(recipient->amount()->satoshi(), 30'000'000);
        QCOMPARE(recipient->paymentRequestLabel(), QStringLiteral("newer-import"));
    }

    void manualImportRejectsMalformedUriWithoutChangingTheDraft()
    {
        auto* recipient = m_wallet->sendRecipientList()->currentRecipient();
        recipient->setAddress(m_address);
        recipient->amount()->setSatoshi(10'000);
        QVERIFY(qmltest::Click(m_app, "sendOptionsButton"));
        QVERIFY(qmltest::Click(m_app, "sendOptionsOpenPaymentRequestButton"));
        QVERIFY(qmltest::SetText(m_app, "sendUriImportInput", "bitcoin://" + m_address + "?amount=1"));
        QVERIFY(qmltest::Click(m_app, "sendUriImportApplyButton"));
        auto* status = qmltest::WaitForVisible(m_app, "sendPaymentRequestStatusText");
        QVERIFY(status);
        QTRY_VERIFY(status->property("text").toString().contains("bitcoin://"));
        QCOMPARE(recipient->amount()->satoshi(), 10'000);
        QVERIFY(qmltest::Click(m_app, "sendOptionsButton"));
        QVERIFY(qmltest::Click(m_app, "sendOptionsOpenPaymentRequestButton"));
        QVERIFY(qmltest::SetText(m_app, "sendUriImportInput", "bitcoin:" + m_address + "?amount=0.02&label=manual"));
        QVERIFY(qmltest::Click(m_app, "sendUriImportApplyButton"));
        auto* replacement = qmltest::Find(m_app, "sendPaymentUriOverwritePopup"); QVERIFY(replacement);
        QTRY_VERIFY(replacement->property("opened").toBool()); QCOMPARE(recipient->amount()->satoshi(), 10'000);
        QVERIFY(qmltest::Click(m_app, "sendPaymentUriOverwriteConfirmButton"));
        QTRY_COMPARE(recipient->amount()->satoshi(), 2'000'000);
        QCOMPARE(recipient->paymentRequestLabel(), QStringLiteral("manual"));
    }

    void localFilesAndFileUrlsReachTheSendModel_data()
    {
        QTest::addColumn<bool>("file_url");
        QTest::newRow("path") << false;
        QTest::newRow("file-url") << true;
    }

    void localFilesAndFileUrlsReachTheSendModel()
    {
        QFETCH(bool, file_url);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QFile file{directory.filePath(QString::fromUtf8("payment request ü.txt"))};
        QVERIFY(file.open(QIODevice::WriteOnly));
        const auto uri = ("bitcoin:" + m_address + "?amount=0.03&label=file&message=invoice").toUtf8();
        QCOMPARE(file.write(uri), uri.size());
        file.close();
        const auto path = file_url ? QUrl::fromLocalFile(file.fileName()).toString() : file.fileName();
        auto* page = qmltest::Find(m_app, "walletSendPage");
        QVERIFY(Imports(page)); Imports(page)->importFile(path);
        auto* recipient = m_wallet->sendRecipientList()->currentRecipient();
        QTRY_COMPARE(recipient->amount()->satoshi(), 3'000'000);
        QCOMPARE(recipient->paymentRequestLabel(), QStringLiteral("file"));
        QCOMPARE(recipient->message(), QStringLiteral("invoice"));
        QCOMPARE(qmltest::Rpc(m_app, "getrawmempool").size(), 0U);
    }

    void nativePasteImportsUrisFromOtherRecipientFields_data()
    {
        QTest::addColumn<QString>("field");
        QTest::newRow("amount") << "sendAmountInput";
        QTest::newRow("private-note") << "sendNoteInput";
    }

    void nativePasteImportsUrisFromOtherRecipientFields()
    {
        QFETCH(QString, field);
        QGuiApplication::clipboard()->setText("bitcoin:" + m_address + "?amount=0.07&label=field-paste");
        QVERIFY(qmltest::Click(m_app, qPrintable(field)));
        auto* input = qmltest::Find(m_app, qPrintable(field));
        QVERIFY(input);
        QVERIFY(QMetaObject::invokeMethod(input, "forceActiveFocus"));
        QVERIFY(QMetaObject::invokeMethod(input, "paste"));
        auto* recipient = m_wallet->sendRecipientList()->currentRecipient();
        QTRY_COMPARE(recipient->amount()->satoshi(), 7'000'000);
        QCOMPARE(recipient->paymentRequestLabel(), QStringLiteral("field-paste"));
        QCOMPARE(qmltest::Rpc(m_app, "getrawmempool").size(), 0U);
    }

    void ordinaryPasteIntoThePrivateNoteKeepsNormalEditing()
    {
        QVERIFY(qmltest::SetText(m_app, "sendNoteInput", "Private note"));
        QGuiApplication::clipboard()->setText("-ordinary-paste");
        auto* input = qmltest::Find(m_app, "sendNoteInput");
        QVERIFY(input);
        QVERIFY(QMetaObject::invokeMethod(input, "paste"));
        QTRY_COMPARE(input->property("text").toString(), QStringLiteral("Private note-ordinary-paste"));
        QVERIFY(m_wallet->sendRecipientList()->currentRecipient()->paymentRequestLabel().isEmpty());
    }

    void lockedWalletImportsOnlyLocalDraftValues() {
        QVERIFY(m_wallet->encryptWallet(QStringLiteral("local-import-only")));
        QTRY_COMPARE_WITH_TIMEOUT(m_wallet->securityState(), WalletModel::SecurityState::SecuritySucceeded, qmltest::kSecurityStateTimeoutMs);
        QTRY_VERIFY(m_wallet->isLocked());
        auto* recipient = m_wallet->sendRecipientList()->currentRecipient();
        QVERIFY(recipient->editPrivateNote(QStringLiteral("Private locked note")));
        QGuiApplication::clipboard()->setText("bitcoin:" + m_address + "?amount=0.007&label=Public&message=Locked%20draft");
        auto* input = qmltest::Find(m_app, "sendAddressInput"); QVERIFY(input);
        QVERIFY(QMetaObject::invokeMethod(input, "forceActiveFocus")); QVERIFY(QMetaObject::invokeMethod(input, "paste"));
        auto* source = Imports(qmltest::Find(m_app, "walletSendPage"))->operation();
        QVERIFY2(QTest::qWaitFor([&] { return recipient->amount()->satoshi() == 700'000; }, 5000), qPrintable(importDiagnostic(source)));
        QCOMPARE(recipient->paymentRequestLabel(), QStringLiteral("Public")); QCOMPARE(recipient->message(), QStringLiteral("Locked draft"));
        QCOMPARE(recipient->label(), QStringLiteral("Private locked note")); QTRY_VERIFY(m_wallet->isLocked());
        QCOMPARE(qmltest::Rpc(m_app, "getwalletinfo", {}, m_wallet->name()).find_value("unlocked_until").getInt<int64_t>(), 0);
        QVERIFY(!m_wallet->currentTransaction()); QCOMPARE(qmltest::Rpc(m_app, "getrawmempool").size(), 0U);
    }

    void clipboardOfferComparesPublicMessageIndependentlyOfPrivateNote() {
        auto* recipient = m_wallet->sendRecipientList()->currentRecipient();
        const QString original = "bitcoin:" + m_address + "?amount=0.007&label=Public&message=Original";
        auto* page = qmltest::Find(m_app, "walletSendPage"); QVERIFY(page);
        QVERIFY(Imports(page)); Imports(page)->importText(original, SendImportModel::Source::ManualEntry);
        QTRY_COMPARE(recipient->message(), QStringLiteral("Original"));
        QVERIFY(recipient->editPrivateNote(QStringLiteral("Private bookkeeping")));
        const QString reordered = "BITCOIN:" + m_address + "?message=%4Friginal&label=Public&amount=0.00700000";
        QGuiApplication::clipboard()->setText(reordered);
        QTRY_VERIFY(Imports(page)->clipboardCheck());
        QTRY_VERIFY(!Imports(page)->clipboardCheck()->property("pending").toBool());
        auto* button = qmltest::Find(m_app, "clipboardUriPasteButton"); QVERIFY(button);
        QVERIFY(!button->property("visible").toBool());
        QGuiApplication::clipboard()->setText("bitcoin:" + m_address + "?amount=0.007&label=Public&message=Changed");
        QTRY_VERIFY(button->property("visible").toBool()); QVERIFY(qmltest::Click(m_app, "clipboardUriPasteButton"));
        QTRY_COMPARE(recipient->message(), QStringLiteral("Changed"));
        QCOMPARE(recipient->label(), QStringLiteral("Private bookkeeping")); QCOMPARE(recipient->amount()->satoshi(), 700'000);
        QVERIFY(!m_wallet->currentTransaction()); QCOMPARE(qmltest::Rpc(m_app, "getrawmempool").size(), 0U);
    }

    void droppedTextAndUrlsReachTheProductionDropArea_data()
    {
        QTest::addColumn<QString>("kind");
        QTest::newRow("text") << "text";
        QTest::newRow("bitcoin-url") << "bitcoin-url";
        QTest::newRow("file-url") << "file-url";
        QTest::newRow("multiple-urls") << "multiple-urls";
    }

    void droppedTextAndUrlsReachTheProductionDropArea()
    {
        QFETCH(QString, kind);
        const QString uri = "bitcoin:" + m_address + "?amount=0.04&label=dropped";
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QMimeData mime;
        if (kind == "text") {
            mime.setText(uri);
        } else if (kind == "bitcoin-url" || kind == "multiple-urls") {
            auto urls = QList<QUrl>{QUrl{uri}};
            if (kind == "multiple-urls") urls.push_back(QUrl{uri + "-second"});
            mime.setUrls(urls);
        } else {
            QFile file{directory.filePath(QString::fromUtf8("dropped payment ü.txt"))};
            QVERIFY(file.open(QIODevice::WriteOnly));
            const auto bytes = uri.toUtf8();
            QCOMPARE(file.write(bytes), bytes.size());
            file.close();
            mime.setUrls({QUrl::fromLocalFile(file.fileName())});
        }
        auto* target = qobject_cast<QQuickItem*>(qmltest::WaitForVisible(m_app, "sendDropArea"));
        QVERIFY(target);
        QVERIFY(qmltest::WaitForPolish(target));
        const auto position = target->mapToScene(target->boundingRect().center());
        QDragEnterEvent enter{position.toPoint(), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier};
        QCoreApplication::sendEvent(m_app.window(), &enter);
        QVERIFY(enter.isAccepted());
        QDropEvent drop{position, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier};
        QCoreApplication::sendEvent(m_app.window(), &drop);
        auto* recipient = m_wallet->sendRecipientList()->currentRecipient();
        if (kind == "multiple-urls") {
            auto* status = qmltest::WaitForVisible(m_app, "sendPaymentRequestStatusText"); QVERIFY(status);
            QTRY_COMPARE(status->property("text").toString(), QStringLiteral("Drop one payment request or PSBT at a time."));
            QCOMPARE(recipient->amount()->satoshi(), 0); QVERIFY(!recipient->hasPaymentRequest());
        } else {
            QTRY_COMPARE(recipient->amount()->satoshi(), 4'000'000);
            QCOMPARE(recipient->paymentRequestLabel(), QStringLiteral("dropped"));
        }
        QCOMPARE(qmltest::Rpc(m_app, "getrawmempool").size(), 0U);
    }
};

std::unique_ptr<QObject> CreatePaymentImportTests(BitcoinQmlApplication& app, WalletMode)
{
    return std::make_unique<PaymentImportIntegrationTests>(app);
}

#include <test_payment_import.moc>
