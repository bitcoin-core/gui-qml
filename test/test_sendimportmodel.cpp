// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/sendimportmodel.h>
#include <test/qt_test_registry.h>

#include <QPointer>
#include <QSignalSpy>
#include <QtTest/QtTest>

#include <vector>

namespace {
class FakeOperation : public ImportOperation
{
public:
    using ImportOperation::ImportOperation;
    bool pending() const override { return is_pending; }
    bool applied() const override { return is_applied; }
    bool reviewOpened() const override { return is_review; }
    bool offersPayment() const override { return offers; }
    quint64 promptToken() const override { return prompt; }
    QString resultToken() const override { return result; }
    QString error() const override { return failure; }
    QString message() const override { return note; }
    void use() override { ++uses; }
    void replace(quint64 token) override { if (token == prompt) { replaced = true; prompt = 0; is_applied = true; Q_EMIT changed(); } }
    bool requestFile(const QUrl& url) override { file = url; return true; }
    void cancel() override { cancelled = true; Q_EMIT changed(); }
    void publish() { Q_EMIT changed(); }

    bool is_pending{false}, is_applied{false}, is_review{false}, offers{false}, replaced{false}, cancelled{false};
    quint64 prompt{0};
    QString result, failure, note;
    QUrl file;
    int uses{0};
};

class FakeTarget : public ImportTarget
{
public:
    using ImportTarget::ImportTarget;
    bool available() const override { return is_available; }
    QString draftRevision() const override { return QString::number(revision); }
    bool importPending() const override { return pending_import; }
    bool recipientAddressEmpty() const override { return address_empty; }
    QString importedReviewToken() const override { return review_token; }
    ImportOperation* importText(const QString& text, QObject* owner) override { texts.push_back(text); return make(owner); }
    ImportOperation* importPaste(const QString& text, int field, QObject*, QObject* owner) override
    {
        pastes.push_back(text);
        last_field = field;
        return make(owner);
    }
    ImportOperation* importFile(const QString& path, QObject* owner) override { files.push_back(path); return make(owner); }
    ImportOperation* importDrop(int, const QUrl&, const QString& text, QObject* owner) override { texts.push_back(text); return make(owner); }
    ImportOperation* captureFile(QObject* owner) override { return make(owner); }
    bool adopt(ImportOperation* operation) override { adopted = operation; return is_available; }

    FakeOperation* make(QObject* owner)
    {
        if (!services) return nullptr;
        auto* operation = new FakeOperation(owner);
        created.emplace_back(operation);
        return operation;
    }
    FakeOperation* last() const { return created.empty() ? nullptr : created.back().data(); }
    void setAvailable(bool value) { is_available = value; Q_EMIT availableChanged(); }
    void setPending(bool value) { pending_import = value; Q_EMIT importPendingChanged(); }

    bool is_available{true}, pending_import{false}, address_empty{false}, services{true};
    int revision{0}, last_field{-1};
    QString review_token;
    QPointer<ImportOperation> adopted;
    std::vector<QString> texts, pastes, files;
    std::vector<QPointer<FakeOperation>> created;
};

struct Fixture {
    FakeTarget target;
    SendImportModel model;
    Fixture()
    {
        model.setTarget(&target);
        model.setActive(true);
    }
};
} // namespace

class SendImportModelTests : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void pasteStartsOneImportAndReportsItsSource()
    {
        Fixture f;
        f.model.importPaste(1, QStringLiteral("bitcoin:addr?amount=1"));
        auto* operation = f.target.last();
        QVERIFY(operation);
        QCOMPARE(f.target.last_field, 1);
        QCOMPARE(operation->uses, 1);
        QCOMPARE(f.model.operation(), operation);
        operation->is_pending = true; operation->publish();
        QVERIFY(f.model.pending());
        QCOMPARE(f.model.status(), QString{});
        operation->is_pending = false; operation->is_applied = true; operation->note = QStringLiteral("Pay me"); operation->publish();
        QCOMPARE(f.model.status(), QStringLiteral("Payment request imported from clipboard"));
        QVERIFY(!f.model.statusIsError());
        QCOMPARE(f.model.message(), QStringLiteral("Pay me"));
    }

    void anImportFailureIsTheStatus()
    {
        Fixture f;
        f.model.importText(QStringLiteral("not a request"), SendImportModel::Source::ManualEntry);
        f.target.last()->failure = QStringLiteral("Not a valid Bitcoin payment URI.");
        f.target.last()->publish();
        QCOMPARE(f.model.status(), QStringLiteral("Not a valid Bitcoin payment URI."));
        QVERIFY(f.model.statusIsError());
    }

    void unavailableServicesAreReportedAndClearedByTheNextRecipient()
    {
        Fixture f;
        f.target.services = false;
        f.model.importText(QStringLiteral("bitcoin:addr"), SendImportModel::Source::ManualEntry);
        QCOMPARE(f.model.status(), QStringLiteral("Payment URI services are unavailable. Try again."));
        QVERIFY(f.model.statusIsError());
        Q_EMIT f.target.currentRecipientChanged();
        QCOMPARE(f.model.status(), QString{});
    }

    void aNewImportCancelsTheCurrentOne()
    {
        Fixture f;
        f.model.importText(QStringLiteral("bitcoin:first"), SendImportModel::Source::ManualEntry);
        QPointer<FakeOperation> first = f.target.last();
        f.model.importFile(QStringLiteral("/tmp/request.txt"));
        QVERIFY(first->cancelled);
        QCOMPARE(f.model.operation(), f.target.last());
        QVERIFY(!f.target.last()->cancelled);
    }

    void aDraftEditCancelsUnlessTheImportIsWriting()
    {
        Fixture f;
        f.model.importText(QStringLiteral("bitcoin:addr"), SendImportModel::Source::ManualEntry);
        QPointer<FakeOperation> writing = f.target.last();
        f.target.pending_import = true;
        f.model.draftChanged();
        QVERIFY(!writing->cancelled);
        QCOMPARE(f.model.operation(), writing.data());
        f.target.pending_import = false;
        f.model.draftChanged();
        QVERIFY(writing->cancelled);
        QCOMPARE(f.model.operation(), nullptr);
    }

    void hidingCancelsAndShowingChecksTheClipboardAgain()
    {
        Fixture f;
        f.model.setClipboardText(QStringLiteral("bitcoin:offered"));
        QPointer<FakeOperation> check = f.target.last();
        QVERIFY(check);
        QCOMPARE(f.model.clipboardCheck(), check.data());
        f.model.importText(QStringLiteral("bitcoin:addr"), SendImportModel::Source::ManualEntry);
        QPointer<FakeOperation> current = f.target.last();
        f.model.setActive(false);
        QVERIFY(check->cancelled);
        QVERIFY(current->cancelled);
        QCOMPARE(f.model.clipboardCheck(), nullptr);
        const auto checks = f.target.texts.size();
        f.model.setActive(true);
        QCOMPARE(f.target.texts.size(), checks + 1);
        QCOMPARE(f.target.texts.back(), QStringLiteral("bitcoin:offered"));
    }

    void theClipboardIsOfferedOnlyWhileItsCheckOffersAPayment()
    {
        Fixture f;
        f.model.setClipboardText(QStringLiteral("bitcoin:offered"));
        auto* check = f.target.last();
        QVERIFY(!f.model.clipboardOffered());
        check->offers = true; check->publish();
        QVERIFY(f.model.clipboardOffered());
        QCOMPARE(f.model.offeredText(), QStringLiteral("bitcoin:offered"));
        // A recipient edit checks again; the old check no longer counts.
        Q_EMIT f.target.recipientEdited();
        QVERIFY(check->cancelled);
        QVERIFY(!f.model.clipboardOffered());
        f.target.setAvailable(false);
        QVERIFY(!f.model.clipboardOffered());
        // No check while the target is unavailable or an import is writing.
        const auto checks = f.target.texts.size();
        f.model.setClipboardText(QStringLiteral("bitcoin:other"));
        QCOMPARE(f.target.texts.size(), checks);
    }

    void aRecipientEditOfAnEmptyAddressClearsTheServiceError()
    {
        Fixture f;
        f.target.services = false;
        f.model.importText(QStringLiteral("bitcoin:addr"), SendImportModel::Source::ManualEntry);
        QVERIFY(f.model.statusIsError());
        Q_EMIT f.target.recipientEdited();
        QVERIFY(f.model.statusIsError());
        f.target.address_empty = true;
        Q_EMIT f.target.recipientEdited();
        QVERIFY(!f.model.statusIsError());
    }

    void theReplacementPromptIsAnsweredOnce()
    {
        Fixture f;
        f.model.importPaste(0, QStringLiteral("bitcoin:addr"));
        auto* operation = f.target.last();
        operation->prompt = 7; operation->publish();
        QVERIFY(f.model.confirmationRequired());
        f.model.replace();
        QVERIFY(operation->replaced);
        QVERIFY(!f.model.confirmationRequired());
        f.model.replace();
        f.model.importPaste(0, QStringLiteral("bitcoin:other"));
        f.target.last()->prompt = 8; f.target.last()->publish();
        f.model.cancel();
        QVERIFY(!f.model.confirmationRequired());
        QCOMPARE(f.model.operation(), nullptr);
    }

    void anImportedReviewIsReadyOncePerResult()
    {
        Fixture f;
        QSignalSpy ready{&f.model, &SendImportModel::reviewReady};
        QVERIFY(f.model.beginFileChoice());
        auto* operation = f.target.last();
        f.model.chooseFile(operation, QUrl::fromLocalFile(QStringLiteral("/tmp/tx.psbt")));
        QCOMPARE(operation->file, QUrl::fromLocalFile(QStringLiteral("/tmp/tx.psbt")));
        QCOMPARE(operation->uses, 1);
        operation->is_review = true; operation->result = QStringLiteral("4:4"); operation->publish();
        QCOMPARE(ready.count(), 0); // The wallet is not reviewing this result yet.
        f.target.review_token = QStringLiteral("4:4");
        Q_EMIT f.target.importedReviewChanged();
        QCOMPARE(ready.count(), 1);
        operation->publish();
        Q_EMIT f.target.importedReviewChanged();
        QCOMPARE(ready.count(), 1);
    }

    void aStaleFileChoiceIsIgnored()
    {
        Fixture f;
        QVERIFY(f.model.beginFileChoice());
        QPointer<FakeOperation> stale = f.target.last();
        QVERIFY(f.model.beginFileChoice());
        f.model.chooseFile(stale, QUrl::fromLocalFile(QStringLiteral("/tmp/tx.psbt")));
        QVERIFY(stale->file.isEmpty());
        QCOMPARE(stale->uses, 0);
    }

    void anImportFromElsewhereNeedsTheSameContext()
    {
        Fixture f;
        auto* accepted = new FakeOperation;
        QVERIFY(f.model.canAccept());
        QVERIFY(f.model.acceptImport(accepted, f.model.context()));
        QCOMPARE(f.target.adopted.data(), accepted);
        QCOMPARE(accepted->parent(), &f.model);
        QCOMPARE(accepted->uses, 1);

        auto* moved = new FakeOperation{&f.model};
        const auto context = f.model.context();
        Q_EMIT f.target.currentRecipientChanged();
        QVERIFY(!f.model.acceptImport(moved, context));
        QVERIFY(moved->cancelled);
        QCOMPARE(moved->uses, 0);

        f.target.setPending(true);
        QVERIFY(!f.model.canAccept());
    }

    void anotherTargetCancelsEverything()
    {
        Fixture f;
        f.model.setClipboardText(QStringLiteral("bitcoin:offered"));
        QPointer<FakeOperation> check = f.target.last();
        f.model.importText(QStringLiteral("bitcoin:addr"), SendImportModel::Source::ManualEntry);
        QPointer<FakeOperation> current = f.target.last();
        FakeTarget other;
        f.model.setTarget(&other);
        QVERIFY(check->cancelled);
        QVERIFY(current->cancelled);
        QCOMPARE(other.texts.size(), size_t{1}); // The clipboard is checked against the new draft.
    }
};

BITCOINQML_REGISTER_QT_TEST(SendImportModelTests)
#include <test_sendimportmodel.moc>
