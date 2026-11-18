// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/integration/runner/test_cases.h>
#include <test/integration/fixture/test_wallet.h>

#include <QClipboard>
#include <QFile>
#include <QGuiApplication>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QTemporaryDir>
#include <QUrl>

class PaymentImportIntegrationTests : public QObject
{
    Q_OBJECT
    BitcoinQmlApplication& m_app;
    WalletQmlModel* m_wallet{nullptr};
    QString m_address;

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

    void clipboardPasteRequiresConfirmationBeforeOverwriting()
    {
        QVERIFY(qmltest::SetText(m_app, "sendNoteInput", "My private note"));
        const auto uri = "bitcoin:" + m_address + "?amount=0.01234567&label=Alice&message=Lunch";
        QGuiApplication::clipboard()->setText(uri);
        auto* recipient = m_wallet->sendRecipientList()->currentRecipient();
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
        QCOMPARE(recipient->amount()->satoshi(), 1'234'567);
        QCOMPARE(recipient->paymentRequestLabel(), QStringLiteral("Alice"));
        QCOMPARE(recipient->message(), QStringLiteral("Lunch"));
        QCOMPARE(recipient->label(), QStringLiteral("Lunch - Alice"));
        QCOMPARE(qmltest::Rpc(m_app, "getrawmempool").size(), 0U);
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
        QVERIFY(QMetaObject::invokeMethod(page, "applyPaymentRequestFromFile", Q_ARG(QVariant, path)));
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

    void droppedTextAndUrlsReachTheProductionDropArea_data()
    {
        QTest::addColumn<QString>("kind");
        QTest::newRow("text") << "text";
        QTest::newRow("bitcoin-url") << "bitcoin-url";
        QTest::newRow("file-url") << "file-url";
    }

    void droppedTextAndUrlsReachTheProductionDropArea()
    {
        QFETCH(QString, kind);
        const auto uri = "bitcoin:" + m_address + "?amount=0.04&label=dropped";
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QMimeData mime;
        if (kind == "text") {
            mime.setText(uri);
        } else if (kind == "bitcoin-url") {
            mime.setUrls({QUrl{uri}});
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
        QTRY_COMPARE(recipient->amount()->satoshi(), 4'000'000);
        QCOMPARE(recipient->paymentRequestLabel(), QStringLiteral("dropped"));
        QCOMPARE(qmltest::Rpc(m_app, "getrawmempool").size(), 0U);
    }
};

std::unique_ptr<QObject> CreatePaymentImportTests(BitcoinQmlApplication& app, WalletMode)
{
    return std::make_unique<PaymentImportIntegrationTests>(app);
}

#include <test_payment_import.moc>
