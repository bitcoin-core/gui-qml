// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/mocks/messagebumptest.h>
#include <test/mocks/sendtest.h>
#include <test/mocks/preparationtest.h>
#include <test/mocks/receiverequesttest.h>
#include <test/integration/runner/test_cases.h>
#include <test/integration/fixture/test_wallet.h>
#include <qml/executor.h>
#include <qml/models/activityfilterproxymodel.h>
#include <qml/models/addresslistmodel.h>
#include <qml/walletrequests.h>

#include <common/args.h>
#include <key_io.h>
#include <util/string.h>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QThread>
#include <QSignalSpy>
#include <QLocale>
#include <QScopeGuard>
#include <QClipboard>
#include <QGuiApplication>
#include <QInputMethodEvent>
#include <QPersistentModelIndex>
#include <QQmlEngine>
#include <qml/models/texteditsessionmodel.h>
#include <qml/models/options_model.h>
#include <streams.h>
#include <core_io.h>
#include <rpc/util.h>
#include <sync.h>
#include <wallet/wallet.h>
#include <QElapsedTimer>
#include <atomic>
#include <future>
#include <stdexcept>
#include <thread>

class ReceiveIntegrationTests : public QObject
{
    Q_OBJECT
    BitcoinQmlApplication& m_app;

    static std::shared_ptr<walletopening::Session> makeOpening(execution::Executor& executor)
    {
        const auto core = std::make_shared<execution::SerialQueue>(executor.makeQueue(execution::Lane::Core));
        const auto utility = std::make_shared<execution::SerialQueue>(executor.makeQueue(execution::Lane::Utility));
        const auto cleanup = std::make_shared<execution::SerialQueue>(executor.makeQueue(execution::Lane::Utility));
        auto stop = [core, utility](auto done) {
            core->close();
            core->whenDrained([utility, done = std::move(done)] {
                utility->close();
                utility->whenDrained(done);
            });
        };
        return std::make_shared<walletopening::Session>(nullptr, nullptr, std::make_shared<std::atomic_bool>(false),
            [] {}, walletopening::Session::Preferences{}, std::chrono::steady_clock::now, nullptr,
            walletopening::NativeEvents{}, walletopening::Session::Queues{cleanup, std::move(stop), core, utility});
    }

    QObject* addressRow(const QString& address)
    {
        QList<QQuickItem*> pending{m_app.window()->contentItem()};
        while (!pending.isEmpty()) {
            auto* item = pending.takeLast();
            if (item->isVisible() && item->objectName() == "addressRow" && item->property("address").toString() == address) return item;
            pending.append(item->childItems());
        }
        return nullptr;
    }

    bool hasTransaction(WalletQmlModel& wallet, const QString& txid)
    {
        auto* activity = wallet.transactionActivityModel();
        for (int row{0}; row < activity->rowCount(); ++row) {
            if (activity->index(row, 0).data(TransactionActivityModel::TxidRole).toString() == txid) return true;
        }
        return false;
    }

    bool enterText(QObject* object, const QString& text)
    {
        auto* item = qobject_cast<QQuickItem*>(object);
        if (!item || !item->window()) return false;
        item->forceActiveFocus();
        if (!item->hasActiveFocus()) return false;
        if (!QMetaObject::invokeMethod(item, "selectAll")) return false;
        QTest::keyClick(item->window(), Qt::Key_Backspace, Qt::NoModifier, 1);
        QInputMethodEvent edit; edit.setCommitString(text);
        QCoreApplication::sendEvent(item, &edit); return edit.isAccepted();
    }
    QString addressNote(const QString& name, const QString& address)
    {
        UniValue parameters{UniValue::VARR}; parameters.push_back(address.toStdString());
        const auto labels = qmltest::Rpc(m_app, "getaddressinfo", parameters, name).find_value("labels");
        return labels.empty() ? QString{} : QString::fromStdString(labels[0].get_str());
    }
    bool openAddresses()
    {
        if (!qmltest::SelectSettings(m_app, "wallet") || !qmltest::Click(m_app, "walletAddressesRow")) return false;
        auto* page = qmltest::WaitForVisible(m_app, "addressListPage");
        return page && QTest::qWaitFor([page] {
            auto* stack = page->property("navigationStack").value<QObject*>();
            return !stack || !stack->property("busy").toBool();
        });
    }
    bool editorCenterIsHittable(QObject* object)
    {
        auto* item = qobject_cast<QQuickItem*>(object);
        return item && QTest::qWaitFor([item] {
            if (!item->window() || !item->isVisible() || !item->isEnabled() || item->width() <= 0 || item->height() <= 0) return false;
            const auto scene = item->mapToScene(item->boundingRect().center());
            if (!QRectF{0, 0, qreal(item->window()->width()), qreal(item->window()->height())}.contains(scene)) return false;
            for (auto* parent = item->parentItem(); parent; parent = parent->parentItem())
                if (parent->clip() && !parent->boundingRect().contains(parent->mapFromScene(scene))) return false;
            return true;
        });
    }
    bool openMessages()
    {
        return qmltest::SelectSettings(m_app, "wallet") && qmltest::Click(m_app, "walletSignVerifyMessageRow") &&
            qmltest::WaitForVisible(m_app, "signVerifyMessagePage");
    }

    // Saves the typed editor's request and starts a fresh one; returns its id.
    QString saveTypedRequest(WalletReceiveModel* receive, CAmount amount)
    {
        auto* wallet = receive ? qobject_cast<WalletQmlModel*>(receive->parent()) : nullptr;
        if (!wallet) return {};
        const auto saved = SaveTypedRequest(*wallet, [amount](PaymentRequestEditorModel& request) { if (amount > 0) request.amount()->setSatoshi(amount); });
        return saved ? saved->id : QString{};
    }
    std::vector<std::string> storedRequests(WalletQmlModel& wallet)
    {
        const auto opening = wallet.opening();
        const auto native = opening ? opening->borrow() : nullptr;
        if (!native) throw std::runtime_error{"The test wallet is unavailable"};
        return qmltest::Call(m_app, [native](interfaces::Node*) { return native->getAddressReceiveRequests(); });
    }

    std::optional<CAmount> persistedAmount(WalletQmlModel& wallet, const QString& address)
    {
        const auto blobs = storedRequests(wallet);
        for (const auto& entry : walletrequests::Decode(blobs).entries) if (QString::fromStdString(entry.recipient.address) == address) return entry.recipient.amount;
        return std::nullopt;
    }
    // Closes the shown request once its own editor has settled: Close is refused
    // while it saves, and a receipt poll can answer the save with Checking, which
    // the user saves again.
    bool closeSettledRequest(WalletModel& wallet, QObject* modal)
    {
        const auto shown = [modal] { return modal->property("openedEditor").value<PaymentRequestEditorModel*>(); };
        for (int attempt{0}; attempt < 5; ++attempt) {
            if (!QTest::qWaitFor([&] { return shown() && !shown()->busy(); }, 30'000)) return false;
            if (!shown()->dirty()) break;
            if (shown()->saveResult().error != ReceiveError::Code::Checking) return false;
            if (!QTest::qWaitFor([&] { return shown() && !wallet.receiveRequestReconciliationPending() && shown()->canSave(); }, 30'000)) return false;
            shown()->save();
        }
        return qmltest::Click(m_app, "savedRequestCloseButton") &&
            QTest::qWaitFor([&] { return !modal->property("visible").toBool(); }, 30'000);
    }
    // Opens a saved request from its Activity row and returns the shown editor.
    PaymentRequestEditorModel* openFromActivity(const QString& id)
    {
        auto* row = qmltest::WaitForVisible(m_app, qPrintable(QStringLiteral("activityRequest_") + id));
        if (!row || !qmltest::Click(row)) return nullptr;
        auto* modal = qmltest::WaitForVisible(m_app, "paymentRequestModal");
        PaymentRequestEditorModel* editor{nullptr};
        if (!modal || !QTest::qWaitFor([&] {
                editor = modal->property("openedEditor").value<PaymentRequestEditorModel*>();
                return editor && editor->requestId() == id && modal->property("opened").toBool();
            }, 30'000)) return nullptr;
        return editor;
    }

    // Saves a request with a distinguishing public message; returns its id.
    QString saveMessageRequest(WalletReceiveModel* receive, const QString& message)
    {
        if (!QTest::qWaitFor([&] { return receive->editor() && receive->ensureAddress() && receive->reservation()->available() && receive->editor()->canSave(); }, 30'000)) return {};
        receive->editor()->message()->edit(message); return saveTypedRequest(receive, 0);
    }
    QObject* viewFor(WalletQmlModel* wallet)
    {
        auto* page = qmltest::Find(m_app, "desktopWalletsPage"); QVariant view;
        if (!page || !QMetaObject::invokeMethod(page, "viewForWallet", Q_RETURN_ARG(QVariant, view), Q_ARG(QVariant, QVariant::fromValue<QObject*>(wallet)))) return nullptr;
        return view.value<QObject*>();
    }
    // Holds Core's wallet lock on another thread until released, or until a
    // hang-breaker deadline expires. Reaching the deadline is recorded so a
    // blocked GUI thread cannot pass by waiting it out.
    struct HeldWalletLock {
        std::promise<void> release;
        std::atomic_bool expired{false};
        std::thread thread;
        explicit HeldWalletLock(wallet::CWallet& core, std::chrono::milliseconds deadline = std::chrono::seconds{60})
        {
            std::promise<void> held; auto ready = held.get_future();
            thread = std::thread([&core, &held, released = release.get_future(), deadline, this]() mutable {
                LOCK(core.cs_wallet); held.set_value();
                if (released.wait_for(deadline) == std::future_status::timeout) expired.store(true);
            });
            ready.wait();
        }
        void unlock() { if (thread.joinable()) { release.set_value(); thread.join(); } }
        ~HeldWalletLock() { unlock(); }
    };

    QString labeledAddress(const QString& name, const QString& label)
    {
        UniValue parameters{UniValue::VARR}; parameters.push_back(label.toStdString());
        return QString::fromStdString(qmltest::Rpc(m_app, "getnewaddress", parameters, name).get_str());
    }

public:
    explicit ReceiveIntegrationTests(BitcoinQmlApplication& app) : m_app{app} {}

private Q_SLOTS:
    void savedQrActionsRequireAnEncodableOriginalPayload_data()
    {
        QTest::addColumn<int>("characters");
        QTest::newRow("encodable-control") << 20; QTest::newRow("original-4250-character-failure") << 4250;
    }
    void savedQrActionsRequireAnEncodableOriginalPayload()
    {
        QFETCH(int, characters);
        auto* wallet = qmltest::CreateWallet(m_app, "qr-action-gate"); QVERIFY(wallet);
        const auto saved = SaveTypedRequest(*wallet, [characters](PaymentRequestEditorModel& request) {
            request.message()->edit(QString(characters, QLatin1Char('x')));
        });
        QVERIFY(saved); QTRY_VERIFY(!wallet->receiveRequestReconciliationPending());
        QVERIFY(qmltest::Click(m_app, "activityTabButton")); auto* activity = qmltest::Find(m_app, "activityStack"); QVERIFY(activity);
        QVERIFY(QMetaObject::invokeMethod(activity, "openPaymentRequest", Q_ARG(QVariant, QVariant{saved->id})));
        QVERIFY(qmltest::WaitForVisible(m_app, "paymentRequestModal"));
        QObject* copy{nullptr}; QObject* save{nullptr};
        QTRY_VERIFY((copy = qmltest::Find(m_app, "savedRequestCopyQrButton")));
        QTRY_VERIFY((save = qmltest::Find(m_app, "savedRequestExportQrButton")));
        const bool encodable = characters == 20;
        if (encodable) { QTRY_VERIFY(copy->property("enabled").toBool()); QTRY_VERIFY(save->property("enabled").toBool()); }
        else {
            // The original provider returned a transparent Image.Ready result
            // for this real saved payload. Neither QR action may admit it.
            auto* error = qmltest::Find(m_app, "savedRequestSharingError"); QVERIFY(error);
            QTRY_VERIFY(!error->property("text").toString().isEmpty());
            QVERIFY(!copy->property("enabled").toBool()); QVERIFY(!save->property("enabled").toBool());
        }
        const auto stored = qmltest::Call(m_app, [name = wallet->name()](interfaces::Node* node) {
            for (auto& handle : node->walletLoader().getWallets()) if (handle->getWalletName() == name.toStdString()) return handle->getAddressReceiveRequests();
            return std::vector<std::string>{};
        });
        QCOMPARE(stored.size(), size_t{1}); const auto decoded = walletrequests::Decode(stored);
        QCOMPARE(decoded.entries.size(), size_t{1}); QCOMPARE(decoded.entries.front().recipient.message, std::string(size_t(characters), 'x'));
    }
    void typedReceiveKeepsCanonicalAmountAndExportsTheExactSavedPayload_data()
    {
        QTest::addColumn<bool>("encrypted");
        QTest::newRow("plain-wallet") << false; QTest::newRow("encrypted-wallet") << true;
    }
    void typedReceiveKeepsCanonicalAmountAndExportsTheExactSavedPayload()
    {
        QFETCH(bool, encrypted);
        auto* wallet = encrypted ? qmltest::CreateLockedHardenedWallet(m_app, "typed-roundtrip", "roundtrip password")
                                 : qmltest::CreateWallet(m_app, "typed-roundtrip");
        QVERIFY(wallet); auto* receive = wallet->receive(); QVERIFY(receive);
        auto* options = qmltest::Model<OptionsQmlModel>(m_app, "optionsModel"); QVERIFY(options); options->setDisplayUnit(0);
        QVERIFY(qmltest::Click(m_app, "receiveTabButton"));
        QVERIFY(qmltest::WaitForVisible(m_app, "receivePage"));
        QTRY_VERIFY(receive->reservation()->available() && receive->editor()->canSave());
        QVERIFY(enterText(qmltest::Find(m_app, "receiveAmountInput"), QStringLiteral("1.25")));
        QVERIFY(enterText(qmltest::Find(m_app, "receivePublicLabelInput"), QStringLiteral("invoice & tea")));
        QVERIFY(enterText(qmltest::Find(m_app, "receiveMessageInput"), QString::fromUtf8("caf\xc3\xa9 ? + &")));
        QVERIFY(enterText(qmltest::Find(m_app, "receivePrivateNoteInput"), QStringLiteral("private-not-in-QR")));
        options->setDisplayUnit(3); // A second consumer changes the global unit.
        QCOMPARE(receive->editor()->amount()->satoshi(), CAmount{125'000'000});
        QVERIFY(qmltest::Click(m_app, "receiveSaveRequestButton"));
        QTRY_VERIFY(receive->editor()->saveResult().requestSaved);
        QVERIFY(qmltest::WaitForVisible(m_app, "paymentRequestModal"));
        const auto blobs = qmltest::Call(m_app, [name = wallet->name()](interfaces::Node* node) {
            for (auto& handle : node->walletLoader().getWallets()) if (handle->getWalletName() == name.toStdString()) return handle->getAddressReceiveRequests();
            return std::vector<std::string>{};
        });
        QCOMPARE(blobs.size(), size_t{1});
        // Decode the Core bytes independently of the app's Decode/Prepare and
        // URI formatter. Keep each original scalar and privacy oracle explicit.
        std::vector<uint8_t> bytes(blobs[0].begin(), blobs[0].end()); DataStream stored{bytes};
        int version, recipient_version; int64_t id; uint32_t time;
        std::string address, label, message, payment, merchant, note; CAmount amount;
        stored >> version >> id >> time >> recipient_version >> address >> label >> amount >> message >> payment >> merchant;
        if (!stored.empty()) stored >> note;
        QCOMPARE(version, 1); QCOMPARE(recipient_version, 1); QVERIFY(id > 0); QVERIFY(time > 0);
        QCOMPARE(amount, CAmount{125'000'000}); QCOMPARE(label, std::string{"invoice & tea"});
        QCOMPARE(message, std::string{"caf\xc3\xa9 ? + &"}); QCOMPARE(note, std::string{"private-not-in-QR"});
        const auto info = qmltest::Rpc(m_app, "getaddressinfo", qmltest::Json("[\"" + address + "\"]"), wallet->name());
        QVERIFY(info.find_value("ismine").get_bool()); QCOMPARE(info.find_value("labels")[0].get_str(), note);
        const QString expected = QStringLiteral("bitcoin:") + QString::fromStdString(address) +
            QStringLiteral("?amount=1.25000000&label=invoice%20%26%20tea&message=caf%C3%A9%20%3F%20%2B%20%26");
        auto sharing = std::unique_ptr<RequestSharingModel>{receive->sharing(receive->editor())}; QVERIFY(sharing);
        sharing->setActive(true); QTRY_VERIFY(sharing->canCopyQr()); QCOMPARE(sharing->paymentUri(), expected);
        QGuiApplication::clipboard()->setText("prior clipboard");
        auto copy = std::unique_ptr<ShareOperationModel>{sharing->copyUri()}; QTRY_VERIFY(!copy->busy());
        QVERIFY(copy->result().performed); QCOMPARE(QGuiApplication::clipboard()->text(), expected);
        QTemporaryDir temporary; QVERIFY(temporary.isValid());
        const auto retained = qEnvironmentVariable("BITCOIN_QML_R30_QR_ARTIFACT_DIR");
        const auto directory = retained.isEmpty() ? temporary.path() : retained;
        QVERIFY(QDir{}.mkpath(directory));
        const auto stem = encrypted ? QStringLiteral("roundtrip-encrypted") : QStringLiteral("roundtrip-plain");
        const auto path = QDir{directory}.filePath(stem + QStringLiteral(".png")); QVERIFY(!QFileInfo::exists(path));
        auto image = QPointer<RequestImageExportOperation>{sharing->beginQrExport()}; QTRY_VERIFY(image->choosing());
        const QImage approved_image = sharing->image(); QVERIFY(!approved_image.isNull());
        QVERIFY(image->choose(QUrl::fromLocalFile(path))); QTRY_VERIFY(image->result().present);
        QVERIFY2(image->result().written, qPrintable(image->error()));
        const QImage actual{path}; QVERIFY(!actual.isNull()); QCOMPARE(actual, approved_image);
        QFile oracle{QDir{directory}.filePath(stem + QStringLiteral(".expected.txt"))}; QVERIFY(oracle.open(QIODevice::WriteOnly));
        QCOMPARE(oracle.write(expected.toUtf8()), qint64(expected.toUtf8().size())); oracle.close();
        // A retained task artifact is independently decoded by the focused gate
        // helper, rather than treating a PNG signature as QR correctness.
        QVERIFY(!expected.contains(QStringLiteral("private-not-in-QR")));
        QCOMPARE(qmltest::Rpc(m_app, "getwalletinfo", {}, wallet->name()).find_value("private_keys_enabled").get_bool(), true);
        if (encrypted) QCOMPARE(qmltest::Rpc(m_app, "getwalletinfo", {}, wallet->name()).find_value("unlocked_until").getInt<int64_t>(), 0);
    }
    void typedReceiveRejectsOversizeQrAndRecoversWithoutBlankExport()
    {
        auto* wallet = qmltest::CreateWallet(m_app, "typed-long-qr"); QVERIFY(wallet);
        auto* receive = wallet->receive(); QVERIFY(receive); QTRY_VERIFY(wallet->property("receiveRequestsLoaded").toBool()); QVERIFY(receive->ensureAddress());
        QTRY_VERIFY(receive->reservation()->available() && receive->editor()->canSave());
        const auto long_message = QString(4250, QLatin1Char('x'));
        receive->editor()->message()->edit(long_message); receive->editor()->save();
        QTRY_VERIFY(receive->editor()->saveResult().requestSaved);
        auto sharing = std::unique_ptr<RequestSharingModel>{receive->sharing(receive->editor())}; QVERIFY(sharing);
        sharing->setActive(true); QTRY_COMPARE(sharing->error(), requestshare::Error::QrEncoding);
        QVERIFY(sharing->canCopyUri()); QVERIFY(!sharing->canExportQr()); QVERIFY(sharing->image().isNull());
        QTemporaryDir directory; QVERIFY(directory.isValid()); const auto path = directory.filePath("oversize.png");
        auto failed = QPointer<RequestImageExportOperation>{sharing->beginQrExport()}; QTRY_VERIFY(failed->result().present);
        QCOMPARE(failed->errorCode(), RequestImageExportError::Source); QVERIFY(!failed->result().written);
        QVERIFY(!failed->choose(QUrl::fromLocalFile(path))); QVERIFY(!QFileInfo::exists(path));
        const auto blobs = qmltest::Call(m_app, [name = wallet->name()](interfaces::Node* node) {
            for (auto& handle : node->walletLoader().getWallets()) if (handle->getWalletName() == name.toStdString()) return handle->getAddressReceiveRequests();
            return std::vector<std::string>{};
        });
        QCOMPARE(blobs.size(), size_t{1}); const auto decoded = walletrequests::Decode(blobs);
        QCOMPARE(decoded.entries.size(), size_t{1}); QCOMPARE(QString::fromStdString(decoded.entries[0].recipient.message), long_message);
        const auto first_result = receive->editor()->saveResult().operationId;
        receive->editor()->message()->edit("short recovery"); receive->editor()->save();
        QTRY_VERIFY(receive->editor()->saveResult().operationId != first_result && receive->editor()->saveResult().requestSaved);
        QTRY_VERIFY(sharing->canExportQr()); QVERIFY(!sharing->image().isNull());
        QCOMPARE(failed->errorCode(), RequestImageExportError::Source); QVERIFY(!failed->result().written);
    }
    // draft-227-D227: a failed QR save reports its error directly above the Save button that started it,
    // inside the visible part of the request modal.
    void failedQrSaveReportsItsErrorBesideTheSaveButton()
    {
        auto* wallet = qmltest::CreateWallet(m_app, "qr-save-failure"); QVERIFY(wallet);
        const auto id = saveMessageRequest(wallet->receive(), "save failure"); QVERIFY(!id.isEmpty());
        QVERIFY(qmltest::Click(m_app, "activityTabButton")); QVERIFY(openFromActivity(id));
        auto* save = qobject_cast<QQuickItem*>(qmltest::WaitForVisible(m_app, "savedRequestExportQrButton")); QVERIFY(save);
        QTRY_VERIFY(save->isEnabled());
        // Scroll the modal as a user would until Save is in view.
        auto* scroll = qmltest::Find(m_app, "paymentRequestModal")->property("contentItem").value<QQuickItem*>(); QVERIFY(scroll);
        auto* flickable = scroll->property("contentItem").value<QQuickItem*>(); QVERIFY(flickable && flickable->property("contentItem").value<QQuickItem*>());
        const auto content_y = save->mapToItem(flickable->property("contentItem").value<QQuickItem*>(), QPointF{}).y();
        const auto limit = std::max<qreal>(0, flickable->property("contentHeight").toReal() - flickable->height());
        flickable->setProperty("contentY", std::clamp<qreal>(content_y - flickable->height() / 2, 0, limit));
        QVERIFY(editorCenterIsHittable(save));
        QObject* sharing{save};
        while (sharing && sharing->metaObject()->indexOfProperty("exportOperation") < 0) sharing = sharing->parent();
        QVERIFY(sharing);
        QVERIFY(qmltest::Click(save));
        RequestImageExportOperation* operation{nullptr};
        QTRY_VERIFY((operation = sharing->property("exportOperation").value<RequestImageExportOperation*>()) && operation->choosing());
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto path = directory.filePath("missing-folder/request.png");
        QVERIFY(operation->choose(QUrl::fromLocalFile(path)));
        QTRY_VERIFY(operation->confirmationRequired() || operation->result().present);
        if (operation->confirmationRequired()) QVERIFY(operation->confirm());
        QTRY_VERIFY(operation->result().present); QVERIFY(!operation->result().written); QVERIFY(!QFileInfo::exists(path));
        auto* error = qobject_cast<QQuickItem*>(qmltest::Find(sharing, QStringLiteral("savedRequestSharingError"))); QVERIFY(error);
        QTRY_VERIFY(error->isVisible() && !error->property("text").toString().isEmpty());
        QCOMPARE(error->property("text").toString(), operation->error());
        QVERIFY(qmltest::WaitForPolish(error));
        const auto shown = error->mapRectToScene(error->boundingRect()), button = save->mapRectToScene(save->boundingRect());
        QVERIFY2(shown.bottom() <= button.top() && button.top() - shown.bottom() <= 24,
            qPrintable(QStringLiteral("error %1..%2, Save %3").arg(shown.top()).arg(shown.bottom()).arg(button.top())));
        QVERIFY(editorCenterIsHittable(error)); QVERIFY(editorCenterIsHittable(save));
    }
    void typedReceiveUnknownImportedTypeDoesNotKeepAnOldReservationArmed()
    {
        const QString name{QStringLiteral("typed-imported")};
        UniValue create{UniValue::VOBJ}; create.pushKV("wallet_name", name.toStdString()); create.pushKV("blank", true); create.pushKV("disable_private_keys", true);
        qmltest::Rpc(m_app, "createwallet", create);
        const std::string key{"tprv8ZgxMBicQKsPdYeeZbPSKd2KYLmeVKtcFA7kqCxDvDR13MQ6us8HopUR2wLcS2ZKPhLyKsqpDL2FtL73LMHcgoCL7DXsciA8eX8nbjCR2eG"};
        UniValue describe{UniValue::VARR}; describe.push_back("pkh(" + key + "/0/*)");
        const auto descriptor = qmltest::Rpc(m_app, "getdescriptorinfo", describe).find_value("descriptor").get_str();
        UniValue item{UniValue::VOBJ}; item.pushKV("desc", descriptor); item.pushKV("timestamp", "now"); item.pushKV("active", true); item.pushKV("range", qmltest::Json("[0,4]"));
        UniValue imports{UniValue::VARR}; imports.push_back(item); UniValue parameters{UniValue::VARR}; parameters.push_back(imports);
        const auto imported = qmltest::Rpc(m_app, "importdescriptors", parameters, name); QVERIFY(imported[0].find_value("success").get_bool());
        auto* wallet = qmltest::SelectOpenWalletForSetup(m_app, name); QVERIFY(wallet); QTRY_VERIFY(wallet->capabilitiesKnown() && wallet->property("receiveRequestsLoaded").toBool());
        auto* receive = wallet->receive(); QVERIFY(receive); QVERIFY(receive->chooseAddressType("legacy")); QTRY_VERIFY(receive->reservation()->available());
        const auto original = receive->reservation()->address(); QVERIFY(!original.isEmpty());
        const auto types = wallet->availableReceiveAddressTypes(); bool saw_unknown{false}, saw_proven{false};
        for (const auto& row : types) {
            const auto type = row.toMap();
            if (type.value("id").toString() == "bech32") saw_unknown = type.value("capability").toInt() == int(ReceiveTypeStatus::Support::Unknown);
            if (type.value("id").toString() == "legacy") saw_proven = type.value("capability").toInt() == int(ReceiveTypeStatus::Support::Proven);
        }
        QVERIFY(saw_unknown); QVERIFY(saw_proven);
        QVERIFY(receive->chooseAddressType("bech32")); QVERIFY(!receive->editor()->canSave());
        QTRY_COMPARE(receive->reservation()->phase(), ReceiveReservationModel::Failed);
        QVERIFY(!receive->reservation()->available()); QVERIFY(!receive->reservation()->prompt()); QCOMPARE(receive->reservation()->address(), original);
        receive->editor()->save(); QTRY_VERIFY(!receive->editor()->busy());
        const auto stored = qmltest::Call(m_app, [name](interfaces::Node* node) {
            for (auto& handle : node->walletLoader().getWallets()) if (handle->getWalletName() == name.toStdString()) return handle->getAddressReceiveRequests();
            return std::vector<std::string>{};
        });
        QVERIFY(stored.empty()); const auto address = qmltest::Rpc(m_app, "getaddressinfo", qmltest::Json("[\"" + original.toStdString() + "\"]"), name);
        QVERIFY(!address.find_value("iswitness").get_bool()); QVERIFY(address.find_value("solvable").get_bool());
    }
    void typedReceivePreferenceFailureRetainsTheGeneratedAddress()
    {
        QVERIFY(!gArgs.GetSettingsPath());
        auto* wallet = qmltest::CreateWallet(m_app, "typed-pref-disabled"); QVERIFY(wallet);
        QTRY_VERIFY(wallet->property("receiveRequestsLoaded").toBool()); auto* receive = wallet->receive(); QVERIFY(receive);
        QVERIFY(receive->chooseAddressType("legacy")); QTRY_VERIFY(receive->reservation()->available());
        QVERIFY(receive->reservation()->generationPersisted()); QVERIFY(receive->reservation()->preferenceRequested()); QVERIFY(!receive->reservation()->preferenceSaved());
        QCOMPARE(receive->reservation()->generationError(), walletrequests::Error::PreferencePersistence);
        const auto original = receive->reservation()->address(); QVERIFY(!original.isEmpty());
        QTRY_VERIFY(receive->editor()->canSave()); receive->editor()->message()->edit("request survives preference failure"); receive->editor()->save();
        QTRY_VERIFY(receive->editor()->saveResult().requestSaved); QCOMPARE(receive->editor()->address(), original);
        const auto info = qmltest::Rpc(m_app, "getaddressinfo", qmltest::Json("[\"" + original.toStdString() + "\"]"), wallet->name());
        QVERIFY(info.find_value("ismine").get_bool()); QVERIFY(!info.find_value("iswitness").get_bool());
    }
    void typedReceiveTemplatePreservesTheWalletDefaultAndUsesAFreshAddress()
    {
        auto* wallet = qmltest::CreateWallet(m_app, "typed-template-default"); QVERIFY(wallet);
        auto* receive = wallet->receive(); QVERIFY(receive); QTRY_VERIFY(wallet->property("receiveRequestsLoaded").toBool());
        QVERIFY(receive->chooseAddressType("legacy")); QTRY_VERIFY(receive->reservation()->available() && receive->editor()->canSave());
        auto* saved = receive->editor(); saved->publicLabel()->edit("repeat invoice"); saved->message()->edit("public template terms");
        saved->privateNote()->edit("private template context"); saved->amount()->setSatoshi(12'345); saved->save();
        QTRY_VERIFY(saved->saveResult().requestSaved); const auto id = saved->requestId(), original = saved->address();
        QVERIFY(wallet->setDefaultReceiveAddressType("bech32")); QTRY_COMPARE(wallet->defaultReceiveAddressType(), QStringLiteral("bech32"));
        QVERIFY(receive->useTemplate(id)); QTRY_VERIFY(receive->editor() != saved && receive->reservation()->available() && receive->editor()->canSave());
        auto* repeated = receive->editor(); QCOMPARE(repeated->requestId(), QString{}); QVERIFY(repeated->address() != original);
        QCOMPARE(repeated->addressType(), QStringLiteral("legacy")); QCOMPARE(repeated->publicLabel()->text(), QStringLiteral("repeat invoice"));
        QCOMPARE(repeated->message()->text(), QStringLiteral("public template terms")); QCOMPARE(repeated->privateNote()->text(), QStringLiteral("private template context"));
        QCOMPARE(repeated->amount()->satoshi(), CAmount{12'345}); QCOMPARE(wallet->defaultReceiveAddressType(), QStringLiteral("bech32"));
        const auto repeated_address = repeated->address(); const auto info = qmltest::Rpc(m_app, "getaddressinfo", qmltest::Json("[\"" + repeated_address.toStdString() + "\"]"), wallet->name());
        QVERIFY(info.find_value("ismine").get_bool()); QVERIFY(!info.find_value("iswitness").get_bool()); QCOMPARE(wallet->receiveRequests()->count(), 1);
        QVERIFY(receive->newRequest()); QTRY_VERIFY(repeated->closePrompt());
        repeated->closePrompt()->answer(TextEditClosePrompt::Discard);
        QTRY_VERIFY(receive->editor() != repeated && receive->reservation()->available() && receive->editor()->canSave());
        QCOMPARE(receive->editor()->addressType(), QStringLiteral("bech32")); QVERIFY(receive->editor()->address() != original && receive->editor()->address() != repeated_address);
        QCOMPARE(wallet->defaultReceiveAddressType(), QStringLiteral("bech32")); QCOMPARE(wallet->receiveRequests()->count(), 1);
    }
    // #972: address generation and request saves wait for a busy wallet off the GUI thread, then apply their result.
    void receiveWorkWaitsForABusyWalletOffTheGuiThread()
    {
        auto* wallet = qmltest::CreateWallet(m_app, "busy-receive"); QVERIFY(wallet); auto* receive = wallet->receive(); QVERIFY(receive);
        QTRY_VERIFY(receive->editor() && receive->ensureAddress() && receive->reservation()->available() && receive->editor()->canSave());
        const auto first = receive->reservation()->address(); QVERIFY(!first.isEmpty());
        const auto native = wallet->opening()->borrow();
        QVERIFY(native);
        auto* core = native->wallet(); QVERIFY(core);
        // While the wallet lock is held, the GUI action must return and the
        // event loop must reach a queued turn without the hang-breaker
        // expiring. A blocked GUI thread can do neither until expiry.
        const auto progressesWhileHeld = [this](const HeldWalletLock& busy) {
            bool turned{false};
            QMetaObject::invokeMethod(this, [&turned] { turned = true; }, Qt::QueuedConnection);
            return QTest::qWaitFor([&turned] { return turned; }, 120'000) && !busy.expired.load();
        };
        {
            HeldWalletLock busy{*core};
            QVERIFY(receive->nextAddress());
            QVERIFY2(!busy.expired.load(), "nextAddress waited for the held wallet lock");
            QVERIFY(progressesWhileHeld(busy));
            QVERIFY(!receive->reservation()->available() || receive->reservation()->address() == first);
            busy.unlock();
        }
        QTRY_VERIFY(receive->reservation()->available() && receive->editor()->canSave() && receive->reservation()->address() != first);
        QPointer<PaymentRequestEditorModel> editor{receive->editor()}; const auto address = editor->address(); QCOMPARE(address, receive->reservation()->address());
        editor->message()->edit("saved while busy");
        {
            HeldWalletLock busy{*core};
            editor->save();
            QVERIFY2(!busy.expired.load(), "save waited for the held wallet lock");
            QVERIFY(progressesWhileHeld(busy));
            QVERIFY(editor && !editor->saveResult().requestSaved);
            busy.unlock();
        }
        QTRY_VERIFY(editor && editor->saveResult().requestSaved);
        const auto stored = walletrequests::Decode(storedRequests(*wallet)).entries;
        QCOMPARE(stored.size(), size_t{1}); QCOMPARE(QString::fromStdString(stored.front().recipient.address), address);
        QCOMPARE(QString::fromStdString(stored.front().recipient.message), QStringLiteral("saved while busy"));
    }
    void sentNotesPreserveReceivingPurposeAndPublicInvoiceAfterRealCommit()
    {
        auto* wallet = qmltest::CreateWallet(m_app, "sent-notes"); QVERIFY(wallet);
        QVERIFY(qmltest::FundWallet(m_app, *wallet));
        const auto name = wallet->name(), address = qmltest::NewAddress(m_app, name);
        QmlRecentRequestEntry entry; entry.id = 7; entry.date = QDateTime::fromSecsSinceEpoch(1'700'000'000);
        entry.recipient.address = address.toStdString(); entry.recipient.amount = 1234;
        entry.recipient.label = "public invoice"; entry.recipient.message = "public message"; entry.recipient.noteSelf = "private invoice note";
        const auto original = walletrequests::Encode(entry);
        QVERIFY(qmltest::Call(m_app, [name, address, original](interfaces::Node* node) {
            for (auto& candidate : node->walletLoader().getWallets()) if (candidate->getWalletName() == name.toStdString())
                return candidate->setAddressReceiveRequest(DecodeDestination(address.toStdString()), "7", original);
            return false;
        }));
        auto* recipient = wallet->sendRecipientList()->currentRecipient(); QVERIFY(recipient);
        recipient->address()->setAddress(address, 2); recipient->amount()->setSatoshi(100'000); recipient->setLabel("outgoing note");
        QTRY_VERIFY(!wallet->feeEstimatePending()); QVERIFY(AwaitPreparation(*wallet));
        QVERIFY(AwaitCommittedSend(*wallet));
        QTRY_VERIFY(!wallet->recipientLabelsPending()); QVERIFY2(wallet->recipientLabelsError().isEmpty(), qPrintable(wallet->recipientLabelsError()));
        const auto txid = wallet->recipientLabelsTransactionId(); QVERIFY(!txid.isEmpty());
        const auto mempool = qmltest::Rpc(m_app, "getrawmempool"); bool broadcast{false};
        for (const auto& id : mempool.getValues()) if (id.get_str() == txid.toStdString()) broadcast = true;
        QVERIFY(broadcast);
        QVERIFY(qmltest::Call(m_app, [name, address, original](interfaces::Node* node) {
            for (auto& candidate : node->walletLoader().getWallets()) {
                if (candidate->getWalletName() != name.toStdString()) continue;
                std::string label; wallet::AddressPurpose purpose;
                return candidate->getAddress(DecodeDestination(address.toStdString()), &label, &purpose) &&
                    label == "outgoing note" && purpose == wallet::AddressPurpose::RECEIVE &&
                    candidate->getAddressReceiveRequests() == std::vector<std::string>{original};
            }
            return false;
        }));
    }

    void sendOutcomeReportsObservationOrRejectsStaleReview_data()
    {
        QTest::addColumn<int>("scenario");
        QTest::newRow("accepted") << 0; QTest::newRow("broadcast-disabled") << 1; QTest::newRow("competing-spend-stales-review") << 2;
    }
    void sendOutcomeReportsObservationOrRejectsStaleReview()
    {
        QFETCH(int, scenario);
        QTRY_VERIFY(m_app.window()->property("preferenceGeometryRestored").toBool());
        m_app.window()->resize(900, 800);
        QTRY_COMPARE(m_app.window()->width(), 900);
        QTRY_COMPARE(m_app.window()->height(), 800);
        auto* wallet = qmltest::CreateWallet(m_app, "submission"); QVERIFY(wallet); QVERIFY(qmltest::FundWallet(m_app, *wallet));
        const auto name = wallet->name(), address = qmltest::NewAddress(m_app, name);
        QVERIFY(qmltest::Click(m_app, "sendTabButton"));
        wallet->setCustomFeeEnabled(true); wallet->setCustomFeeRate("1");
        auto* recipient = wallet->sendRecipientList()->currentRecipient(); QVERIFY(recipient);
        recipient->address()->setAddress(address, 2); recipient->amount()->setSatoshi(100'000);
        QVERIFY(AwaitPreparation(*wallet)); const auto approved = wallet->currentTransaction()->getWtx(); QVERIFY(approved);
        QString competing;
        if (scenario == 2) {
            UniValue inputs{UniValue::VARR};
            for (const auto& input : approved->vin) {
                UniValue coin{UniValue::VOBJ}; coin.pushKV("txid", input.prevout.hash.GetHex()); coin.pushKV("vout", input.prevout.n); inputs.push_back(coin);
            }
            const auto conflict = qmltest::Rpc(m_app, "sendall", qmltest::Json("{\"recipients\":[\"" + address.toStdString() + "\"],\"fee_rate\":20,\"options\":{\"inputs\":" + inputs.write() + "}}"), name);
            competing = QString::fromStdString(conflict.find_value("txid").get_str()); QVERIFY(!competing.isEmpty());
        }
        auto* page = qmltest::Find(m_app, "sendPage"); QVERIFY(page); QVERIFY(QMetaObject::invokeMethod(page, "openTransactionReview"));
        QVERIFY(qmltest::WaitForVisible(m_app, "sendTransactionReviewPage"));
        if (scenario == 2) {
            QTRY_VERIFY_WITH_TIMEOUT(!wallet->currentTransactionCanSend() || wallet->currentTransactionNeedsRevalidation(), 30'000);
            auto* button = qmltest::Find(m_app, "sendTransactionReviewSendButton"); QVERIFY(button);
            QTRY_VERIFY_WITH_TIMEOUT(!button->property("visible").toBool() || !button->property("enabled").toBool(), 10'000);
            QVERIFY(!button->property("visible").toBool() || !button->property("enabled").toBool());
            auto* sender = qmltest::Find(m_app, "sendTransactionReviewController"); QVERIFY(sender);
            QCOMPARE(ReviewSendState(*sender), SendOperation::State::None);
            QVERIFY(sender->property("txid").toString().isEmpty());
            const auto reviewed_txid = QString::fromStdString(approved->GetHash().GetHex());
            bool recorded{false};
            const auto history = qmltest::Rpc(m_app, "listtransactions", qmltest::Json("[\"*\",100,0,true]"), name);
            for (const auto& entry : history.getValues()) {
                const auto& txid = entry.find_value("txid");
                recorded |= txid.isStr() && txid.get_str() == reviewed_txid.toStdString();
            }
            QVERIFY(!recorded);
            bool competing_in_mempool{false}, reviewed_in_mempool{false};
            const auto mempool = qmltest::Rpc(m_app, "getrawmempool");
            for (const auto& id : mempool.getValues()) {
                competing_in_mempool |= id.get_str() == competing.toStdString();
                reviewed_in_mempool |= id.get_str() == reviewed_txid.toStdString();
            }
            QVERIFY(competing_in_mempool);
            QVERIFY(!reviewed_in_mempool);
            return;
        }
        QVERIFY(qmltest::Click(m_app, "sendTransactionReviewSendButton"));
        auto* sender = qmltest::Find(m_app, "sendTransactionReviewController"); QVERIFY(sender);
        QTRY_COMPARE(ReviewSendState(*sender), SendOperation::State::Committed);
        QVERIFY(qmltest::WaitForVisible(m_app, "sendCompletePage"));
        auto* complete = qmltest::Find(m_app, "sendCompletePage"); QVERIFY(complete);
        const auto expected = scenario == 0 ? WalletQmlModel::SendSubmission::SubmissionMempool : WalletQmlModel::SendSubmission::SubmissionNotInMempool;
        QCOMPARE(sender->property("submission").toInt(), static_cast<int>(expected)); QCOMPARE(complete->property("submissionState").toInt(), static_cast<int>(expected));
        QCOMPARE(sender->property("record").toInt(), static_cast<int>(WalletQmlModel::SendRecord::SendRecordRecorded));
        QCOMPARE(complete->property("recordState").toInt(), static_cast<int>(WalletQmlModel::SendRecord::SendRecordRecorded));
        QCOMPARE(complete->property("titleText").toString(), scenario == 0 ? QString{"Transaction submitted"} : QString{"Transaction saved"});
        if (scenario != 0) QVERIFY(complete->property("descriptionText").toString().contains("not in the mempool"));
        const auto txid = QString::fromStdString(approved->GetHash().GetHex()); QCOMPARE(sender->property("txid").toString(), txid);
        const auto saved = qmltest::Rpc(m_app, "gettransaction", qmltest::Json("[\"" + txid.toStdString() + "\"]"), name);
        QCOMPARE(QString::fromStdString(saved.find_value("txid").get_str()), txid);
        const auto pool = qmltest::Rpc(m_app, "getrawmempool"); bool found{false}, competitor{false};
        for (const auto& id : pool.getValues()) { found |= id.get_str() == txid.toStdString(); competitor |= id.get_str() == competing.toStdString(); }
        QCOMPARE(found, scenario == 0); if (scenario == 2) QVERIFY(competitor);
    }

    void asynchronousSendKeepsTheCommittedIdentityAndPublicInvoice()
    {
        auto* wallet = qmltest::CreateWallet(m_app, "async-send"); QVERIFY(wallet);
        QVERIFY(qmltest::FundWallet(m_app, *wallet));
        const auto name = wallet->name(), address = qmltest::NewAddress(m_app, name);
        QmlRecentRequestEntry entry; entry.id = 7; entry.date = QDateTime::fromSecsSinceEpoch(1'700'000'000);
        entry.recipient.address = address.toStdString(); entry.recipient.amount = 1234;
        entry.recipient.label = "public invoice"; entry.recipient.message = "public message"; entry.recipient.noteSelf = "private invoice note";
        const auto original = walletrequests::Encode(entry);
        QVERIFY(qmltest::Call(m_app, [name, address, original](interfaces::Node* node) {
            for (auto& candidate : node->walletLoader().getWallets()) if (candidate->getWalletName() == name.toStdString())
                return candidate->setAddressReceiveRequest(DecodeDestination(address.toStdString()), "7", original);
            return false;
        }));
        auto* recipient = wallet->sendRecipientList()->currentRecipient(); QVERIFY(recipient);
        recipient->address()->setAddress(address, 2); recipient->amount()->setSatoshi(100'000); recipient->setLabel("outgoing note");
        QTRY_VERIFY(!wallet->feeEstimatePending()); QVERIFY(AwaitPreparation(*wallet));
        const auto send = StartSend(*wallet); QVERIFY(send);
        QTRY_COMPARE(send->state(), SendOperation::State::Committed);
        QTRY_VERIFY(!wallet->recipientLabelsPending()); QVERIFY2(wallet->recipientLabelsError().isEmpty(), qPrintable(wallet->recipientLabelsError()));
        const auto txid = wallet->recipientLabelsTransactionId(); QVERIFY(!txid.isEmpty());
        const auto mempool = qmltest::Rpc(m_app, "getrawmempool"); bool broadcast{false};
        for (const auto& id : mempool.getValues()) if (id.get_str() == txid.toStdString()) broadcast = true;
        QVERIFY(broadcast);
        QVERIFY(qmltest::Call(m_app, [name, address, original](interfaces::Node* node) {
            for (auto& candidate : node->walletLoader().getWallets()) {
                if (candidate->getWalletName() != name.toStdString()) continue;
                std::string label; wallet::AddressPurpose purpose;
                return candidate->getAddress(DecodeDestination(address.toStdString()), &label, &purpose) &&
                    label == "outgoing note" && purpose == wallet::AddressPurpose::RECEIVE &&
                    candidate->getAddressReceiveRequests() == std::vector<std::string>{original};
            }
            return false;
        }));
    }

    void orderedRequestOwnerPersistsPublicInterfaceEffectsAndNativeReceipts()
    {
        using namespace walletrequests;
        auto* wallet = qmltest::CreateWallet(m_app, "ordered-storage"); QVERIFY(wallet);
        const QString name = wallet->name();
        const auto address = qmltest::NewAddress(m_app, name);
        auto native = qmltest::Call(m_app, [name](interfaces::Node* node) -> std::shared_ptr<interfaces::Wallet> {
            for (auto& candidate : node->walletLoader().getWallets()) {
                if (candidate->getWalletName() == name.toStdString()) return std::move(candidate);
            }
            return {};
        });
        QVERIFY(native);
        execution::Executor executor{execution::Executor::Config{}};
        auto opening = makeOpening(executor);
        auto client = opening->start(native).borrower;
        auto reader = std::make_shared<walletreceipts::Reader>(opening, [] {});
        auto store = std::make_shared<Store>(opening, [] {},
            [weak = std::weak_ptr<walletreceipts::Reader>{reader}](QSet<QString> addresses) {
                if (const auto owner = weak.lock()) owner->reload(std::move(addresses));
            }, QDateTime::currentDateTimeUtc, [weak = std::weak_ptr<walletreceipts::Reader>{reader}] {
                const auto owner = weak.lock(); return owner && owner->settled();
            });
        const auto cleanup = qScopeGuard([&] {
            store->close();
            reader->close();
            client.reset();
            native.reset();
            const auto stopped = std::make_shared<std::atomic_bool>(false);
            opening->stop(false, [stopped] { *stopped = true; });
            if (!QTest::qWaitFor([&] { return stopped->load(); }, 30'000))
                QTest::qFail("Request owner did not physically drain", __FILE__, __LINE__);
            executor.stopAndJoin();
        });
        QVERIFY(client);
        QVERIFY(reader->onSnapshot([weak = std::weak_ptr<Store>{store}](auto snapshot) {
            if (const auto owner = weak.lock()) owner->observed(snapshot);
        }));
        opening->onTransactions([weak = std::weak_ptr<walletreceipts::Reader>{reader}](uint64_t revision, auto changes, bool rescan) {
            if (const auto owner = weak.lock()) owner->notice(revision, std::move(changes), rescan);
        });
        Command reload; reload.kind = Kind::Reload;
        const auto initial = store->submit(reload); QVERIFY(initial);
        QTRY_VERIFY(initial.operation->result().has_value()); QTRY_VERIFY(reader->settled());
        Command create; create.kind = Kind::Create; create.require_unused = true;
        create.draft.address = address; create.draft.amount = 10'000;
        create.draft.label = "public label"; create.draft.message = "public message"; create.draft.note = "private note";
        const auto created = store->submit(create); QVERIFY(created);
        QTRY_VERIFY(created.operation->result().has_value());
        QVERIFY(created.operation->result()->request_saved);
        const auto saved = *created.operation->result()->record;
        QCOMPARE(saved.entry.recipient.amount, CAmount{10'000});
        QTRY_VERIFY(reader->settled());
        // A separately acquired public Wallet handle is the persistence oracle;
        // it does not inspect the actor's snapshot or Core wallet internals.
        const auto read = [&] {
            return qmltest::Call(m_app, [name](interfaces::Node* node) {
                for (auto& candidate : node->walletLoader().getWallets()) {
                    if (candidate->getWalletName() == name.toStdString()) return Decode(candidate->getAddressReceiveRequests()).entries;
                }
                return std::vector<QmlRecentRequestEntry>{};
            });
        };
        QCOMPARE(read().size(), size_t{1}); QCOMPARE(read().front().recipient.noteSelf, std::string{"private note"});
        Command label; label.kind = Kind::Label; label.address = address; label.label = "renamed private note";
        const auto renamed = store->submit(label); QVERIFY(renamed);
        QTRY_VERIFY(renamed.operation->result().has_value()); QCOMPARE(renamed.operation->result()->error, Error::None);
        QVERIFY(renamed.operation->result()->label_saved);
        QCOMPARE(read().front().recipient.noteSelf, std::string{"renamed private note"});
        QCOMPARE(read().front().recipient.label, std::string{"public label"});
        auto* payer = qmltest::CreateWallet(m_app, "storage-payer"); QVERIFY(payer); QVERIFY(qmltest::FundWallet(m_app, *payer));
        qmltest::Rpc(m_app, "sendtoaddress", qmltest::Json("{\"address\":\"" + address.toStdString() + "\",\"amount\":0.00001,\"fee_rate\":1}"), payer->name());
        QTRY_VERIFY_WITH_TIMEOUT(reader->settled() && read().front().payment_received, 30'000);
        const auto refreshed = store->submit(reload); QVERIFY(refreshed);
        QTRY_VERIFY(refreshed.operation->result().has_value()); QTRY_VERIFY(reader->settled());
        const auto publication = store->take(); QVERIFY(publication && publication->snapshot);
        const auto* paid = publication->snapshot->find(saved.entry.id); QVERIFY(paid && paid->entry.payment_received);
        auto edit = create; edit.kind = Kind::Edit; edit.id = paid->entry.id; edit.revision = paid->revision; edit.draft.amount = 20'000;
        const auto refused = store->submit(edit); QVERIFY(refused); QTRY_VERIFY(refused.operation->result().has_value());
        QCOMPARE(refused.operation->result()->error, Error::Paid);
        Command erase; erase.kind = Kind::Erase; erase.id = paid->entry.id; erase.revision = paid->revision;
        const auto erased = store->submit(erase); QVERIFY(erased); QTRY_VERIFY(erased.operation->result().has_value());
        QVERIFY(erased.operation->result()->erased); QVERIFY(read().empty());
    }
    void incrementalLabelPropagationPreservesUnrelatedCoreRecords()
    {
        using namespace walletrequests;
        auto* wallet = qmltest::CreateWallet(m_app, "label-batches"); QVERIFY(wallet);
        const auto name = wallet->name();
        const auto address = qmltest::NewAddress(m_app, name), other = qmltest::NewAddress(m_app, name);
        QVERIFY(qmltest::Call(m_app, [name, address, other](interfaces::Node* node) {
            for (auto& candidate : node->walletLoader().getWallets()) {
                if (candidate->getWalletName() != name.toStdString()) continue;
                for (int64_t id{1}; id <= 85; ++id) {
                    QmlRecentRequestEntry entry; entry.id = id; entry.date = QDateTime::fromSecsSinceEpoch(1'700'000'000);
                    entry.recipient.address = (id <= 65 ? address : other).toStdString();
                    entry.recipient.amount = id * 100; entry.recipient.label = "public"; entry.recipient.message = "message";
                    entry.recipient.noteSelf = "before"; entry.payment_received = id == 1;
                    if (!candidate->setAddressReceiveRequest(DecodeDestination(entry.recipient.address), util::ToString(id), Encode(entry))) return false;
                }
                return true;
            }
            return false;
        }));
        auto native = qmltest::Call(m_app, [name](interfaces::Node* node) -> std::shared_ptr<interfaces::Wallet> {
            for (auto& candidate : node->walletLoader().getWallets()) if (candidate->getWalletName() == name.toStdString()) return std::move(candidate);
            return {};
        }); QVERIFY(native);
        execution::Executor executor{execution::Executor::Config{}};
        auto opening = makeOpening(executor);
        auto client = opening->start(native).borrower;
        auto reader = std::make_shared<walletreceipts::Reader>(opening, [] {});
        std::atomic<QThread*> encoded_on{nullptr}, prepared_on{nullptr};
        auto store = std::make_shared<Store>(opening, [] {}, [reader](QSet<QString> addresses) { reader->reload(std::move(addresses)); },
            QDateTime::currentDateTimeUtc, [reader] { return reader->settled(); }, Store::Release{}, ReservationStorage{},
            [&encoded_on](const QmlRecentRequestEntry& entry) { encoded_on = QThread::currentThread(); return Encode(entry); }, DecodeOne,
            [&prepared_on](Record row) { prepared_on = QThread::currentThread(); return FreezeRecord(std::move(row)); });
        const auto cleanup = qScopeGuard([&] {
            store->close();
            reader->close();
            client.reset();
            native.reset();
            const auto stopped = std::make_shared<std::atomic_bool>(false);
            opening->stop(false, [stopped] { *stopped = true; });
            if (!QTest::qWaitFor([&] { return stopped->load(); }, 30'000))
                QTest::qFail("Label owner did not physically drain", __FILE__, __LINE__);
            executor.stopAndJoin();
        });
        QVERIFY(client);
        QVERIFY(reader->onSnapshot([weak = std::weak_ptr<Store>{store}](auto snapshot) {
            if (const auto owner = weak.lock()) owner->observed(snapshot);
        }));
        Command reload; reload.kind = Kind::Reload; const auto loaded = store->submit(reload); QVERIFY(loaded);
        QTRY_VERIFY(loaded.operation->result()); QCOMPARE(loaded.operation->result()->error, Error::None);
        QTRY_VERIFY(store->presentationCurrent());
        Command label; label.kind = Kind::Label; label.address = address; label.label = "after";
        const auto changed = store->submit(label); QVERIFY(changed); QTRY_VERIFY(changed.operation->result());
        const auto result = *changed.operation->result(); QCOMPARE(result.error, Error::None); QVERIFY(result.label_saved);
        QCOMPARE(result.records_total, size_t{65}); QCOMPARE(result.records_saved, size_t{65});
        QVERIFY(encoded_on.load()); QVERIFY(prepared_on.load());
        QVERIFY(encoded_on.load() != QThread::currentThread()); QVERIFY(prepared_on.load() != QThread::currentThread());
        const auto persisted = qmltest::Call(m_app, [name](interfaces::Node* node) {
            for (auto& candidate : node->walletLoader().getWallets()) if (candidate->getWalletName() == name.toStdString()) return Decode(candidate->getAddressReceiveRequests()).entries;
            return std::vector<QmlRecentRequestEntry>{};
        });
        QCOMPARE(persisted.size(), size_t{85}); std::set<int64_t> seen;
        for (const auto& entry : persisted) {
            QVERIFY(entry.id >= 1 && entry.id <= 85); QVERIFY(seen.insert(entry.id).second);
            QCOMPARE(entry.recipient.address, (entry.id <= 65 ? address : other).toStdString());
            QCOMPARE(entry.recipient.noteSelf, entry.id <= 65 ? std::string{"after"} : std::string{"before"});
            QCOMPARE(entry.recipient.amount, entry.id * 100); QCOMPARE(entry.recipient.label, std::string{"public"});
            QCOMPARE(entry.recipient.message, std::string{"message"}); QCOMPARE(entry.payment_received, entry.id == 1);
        }
    }
    void addressPreferencePersistsThroughPublicSettingsPort()
    {
        auto* wallet = qmltest::CreateWallet(m_app, "preference");
        QVERIFY(wallet); QTRY_VERIFY(wallet->capabilitiesKnown());
        QVERIFY(wallet->setDefaultReceiveAddressType("legacy"));
        QTRY_VERIFY(!wallet->property("receivePreferencePending").toBool());
        QCOMPARE(wallet->property("receivePreferenceError").toString(), QString{});
        QCOMPARE(wallet->defaultReceiveAddressType(), QString{"legacy"});
        fs::path path;
        QVERIFY(gArgs.GetSettingsPath(&path));
        QFile file{QString::fromStdString(fs::PathToString(path))};
        QVERIFY(file.open(QIODevice::ReadOnly));
        UniValue persisted;
        QVERIFY(persisted.read(file.readAll().toStdString()));
        QCOMPARE(persisted["qml_receive_address_types"]["preference"].get_str(), std::string{"legacy"});
        auto* controller = qmltest::Model<WalletQmlController>(m_app, "walletController");
        QPointer<WalletQmlModel> original{wallet};
        controller->closeWallet("preference"); QTRY_VERIFY(original.isNull());
        controller->setSelectedWallet("preference");
        auto* reopened = qmltest::WaitForOpenWallet(m_app, "preference");
        QVERIFY(reopened); QTRY_VERIFY(reopened->capabilitiesKnown());
        QVERIFY(controller->selectWallet(reopened));
        QCOMPARE(reopened->defaultReceiveAddressType(), QString{"legacy"});
    }
    // #972: two wallets saving their receive-type defaults at the same time each keep their own persisted default.
    void concurrentReceiveDefaultsStayPerWallet()
    {
        auto* first = qmltest::CreateWallet(m_app, "default-first"); QVERIFY(first); QTRY_VERIFY(first->capabilitiesKnown());
        auto* second = qmltest::CreateWallet(m_app, "default-second"); QVERIFY(second); QTRY_VERIFY(second->capabilitiesKnown());
        QVERIFY(first->setDefaultReceiveAddressType("legacy")); QVERIFY(second->setDefaultReceiveAddressType("p2sh-segwit"));
        QTRY_VERIFY(!first->property("receivePreferencePending").toBool() && !second->property("receivePreferencePending").toBool());
        QCOMPARE(first->property("receivePreferenceError").toString(), QString{}); QCOMPARE(second->property("receivePreferenceError").toString(), QString{});
        fs::path path; QVERIFY(gArgs.GetSettingsPath(&path));
        QFile file{QString::fromStdString(fs::PathToString(path))}; QVERIFY(file.open(QIODevice::ReadOnly));
        UniValue persisted; QVERIFY(persisted.read(file.readAll().toStdString()));
        const auto saved = [&](const char* name) { const auto& value = persisted["qml_receive_address_types"][name]; return value.isStr() ? value.get_str() : std::string{"<missing>"}; };
        QCOMPARE(saved("default-first"), std::string{"legacy"}); QCOMPARE(saved("default-second"), std::string{"p2sh-segwit"});
        auto* controller = qmltest::Model<WalletQmlController>(m_app, "walletController"); QVERIFY(controller);
        for (const auto& [name, type] : {std::pair{QStringLiteral("default-first"), QStringLiteral("legacy")}, std::pair{QStringLiteral("default-second"), QStringLiteral("p2sh-segwit")}}) {
            QPointer<WalletQmlModel> original{controller->walletForName(name)}; QVERIFY(original);
            controller->closeWallet(name); QTRY_VERIFY(original.isNull());
            controller->setSelectedWallet(name);
            auto* reopened = qmltest::WaitForOpenWallet(m_app, name); QVERIFY(reopened); QTRY_VERIFY(reopened->capabilitiesKnown());
            QCOMPARE(reopened->defaultReceiveAddressType(), type);
        }
    }
    void addressPreferenceWriteReportsDisabledSettings()
    {
        auto* wallet = qmltest::CreateWallet(m_app, "preference-disabled");
        QVERIFY(wallet); QTRY_VERIFY(wallet->capabilitiesKnown());
        QVERIFY(!gArgs.GetSettingsPath());
        QVERIFY(wallet->setDefaultReceiveAddressType("legacy"));
        QTRY_VERIFY(!wallet->property("receivePreferencePending").toBool());
        QVERIFY(!wallet->property("receivePreferenceError").toString().isEmpty());
        // Core keeps the effective value in memory even when persistence fails.
        // Present that value while retaining the failed persistence outcome.
        QCOMPARE(wallet->defaultReceiveAddressType(), QString{"legacy"});
        QVERIFY(wallet->available());
    }
    void localizedTypedRequestPersistsExactAmount_data()
    {
        QTest::addColumn<QString>("text"); QTest::addColumn<int>("unit"); QTest::addColumn<int>("producer");
        QTest::newRow("german-comma") << "0,02" << 0 << 0;
        QTest::newRow("dot-control") << "0.02" << 0 << 0;
        QTest::newRow("native-clipboard-comma") << "0,02" << 0 << 1;
        QTest::newRow("ime-commit-comma") << "0,02" << 0 << 2;
        QTest::newRow("milli-unit") << "20" << 1 << 0;
        QTest::newRow("micro-unit") << "20000" << 2 << 0;
        QTest::newRow("sat-unit") << "2000000" << 3 << 0;
    }

    void localizedTypedRequestPersistsExactAmount()
    {
        QFETCH(QString, text); QFETCH(int, unit); QFETCH(int, producer);
        const QLocale previous;
        const auto restore = qScopeGuard([previous] { QLocale::setDefault(previous); });
        QLocale::setDefault(QLocale("de_DE"));
        auto* wallet = qmltest::CreateWallet(m_app); QVERIFY(wallet);
        auto* options = qmltest::Model<OptionsQmlModel>(m_app, "optionsModel"); QVERIFY(options); options->setDisplayUnit(unit);
        QVERIFY(qmltest::Click(m_app, "receiveTabButton"));
        QVERIFY(qmltest::WaitForVisible(m_app, "receivePage"));
        QTRY_VERIFY(wallet->receive()->reservation()->available());
        QVERIFY(qmltest::SetText(m_app, "receiveAmountInput", ""));
        auto* input = qmltest::Find(m_app, "receiveAmountInput"); QVERIFY(input);
        QVERIFY(QMetaObject::invokeMethod(input, "forceActiveFocus"));
        if (producer == 1) {
            QGuiApplication::clipboard()->setText(text);
            QTest::keyClick(m_app.window(), Qt::Key_V, Qt::ControlModifier, 1);
            QTRY_COMPARE(input->property("text").toString(), text);
        } else if (producer == 2) {
            const auto before_preedit = wallet->receive()->editor()->amount()->satoshi();
            QInputMethodEvent preedit(QStringLiteral("0,"), {});
            QCoreApplication::sendEvent(input, &preedit);
            QCOMPARE(input->property("preeditText").toString(), QString("0,"));
            QCOMPARE(wallet->receive()->editor()->amount()->satoshi(), before_preedit);
            QInputMethodEvent committed;
            committed.setCommitString(text);
            QCoreApplication::sendEvent(input, &committed);
            QVERIFY(committed.isAccepted());
        } else {
            for (QChar key : text) QTest::keyClick(m_app.window(), static_cast<Qt::Key>(key.unicode()), Qt::NoModifier, 1);
        }
        const QString entered = input->property("text").toString();
        QVERIFY(qmltest::Click(m_app, "receiveSaveRequestButton"));
        QVERIFY(qmltest::WaitForVisible(m_app, "paymentRequestModal"));
        const auto blobs = storedRequests(*wallet); QCOMPARE(blobs.size(), size_t{1});
        std::vector<uint8_t> bytes(blobs[0].begin(), blobs[0].end()); DataStream record{bytes};
        int version, recipient_version; int64_t id; uint32_t timestamp; std::string address, label; CAmount persisted;
        record >> version >> id >> timestamp >> recipient_version >> address >> label >> persisted;
        QCOMPARE(version, 1); QCOMPARE(recipient_version, 1); QVERIFY(id > 0); QVERIFY(timestamp > 0); QVERIFY(!address.empty());
        QTRY_VERIFY(wallet->receive()->editor()->paymentUri().contains("amount=0.02000000"));
        const QString uri = wallet->receive()->editor()->paymentUri();
        qInfo() << "Typed Receive amount oracle" << text << "accepted text" << entered << "Core stored sats" << persisted << "QR payload" << uri;
        QVERIFY(qmltest::Click(m_app, "savedRequestCloseButton"));
        wallet->reloadReceiveRequests(); QCOMPARE(wallet->receiveRequests()->count(), 1);
        QCOMPARE(persisted, CAmount{2'000'000});
        QVERIFY(uri.contains("amount=0.02000000"));
    }

    void privateNotesSyncWithoutChangingPublicPaymentMetadata()
    {
        auto* wallet = qmltest::CreateWallet(m_app);
        QVERIFY(wallet);
        const auto saved = SaveTypedRequest(*wallet, [](PaymentRequestEditorModel& request) {
            request.amount()->setSatoshi(10'000);
            request.publicLabel()->edit("Alice");
            request.message()->edit("pizza");
            request.privateNote()->edit("Private lunch note");
        });
        QVERIFY(saved);
        const auto id = saved->id;
        const auto address = saved->address;
        auto detail = OpenTypedRequest(*wallet, id); QVERIFY(detail);
        QTRY_VERIFY(!detail->paymentUri().isEmpty());
        const auto uri = detail->paymentUri();
        QVERIFY(!id.isEmpty());
        QVERIFY(!address.isEmpty());
        QVERIFY(!uri.contains("Private"));
        QCOMPARE(wallet->receiveRequests()->count(), 1);
        UniValue address_params{UniValue::VARR};
        address_params.push_back(address.toStdString());
        const auto address_info = qmltest::Rpc(m_app, "getaddressinfo", address_params, wallet->name());
        QCOMPARE(QString::fromStdString(address_info["labels"][0].get_str()), QStringLiteral("Private lunch note"));
        auto* addresses = wallet->addressListModel();
        const auto find_address = [addresses, address] {
            for (int row{0}; row < addresses->rowCount(); ++row) {
                if (addresses->addressAt(row) == address) return addresses->index(row);
            }
            return QModelIndex{};
        };
        QTRY_VERIFY(!addresses->refreshPending() && find_address().isValid());
        const QPersistentModelIndex original_row{find_address()};
        QCOMPARE(original_row.data(AddressListModel::LabelRole).toString(), QStringLiteral("Private lunch note"));
        QSignalSpy changed{wallet->receiveRequests(), &QAbstractItemModel::modelReset};
        QVERIFY(AwaitReceiveRequest(wallet->addressListModel()->setAddressLabel(address, "Renamed on Addresses page")));
        QTRY_VERIFY(!changed.isEmpty());
        QTRY_VERIFY(!addresses->refreshPending() && original_row.isValid() &&
            original_row.data(AddressListModel::LabelRole).toString() == QStringLiteral("Renamed on Addresses page"));
        const auto renamed_address_info = qmltest::Rpc(m_app, "getaddressinfo", address_params, wallet->name());
        QCOMPARE(QString::fromStdString(renamed_address_info["labels"][0].get_str()), QStringLiteral("Renamed on Addresses page"));
        detail = OpenTypedRequest(*wallet, id); QVERIFY(detail);
        QTRY_COMPARE(detail->privateNote()->text(), QStringLiteral("Renamed on Addresses page"));
        QCOMPARE(detail->publicLabel()->text(), QStringLiteral("Alice"));
        QCOMPARE(detail->message()->text(), QStringLiteral("pizza"));
        QTRY_COMPARE(detail->paymentUri(), uri);
        QVERIFY(qmltest::Click(m_app, "activityTabButton"));
        QVERIFY(qmltest::SetText(m_app, "activitySearchField", "Renamed on Addresses page"));
        auto* filter = qobject_cast<ActivityFilterProxyModel*>(qmltest::Find(m_app, "activityFilterProxyModel"));
        QVERIFY(filter);
        QTRY_COMPARE(filter->rowCount(), 1);
        QCOMPARE(filter->data(filter->index(0, 0), TransactionActivityModel::LabelRole).toString(), QStringLiteral("Renamed on Addresses page"));
        wallet->reloadReceiveRequests();
        QTRY_VERIFY(!wallet->property("receiveRequestsBusy").toBool());
        detail = OpenTypedRequest(*wallet, id); QVERIFY(detail);
        QTRY_COMPARE(detail->privateNote()->text(), QStringLiteral("Renamed on Addresses page"));
        QTRY_COMPARE(detail->paymentUri(), uri);
        QVERIFY(AwaitReceiveRequest(wallet->removeReceiveRequest(id)));
        QCOMPARE(wallet->receiveRequests()->count(), 0);
        QVERIFY(!OpenTypedRequest(*wallet, id));
    }

    void receivingAddressCanBeReusedOrReplaced()
    {
        auto* wallet = qmltest::CreateWallet(m_app);
        QVERIFY(wallet);
        auto* receive = wallet->receive(); QVERIFY(receive);
        auto* reservation = receive->reservation();
        QTRY_VERIFY(receive->ensureAddress() && reservation->available());
        const auto address = reservation->address();
        QVERIFY(!address.isEmpty());
        // Showing Receive again reuses the unpaid address.
        QVERIFY(receive->ensureAddress()); QTRY_VERIFY(reservation->available());
        QCOMPARE(reservation->address(), address);
        QVERIFY(receive->nextAddress());
        QTRY_VERIFY(reservation->available() && reservation->address() != address);
        const auto types = wallet->availableReceiveAddressTypes();
        QVERIFY(!types.isEmpty());
        QVERIFY(receive->chooseAddressType("legacy"));
        QTRY_VERIFY(reservation->available() && reservation->addressType() == QStringLiteral("legacy"));
        const auto legacy = reservation->address();
        QVERIFY(legacy.startsWith('m') || legacy.startsWith('n'));
    }

    void requestImageExportPreservesTheSavedCoreRequestAndOriginalPath()
    {
        auto* wallet = qmltest::CreateWallet(m_app); QVERIFY(wallet);
        const auto saved = SaveTypedRequest(*wallet, [](PaymentRequestEditorModel& request) {
            request.publicLabel()->edit("export identity"); request.message()->edit("public QR terms"); request.amount()->setSatoshi(123);
        });
        QVERIFY(saved);
        QTRY_VERIFY(!wallet->receiveRequestReconciliationPending());
        const auto detail = OpenTypedRequest(*wallet, saved->id); QVERIFY(detail);
        QTRY_VERIFY(!detail->paymentUri().isEmpty());
        const auto request_id = saved->id; const auto payload = detail->paymentUri(); const auto name = wallet->name();
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const QString path = directory.path() + QStringLiteral("/original request.png");
        QObject view;
        auto* sharing = wallet->receive()->sharingRequest(request_id, &view); QVERIFY(sharing); sharing->setActive(true);
        QTRY_VERIFY_WITH_TIMEOUT(sharing->canExportQr(), 30'000);
        const QPointer<RequestImageExportOperation> operation{sharing->beginQrExport()};
        QVERIFY(operation); QTRY_VERIFY(operation->choosing());
        QVERIFY(operation->choose(QUrl::fromLocalFile(path)));
        QTRY_VERIFY(!operation->busy() || operation->confirmationRequired());
        if (operation->confirmationRequired()) QVERIFY(operation->confirm());
        QTRY_VERIFY_WITH_TIMEOUT(operation->result().outcomeKnown, 30'000);
        QVERIFY(operation->result().written);
        QCOMPARE(operation->finalPath(), path); QVERIFY(!QImage{path}.isNull());
        const auto entries = qmltest::Call(m_app, [name](interfaces::Node* node) {
            for (auto& handle : node->walletLoader().getWallets()) {
                if (handle->getWalletName() == name.toStdString()) return walletrequests::Decode(handle->getAddressReceiveRequests()).entries;
            }
            return std::vector<QmlRecentRequestEntry>{};
        });
        QCOMPARE(entries.size(), size_t{1}); QCOMPARE(QString::number(entries.front().id), request_id);
        QCOMPARE(entries.front().recipient.amount, CAmount{123});
        QCOMPARE(QString::fromStdString(entries.front().recipient.message), QStringLiteral("public QR terms"));
        QCOMPARE(detail->paymentUri(), payload);
    }

    void templateResetGeneratesAFreshAddressThroughThePublicWallet()
    {
        auto* wallet = qmltest::CreateWallet(m_app); QVERIFY(wallet);
        auto* receive = wallet->receive(); QVERIFY(receive);
        QVERIFY(receive->chooseAddressType("legacy"));
        QTRY_VERIFY(receive->reservation()->available() && receive->reservation()->addressType() == QStringLiteral("legacy"));
        const auto saved = SaveTypedRequest(*wallet, [](PaymentRequestEditorModel& request) {
            request.publicLabel()->edit("repeat invoice"); request.message()->edit("public terms");
            request.privateNote()->edit("private context"); request.amount()->setSatoshi(12'345);
        });
        QVERIFY(saved);
        const auto id = saved->id; const auto saved_address = saved->address;
        QTRY_VERIFY(receive->ensureAddress() && receive->reservation()->available());
        const auto unused_address = receive->reservation()->address();
        QVERIFY(receive->useTemplate(id));
        QTRY_VERIFY(!receive->replacementPending() && receive->editor() && receive->editor()->publicLabel()->text() == QStringLiteral("repeat invoice"));
        auto* draft = receive->editor();
        QVERIFY(draft->requestId().isEmpty());
        QCOMPARE(draft->message()->text(), QStringLiteral("public terms"));
        QCOMPARE(draft->privateNote()->text(), QStringLiteral("private context"));
        QCOMPARE(draft->amount()->satoshi(), CAmount{12'345});
        QTRY_VERIFY(receive->reservation()->available() && !draft->address().isEmpty());
        QCOMPARE(draft->addressType(), QStringLiteral("legacy"));
        const auto fresh = draft->address();
        QVERIFY(fresh != saved_address && fresh != unused_address);
        const auto info = qmltest::Rpc(m_app, "getaddressinfo", qmltest::Json("[\"" + fresh.toStdString() + "\"]"), wallet->name());
        QVERIFY(info.find_value("ismine").get_bool()); QVERIFY(!info.find_value("iswitness").get_bool());
        QCOMPARE(wallet->receiveRequests()->count(), 1); QVERIFY(draft->requestId().isEmpty());
    }

    void unchangedAddressDraftCannotHideOrOverwriteAnExternalRelabel()
    {
        auto* wallet = qmltest::CreateWallet(m_app, "label-origin"); QVERIFY(wallet);
        const auto address = labeledAddress(wallet->name(), "Original note");
        QVERIFY(openAddresses()); QTRY_VERIFY(addressRow(address));
        auto* field = qmltest::Find(addressRow(address), "addressRowNoteField"); QVERIFY(field);
        // Typing before the note loads is kept as an edit on top of it.
        QTRY_COMPARE(field->property("text").toString(), QStringLiteral("Original note"));
        QVERIFY(enterText(field, "Original notex"));
        QTest::keyClick(m_app.window(), Qt::Key_Backspace, Qt::NoModifier, 1);
        QCOMPARE(field->property("text").toString(), QStringLiteral("Original note"));
        QVERIFY(qmltest::Click(qmltest::Find(addressRow(address), "addressRowDetailsButton")));
        QVERIFY(qmltest::WaitForVisible(m_app, "addressDetailsPopup"));
        UniValue rename{UniValue::VARR}; rename.push_back(address.toStdString()); rename.push_back("External note");
        qmltest::Rpc(m_app, "setlabel", rename, wallet->name());
        auto* addresses = wallet->addressListModel();
        QTRY_VERIFY([&] {
            for (int row{0}; row < addresses->count(); ++row)
                if (addresses->addressAt(row) == address) return addresses->data(addresses->index(row), AddressListModel::LabelRole).toString() == "External note";
            return false;
        }());
        QVERIFY(qmltest::Click(m_app, "addressDetailsCloseButton"));
        QTRY_VERIFY(!qmltest::Find(m_app, "addressDetailsPopup")->property("visible").toBool());
        QTRY_VERIFY(addressRow(address)); field = qmltest::Find(addressRow(address), "addressRowNoteField"); QVERIFY(field);
        const auto rendered_after_external = field->property("text").toString();
        auto* item = qobject_cast<QQuickItem*>(field); QVERIFY(item); item->forceActiveFocus();
        QVERIFY(qmltest::Click(qmltest::Find(addressRow(address), "addressRowDetailsButton")));
        QVERIFY(qmltest::WaitForVisible(m_app, "addressDetailsPopup"));
        auto* page = qmltest::Find(m_app, "addressListPage"); QVERIFY(page);
        // Both application generations expose their actual outcome owner. The
        // independent oracle below is the same public Core value in each case.
        if (auto* operation = page->property("noteOperation").value<QObject*>()) {
            QTRY_VERIFY(!operation->property("pending").toBool());
        } else {
            auto* editor = page->property("selectedEditor").value<QObject*>(); QVERIFY(editor);
            QTRY_VERIFY(editor->property("phase").toInt() != 0 && !editor->property("busy").toBool());
        }
        const auto persisted = addressNote(wallet->name(), address);
        qInfo() << "Unchanged-label oracle" << rendered_after_external << persisted;
        QCOMPARE(persisted, QStringLiteral("External note"));
        QCOMPARE(rendered_after_external, QStringLiteral("External note"));
    }
    void addressDetailsCloseRequiresExplicitDraftConsent_data()
    {
        QTest::addColumn<QString>("choice");
        QTest::newRow("keep-editing") << QStringLiteral("keep");
        QTest::newRow("discard") << QStringLiteral("discard");
        QTest::newRow("save") << QStringLiteral("save");
    }
    void addressDetailsCloseRequiresExplicitDraftConsent()
    {
        QFETCH(QString, choice); auto* wallet = qmltest::CreateWallet(m_app, "note-close"); QVERIFY(wallet);
        const auto address = labeledAddress(wallet->name(), "Original note");
        QVERIFY(openAddresses()); QTRY_VERIFY(addressRow(address));
        QVERIFY(qmltest::Click(qmltest::Find(addressRow(address), "addressRowDetailsButton")));
        auto* popup = qmltest::WaitForVisible(m_app, "addressDetailsPopup"); QVERIFY(popup);
        auto* field = qmltest::WaitForVisible(m_app, "addressDetailsNoteField"); QVERIFY(field);
        QTRY_COMPARE(field->property("text").toString(), QStringLiteral("Original note"));
        QVERIFY(qmltest::SetText(m_app, "addressDetailsNoteField", "Unfinished private note"));
        QVERIFY(qmltest::Click(m_app, "addressDetailsCloseButton"));
        QVERIFY(QTest::qWaitFor([&] {
            auto* prompt = qmltest::Find(m_app, "addressNoteClosePrompt");
            return (prompt && prompt->property("opened").toBool()) ||
                (!popup->property("visible").toBool() && addressNote(wallet->name(), address) == "Unfinished private note");
        }));
        const auto before_consent = addressNote(wallet->name(), address);
        qInfo() << "Dirty-close oracle before consent" << before_consent;
        QCOMPARE(before_consent, QStringLiteral("Original note"));
        if (choice == "keep") {
            QVERIFY(qmltest::Click(m_app, "addressNoteKeepEditingButton"));
            QTRY_VERIFY(!qmltest::Find(m_app, "addressNoteClosePrompt")->property("visible").toBool());
            QVERIFY(popup->property("visible").toBool());
            QCOMPARE(field->property("text").toString(), QStringLiteral("Unfinished private note"));
            QVERIFY(qmltest::Click(m_app, "addressDetailsCloseButton"));
            QVERIFY(qmltest::WaitForVisible(m_app, "addressNoteClosePrompt"));
        }
        QVERIFY(qmltest::Click(m_app, choice == "save" ? "addressNoteSaveAndCloseButton" : "addressNoteDiscardAndCloseButton"));
        QTRY_VERIFY(!popup->property("visible").toBool());
        QCOMPARE(addressNote(wallet->name(), address), choice == "save" ? QStringLiteral("Unfinished private note") : QStringLiteral("Original note"));
    }
    void createRequestFromAddressDetailsAsksBeforeReplacingADraft_data()
    {
        QTest::addColumn<bool>("discard");
        QTest::newRow("keep-editing") << false;
        QTest::newRow("discard") << true;
    }
    void createRequestFromAddressDetailsAsksBeforeReplacingADraft()
    {
        // "Create payment request" in Address details starts a request for
        // another address. An unsaved Receive draft must not be discarded
        // without the user's answer.
        QFETCH(bool, discard); auto* wallet = qmltest::CreateWallet(m_app, "details-request"); QVERIFY(wallet);
        auto* receive = wallet->receive(); QVERIFY(receive);
        QTRY_VERIFY(wallet->property("receiveRequestsLoaded").toBool()); QVERIFY(receive->ensureAddress());
        QTRY_VERIFY(receive->reservation()->available() && receive->editor()->canSave());
        QPointer<PaymentRequestEditorModel> draft{receive->editor()};
        draft->message()->edit(QStringLiteral("Unsaved request draft")); QTRY_VERIFY(draft->dirty());
        const auto drafted_address = draft->address(); QVERIFY(!drafted_address.isEmpty());
        const auto address = QString::fromStdString(qmltest::Rpc(m_app, "getnewaddress", {}, wallet->name()).get_str());
        QVERIFY(address != drafted_address);
        QVERIFY(openAddresses()); QTRY_VERIFY(addressRow(address));
        QVERIFY(qmltest::Click(qmltest::Find(addressRow(address), "addressRowDetailsButton")));
        QVERIFY(qmltest::WaitForVisible(m_app, "addressDetailsPopup"));
        QVERIFY(qmltest::Click(m_app, "addressDetailsCreatePaymentRequestButton"));
        QTRY_VERIFY(draft && draft->closePrompt());
        QCOMPARE(receive->editor(), draft.data()); QVERIFY(receive->replacementPending());
        QCOMPARE(draft->message()->text(), QStringLiteral("Unsaved request draft")); QVERIFY(draft->dirty());
        if (!discard) {
            draft->closePrompt()->answer(TextEditClosePrompt::KeepEditing);
            QTRY_VERIFY(!receive->replacementPending());
            QCOMPARE(receive->editor(), draft.data()); QVERIFY(!draft->closePrompt());
            QCOMPARE(draft->address(), drafted_address);
            QCOMPARE(draft->message()->text(), QStringLiteral("Unsaved request draft"));
            return;
        }
        draft->closePrompt()->answer(TextEditClosePrompt::Discard);
        QTRY_VERIFY(!receive->replacementPending() && receive->editor() && receive->editor()->address() == address);
        QVERIFY(!receive->editor()->dirty());
        QVERIFY(receive->editor()->message()->text().isEmpty());
    }
    void untouchedRequestPrefillUsesTheLatestPrivateMetadata_data()
    {
        QTest::addColumn<QString>("current"); QTest::addColumn<int>("edit");
        QTest::newRow("renamed") << QStringLiteral("Landlord") << 0;
        QTest::newRow("cleared") << QString{} << 0;
        QTest::newRow("explicit-note") << QStringLiteral("Landlord") << 1;
        QTest::newRow("returned-to-base") << QStringLiteral("Landlord") << 2;
    }
    void untouchedRequestPrefillUsesTheLatestPrivateMetadata()
    {
        QFETCH(QString, current); QFETCH(int, edit);
        auto* wallet = qmltest::CreateWallet(m_app, "request-prefill"); QVERIFY(wallet);
        const auto address = labeledAddress(wallet->name(), "Rent");
        auto* receive = wallet->receive(); QVERIFY(receive);
        QTRY_VERIFY(receive->editor() && !receive->editor()->busy());
        QVERIFY(receive->useAddress(address));
        QTRY_VERIFY(receive->editor() && receive->editor()->address() == address && !receive->editor()->busy());
        QPointer<PaymentRequestEditorModel> request{receive->editor()};
        QTRY_COMPARE(request->privateNote()->text(), QStringLiteral("Rent"));
        if (edit == 1) request->privateNote()->edit("Explicit private note");
        if (edit == 2) { request->privateNote()->edit("Rentx"); request->privateNote()->edit("Rent"); }
        request->publicLabel()->edit("Public invoice"); request->message()->edit("Public message"); request->amount()->setSatoshi(1000);
        UniValue rename{UniValue::VARR}; rename.push_back(address.toStdString()); rename.push_back(current.toStdString());
        qmltest::Rpc(m_app, "setlabel", rename, wallet->name());
        QTRY_VERIFY(!wallet->receiveRequestReconciliationPending());
        request->save();
        QTRY_VERIFY_WITH_TIMEOUT(request && request->saveResult().requestSaved, 30'000);
        // An untouched note takes the address label current at the save. An
        // explicit note is saved with the request, but does not overwrite a
        // rename the editor never showed.
        const auto expected = edit == 1 ? QStringLiteral("Explicit private note") : current;
        QCOMPARE(addressNote(wallet->name(), address), current);
        if (edit == 1) QVERIFY(!request->saveResult().labelSaved);
        const auto blobs = storedRequests(*wallet);
        const auto records = walletrequests::Decode(blobs).entries; QCOMPARE(records.size(), size_t{1});
        QCOMPARE(QString::fromStdString(records.front().recipient.noteSelf), expected);
        QCOMPARE(records.front().recipient.label, std::string{"Public invoice"}); QCOMPARE(records.front().recipient.message, std::string{"Public message"});
        QCOMPARE(records.front().recipient.amount, CAmount{1000});
    }
    void nativeMessageEntryPreservesExactCryptoBytes_data()
    {
        QTest::addColumn<QString>("message");
        QTest::newRow("nbsp") << QStringLiteral("Audit\u00a0statement");
        QTest::newRow("ordinary-space") << QStringLiteral("Audit statement");
        QTest::newRow("unicode-tabs-newlines") << QStringLiteral("Caf\u00e9\tstatement\nnext line");
        QTest::newRow("literal-markup") << QStringLiteral("<b>literal message</b>");
    }
    void nativeMessageEntryPreservesExactCryptoBytes()
    {
        QFETCH(QString, message); auto* wallet = qmltest::CreateWallet(m_app, "exact-message"); QVERIFY(wallet);
        const auto address = qmltest::NewAddress(m_app, wallet->name(), "legacy");
        QVERIFY(openMessages());
        QVERIFY(qmltest::SetText(m_app, "signMessageAddressField", address));
        QVERIFY(qmltest::SetText(m_app, "signMessageMessageField", message));
        QVERIFY(qmltest::Click(m_app, "signMessageButton"));
        auto* model = wallet->signVerifyMessageModel(); QTRY_VERIFY(model->signingSucceeded());
        const auto signature = model->signature(); QVERIFY(!signature.isEmpty());
        UniValue parameters{UniValue::VARR}; parameters.push_back(address.toStdString()); parameters.push_back(signature.toStdString()); parameters.push_back(message.toStdString());
        const auto exact = qmltest::Rpc(m_app, "verifymessage", parameters).get_bool();
        qInfo() << "Native message oracle" << message << model->signingMessage() << exact;
        QVERIFY(exact); QCOMPARE(model->signingMessage(), message);
        const QString changed = message + QStringLiteral(" changed");
        UniValue altered{UniValue::VARR}; altered.push_back(address.toStdString()); altered.push_back(signature.toStdString()); altered.push_back(changed.toStdString());
        QVERIFY(!qmltest::Rpc(m_app, "verifymessage", altered).get_bool());
        QVERIFY(qmltest::Click(m_app, "verifyMessageTab"));
        QVERIFY(qmltest::SetText(m_app, "verifyMessageAddressField", address));
        QVERIFY(qmltest::SetText(m_app, "verifyMessageMessageField", message));
        QVERIFY(qmltest::SetText(m_app, "verifyMessageSignatureField", signature));
        QVERIFY(qmltest::Click(m_app, "verifyMessageButton")); QTRY_VERIFY(!model->verificationBusy());
        QVERIFY(model->verificationValid());
        QVERIFY(qmltest::SetText(m_app, "verifyMessageMessageField", changed));
        QVERIFY(qmltest::Click(m_app, "verifyMessageButton")); QTRY_VERIFY(!model->verificationBusy());
        QVERIFY(!model->verificationValid()); QCOMPARE(model->verificationState(), SignVerifyMessageModel::VerificationInvalid);
    }
    void publishedNbspSignatureVerifiesThroughTheNativeEditor()
    {
        auto* wallet = qmltest::CreateWallet(m_app, "nbsp-vector"); QVERIFY(wallet);
        const QString address{"mhfTimpyFDvD3n5TQ8jZtEDUh2cJE9um4z"}, message = QStringLiteral("Audit\u00a0statement");
        const QString signature{"H0NCrVXRxVR5PGH/S9LePF1qq/Qj5oGZNZ0xRTKcYey2Dw+62BnSCqU6iU53npgV3C69d9fF9L0ayZTUulOlipY="};
        UniValue parameters{UniValue::VARR}; parameters.push_back(address.toStdString()); parameters.push_back(signature.toStdString()); parameters.push_back(message.toStdString());
        QVERIFY(qmltest::Rpc(m_app, "verifymessage", parameters).get_bool());
        QVERIFY(openMessages()); QVERIFY(qmltest::Click(m_app, "verifyMessageTab"));
        QVERIFY(qmltest::SetText(m_app, "verifyMessageAddressField", address));
        QVERIFY(qmltest::SetText(m_app, "verifyMessageMessageField", message));
        QVERIFY(qmltest::SetText(m_app, "verifyMessageSignatureField", signature));
        QVERIFY(qmltest::Click(m_app, "verifyMessageButton"));
        auto* model = wallet->signVerifyMessageModel(); QTRY_VERIFY(!model->verificationBusy());
        QVERIFY(model->verificationValid());
        QVERIFY(qmltest::SetText(m_app, "verifyMessageMessageField", "Audit statement"));
        QVERIFY(qmltest::Click(m_app, "verifyMessageButton")); QTRY_VERIFY(!model->verificationBusy());
        QVERIFY(!model->verificationValid());
    }
    // #1091: a non-breaking space that arrives through paste() reaches Verify
    // and then Sign unchanged; Core's signmessage and verifymessage decide.
    void pastedNbspMessageKeepsExactBytesForVerifyAndSign()
    {
        auto* wallet = qmltest::CreateWallet(m_app, "nbsp-paste"); QVERIFY(wallet);
        const auto address = qmltest::NewAddress(m_app, wallet->name(), "legacy");
        const QString message = QStringLiteral("Audit statement"), ordinary = QStringLiteral("Audit statement");
        const auto core_verifies = [&](const QString& signature, const QString& text) {
            UniValue parameters{UniValue::VARR}; parameters.push_back(address.toStdString()); parameters.push_back(signature.toStdString()); parameters.push_back(text.toStdString());
            return qmltest::Rpc(m_app, "verifymessage", parameters).get_bool();
        };
        UniValue sign{UniValue::VARR}; sign.push_back(address.toStdString()); sign.push_back(message.toStdString());
        const auto core_signature = QString::fromStdString(qmltest::Rpc(m_app, "signmessage", sign, wallet->name()).get_str());
        QVERIFY(core_verifies(core_signature, message)); QVERIFY(!core_verifies(core_signature, ordinary));
        const auto paste = [&](const char* name) {
            QPointer<QQuickItem> field{qobject_cast<QQuickItem*>(qmltest::WaitForVisible(m_app, name))};
            if (!field) return false;
            field->forceActiveFocus();
            if (!field->hasActiveFocus() || !QMetaObject::invokeMethod(field, "selectAll")) return false;
            QTest::keyClick(field->window(), Qt::Key_Backspace, Qt::NoModifier, 1);
            QGuiApplication::clipboard()->setText(message);
            return field && QMetaObject::invokeMethod(field, "paste") && !field->property("text").toString().isEmpty();
        };
        QVERIFY(openMessages()); QVERIFY(qmltest::Click(m_app, "verifyMessageTab"));
        QVERIFY(qmltest::SetText(m_app, "verifyMessageAddressField", address));
        QVERIFY(paste("verifyMessageMessageField"));
        QVERIFY(qmltest::SetText(m_app, "verifyMessageSignatureField", core_signature));
        auto* model = wallet->signVerifyMessageModel();
        QVERIFY(qmltest::Click(m_app, "verifyMessageButton")); QTRY_VERIFY(!model->verificationBusy());
        QCOMPARE(model->verificationMessage(), message); QVERIFY(model->verificationValid());
        QVERIFY(qmltest::Click(m_app, "signMessageTab"));
        QVERIFY(qmltest::SetText(m_app, "signMessageAddressField", address));
        QVERIFY(paste("signMessageMessageField"));
        QVERIFY(qmltest::Click(m_app, "signMessageButton")); QTRY_VERIFY(model->signingSucceeded());
        QCOMPARE(model->signingMessage(), message);
        const auto signature = model->signature(); QVERIFY(!signature.isEmpty());
        QVERIFY(core_verifies(signature, message)); QVERIFY(!core_verifies(signature, ordinary));
    }
    void cancelledMessageAuthorizationClearsItsErrorAndRetainsEarlierSignature_data()
    {
        QTest::addColumn<bool>("prior_signature");
        QTest::newRow("no-prior-signature") << false; QTest::newRow("retained-signature") << true;
    }
    void cancelledMessageAuthorizationClearsItsErrorAndRetainsEarlierSignature()
    {
        QFETCH(bool, prior_signature); auto* wallet = qmltest::CreateWallet(m_app, "auth-message"); QVERIFY(wallet);
        const auto address = qmltest::NewAddress(m_app, wallet->name(), "legacy");
        QVERIFY(wallet->encryptWallet("signing password")); QTRY_COMPARE_WITH_TIMEOUT(wallet->securityState(), WalletQmlModel::SecurityState::SecuritySucceeded, qmltest::kSecurityStateTimeoutMs);
        QVERIFY(openMessages()); QVERIFY(qmltest::SetText(m_app, "signMessageAddressField", address));
        QVERIFY(qmltest::SetText(m_app, "signMessageMessageField", "Exact signing statement"));
        auto* model = wallet->signVerifyMessageModel(); QString earlier;
        if (prior_signature) {
            QVERIFY(qmltest::Click(m_app, "signMessageButton")); QVERIFY(qmltest::WaitForVisible(m_app, "signMessagePassphrasePopup"));
            QVERIFY(qmltest::SetText(m_app, "signMessagePassphraseField", "signing password"));
            QVERIFY(qmltest::Click(m_app, "signMessagePassphraseConfirmButton")); QTRY_VERIFY(model->signingSucceeded());
            earlier = model->signature(); QVERIFY(!earlier.isEmpty());
        }
        QVERIFY(qmltest::Click(m_app, "signMessageButton")); QVERIFY(qmltest::WaitForVisible(m_app, "signMessagePassphrasePopup"));
        QVERIFY(qmltest::SetText(m_app, "signMessagePassphraseField", "wrong password"));
        QVERIFY(qmltest::Click(m_app, "signMessagePassphraseConfirmButton"));
        QTRY_VERIFY(!model->signingBusy() && !model->signingError().isEmpty());
        QVERIFY(qmltest::Click(m_app, "signMessagePassphraseCancelButton"));
        QTRY_VERIFY(!qmltest::Find(m_app, "signMessagePassphrasePopup")->property("visible").toBool());
        qInfo() << "Cancelled authorization oracle" << model->signingNeedsUnlock() << model->signingError() << model->signature().size();
        QVERIFY(!model->signingNeedsUnlock()); QVERIFY(model->signingError().isEmpty());
        QCOMPARE(model->signature(), earlier);
        QCOMPARE(qmltest::Rpc(m_app, "getwalletinfo", {}, wallet->name()).find_value("unlocked_until").getInt<int64_t>(), 0);
        QVERIFY(model->signingResult()); QCOMPARE(model->signingResult()->error, walletmessage::Error::IncorrectPassphrase);
    }
    void addressDraftSurvivesBackWithoutImplicitPersistence()
    {
        auto* wallet = qmltest::CreateWallet(m_app, "draft-back"); QVERIFY(wallet);
        const auto address = labeledAddress(wallet->name(), "Original note");
        QVERIFY(openAddresses()); QTRY_VERIFY(addressRow(address));
        auto* field = qmltest::Find(addressRow(address), "addressRowNoteField"); QVERIFY(field);
        QVERIFY(enterText(field, "Unfinished private note"));
        QVERIFY(qmltest::Click(m_app, "addressListBackButton"));
        QVERIFY(qmltest::WaitForVisible(m_app, "walletAddressesRow"));
        QCOMPARE(addressNote(wallet->name(), address), QStringLiteral("Original note"));
        QVERIFY(qmltest::Click(m_app, "walletAddressesRow")); QTRY_VERIFY(addressRow(address));
        field = qmltest::Find(addressRow(address), "addressRowNoteField"); QVERIFY(field);
        QTRY_COMPARE(field->property("text").toString(), QStringLiteral("Unfinished private note"));
        auto* editor = wallet->addressListModel()->labelEditor(address); QVERIFY(editor); QVERIFY(editor->dirty());
        editor->discard(); QTRY_COMPARE(field->property("text").toString(), QStringLiteral("Original note"));
        QCOMPARE(addressNote(wallet->name(), address), QStringLiteral("Original note"));
    }
    void labelCommitPreservesTheNextRowsFocusedEditor()
    {
        auto* wallet = qmltest::CreateWallet(m_app, "label-focus"); QVERIFY(wallet);
        const auto alpha = labeledAddress(wallet->name(), "Alpha"), beta = labeledAddress(wallet->name(), "Beta");
        QVERIFY(openAddresses()); QTRY_VERIFY(addressRow(alpha) && addressRow(beta));
        QPointer<QObject> first = addressRow(alpha), second = addressRow(beta);
        QPointer<QObject> field = qmltest::Find(second, "addressRowNoteField"); QVERIFY(field);
        QSignalSpy reset{wallet->addressListModel(), &QAbstractItemModel::modelReset};
        QVERIFY(enterText(qmltest::Find(first, "addressRowNoteField"), "Alpha committed"));
        QVERIFY(editorCenterIsHittable(field));
        QVERIFY(qmltest::Click(field)); QTRY_VERIFY(field->property("activeFocus").toBool());
        QTRY_COMPARE(addressNote(wallet->name(), alpha), QStringLiteral("Alpha committed"));
        QVERIFY(first && second && field); QCOMPARE(addressRow(beta), second.data());
        QVERIFY(field->property("activeFocus").toBool()); QCOMPARE(field->property("text").toString(), QStringLiteral("Beta"));
        QCOMPARE(reset.size(), 0); QCOMPARE(addressNote(wallet->name(), beta), QStringLiteral("Beta"));
    }
    void publicWatchOnlyWalletRefusesSigningWithoutPasswordPrompt()
    {
        auto* owner = qmltest::CreateWallet(m_app, "message-key-owner"); QVERIFY(owner);
        const auto address = qmltest::NewAddress(m_app, owner->name(), "legacy");
        UniValue create{UniValue::VOBJ}; create.pushKV("wallet_name", "message-watch-only"); create.pushKV("disable_private_keys", true);
        qmltest::Rpc(m_app, "createwallet", create);
        auto* wallet = qmltest::SelectOpenWalletForSetup(m_app, "message-watch-only"); QVERIFY(wallet);
        QVERIFY(!qmltest::Rpc(m_app, "getwalletinfo", {}, wallet->name()).find_value("private_keys_enabled").get_bool());
        QVERIFY(openMessages()); QVERIFY(qmltest::SetText(m_app, "signMessageAddressField", address));
        QVERIFY(qmltest::SetText(m_app, "signMessageMessageField", "Watch-only cannot sign"));
        QVERIFY(qmltest::Click(m_app, "signMessageButton")); auto* model = wallet->signVerifyMessageModel();
        QTRY_VERIFY(!model->signingBusy()); QCOMPARE(model->signingState(), SignVerifyMessageModel::SigningFailed);
        QVERIFY(!model->signingNeedsUnlock()); QVERIFY(!model->signingPrompt()); QVERIFY(model->signature().isEmpty());
        QVERIFY(!qmltest::Find(m_app, "signMessagePassphrasePopup")->property("visible").toBool());
        QCOMPARE(model->error().code, MessageSigningError::Signing);
    }
    void retiredLabelEditorCannotWriteToTheReopenedWallet()
    {
        auto* wallet = qmltest::CreateWallet(m_app, "label-reopen"); QVERIFY(wallet); const auto name = wallet->name();
        const auto opening = wallet->opening()->identity();
        const auto address = labeledAddress(name, "Original note");
        QVERIFY(openAddresses()); QTRY_VERIFY(addressRow(address));
        QVERIFY(qmltest::Click(qmltest::Find(addressRow(address), "addressRowDetailsButton")));
        QVERIFY(qmltest::WaitForVisible(m_app, "addressDetailsPopup"));
        QVERIFY(qmltest::SetText(m_app, "addressDetailsNoteField", "Retained original draft"));
        auto* page = qmltest::Find(m_app, "addressListPage"); QVERIFY(page);
        auto* editor = qobject_cast<TextEditSessionModel*>(page->property("selectedEditor").value<QObject*>()); QVERIFY(editor);
        QTRY_VERIFY(editor->dirty()); editor->setParent(nullptr); QQmlEngine::setObjectOwnership(editor, QQmlEngine::CppOwnership);
        std::unique_ptr<TextEditSessionModel> retained{editor};
        UniValue unload{UniValue::VARR}; unload.push_back(name.toStdString());
        // No dialog-close command precedes unload. The retained projection is
        // deliberately detached; Core must not wait for its GUI destruction.
        qmltest::Rpc(m_app, "unloadwallet", unload);
        auto* controller = qmltest::Model<WalletQmlController>(m_app, "walletController"); QVERIFY(controller);
        QTRY_VERIFY(!controller->walletForName(name)); QTRY_VERIFY(!editor->available());
        UniValue load{UniValue::VARR}; load.push_back(name.toStdString()); qmltest::Rpc(m_app, "loadwallet", load);
        QTRY_VERIFY(controller->walletForName(name) && controller->walletForName(name)->available());
        QVERIFY(controller->walletForName(name)->opening()->identity() != opening);
        QSignalSpy rejected{editor, &TextEditSessionModel::commandRejected};
        editor->text()->edit("Must not reach successor"); editor->save();
        QCOMPARE(rejected.size(), 2); QCOMPARE(editor->entityId(), address);
        QCOMPARE(editor->error().code, TextEditError::Closed); QVERIFY(!editor->saveResult().present);
        QCOMPARE(addressNote(name, address), QStringLiteral("Original note"));
    }

    void signingUnlocksOnlyForTheOperation()
    {
        auto* wallet = qmltest::CreateWallet(m_app);
        QVERIFY(wallet);
        const auto address = qmltest::NewAddress(m_app, wallet->name(), "legacy");
        QVERIFY(wallet->encryptWallet("signing password"));
        QTRY_COMPARE_WITH_TIMEOUT(wallet->securityState(), WalletQmlModel::SecurityState::SecuritySucceeded, qmltest::kSecurityStateTimeoutMs);
        auto* signer = wallet->signVerifyMessageModel();
        QVERIFY(!AwaitMessageSigning(*signer, address, "integration message"));
        QVERIFY(signer->signingNeedsUnlock());
        QVERIFY(!AwaitMessageSigning(*signer, address, "integration message", "incorrect"));
        QVERIFY(AwaitMessageSigning(*signer, address, "integration message", "signing password"));
        const auto signature = signer->signature();
        QVERIFY(!signature.isEmpty());
        QVERIFY(AwaitMessageVerification(*signer, address, "integration message", signature));
        QVERIFY(!AwaitMessageVerification(*signer, address, "changed message", signature));
        QVERIFY(wallet->isLocked());
        QCOMPARE(qmltest::Rpc(m_app, "getwalletinfo", {}, wallet->name()).find_value("unlocked_until").getInt<int64_t>(), 0);
    }

    void receivingUnlockCanBeCancelledRetriedAndRelocks()
    {
        auto* wallet = qmltest::CreateLockedHardenedWallet(m_app, "receive_unlock", "receiving password");
        QVERIFY(wallet);
        const auto info = qmltest::Rpc(m_app, "getwalletinfo", {}, wallet->name());
        const auto cached_keys = info.find_value("keypoolsize").getInt<int>();
        for (int i{0}; i < cached_keys; ++i) qmltest::NewAddress(m_app, wallet->name());
        QCOMPARE(qmltest::Rpc(m_app, "getwalletinfo", {}, wallet->name()).find_value("keypoolsize").getInt<int>(), 0);
        QVERIFY(wallet->receive()->reservation()->address().isEmpty());

        // Cancelling address creation must leave both Core and request history unchanged.
        QVERIFY(qmltest::Click(m_app, "receiveTabButton"));
        auto* popup = qmltest::WaitForVisible(m_app, "receivePassphrasePopup");
        QVERIFY(popup);
        QTRY_VERIFY(popup->property("opened").toBool());
        QVERIFY(qmltest::SetText(m_app, "receivePassphraseField", "cancelled password"));
        QVERIFY(qmltest::Click(m_app, "receivePassphraseCancelButton"));
        QTRY_VERIFY(!popup->property("visible").toBool());
        QVERIFY(qmltest::SecretTextLocations(m_app.window(), "cancelled password").isEmpty());
        QCOMPARE(qmltest::Find(m_app, "receivePassphraseField")->property("text").toString(), QString{});
        QVERIFY(wallet->receive()->reservation()->address().isEmpty());
        QCOMPARE(wallet->receiveRequests()->count(), 0);
        QVERIFY(wallet->isLocked());
        QCOMPARE(qmltest::Rpc(m_app, "getwalletinfo", {}, wallet->name()).find_value("unlocked_until").getInt<int64_t>(), 0);

        // Reopening does not replay cancelled authorization. An explicit retry
        // captures a fresh attempt, retaining the genuine wrong-password oracle.
        QVERIFY(qmltest::Click(m_app, "activityTabButton"));
        QVERIFY(qmltest::Click(m_app, "receiveTabButton"));
        QVERIFY(!popup->property("opened").toBool());
        QCOMPARE(wallet->receive()->reservation()->phase(), ReceiveReservationModel::Cancelled);
        QVERIFY(qmltest::Click(m_app, "receiveRetryAddressButton"));
        QTRY_VERIFY(popup->property("opened").toBool());
        QVERIFY(qmltest::SetText(m_app, "receivePassphraseField", "wrong password"));
        QVERIFY(qmltest::Click(m_app, "receivePassphraseConfirmButton"));
        auto* error = qmltest::WaitForVisible(m_app, "receivePassphraseError");
        QVERIFY(error);
        QVERIFY(error->property("text").toString().contains("incorrect"));
        QVERIFY(popup->property("opened").toBool());
        QCOMPARE(qmltest::Find(m_app, "receivePassphraseField")->property("text").toString(), QString{});
        QVERIFY(qmltest::SecretTextLocations(m_app.window(), "wrong password").isEmpty());
        QVERIFY(wallet->receive()->reservation()->address().isEmpty());
        QVERIFY(wallet->isLocked());
        QCOMPARE(wallet->receiveRequests()->count(), 0);
        QCOMPARE(qmltest::Rpc(m_app, "getwalletinfo", {}, wallet->name()).find_value("unlocked_until").getInt<int64_t>(), 0);

        // A correct password generates the requested address and immediately relocks.
        QVERIFY(qmltest::SetText(m_app, "receivePassphraseField", "receiving password"));
        QVERIFY(qmltest::Click(m_app, "receivePassphraseConfirmButton"));
        QTRY_VERIFY(!popup->property("visible").toBool());
        QVERIFY(qmltest::SecretTextLocations(m_app.window(), "receiving password").isEmpty());
        const auto address = wallet->receive()->reservation()->address();
        QVERIFY(!address.isEmpty());
        QTRY_VERIFY(qmltest::Find(m_app, "receiveSaveRequestButton")->property("enabled").toBool());
        const auto address_info = qmltest::Rpc(m_app, "getaddressinfo", qmltest::Json("[\"" + address.toStdString() + "\"]"), wallet->name());
        QVERIFY(address_info.find_value("ismine").get_bool());
        QCOMPARE(address_info.find_value("witness_version").getInt<int>(), 0);
        QVERIFY(wallet->isLocked());
        QCOMPARE(qmltest::Rpc(m_app, "getwalletinfo", {}, wallet->name()).find_value("unlocked_until").getInt<int64_t>(), 0);
        QCOMPARE(wallet->receiveRequests()->count(), 0);
        QVERIFY(wallet->receive()->editor()->requestId().isEmpty());

        // The next-address request must survive another unlock, without reusing this address.
        QVERIFY(wallet->receive()->nextAddress());
        QTRY_VERIFY(popup->property("opened").toBool());
        QVERIFY(qmltest::SetText(m_app, "receivePassphraseField", "receiving password"));
        QVERIFY(qmltest::Click(m_app, "receivePassphraseConfirmButton"));
        QTRY_VERIFY(!popup->property("visible").toBool());
        QVERIFY(qmltest::SecretTextLocations(m_app.window(), "receiving password").isEmpty());
        QVERIFY(!wallet->receive()->reservation()->address().isEmpty());
        QVERIFY(wallet->receive()->reservation()->address() != address);
        QVERIFY(wallet->isLocked());
        QCOMPARE(qmltest::Rpc(m_app, "getwalletinfo", {}, wallet->name()).find_value("unlocked_until").getInt<int64_t>(), 0);
        QCOMPARE(wallet->receiveRequests()->count(), 0);
    }

    void addressDetailsCreateAndUpdateTheSamePaymentRequest()
    {
        auto* wallet = qmltest::CreateWallet(m_app);
        QVERIFY(wallet);
        const auto address = QString::fromStdString(qmltest::Rpc(m_app, "getnewaddress", qmltest::Json("[\"invoice 1024\"]"), wallet->name()).get_str());
        QVERIFY(qmltest::SelectSettings(m_app, "wallet"));
        QVERIFY(qmltest::Click(m_app, "walletAddressesRow"));
        QVERIFY(qmltest::WaitForVisible(m_app, "addressListPage"));
        QTRY_VERIFY(addressRow(address));
        QVERIFY(qmltest::Click(qmltest::Find(addressRow(address), "addressRowDetailsButton")));
        QVERIFY(qmltest::WaitForVisible(m_app, "addressDetailsPopup"));
        auto* page = qmltest::Find(m_app, "addressListPage"); QVERIFY(page);
        QSignalSpy receive_requested{page, SIGNAL(receiveRequested())};
        QVERIFY(qmltest::Click(m_app, "addressDetailsCreatePaymentRequestButton"));
        QTRY_COMPARE(receive_requested.count(), 1);
        QVERIFY(qmltest::WaitForVisible(m_app, "receivePage"));
        QTRY_COMPARE(wallet->receive()->reservation()->address(), address);
        QTRY_VERIFY(wallet->receive()->reservation()->available());
        QTRY_COMPARE(wallet->receive()->editor()->privateNote()->text(), QStringLiteral("invoice 1024"));
        QVERIFY(qmltest::SetText(m_app, "receiveAmountInput", "0.25"));
        QVERIFY(QMetaObject::invokeMethod(qmltest::Find(m_app, "receiveAmountInput"), "editingFinished"));
        QVERIFY(qmltest::Click(m_app, "receiveSaveRequestButton"));
        auto* saved = qmltest::WaitForVisible(m_app, "paymentRequestModal"); QVERIFY(saved);
        QCOMPARE(wallet->receiveRequests()->count(), 1);
        // The modal blocks the tab bar until it has actually closed.
        QVERIFY(closeSettledRequest(*wallet, saved));

        // The Addresses stack is retained, and its action now views the saved request.
        QVERIFY(qmltest::Click(m_app, "desktopWalletSettingsTabButton"));
        QVERIFY(qmltest::WaitForVisible(m_app, "addressListPage"));
        QTRY_VERIFY(addressRow(address));
        QVERIFY(qmltest::Click(qmltest::Find(addressRow(address), "addressRowDetailsButton")));
        auto* action = qmltest::WaitForVisible(m_app, "addressDetailsCreatePaymentRequestButton");
        QVERIFY(action);
        QCOMPARE(action->property("text").toString(), QStringLiteral("View payment request"));
        QVERIFY(qmltest::Click(action));
        auto* viewed = qmltest::WaitForVisible(m_app, "paymentRequestModal"); QVERIFY(viewed);
        QVERIFY(qmltest::SetText(m_app, "receiveAmountInput", "0.5"));
        QVERIFY(QMetaObject::invokeMethod(qmltest::Find(m_app, "receiveAmountInput"), "editingFinished"));
        QCOMPARE(wallet->receiveRequests()->count(), 1);
        QVERIFY(qmltest::Click(m_app, "receiveSaveRequestButton"));
        QVERIFY(qmltest::WaitForVisible(m_app, "savedRequestCopyUriButton"));
        QCOMPARE(wallet->receiveRequests()->count(), 1);
        QCOMPARE(wallet->receive()->editor()->address(), address);
        QVERIFY(closeSettledRequest(*wallet, viewed));
        QVERIFY(qmltest::Click(m_app, "desktopWalletSettingsTabButton"));
        QVERIFY(qmltest::WaitForVisible(m_app, "addressListPage"));
    }
    // #1083: Activity reuses its request modal. Clearing A's amount and then
    // opening blank B changes no text, so Undo must not bring A's amount back.
    void activityRequestUndoStaysWithTheOpenedRequest()
    {
        auto* wallet = qmltest::CreateWallet(m_app, "request-undo"); QVERIFY(wallet); auto* receive = wallet->receive(); QVERIFY(receive);
        const auto first = saveTypedRequest(receive, 421'000); QVERIFY(!first.isEmpty());
        const auto second = saveTypedRequest(receive, 0); QVERIFY(!second.isEmpty());
        QVERIFY(qmltest::Click(m_app, "activityTabButton"));
        auto* a = openFromActivity(first); QVERIFY(a); const auto a_address = a->address(); QCOMPARE(persistedAmount(*wallet, a_address), std::optional<CAmount>{421'000});
        QPointer<QQuickItem> amount{qobject_cast<QQuickItem*>(qmltest::WaitForVisible(m_app, "receiveAmountInput"))}; QVERIFY(amount);
        QVERIFY(!amount->property("text").toString().isEmpty());
        amount->forceActiveFocus(); QVERIFY(amount->hasActiveFocus()); QVERIFY(QMetaObject::invokeMethod(amount, "selectAll"));
        QTest::keyClick(amount->window(), Qt::Key_Backspace, Qt::NoModifier, 1);
        QCOMPARE(amount->property("text").toString(), QString{}); QTRY_COMPARE(a->amount()->satoshi(), CAmount{0});
        QVERIFY(qmltest::Click(m_app, "receiveSaveRequestButton")); QTRY_COMPARE(persistedAmount(*wallet, a_address), std::optional<CAmount>{0});
        QTRY_VERIFY(!a->busy());
        QVERIFY(qmltest::Click(m_app, "savedRequestCloseButton"));
        auto* modal = qmltest::Find(m_app, "paymentRequestModal"); QTRY_VERIFY(!modal->property("visible").toBool());
        auto* b = openFromActivity(second); QVERIFY(b); QVERIFY(b != a); const auto b_address = b->address();
        amount = qobject_cast<QQuickItem*>(qmltest::WaitForVisible(m_app, "receiveAmountInput")); QVERIFY(amount);
        QCOMPARE(amount->property("text").toString(), QString{}); QCOMPARE(b->amount()->satoshi(), CAmount{0});
        amount->forceActiveFocus(); QVERIFY(amount->hasActiveFocus());
        QTest::keyClick(amount->window(), Qt::Key_Z, Qt::ControlModifier, 1);
        QCOMPARE(amount->property("text").toString(), QString{}); QCOMPARE(b->amount()->satoshi(), CAmount{0});
        auto* update = qmltest::Find(m_app, "receiveSaveRequestButton"); QVERIFY(update);
        if (update->property("enabled").toBool()) { QVERIFY(qmltest::Click(update)); QTRY_VERIFY(!b->busy()); }
        QCOMPARE(persistedAmount(*wallet, b_address), std::optional<CAmount>{0}); QCOMPARE(persistedAmount(*wallet, a_address), std::optional<CAmount>{0});
        QVERIFY(qmltest::Click(m_app, "savedRequestCloseButton")); QTRY_VERIFY(!modal->property("visible").toBool());
        b = openFromActivity(second); QVERIFY(b); QCOMPARE(b->amount()->satoshi(), CAmount{0});
    }
    // draft-185-RC43: switching wallets fades the open request modal; until it is gone it must keep showing
    // the original wallet's request, never the new wallet's previously viewed one.
    void walletSwitchKeepsTheFadingRequestModalOnItsOriginalRequest()
    {
        auto* other = qmltest::CreateWallet(m_app, "rc43-other"); QVERIFY(other);
        const auto other_id = saveMessageRequest(other->receive(), "other wallet request"); QVERIFY(!other_id.isEmpty());
        QVERIFY(qmltest::Click(m_app, "activityTabButton")); QVERIFY(openFromActivity(other_id));
        QVERIFY(qmltest::Click(m_app, "savedRequestCloseButton"));
        auto* other_view = viewFor(other); QVERIFY(other_view); auto* other_modal = qmltest::Find(other_view, "paymentRequestModal"); QVERIFY(other_modal);
        QTRY_VERIFY(!other_modal->property("visible").toBool());
        auto* wallet = qmltest::CreateWallet(m_app, "rc43-original"); QVERIFY(wallet);
        const auto id = saveMessageRequest(wallet->receive(), "original wallet request"); QVERIFY(!id.isEmpty());
        QVERIFY(qmltest::Click(m_app, "activityTabButton"));
        QPointer<PaymentRequestEditorModel> opened{openFromActivity(id)}; QVERIFY(opened);
        auto* view = viewFor(wallet); QVERIFY(view && view != other_view);
        auto* modal = qmltest::Find(view, "paymentRequestModal"); QVERIFY(modal && modal->property("opened").toBool());
        auto* card = modal->property("card").value<QObject*>(); QVERIFY(card);
        auto* controller = qmltest::Model<WalletQmlController>(m_app, "walletController"); QVERIFY(controller);
        QVERIFY(controller->selectWallet(other));
        QElapsedTimer fade; fade.start();
        do {
            auto* editor = card->property("editor").value<PaymentRequestEditorModel*>();
            const auto* receive = card->property("receive").value<WalletReceiveModel*>();
            QVERIFY(!editor || editor == opened); QVERIFY(!receive || receive == wallet->receive());
            if (editor) QCOMPARE(editor->message()->text(), QStringLiteral("original wallet request"));
            QVERIFY(!other_modal->property("visible").toBool());
            QTest::qWait(5);
        } while (modal->property("visible").toBool() && fade.elapsed() < 5000);
        QVERIFY(!modal->property("visible").toBool()); QCOMPARE(modal->property("openedEditor").value<QObject*>(), nullptr);
        QVERIFY(!other_modal->property("visible").toBool()); QCOMPARE(other_modal->property("openedEditor").value<QObject*>(), nullptr);
        QVERIFY(controller->selectWallet(wallet)); QTRY_COMPARE(controller->selectedWallet(), wallet);
        QTest::qWait(200); QVERIFY(!modal->property("visible").toBool()); QCOMPARE(modal->property("openedEditor").value<QObject*>(), nullptr);
    }
    // draft-287-RC43: a wallet switch must not close the transaction page that owns the open request modal.
    void walletSwitchKeepsThePageThatOwnsTheOpenRequestModal()
    {
        auto* other = qmltest::CreateWallet(m_app, "rc43-page-other"); QVERIFY(other);
        auto* wallet = qmltest::CreateWallet(m_app, "rc43-page"); QVERIFY(wallet); QVERIFY(qmltest::FundWallet(m_app, *wallet));
        const auto id = saveMessageRequest(wallet->receive(), "owned request"); QVERIFY(!id.isEmpty());
        const auto txid = QString::fromStdString(qmltest::Rpc(m_app, "sendtoaddress", qmltest::Json("{\"address\":\"" + qmltest::NewAddress(m_app, wallet->name()).toStdString() + "\",\"amount\":1,\"fee_rate\":1}"), wallet->name()).get_str());
        QTRY_VERIFY(hasTransaction(*wallet, txid));
        QVERIFY(qmltest::Click(m_app, "activityTabButton"));
        auto* view = viewFor(wallet); QVERIFY(view); auto* stack = qmltest::Find(view, "activityStack"); QVERIFY(stack);
        QVERIFY(QMetaObject::invokeMethod(stack, "navigateToTransaction", Q_ARG(QVariant, txid), Q_ARG(QVariant, -1)));
        QTRY_COMPARE(stack->property("depth").toInt(), 2);
        QPointer<QObject> detail{stack->property("currentItem").value<QObject*>()}; QVERIFY(detail); QCOMPARE(detail->objectName(), QStringLiteral("activityDetailsPage"));
        QVERIFY(QMetaObject::invokeMethod(detail, "openPaymentRequestDetail", Q_ARG(QVariant, id)));
        auto* host = qmltest::Find(detail, "transactionPaymentRequestModal"); QVERIFY(host); QTRY_VERIFY(host->property("opened").toBool());
        auto* controller = qmltest::Model<WalletQmlController>(m_app, "walletController"); QVERIFY(controller);
        QVERIFY(controller->selectWallet(other)); QTRY_COMPARE(controller->selectedWallet(), other);
        QTRY_VERIFY(!host->property("visible").toBool()); QTest::qWait(300);
        QVERIFY(detail); QCOMPARE(stack->property("depth").toInt(), 2); QCOMPARE(stack->property("currentItem").value<QObject*>(), detail.data());
        QVERIFY(controller->selectWallet(wallet)); QTRY_COMPARE(controller->selectedWallet(), wallet); QTest::qWait(200);
        QVERIFY(detail); QCOMPARE(stack->property("depth").toInt(), 2); QCOMPARE(stack->property("currentItem").value<QObject*>(), detail.data());
        QCOMPARE(detail->property("txid").toString(), txid); QVERIFY(!host->property("visible").toBool());
    }
    // #972: request ids repeat across wallets; deleting one wallet's request leaves the other wallet's request with the same id intact.
    void deletingARequestLeavesTheSameIdInAnotherWallet()
    {
        auto* other = qmltest::CreateWallet(m_app, "same-id-other"); QVERIFY(other);
        const auto other_id = saveMessageRequest(other->receive(), "other wallet request"); QVERIFY(!other_id.isEmpty());
        QVERIFY(qmltest::Click(m_app, "activityTabButton")); QVERIFY(openFromActivity(other_id));
        auto* wallet = qmltest::CreateWallet(m_app, "same-id-original"); QVERIFY(wallet);
        const auto id = saveMessageRequest(wallet->receive(), "original wallet request"); QCOMPARE(id, other_id);
        QPointer<PaymentRequestEditorModel> deletion{wallet->receive()->deleteRequest(id)}; QVERIFY(deletion);
        QTRY_VERIFY(deletion && deletion->closePrompt()); deletion->closePrompt()->answer(TextEditClosePrompt::Save);
        QTRY_VERIFY(deletion && deletion->saveResult().erased); QTRY_COMPARE(wallet->receiveRequests()->count(), 0);
        const auto stored = [&](WalletQmlModel* owner) {
            return walletrequests::Decode(storedRequests(*owner)).entries;
        };
        QVERIFY(stored(wallet).empty());
        const auto kept = stored(other); QCOMPARE(kept.size(), size_t{1}); QCOMPARE(QString::number(kept.front().id), other_id);
        QCOMPARE(QString::fromStdString(kept.front().recipient.message), QStringLiteral("other wallet request"));
        QCOMPARE(other->receiveRequests()->count(), 1);
        auto* controller = qmltest::Model<WalletQmlController>(m_app, "walletController"); QVERIFY(controller);
        QVERIFY(controller->selectWallet(other)); QTRY_COMPARE(controller->selectedWallet(), other);
        auto* modal = qmltest::Find(viewFor(other), "paymentRequestModal"); QVERIFY(modal); QTRY_VERIFY(!modal->property("visible").toBool());
        auto* reopened = openFromActivity(other_id); QVERIFY(reopened); QCOMPARE(reopened->message()->text(), QStringLiteral("other wallet request"));
    }

    void originalReorgRepaintsTheOpenRequestFromActualCurrentCredit_data()
    {
        QTest::addColumn<bool>("invalidate");
        QTest::newRow("active-receipt-control") << false;
        QTest::newRow("original-reorg-to-zero") << true;
    }
    void originalReorgRepaintsTheOpenRequestFromActualCurrentCredit()
    {
        QFETCH(bool, invalidate);
        auto* wallet = qmltest::CreateWallet(m_app, "original-reorg"); QVERIFY(wallet);
        const auto saved = SaveTypedRequest(*wallet, [](PaymentRequestEditorModel& editor) {
            editor.amount()->setSatoshi(100'000'000); editor.message()->edit("Original reorg receipt");
        });
        QVERIFY(saved); const auto id = saved->id, address = saved->address;
        QVERIFY(qmltest::Click(m_app, "activityTabButton"));
        QPointer<PaymentRequestEditorModel> detail{openFromActivity(id)}; QVERIFY(detail);
        QObject view;
        auto* sharing = wallet->receive()->sharingRequest(id, &view); QVERIFY(sharing); sharing->setActive(true);
        // The saved request renders actual reconciled credit, while its
        // independent immutable address-use guard remains set.
        QPointer<QObject> status = qmltest::Find(m_app, "savedRequestReceiptStatus");
        QVERIFY(status);
        const auto status_text = [&] { return status ? status->property("text").toString() : QString{}; };
        UniValue mine{UniValue::VARR}; mine.push_back(1); mine.push_back(address.toStdString());
        const auto blocks = qmltest::Rpc(m_app, "generatetoaddress", mine);
        UniValue block{UniValue::VARR}; block.push_back(blocks[0].get_str());
        qmltest::Rpc(m_app, "syncwithvalidationinterfacequeue");
        QTRY_COMPARE_WITH_TIMEOUT(detail->receipts()->receivedAmountSatoshi(), qint64{50 * COIN}, 30'000);
        QTRY_COMPARE_WITH_TIMEOUT(status_text(), QStringLiteral("Payment received"), 30'000);
        if (invalidate) {
            qmltest::Rpc(m_app, "invalidateblock", block); qmltest::Rpc(m_app, "syncwithvalidationinterfacequeue");
            QTRY_COMPARE_WITH_TIMEOUT(detail->receipts()->receivedAmountSatoshi(), qint64{0}, 30'000);
        }
        QTRY_VERIFY_WITH_TIMEOUT(!wallet->receiveRequestReconciliationPending(), 30'000);
        QTRY_VERIFY_WITH_TIMEOUT(detail->historicallyUsed(), 30'000);
        QTRY_VERIFY_WITH_TIMEOUT(!sharing->canCopyQr() && !sharing->canExportQr(), 30'000);
        QTRY_COMPARE_WITH_TIMEOUT(status_text(), invalidate ? QStringLiteral("Awaiting payment") : QStringLiteral("Payment received"), 30'000);
        QCOMPARE(detail->requestId(), id); QCOMPARE(detail->address(), address);
        QVERIFY(qmltest::Find(m_app, "paymentRequestModal")->property("visible").toBool());
    }
    // #1042: an RBF replacement paying elsewhere takes the request's only
    // credit, first in the mempool and then mined as a conflict. The status
    // must return to Awaiting payment while the address stays used, after a
    // reopen and after reloading the wallet with the request open.
    void replacementPayingElsewhereReturnsTheOpenRequestToAwaitingPayment()
    {
        auto* payer = qmltest::CreateWallet(m_app, "rbf-payer"); QVERIFY(payer); QVERIFY(qmltest::FundWallet(m_app, *payer));
        auto* wallet = qmltest::CreateWallet(m_app, "rbf-receiver"); QVERIFY(wallet); const auto name = wallet->name();
        const auto saved = SaveTypedRequest(*wallet, [](PaymentRequestEditorModel& editor) {
            editor.amount()->setSatoshi(200'000); editor.message()->edit("Replaced elsewhere");
        });
        QVERIFY(saved); const auto id = saved->id, address = saved->address;
        QVERIFY(qmltest::Click(m_app, "activityTabButton"));
        QPointer<PaymentRequestEditorModel> editor{openFromActivity(id)}; QVERIFY(editor);
        QObject view;
        QPointer<RequestSharingModel> sharing{wallet->receive()->sharingRequest(id, &view)}; QVERIFY(sharing); sharing->setActive(true);
        const auto status_text = [&] { auto* status = qmltest::Find(m_app, "savedRequestReceiptStatus"); return status ? status->property("text").toString() : QString{}; };
        const auto payer_address = qmltest::NewAddress(m_app, payer->name()), change = qmltest::NewAddress(m_app, payer->name());
        const auto coins = qmltest::Rpc(m_app, "listunspent", {}, payer->name()); QVERIFY(coins.size() > 0);
        UniValue input{UniValue::VOBJ}; input.pushKV("txid", coins[0].find_value("txid")); input.pushKV("vout", coins[0].find_value("vout"));
        UniValue inputs{UniValue::VARR}; inputs.push_back(input); const auto value = AmountFromValue(coins[0].find_value("amount"));
        const auto broadcast = [&](const UniValue& outputs) {
            UniValue create{UniValue::VARR}; create.push_back(inputs); create.push_back(outputs); create.push_back(0); create.push_back(true);
            UniValue raw{UniValue::VARR}; raw.push_back(qmltest::Rpc(m_app, "createrawtransaction", create));
            const auto signed_tx = qmltest::Rpc(m_app, "signrawtransactionwithwallet", raw, payer->name());
            UniValue hex{UniValue::VARR}; hex.push_back(signed_tx.find_value("hex"));
            return signed_tx.find_value("complete").get_bool() ? qmltest::Rpc(m_app, "sendrawtransaction", hex).get_str() : std::string{};
        };
        UniValue paying{UniValue::VOBJ}; paying.pushKV(address.toStdString(), ValueFromAmount(200'000)); paying.pushKV(change.toStdString(), ValueFromAmount(value - 200'000 - 1'000));
        const auto original = broadcast(paying); QVERIFY(!original.empty());
        QTRY_VERIFY_WITH_TIMEOUT(hasTransaction(*wallet, QString::fromStdString(original)), 30'000);
        QTRY_COMPARE_WITH_TIMEOUT(editor->receipts()->receivedAmountSatoshi(), qint64{200'000}, 30'000);
        QTRY_COMPARE_WITH_TIMEOUT(status_text(), QStringLiteral("Payment received"), 30'000);
        UniValue elsewhere{UniValue::VOBJ}; elsewhere.pushKV(payer_address.toStdString(), ValueFromAmount(value - 20'000));
        const auto replacement = broadcast(elsewhere); QVERIFY(!replacement.empty()); QVERIFY(replacement != original);
        qmltest::Rpc(m_app, "syncwithvalidationinterfacequeue");
        // Mempool replacement: no output to the request remains.
        QTRY_COMPARE_WITH_TIMEOUT(editor->receipts()->receivedAmountSatoshi(), qint64{0}, 30'000);
        QTRY_VERIFY_WITH_TIMEOUT(!wallet->receiveRequestReconciliationPending(), 30'000);
        QTRY_COMPARE_WITH_TIMEOUT(status_text(), QStringLiteral("Awaiting payment"), 10'000);
        QTRY_VERIFY_WITH_TIMEOUT(editor->historicallyUsed(), 30'000); QVERIFY(editor->receipts()->historicallyUsed());
        QTRY_VERIFY_WITH_TIMEOUT(!sharing->canCopyQr() && !sharing->canExportQr(), 30'000);
        // The replacement confirms, so the original is now a mined conflict.
        UniValue mine{UniValue::VARR}; mine.push_back(1); mine.push_back(payer_address.toStdString());
        qmltest::Rpc(m_app, "generatetoaddress", mine); qmltest::Rpc(m_app, "syncwithvalidationinterfacequeue");
        UniValue tx{UniValue::VARR}; tx.push_back(replacement);
        QCOMPARE(qmltest::Rpc(m_app, "gettransaction", tx, payer->name()).find_value("confirmations").getInt<int>(), 1);
        QTRY_VERIFY_WITH_TIMEOUT(!wallet->receiveRequestReconciliationPending(), 30'000);
        QTRY_VERIFY_WITH_TIMEOUT(editor->receipts()->fresh() && editor->receipts()->receivedAmountSatoshi() == 0, 30'000);
        QCOMPARE(status_text(), QStringLiteral("Awaiting payment")); QVERIFY(editor->historicallyUsed());
        // Reopening the request reads the same current credit.
        QVERIFY(qmltest::Click(m_app, "savedRequestCloseButton"));
        QPointer<QObject> modal{qmltest::Find(m_app, "paymentRequestModal")}; QTRY_VERIFY(!modal->property("visible").toBool());
        editor = openFromActivity(id); QVERIFY(editor);
        QTRY_VERIFY_WITH_TIMEOUT(editor->receipts()->fresh(), 30'000);
        QCOMPARE(editor->receipts()->receivedAmountSatoshi(), qint64{0}); QVERIFY(editor->receipts()->historicallyUsed());
        QTRY_COMPARE_WITH_TIMEOUT(status_text(), QStringLiteral("Awaiting payment"), 10'000);
        // Reload the wallet while the request is still open.
        UniValue unload{UniValue::VARR}; unload.push_back(name.toStdString());
        qmltest::Rpc(m_app, "unloadwallet", unload);
        auto* controller = qmltest::Model<WalletQmlController>(m_app, "walletController"); QVERIFY(controller);
        QTRY_VERIFY(!controller->walletForName(name));
        QTRY_VERIFY(status_text() != QStringLiteral("Payment received"));
        qmltest::Rpc(m_app, "loadwallet", unload);
        wallet = qmltest::SelectOpenWalletForSetup(m_app, name); QVERIFY(wallet);
        QVERIFY(qmltest::Click(m_app, "activityTabButton"));
        // The wallet switch may retire the old modal; only a fresh open counts.
        QPointer<QObject> reloaded{qmltest::Find(m_app, "paymentRequestModal")};
        if (reloaded && reloaded->property("visible").toBool()) { QVERIFY(qmltest::Click(m_app, "savedRequestCloseButton")); QTRY_VERIFY(!reloaded || !reloaded->property("visible").toBool()); }
        editor = openFromActivity(id); QVERIFY(editor);
        QTRY_VERIFY_WITH_TIMEOUT(editor->receipts()->fresh(), 30'000);
        QCOMPARE(editor->receipts()->receivedAmountSatoshi(), qint64{0}); QVERIFY(editor->receipts()->historicallyUsed()); QVERIFY(editor->historicallyUsed());
        QTRY_COMPARE_WITH_TIMEOUT(status_text(), QStringLiteral("Awaiting payment"), 10'000);
    }
    void typedCurrentCreditSurvivesReorgAndWalletReload_data()
    {
        QTest::addColumn<bool>("restore_block");
        QTest::newRow("zero-credit-reload") << false;
        QTest::newRow("reconsidered-credit-reload") << true;
    }
    void typedCurrentCreditSurvivesReorgAndWalletReload()
    {
        QFETCH(bool, restore_block);
        auto* wallet = qmltest::CreateWallet(m_app, "receipt-reorg"); QVERIFY(wallet);
        const auto name = wallet->name(); const auto original_opening = wallet->opening()->identity();
        const auto saved = SaveTypedRequest(*wallet, [](PaymentRequestEditorModel& editor) {
            editor.amount()->setSatoshi(100'000'000); editor.message()->edit("Current credit is not invoice fulfillment");
            editor.privateNote()->edit("Private receipt note");
        });
        QVERIFY(saved); const auto id = saved->id, address = saved->address;
        QPointer<PaymentRequestEditorModel> editor = OpenTypedRequest(*wallet, id); QVERIFY(editor);
        QTRY_VERIFY_WITH_TIMEOUT(editor->receipts()->fresh(), 30'000);
        QCOMPARE(editor->receipts()->receiptState(), RequestReceiptModel::Awaiting);
        QObject view;
        auto* sharing = wallet->receive()->sharingRequest(id, &view); QVERIFY(sharing); sharing->setActive(true);
        QTRY_VERIFY_WITH_TIMEOUT(sharing->canCopyUri(), 30'000);
        UniValue mine{UniValue::VARR}; mine.push_back(1); mine.push_back(address.toStdString());
        const auto blocks = qmltest::Rpc(m_app, "generatetoaddress", mine);
        const auto block = blocks[0].get_str();
        qmltest::Rpc(m_app, "syncwithvalidationinterfacequeue");
        QTRY_VERIFY_WITH_TIMEOUT(editor->receipts()->fresh() && editor->receipts()->receivedAmountSatoshi() == 50 * COIN, 30'000);
        QCOMPARE(editor->receipts()->receiptState(), RequestReceiptModel::Received);
        QTRY_VERIFY(editor->historicallyUsed()); QTRY_VERIFY(!sharing->canCopyUri());
        UniValue original_block{UniValue::VARR}; original_block.push_back(block);
        qmltest::Rpc(m_app, "invalidateblock", original_block);
        qmltest::Rpc(m_app, "syncwithvalidationinterfacequeue");
        QTRY_VERIFY_WITH_TIMEOUT(editor->receipts()->fresh() && editor->receipts()->receivedAmountSatoshi() == 0, 30'000);
        QCOMPARE(editor->receipts()->receiptState(), RequestReceiptModel::Awaiting);
        QVERIFY(editor->receipts()->historicallyUsed()); QVERIFY(editor->historicallyUsed());
        QVERIFY(!sharing->canCopyUri()); QVERIFY(!sharing->canCopyQr());
        QCOMPARE(editor->requestId(), id); QCOMPARE(editor->address(), address);
        QCOMPARE(editor->privateNote()->text(), QStringLiteral("Private receipt note"));
        if (restore_block) {
            qmltest::Rpc(m_app, "reconsiderblock", original_block);
            qmltest::Rpc(m_app, "syncwithvalidationinterfacequeue");
            QTRY_VERIFY_WITH_TIMEOUT(editor->receipts()->fresh() && editor->receipts()->receivedAmountSatoshi() == 50 * COIN, 30'000);
        }
        UniValue unload{UniValue::VARR}; unload.push_back(name.toStdString());
        qmltest::Rpc(m_app, "unloadwallet", unload);
        auto* controller = qmltest::Model<WalletQmlController>(m_app, "walletController"); QVERIFY(controller);
        QTRY_VERIFY(!controller->walletForName(name));
        // Reload is a real persisted Wallet boundary, not a new fake ledger.
        // Process restart is independently exercised by the functional case.
        qmltest::Rpc(m_app, "loadwallet", unload);
        wallet = qmltest::SelectOpenWalletForSetup(m_app, name); QVERIFY(wallet);
        QVERIFY(wallet->opening()->identity() != original_opening);
        editor = OpenTypedRequest(*wallet, id); QVERIFY(editor);
        const auto expected = restore_block ? 50 * COIN : 0;
        QTRY_VERIFY_WITH_TIMEOUT(editor->receipts()->fresh() && editor->receipts()->receivedAmountSatoshi() == expected, 30'000);
        QCOMPARE(editor->receipts()->receiptState(), restore_block ? RequestReceiptModel::Received : RequestReceiptModel::Awaiting);
        QVERIFY(editor->receipts()->historicallyUsed()); QVERIFY(editor->historicallyUsed());
        QCOMPARE(editor->requestId(), id); QCOMPARE(editor->privateNote()->text(), QStringLiteral("Private receipt note"));
        auto* after = wallet->receive()->sharingRequest(id, &view); QVERIFY(after); after->setActive(true);
        QTRY_VERIFY(!after->canCopyUri()); QVERIFY(!after->canCopyQr());
    }
    void replacementAndConfirmedPaymentsDoNotDoubleCountReceipts()
    {
        auto* payer = qmltest::CreateWallet(m_app, "payer");
        QVERIFY(payer);
        QVERIFY(qmltest::FundWallet(m_app, *payer));
        auto* receiver = qmltest::CreateWallet(m_app, "receiver");
        QVERIFY(receiver);
        const auto saved = SaveTypedRequest(*receiver, [](PaymentRequestEditorModel& editor) {
            editor.amount()->setSatoshi(10'000); editor.message()->edit("pizza"); editor.privateNote()->edit("Private lunch note");
        });
        QVERIFY(saved); const auto id = saved->id, address = saved->address;
        QPointer<PaymentRequestEditorModel> typed = OpenTypedRequest(*receiver, id); QVERIFY(typed);
        auto* receipts = typed->receipts();
        QObject view;
        auto* sharing = receiver->receive()->sharingRequest(id, &view); QVERIFY(sharing); sharing->setActive(true);

        const auto original = qmltest::Rpc(m_app, "sendtoaddress", qmltest::Json("{\"address\":\"" + address.toStdString() + "\",\"amount\":0.00001,\"fee_rate\":1}"), payer->name()).get_str();
        QTRY_VERIFY_WITH_TIMEOUT(typed->historicallyUsed(), 30'000);
        QTRY_COMPARE(receipts->receivedAmountSatoshi(), qint64{1'000});
        QTRY_VERIFY(!sharing->canCopyQr());
        const auto replacement = qmltest::Rpc(m_app, "bumpfee", qmltest::Json("[\"" + original + "\",{\"fee_rate\":20}]"), payer->name());
        const auto replacement_id = QString::fromStdString(replacement.find_value("txid").get_str());
        QVERIFY(replacement_id != QString::fromStdString(original));
        QTRY_VERIFY_WITH_TIMEOUT(hasTransaction(*receiver, replacement_id), 30'000);
        QCOMPARE(receipts->receivedAmountSatoshi(), qint64{1'000});

        const auto mining_address = qmltest::NewAddress(m_app, payer->name());
        const auto mine = [&] {
            qmltest::Rpc(m_app, "generatetoaddress", qmltest::Json("[1,\"" + mining_address.toStdString() + "\"]"));
            // Mining returns before wallet notifications and reconciliation finish.
            qmltest::Rpc(m_app, "syncwithvalidationinterfacequeue");
        };
        mine();
        QTRY_VERIFY_WITH_TIMEOUT(!receiver->receiveRequestReconciliationPending(), 30'000);
        QCOMPARE(receipts->receivedAmountSatoshi(), qint64{1'000});
        const auto second = QString::fromStdString(qmltest::Rpc(m_app, "sendtoaddress", qmltest::Json("{\"address\":\"" + address.toStdString() + "\",\"amount\":0.00002,\"fee_rate\":1}"), payer->name()).get_str());
        QTRY_VERIFY_WITH_TIMEOUT(hasTransaction(*receiver, second), 30'000);
        QTRY_COMPARE(receipts->receivedAmountSatoshi(), qint64{3'000});
        QTRY_VERIFY(typed->receipts()->fresh()); QCOMPARE(typed->receipts()->receivedAmountSatoshi(), qint64{3'000});
        QCOMPARE(typed->receipts()->receiptState(), RequestReceiptModel::Received); QVERIFY(typed->receipts()->historicallyUsed());
        mine();
        QTRY_VERIFY_WITH_TIMEOUT(!receiver->receiveRequestReconciliationPending(), 30'000);
        QCOMPARE(receipts->receivedAmountSatoshi(), qint64{3'000});
        // Saves can also be refused while a queued transaction notification is
        // pending. Retry that busy result without changing the public metadata.
        typed->privateNote()->edit("Partial payment received");
        QTRY_VERIFY_WITH_TIMEOUT(typed->canSave(), 30'000);
        typed->save();
        QTRY_VERIFY_WITH_TIMEOUT(typed->saveResult().requestSaved, 30'000);
        QPointer<PaymentRequestEditorModel> reopened = OpenTypedRequest(*receiver, id); QVERIFY(reopened);
        QCOMPARE(reopened->privateNote()->text(), QStringLiteral("Partial payment received"));
        QCOMPARE(reopened->message()->text(), QStringLiteral("pizza"));
        QCOMPARE(reopened->amount()->satoshi(), qint64{10'000});
        QCOMPARE(receiver->receiveRequests()->count(), 1);
        QVERIFY(reopened->historicallyUsed());
    }
};

std::unique_ptr<QObject> CreateReceiveTests(BitcoinQmlApplication& app, WalletMode)
{
    return std::make_unique<ReceiveIntegrationTests>(app);
}

QStringList ReceiveTestArguments(const QString& selector)
{
    if (selector == "sendOutcomeReportsObservationOrRejectsStaleReview:broadcast-disabled") return {"-walletbroadcast=0"};
    if (selector == "addressPreferenceWriteReportsDisabledSettings" || selector == "typedReceivePreferenceFailureRetainsTheGeneratedAddress") return {"-nosettings"};
    if (selector == "receivingUnlockCanBeCancelledRetriedAndRelocks" ||
        selector == "typedReceiveKeepsCanonicalAmountAndExportsTheExactSavedPayload:encrypted-wallet") return {"-keypool=1"};
    return {};
}

#include <test_receive.moc>
