// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/mocks/walletcreationtest.h>
#include <test/mocks/walletopentest.h>
#include <test/mocks/messagebumptest.h>
#include <test/mocks/provisioningtest.h>
#include <test/integration/runner/test_cases.h>
#include <test/integration/fixture/test_wallet.h>
#include <qml/executor.h>
#include <qml/models/nodemodel.h>
#include <qml/models/uipreferences.h>
#include <qml/models/walletbackupcontroller.h>
#include <qml/models/walletprovisioningcheck.h>
#include <util/moneystr.h>
#include <interfaces/handler.h>
#include <interfaces/node.h>
#include <node/interface_ui.h>
#include <interfaces/wallet.h>
#include <rpc/protocol.h>
#include <util/fs.h>
#include <util/translation.h>
#include <wallet/walletutil.h>

#include <QApplication>
#include <QEvent>
#include <QFileDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QScopeGuard>
#include <QTimer>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QPointer>
#include <QSignalSpy>
#include <QSemaphore>
#include <QTemporaryDir>
#include <QThread>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <exception>
#include <future>
#include <memory>
#include <system_error>
#include <optional>
#include <set>
#include <thread>

#ifndef WIN32
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace {
class RefuseNativeOverwrite final : public QObject
{
public:
    int prompts{0};
    bool eventFilter(QObject* object, QEvent* event) override
    {
        auto* message = qobject_cast<QMessageBox*>(object);
        if (message && event->type() == QEvent::Show) {
            ++prompts;
            QTimer::singleShot(0, message, [message] { message->done(QMessageBox::No); });
        }
        return false;
    }
};
#ifndef WIN32
// #966: Core's ListDatabases() opens <walletdir>/<name>/wallet.dat to sniff
// its format. A FIFO there blocks that real read until a writer appears, so a
// test can hold wallet directory discovery inside Core. The write end opens
// only when released, or after a hang deadline. Reaching the deadline is
// recorded: only a GUI thread blocked on the read needs it to make progress,
// so the deadline is a hang breaker, not a speed limit.
class WalletDirectoryStall
{
public:
    explicit WalletDirectoryStall(const QString& fifo) : m_fifo{fifo.toStdString()}
    {
        m_ready = ::mkfifo(m_fifo.c_str(), 0600) == 0;
        hold();
        m_thread = std::thread{[this] {
            while (!m_stop) {
                const bool expired = !m_open && std::chrono::steady_clock::now().time_since_epoch().count() > m_deadline;
                if (m_open || expired) {
                    if (const int fd = ::open(m_fifo.c_str(), O_WRONLY | O_NONBLOCK); fd >= 0) {
                        ++m_released; if (expired) ++m_expired;
                        ::close(fd);
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds{2});
            }
        }};
    }
    ~WalletDirectoryStall()
    {
        m_open = true;
        std::this_thread::sleep_for(std::chrono::milliseconds{100});
        ::unlink(m_fifo.c_str());
        m_stop = true;
        m_thread.join();
    }
    bool ready() const { return m_ready; }
    void hold() { m_open = false; m_deadline = (std::chrono::steady_clock::now() + std::chrono::seconds{60}).time_since_epoch().count(); }
    void release() { m_open = true; }
    int released() const { return m_released; }
    int expired() const { return m_expired; }

private:
    const std::string m_fifo;
    bool m_ready{false};
    std::atomic_bool m_open{false}, m_stop{false};
    std::atomic<int64_t> m_deadline{0};
    std::atomic_int m_released{0}, m_expired{0};
    std::thread m_thread;
};
#endif

QFileDialog* VisibleBackupChooser()
{
    for (auto* widget : QApplication::topLevelWidgets()) {
        if (auto* dialog = qobject_cast<QFileDialog*>(widget); dialog && dialog->isVisible()) return dialog;
    }
    return nullptr;
}
QByteArray BackupBytes(const QString& path)
{
    QFile file{path};
    if (!file.open(QIODevice::ReadOnly)) throw std::runtime_error{"Cannot read backup oracle"};
    return file.readAll();
}
//! The backup the selected wallet's settings page started.
BackupFileOperation* PageBackup(BitcoinQmlApplication& app)
{
    auto* controller = qmltest::Find(app, "walletBackupController");
    return controller ? qobject_cast<BackupFileOperation*>(controller->property("operation").value<QObject*>()) : nullptr;
}
//! A backup started outside the settings page, through its own controller.
struct BackupPage {
    std::unique_ptr<WalletBackupController> controller{std::make_unique<WalletBackupController>()};
    explicit BackupPage(WalletModel& wallet) { controller->setBackups(wallet.backups()); controller->setActive(true); }
    bool begin(const QString& path) { return controller->begin(path); }
    BackupFileOperation* operation() const { return qobject_cast<BackupFileOperation*>(controller->operation()); }
};
} // namespace

class WalletIntegrationTests : public QObject
{
    Q_OBJECT
    BitcoinQmlApplication& m_app;

    bool openSelector()
    {
        auto* popup = qmltest::Find(m_app, "walletSelectPopup");
        if (popup && popup->property("opened").toBool()) return true;
        return qmltest::Click(m_app, "walletBadge") && qmltest::WaitForVisible(m_app, "walletSelectPopup");
    }

    bool selectWallet(const QString& name)
    {
        if (!openSelector() || !qmltest::Click(m_app, qPrintable("walletSelectItem_" + name))) return false;
        auto* opened = qmltest::WaitForOpenWallet(m_app, name);
        auto* controller = qmltest::Model<WalletController>(m_app, "walletController");
        if (!opened || !controller) return false;
        // Loading publishes an available row. Opening it is a second explicit
        // action; the completion itself cannot retarget this window.
        if (controller->selectedWallet() != opened && !qmltest::Click(m_app, qPrintable("walletSelectItem_" + name))) return false;
        return qmltest::WaitForWallet(m_app, name);
    }

    bool walletAction(const QString& name, const char* action)
    {
        return openSelector() && qmltest::Click(m_app, qPrintable("walletSelectActions_" + name)) &&
               qmltest::Click(m_app, qPrintable(QString{"walletSelect%1_%2"}.arg(QString::fromLatin1(action), name)));
    }

public:
    explicit WalletIntegrationTests(BitcoinQmlApplication& app) : m_app{app} {}

private Q_SLOTS:
    void walletOpeningsHaveIndependentQueues()
    {
        auto* first = qmltest::CreateWallet(m_app, "first-queue");
        auto* second = qmltest::CreateWallet(m_app, "second-queue");
        QVERIFY(first);
        QVERIFY(second);
        const auto first_opening = first->opening();
        const auto second_opening = second->opening();
        QVERIFY(first_opening);
        QVERIFY(second_opening);
        QVERIFY(first_opening->coreQueue());
        QVERIFY(second_opening->coreQueue());
        QVERIFY(first_opening->coreQueue() != second_opening->coreQueue());
        QVERIFY(first_opening->utilityQueue());
        QVERIFY(second_opening->utilityQueue());
        QVERIFY(first_opening->utilityQueue() != second_opening->utilityQueue());
        QTRY_VERIFY(!first->receiveRequestReconciliationPending());
        QTRY_VERIFY(first->balanceKnown());

        // Settle the first opening's queued reads before holding Core on
        // the second opening. Avoid processing Qt timers in between.
        const auto settled = std::make_shared<QSemaphore>();
        QVERIFY(first_opening->read([](interfaces::Wallet&) { return true; },
            [settled](bool) { settled->release(); }));
        QVERIFY(settled->tryAcquire(1, 5000));

        struct Gate { QSemaphore entered, release; };
        const auto gate = std::make_shared<Gate>();
        const auto release_on_exit = qScopeGuard([gate] { gate->release.release(); });
        const auto action = second_opening->startAction();
        QVERIFY(action);
        QVERIFY(second_opening->perform(action, [gate](interfaces::Wallet& wallet) {
            gate->entered.release();
            gate->release.acquire();
            return wallet.getWalletName();
        }, [](std::string) {}));
        QVERIFY(gate->entered.tryAcquire(1, 5000));

        QSignalSpy stopped{first, &WalletModel::shutdownFinished};
        auto* controller = qmltest::Model<WalletController>(m_app, "walletController");
        QVERIFY(controller);
        // The user can close the idle wallet while the other opening's
        // action is still running on Core.
        controller->closeWallet(first->name());
        QTRY_COMPARE(stopped.size(), 1);
        gate->release.release();
        auto receipt = std::make_shared<std::promise<std::string>>();
        auto result = receipt->get_future();
        QVERIFY(second_opening->read([](interfaces::Wallet& wallet) { return wallet.getWalletName(); },
                                     [receipt](std::string name) { receipt->set_value(std::move(name)); }));
        using namespace std::chrono_literals;
        QTRY_VERIFY(result.wait_for(0ms) == std::future_status::ready);
        QCOMPARE(QString::fromStdString(result.get()), QStringLiteral("second-queue"));
    }

    void walletCloseLetsAcceptedWorkFinishBeforeNativeUnload()
    {
        const QString name = QStringLiteral("close-after-work");
        auto* wallet = qmltest::CreateWallet(m_app, name);
        auto* controller = qmltest::Model<WalletController>(m_app, "walletController");
        auto* node = qmltest::Model<NodeModel>(m_app, "nodeModel");
        QVERIFY(wallet);
        QVERIFY(controller);
        QVERIFY(node);
        const auto opening = wallet->opening();
        QVERIFY(opening);
        QTRY_VERIFY(!wallet->receiveRequestReconciliationPending());

        struct Gate { QSemaphore entered, release; };
        const auto gate = std::make_shared<Gate>();
        const auto release_on_exit = qScopeGuard([gate] { gate->release.release(); });
        const auto completed = std::make_shared<std::promise<std::string>>();
        auto wallet_used = completed->get_future();
        {
            const auto action = opening->startAction();
            QVERIFY(action);
            QVERIFY(opening->perform(action, [gate](interfaces::Wallet& original) {
                gate->entered.release();
                gate->release.acquire();
                return original.getWalletName();
            }, [completed](std::string result) { completed->set_value(std::move(result)); }));
        }
        QTRY_VERIFY(gate->entered.available() > 0);

        QSignalSpy stopped{wallet, &WalletModel::shutdownFinished};
        controller->closeWallet(name);
        const auto receipt = std::make_shared<std::promise<bool>>();
        auto loaded = receipt->get_future();
        QVERIFY(node->valueRetirement()([receipt, core = &m_app.node(), name = name.toStdString()] {
            try {
                const auto wallets = core->executeRpc("listwallets", UniValue{UniValue::VARR}, "");
                receipt->set_value(std::ranges::any_of(wallets.getValues(), [&name](const auto& value) { return value.get_str() == name; }));
            } catch (...) {
                receipt->set_exception(std::current_exception());
            }
        }));
        // This worker has run any previously posted native cleanup. The
        // closing opening must still exist in Core until its own work settles.
        using namespace std::chrono_literals;
        QTRY_VERIFY(loaded.wait_for(0ms) == std::future_status::ready);
        QVERIFY(loaded.get());
        QCOMPARE(stopped.size(), 0);

        gate->release.release();
        QTRY_COMPARE(stopped.size(), 1);
        QCOMPARE(wallet_used.wait_for(0ms), std::future_status::ready);
        QCOMPARE(wallet_used.get(), name.toStdString());
        QCOMPARE(qmltest::Rpc(m_app, "listwallets").size(), 0U);
    }

    void walletCloseLetsAnInFlightReadDeliverItsOutcome()
    {
        const QString name = QStringLiteral("close-during-read");
        auto* wallet = qmltest::CreateWallet(m_app, name);
        QVERIFY(wallet);
        auto* controller = qmltest::Model<WalletController>(m_app, "walletController");
        QVERIFY(controller);
        const auto opening = wallet->opening();
        QVERIFY(opening);
        QTRY_VERIFY(!wallet->receiveRequestReconciliationPending());

        struct Gate { QSemaphore entered, release; std::atomic_bool finished{false}; };
        const auto gate = std::make_shared<Gate>();
        const auto unblock = qScopeGuard([gate] { gate->release.release(); });
        const auto receipt = std::make_shared<std::promise<std::string>>();
        auto result = receipt->get_future();
        QVERIFY(opening->read([gate](interfaces::Wallet& original) {
            gate->entered.release();
            gate->release.acquire();
            auto name = original.getWalletName();
            gate->finished = true;
            return name;
        }, [receipt](std::string name) { receipt->set_value(std::move(name)); },
        [receipt](std::exception_ptr) { receipt->set_value({}); }));
        QTRY_VERIFY(gate->entered.available() > 0);

        QVERIFY(opening->utilityQueue());
        const auto outcome_gate = std::make_shared<Gate>();
        const auto unblock_outcome = qScopeGuard([outcome_gate] { outcome_gate->release.release(); });
        QVERIFY(opening->utilityQueue()->post([outcome_gate] {
            outcome_gate->entered.release();
            outcome_gate->release.acquire();
        }));
        QVERIFY(outcome_gate->entered.tryAcquire(1, 5000));
        QSignalSpy stopped{wallet, &WalletModel::shutdownFinished};
        controller->closeWallet(name);
        QTRY_VERIFY(!opening->available());
        QCOMPARE(stopped.size(), 0);
        using namespace std::chrono_literals;
        QCOMPARE(result.wait_for(0ms), std::future_status::timeout);

        gate->release.release();
        QTRY_VERIFY(gate->finished.load());
        QCOMPARE(stopped.size(), 0);
        QCOMPARE(result.wait_for(0ms), std::future_status::timeout);
        outcome_gate->release.release();
        QTRY_COMPARE(stopped.size(), 1);
        QCOMPARE(result.wait_for(0ms), std::future_status::ready);
        QCOMPARE(result.get(), name.toStdString());
    }

    void walletCatalogAndNodeQueriesShareTheCoreWorker()
    {
        auto* node = qmltest::Model<NodeModel>(m_app, "nodeModel");
        auto* wallet = qmltest::Model<WalletController>(m_app, "walletController");
        QVERIFY(node);
        QVERIFY(wallet);

        const auto worker = [](const auto& post) {
            auto receipt = std::make_shared<std::promise<std::thread::id>>();
            auto result = receipt->get_future();
            const auto accepted = post(execution::MakeTask(
                [] { return std::this_thread::get_id(); },
                [receipt](std::thread::id id) { receipt->set_value(id); }, {}));
            return accepted ? std::move(result) : std::future<std::thread::id>{};
        };
        const auto post_node = node->postNative(execution::Lane::Core);
        QVERIFY(post_node);
        auto node_worker = worker(post_node);
        auto wallet_worker = worker([wallet](auto task) { return wallet->postNative(std::move(task)); });
        QVERIFY(node_worker.valid());
        QVERIFY(wallet_worker.valid());
        using namespace std::chrono_literals;
        QTRY_VERIFY(node_worker.wait_for(0ms) == std::future_status::ready);
        QTRY_VERIFY(wallet_worker.wait_for(0ms) == std::future_status::ready);
        const auto node_thread = node_worker.get();
        QVERIFY(node_thread != std::this_thread::get_id());
        QVERIFY(node_thread == wallet_worker.get());
    }

    void restoredModernNamesKeepTheirExactDatabaseIdentity_data()
    {
        QTest::addColumn<QString>("target");
        QTest::newRow("modern-legacy-suffix") << QStringLiteral("Treasury.legacy");
        QTest::newRow("ordinary-suffix-control") << QStringLiteral("Treasury.archive");
        QTest::newRow("significant-space-import") << QStringLiteral("Savings ");
        QTest::newRow("encoded-file-url") << QStringLiteral("Trésor #1%20");
    }
    void restoredModernNamesKeepTheirExactDatabaseIdentity()
    {
        QFETCH(QString, target);
        auto* controller = qmltest::Model<WalletController>(m_app, "walletController"); QVERIFY(controller);
        auto* source = qmltest::CreateWallet(m_app, "source-original"); QVERIFY(source);
        const auto address = qmltest::NewAddress(m_app, source->name());
        QTemporaryDir directory; QVERIFY(directory.isValid()); const auto backup = directory.filePath(target + QStringLiteral(".bak"));
        UniValue backup_request{UniValue::VARR}; backup_request.push_back(backup.toStdString()); qmltest::Rpc(m_app, "backupwallet", backup_request, source->name()); const auto original_bytes = BackupBytes(backup);
        QPointer<WalletModel> original{source}; controller->closeWallet(source->name()); QTRY_VERIFY(original.isNull());
        const auto unrelated_name = target.startsWith("Treasury") ? QStringLiteral("Treasury") : QStringLiteral("Savings");
        auto* unrelated = qmltest::CreateWallet(m_app, unrelated_name); QVERIFY(unrelated); const auto unrelated_address = qmltest::NewAddress(m_app, unrelated_name);
        auto* operation = controller->creation()->restore(QUrl::fromLocalFile(backup)); QVERIFY(operation); QTRY_VERIFY_WITH_TIMEOUT(operation->terminal(), 30'000);
        QCOMPARE(operation->phase(), WalletOperationModel::Succeeded); QCOMPARE(operation->targetWalletId(), target); QCOMPARE(operation->result().wallet_id, target);
        auto* restored = qmltest::WaitForOpenWallet(m_app, target); QVERIFY(restored); QCOMPARE(operation->resultWallet(), restored); QCOMPARE(controller->selectedWallet(), unrelated); QVERIFY(operation->canOpenResult());
        UniValue info{UniValue::VARR}; info.push_back(address.toStdString()); QVERIFY(qmltest::Rpc(m_app, "getaddressinfo", info, target).find_value("ismine").get_bool()); QVERIFY(!qmltest::Rpc(m_app, "getaddressinfo", info, unrelated_name).find_value("ismine").get_bool());
        UniValue other{UniValue::VARR}; other.push_back(unrelated_address.toStdString()); QVERIFY(!qmltest::Rpc(m_app, "getaddressinfo", other, target).find_value("ismine").get_bool()); QCOMPARE(BackupBytes(backup), original_bytes);
        QVERIFY(restored->displayName() != unrelated->displayName()); QVERIFY(controller->names()->setDisplayName(target, "Restored original alias")); QTRY_VERIFY(!controller->names()->nameWritePending()); QVERIFY(controller->names()->nameWriteSucceeded()); QTRY_COMPARE(restored->displayName(), QStringLiteral("Restored original alias")); QVERIFY(unrelated->displayName() != restored->displayName());
        QVERIFY(controller->selectWallet(operation->resultWallet())); QCOMPARE(controller->selectedWallet(), restored);
    }

    void restoreRejectsInvalidAndDuplicateBackupsWithoutFilenameClassification()
    {
        auto* controller = qmltest::Model<WalletController>(m_app, "walletController"); QVERIFY(controller);
        QTemporaryDir directory; QVERIFY(directory.isValid()); const auto invalid = directory.filePath("invalid_migration.bak"); QFile invalid_file{invalid}; QVERIFY(invalid_file.open(QIODevice::WriteOnly)); QCOMPARE(invalid_file.write("not a wallet backup"), qint64{19}); invalid_file.close(); const auto invalid_bytes = BackupBytes(invalid);
        auto* failed = controller->creation()->restore(QUrl::fromLocalFile(invalid)); QVERIFY(failed); QTRY_VERIFY(failed->terminal()); QCOMPARE(failed->phase(), WalletOperationModel::Failed); QCOMPARE(failed->result().effect, WalletOperationResult::NoEffect); QCOMPARE(BackupBytes(invalid), invalid_bytes); QVERIFY(!controller->walletForName("invalid_migration"));
        auto* wallet = qmltest::CreateWallet(m_app, "audit_migration_73"); QVERIFY(wallet); const auto address = qmltest::NewAddress(m_app, wallet->name()); const auto backup = directory.filePath("audit_migration_73.bak"); UniValue request{UniValue::VARR}; request.push_back(backup.toStdString()); qmltest::Rpc(m_app, "backupwallet", request, wallet->name()); const auto valid_bytes = BackupBytes(backup);
        QPointer<WalletModel> original{wallet}; controller->closeWallet(wallet->name()); QTRY_VERIFY(original.isNull());
        auto* duplicate = controller->creation()->restore(QUrl::fromLocalFile(backup)); QVERIFY(duplicate); QTRY_VERIFY(duplicate->terminal()); QCOMPARE(duplicate->phase(), WalletOperationModel::Failed); QCOMPARE(duplicate->error().code, QStringLiteral("TargetExists")); QVERIFY(!duplicate->errorTitle().contains("migrat", Qt::CaseInsensitive)); QCOMPARE(duplicate->result().effect, WalletOperationResult::NoEffect); QCOMPARE(BackupBytes(backup), valid_bytes);
        const auto corrected = directory.filePath("restored-control.bak"); QVERIFY(QFile::copy(backup, corrected)); auto* restored = controller->creation()->restore(QUrl::fromLocalFile(corrected)); QVERIFY(restored); QTRY_VERIFY_WITH_TIMEOUT(restored->terminal(), 30'000); QCOMPARE(restored->phase(), WalletOperationModel::Succeeded);
        UniValue info{UniValue::VARR}; info.push_back(address.toStdString()); QVERIFY(qmltest::Rpc(m_app, "getaddressinfo", info, "restored-control").find_value("ismine").get_bool()); QCOMPARE(BackupBytes(backup), valid_bytes);
    }

    // #947: importing a backup of a wallet that is still loaded names the
    // existing wallet as the problem, not the backup, and keeps both intact.
    void importingABackupOfALoadedWalletExplainsTheExistingWallet()
    {
        auto* controller = qmltest::Model<WalletController>(m_app, "walletController"); QVERIFY(controller);
        auto* wallet = qmltest::CreateWallet(m_app, "already-imported"); QVERIFY(wallet); const auto address = qmltest::NewAddress(m_app, wallet->name());
        QTemporaryDir directory; QVERIFY(directory.isValid()); const auto backup = directory.filePath("already-imported.bak"); UniValue request{UniValue::VARR}; request.push_back(backup.toStdString()); qmltest::Rpc(m_app, "backupwallet", request, wallet->name()); const auto bytes = BackupBytes(backup);
        QVERIFY(qmltest::Click(m_app, "walletBadge")); QVERIFY(qmltest::WaitForVisible(m_app, "walletSelectPopup")); QVERIFY(qmltest::Click(m_app, "walletSelectAddWalletButton")); QVERIFY(qmltest::WaitForVisible(m_app, "createWalletWizard"));
        auto* import_path = qmltest::Find(m_app, "importWalletPathField"); QVERIFY(import_path); QVERIFY(import_path->setProperty("text", backup)); QVERIFY(qmltest::Click(m_app, "walletTypeImport")); QVERIFY(qmltest::WaitForVisible(m_app, "walletImportErrorPage"));
        auto* operation = controller->creation()->operation(); QVERIFY(operation); QCOMPARE(operation->kind(), WalletOperationModel::Restore); QCOMPARE(operation->phase(), WalletOperationModel::Failed); QCOMPARE(operation->error().code, QStringLiteral("TargetExists")); QCOMPARE(operation->result().effect, WalletOperationResult::NoEffect);
        auto* title = qmltest::Find(m_app, "importWalletErrorTitle"); QVERIFY(title); auto* description = qmltest::Find(m_app, "importWalletErrorDescription"); QVERIFY(description);
        QCOMPARE(title->property("text").toString(), QStringLiteral("A wallet with this name already exists"));
        const auto explanation = description->property("text").toString(); QVERIFY2(explanation.contains("already contains a wallet") && explanation.contains("no existing wallet was overwritten"), qPrintable(explanation));
        for (const auto& text : {title->property("text").toString(), explanation}) QVERIFY2(!text.contains("valid", Qt::CaseInsensitive) && !text.contains("migrat", Qt::CaseInsensitive), qPrintable(text));
        QCOMPARE(controller->walletForName("already-imported"), wallet); QCOMPARE(BackupBytes(backup), bytes);
        UniValue info{UniValue::VARR}; info.push_back(address.toStdString()); QVERIFY(qmltest::Rpc(m_app, "getaddressinfo", info, "already-imported").find_value("ismine").get_bool());
    }

    void restoreSplitKeepsStorageWhenStartupSettingsAreDisabled()
    {
        auto* controller = qmltest::Model<WalletController>(m_app, "walletController"); QVERIFY(controller);
        qmltest::Rpc(m_app, "createwallet", qmltest::Json("{\"wallet_name\":\"nosettings-source\",\"load_on_startup\":null}"));
        auto* source = qmltest::WaitForOpenWallet(m_app, "nosettings-source"); QVERIFY(source); const auto address = qmltest::NewAddress(m_app, source->name());
        QTemporaryDir directory; QVERIFY(directory.isValid()); const auto backup = directory.filePath("nosettings-restored.bak"); UniValue request{UniValue::VARR}; request.push_back(backup.toStdString()); qmltest::Rpc(m_app, "backupwallet", request, source->name()); const auto bytes = BackupBytes(backup);
        QPointer<WalletModel> original{source}; controller->closeWallet(source->name()); QTRY_VERIFY(original.isNull());
        // Pinned Core already converts disabled startup settings into a
        // warning. Preserve that independent public-RPC compatibility control;
        // manual old-implementation exception tests cover the split's safety.
        const auto wallet_directory = qmltest::Call(m_app, [](interfaces::Node* node) { return QString::fromStdString(node->walletLoader().getWalletDir()); });
        UniValue control{UniValue::VARR}; control.push_back("rpc-settings-control"); control.push_back(backup.toStdString()); control.push_back(true);
        const auto control_result = qmltest::Rpc(m_app, "restorewallet", control);
        QCOMPARE(QString::fromStdString(control_result.find_value("name").get_str()), QStringLiteral("rpc-settings-control"));
        QVERIFY(control_result.find_value("warnings").write().find("startup setting could not be updated") != std::string::npos);
        QCOMPARE(BackupBytes(backup), bytes); QVERIFY(QFileInfo::exists(QDir(wallet_directory).filePath("rpc-settings-control/wallet.dat")));
        const auto loaded = qmltest::Rpc(m_app, "listwallets");
        for (const auto& name : loaded.getValues()) if (name.get_str() == "rpc-settings-control") { UniValue unload{UniValue::VARR}; unload.push_back("rpc-settings-control"); qmltest::Rpc(m_app, "unloadwallet", unload); }
        QTRY_VERIFY(!controller->walletForName("rpc-settings-control"));
        const QPointer<WalletModel> selected_before{controller->selectedWallet()};
        auto* operation = controller->creation()->restore(QUrl::fromLocalFile(backup)); QVERIFY(operation); QTRY_VERIFY_WITH_TIMEOUT(operation->terminal(), 30'000);
        QCOMPARE(operation->phase(), WalletOperationModel::Succeeded); QVERIFY(operation->warningText().contains("startup setting could not be updated")); QVERIFY(operation->canOpenResult()); QCOMPARE(controller->selectedWallet(), selected_before.data());

        const auto restored_file = QDir(wallet_directory).filePath("nosettings-restored/wallet.dat"); QVERIFY(QFileInfo::exists(restored_file)); QCOMPARE(BackupBytes(backup), bytes);
        UniValue info{UniValue::VARR}; info.push_back(address.toStdString()); QVERIFY(qmltest::Rpc(m_app, "getaddressinfo", info, "nosettings-restored").find_value("ismine").get_bool());
        QPointer<WalletModel> result{operation->resultWallet()}; controller->closeWallet("nosettings-restored"); QTRY_VERIFY(result.isNull()); QVERIFY(QFileInfo::exists(restored_file)); QCOMPARE(BackupBytes(backup), bytes);
    }

    void openedWalletWarningsRetainTheExactOriginalTarget()
    {
        // SQLite's VFS rejects the former >1024-byte fixture path before any
        // GUI operation. A supported nested, spaced and percent-bearing ID
        // still distinguishes the exact original target from a display label.
        const QString original_name = QStringLiteral("nested original/Treasury #1%20");
        auto* original = qmltest::CreateWallet(m_app, original_name); QVERIFY(original);
        auto* controller = qmltest::Model<WalletController>(m_app, "walletController"); QVERIFY(controller); QPointer<WalletModel> closed{original}; controller->closeWallet(original_name); QTRY_VERIFY(closed.isNull());
        auto* standby = qmltest::CreateWallet(m_app, "open-warning-standby"); QVERIFY(standby);
        QSignalSpy offered{controller, &WalletController::walletLoadWarningsOffered}; controller->setSelectedWallet(original_name, "sqlite");
        QTRY_COMPARE(offered.count(), 1); auto* operation = qvariant_cast<WalletOperationModel*>(offered.front().front()); QVERIFY(operation); QCOMPARE(operation->kind(), WalletOperationModel::Open); QCOMPARE(operation->targetWalletId(), original_name); QCOMPARE(operation->result().wallet_id, original_name);
        QCOMPARE(operation->phase(), WalletOperationModel::Succeeded); QVERIFY(operation->warningText().contains("fallbackfee")); QVERIFY(operation->canOpenResult()); QCOMPARE(controller->selectedWallet(), standby);
        QVERIFY(qmltest::WaitForVisible(m_app, "walletMigrationResultPopup")); auto* warnings = qmltest::Find(m_app, "walletCreationWarnings"); QVERIFY(warnings); QVERIFY(warnings->property("visible").toBool()); QVERIFY(warnings->property("text").toString().contains("fallbackfee"));
        QVERIFY(qmltest::Click(m_app, "walletMigrationOpenResultButton")); QTRY_COMPARE(controller->selectedWallet(), operation->resultWallet()); QCOMPARE(controller->selectedWallet()->name(), original_name);
    }

    void restoreWarningsRemainOnTheInitiatingOwnedResult()
    {
        auto* source = qmltest::CreateWallet(m_app, "warning-source"); QVERIFY(source); const auto address = qmltest::NewAddress(m_app, source->name());
        QTemporaryDir directory; QVERIFY(directory.isValid()); const auto backup = directory.filePath("warning-restored.bak"); UniValue request{UniValue::VARR}; request.push_back(backup.toStdString()); qmltest::Rpc(m_app, "backupwallet", request, source->name());
        auto* controller = qmltest::Model<WalletController>(m_app, "walletController"); QVERIFY(controller); QPointer<WalletModel> original{source}; controller->closeWallet(source->name()); QTRY_VERIFY(original.isNull());
        auto* standby = qmltest::CreateWallet(m_app, "warning-standby"); QVERIFY(standby);
        QVERIFY(qmltest::Click(m_app, "walletBadge")); QVERIFY(qmltest::WaitForVisible(m_app, "walletSelectPopup")); QVERIFY(qmltest::Click(m_app, "walletSelectAddWalletButton")); QVERIFY(qmltest::WaitForVisible(m_app, "createWalletWizard"));
        auto* import_path = qmltest::Find(m_app, "importWalletPathField"); QVERIFY(import_path); QVERIFY(import_path->setProperty("text", backup)); QVERIFY(qmltest::Click(m_app, "walletTypeImport")); QVERIFY(qmltest::WaitForVisible(m_app, "importWalletSuccessPage"));
        auto* operation = controller->creation()->operation(); QVERIFY(operation); QCOMPARE(operation->kind(), WalletOperationModel::Restore); QCOMPARE(operation->phase(), WalletOperationModel::Succeeded); QVERIFY(operation->warningText().contains("fallbackfee"));
        auto* warnings = qmltest::Find(m_app, "walletCreationWarnings"); QVERIFY(warnings); QVERIFY(warnings->property("visible").toBool()); QVERIFY(warnings->property("text").toString().contains("fallbackfee")); QCOMPARE(controller->selectedWallet(), standby);
        UniValue info{UniValue::VARR}; info.push_back(address.toStdString()); QVERIFY(qmltest::Rpc(m_app, "getaddressinfo", info, "warning-restored").find_value("ismine").get_bool()); QVERIFY(qmltest::Click(m_app, "importWalletSuccessOverviewButton")); QTRY_COMPARE(controller->selectedWallet(), operation->resultWallet());
    }

    void prunedValidBackupKeepsItsFilesAndReportsTheActualReason()
    {
        auto* source = qmltest::CreateWallet(m_app, "prune-source"); QVERIFY(source); const auto address = qmltest::NewAddress(m_app, source->name());
        auto mine = [&](int blocks) { UniValue request{UniValue::VARR}; request.push_back(blocks); request.push_back(address.toStdString()); qmltest::Rpc(m_app, "generatetoaddress", request); };
        mine(106); QTemporaryDir directory; QVERIFY(directory.isValid()); const auto backup = directory.filePath("old-pruned.bak"); UniValue request{UniValue::VARR}; request.push_back(backup.toStdString()); qmltest::Rpc(m_app, "backupwallet", request, source->name()); const auto original_bytes = BackupBytes(backup);
        auto* controller = qmltest::Model<WalletController>(m_app, "walletController"); QVERIFY(controller); QPointer<WalletModel> original{source}; controller->closeWallet(source->name()); QTRY_VERIFY(original.isNull()); mine(600);
        UniValue prune{UniValue::VARR}; prune.push_back(400); qmltest::Rpc(m_app, "pruneblockchain", prune); QVERIFY(qmltest::Rpc(m_app, "getblockchaininfo").find_value("pruneheight").getInt<int>() > 106);
        UniValue rpc_restore{UniValue::VARR}; rpc_restore.push_back("rpc-prune-control"); rpc_restore.push_back(backup.toStdString()); QString rpc_reason; try { qmltest::Rpc(m_app, "restorewallet", rpc_restore); } catch (const std::runtime_error& error) { rpc_reason = QString::fromUtf8(error.what()); } QVERIFY2(rpc_reason.contains("Prune:"), qPrintable(rpc_reason)); QCOMPARE(BackupBytes(backup), original_bytes);
        auto* operation = controller->creation()->restore(QUrl::fromLocalFile(backup)); QVERIFY(operation); QTRY_VERIFY_WITH_TIMEOUT(operation->terminal(), 30'000); QCOMPARE(operation->phase(), WalletOperationModel::Partial); QCOMPARE(operation->error().code, QStringLiteral("PrunedDataUnavailable")); QVERIFY(operation->errorText().contains("-reindex")); QVERIFY(operation->canReconcile()); QCOMPARE(BackupBytes(backup), original_bytes);
        const auto wallet_directory = qmltest::Call(m_app, [](interfaces::Node* node) { return QString::fromStdString(node->walletLoader().getWalletDir()); }); QVERIFY(QFileInfo::exists(QDir(wallet_directory).filePath("old-pruned/wallet.dat")));
        auto* fresh = qmltest::CreateWallet(m_app, "fresh-source"); QVERIFY(fresh); const auto fresh_backup = directory.filePath("fresh-control.bak"); UniValue fresh_request{UniValue::VARR}; fresh_request.push_back(fresh_backup.toStdString()); qmltest::Rpc(m_app, "backupwallet", fresh_request, fresh->name()); QPointer<WalletModel> fresh_original{fresh}; controller->closeWallet(fresh->name()); QTRY_VERIFY(fresh_original.isNull());
        auto* control = controller->creation()->restore(QUrl::fromLocalFile(fresh_backup)); QVERIFY(control); QTRY_VERIFY_WITH_TIMEOUT(control->terminal(), 30'000); QCOMPARE(control->phase(), WalletOperationModel::Succeeded); QVERIFY(control->canOpenResult()); QCOMPARE(BackupBytes(backup), original_bytes);
    }

    void createdWalletUnloadedBeforeGuiPublicationKeepsTheViewAlive()
    {
        QVERIFY(qmltest::CreateWallet(m_app, "unload-standby"));
        auto* controller = qmltest::Model<WalletController>(m_app, "walletController"); QVERIFY(controller);
        auto* catalog = qmltest::Model<WalletListModel>(m_app, "walletListModel"); QVERIFY(catalog);
        struct Gate {
            QSemaphore entered, release, added, unloaded;
            std::unique_ptr<interfaces::Handler> added_observation, observation;
            std::atomic_bool callback_off_gui{false}, timed_out{false}, rpc_finished{false};
            std::exception_ptr error;
        };
        const auto gate = std::make_shared<Gate>(); const std::weak_ptr<Gate> weak = gate;
        auto producer = std::make_shared<std::unique_ptr<interfaces::Handler>>(qmltest::Call(m_app, [weak](interfaces::Node* node) {
            return node->walletLoader().handleLoadWallet([weak](std::unique_ptr<interfaces::Wallet> wallet) {
                if (!wallet || wallet->getWalletName() != "retired-created") return;
                const auto owner = weak.lock(); if (!owner) return;
                owner->callback_off_gui = QThread::currentThread() != QCoreApplication::instance()->thread();
                owner->added_observation = wallet->handleCanGetAddressesChanged([weak] {
                    if (const auto observed = weak.lock()) observed->added.release();
                });
                owner->observation = wallet->handleUnload([weak] {
                    if (const auto observed = weak.lock()) { observed->observation.reset(); observed->unloaded.release(); }
                });
                owner->entered.release();
                if (!owner->release.tryAcquire(1, 15'000)) owner->timed_out = true;
            });
        }));
        QVERIFY(*producer);
        std::thread rpc;
        const auto cleanup = qScopeGuard([&] {
            gate->release.release();
            if (rpc.joinable()) {
                if (!gate->rpc_finished.load()) {
                    controller->beginShutdown();
                    (void)QTest::qWaitFor([&] { return gate->rpc_finished.load(); }, 15'000);
                }
                rpc.join();
            }
            qmltest::Call(m_app, [producer, gate](interfaces::Node*) {
                producer->reset();
                gate->added_observation.reset();
                gate->observation.reset();
                return true;
            });
        });
        bool phantom{false};
        const auto observation = connect(controller, &WalletController::openWalletsChanged, controller, [&] { if (controller->walletForName("retired-created")) phantom = true; });
        const auto disconnect_observation = qScopeGuard([&] { disconnect(observation); });
        QVERIFY(qmltest::Click(m_app, "walletBadge")); QVERIFY(qmltest::Click(m_app, "walletSelectAddWalletButton")); QVERIFY(qmltest::Click(m_app, "walletTypeRegular"));
        QVERIFY(qmltest::SetText(m_app, "createWalletNameInput", "retired-created"));
        QVERIFY(qmltest::SetText(m_app, "createWalletPasswordInput", "original password"));
        QVERIFY(qmltest::SetText(m_app, "createWalletPasswordRepeatInput", "original password"));
        QVERIFY(qmltest::Click(m_app, "createWalletPasswordConfirmToggle"));
        auto* create = qmltest::Find(m_app, "createWalletFormCreateButton"); QVERIFY(create); QTRY_VERIFY(create->property("enabled").toBool());
        QVERIFY(qmltest::Click(m_app, "createWalletFormCreateButton"));
        QVERIFY(QTest::qWaitFor([&] { return gate->entered.tryAcquire(); }, 15'000)); QVERIFY(gate->callback_off_gui.load());
        QPointer<QObject> modal{qmltest::Find(m_app, "walletCreationModal")}; QVERIFY(modal); QVERIFY(modal->property("opened").toBool());
        // Core reports the load before AddWallet inserts it into the registry.
        // AddWallet emits the address notification after insertion. Keep GUI
        // delivery held across that handoff and the subsequent unload.
        gate->release.release();
        QVERIFY(gate->added.tryAcquire(1, 15'000));
        auto* node = &m_app.node();
        rpc = std::thread([node, gate] {
            try { UniValue params{UniValue::VARR}; params.push_back("retired-created"); node->executeRpc("unloadwallet", params, ""); }
            catch (...) { gate->error = std::current_exception(); }
            gate->rpc_finished = true;
        });
        QVERIFY(gate->unloaded.tryAcquire(1, 15'000));
        QVERIFY(!gate->timed_out.load());
        QVERIFY(modal && modal->property("opened").toBool()); QVERIFY(!phantom);
        QTRY_VERIFY_WITH_TIMEOUT(gate->rpc_finished.load(), 30'000); rpc.join();
        QVERIFY(!gate->error); QVERIFY(!gate->timed_out.load()); QVERIFY(!phantom);
        QTRY_VERIFY(!controller->walletLoadInProgress()); QVERIFY(!controller->walletForName("retired-created"));
        QVERIFY(qmltest::WaitForVisible(m_app, "walletCreationOutcomePage"));
        auto* creation = controller->creation()->operation(); QVERIFY(creation); QVERIFY(creation->terminal());
        QVERIFY(modal && modal->property("opened").toBool()); QVERIFY(!creation->canOpenResult());
        const auto loaded = qmltest::Rpc(m_app, "listwallets").write(); QVERIFY(loaded.find("retired-created") == std::string::npos);
        catalog->listWalletDir(); QTRY_VERIFY(catalog->fresh());
        int row{-1}; for (int index = 0; index < catalog->rowCount(); ++index) if (catalog->data(catalog->index(index), WalletListModel::NameRole).toString() == "retired-created") row = index;
        QVERIFY(row >= 0); QCOMPARE(catalog->data(catalog->index(row), WalletListModel::LoadStateRole).toInt(), int(WalletListModel::LoadState::Closed));
    }

    void liveCreationEditsKeepTheRealCoreRequestAndMenuAdmission()
    {
        auto* standby = qmltest::CreateWallet(m_app, "edit-standby"); QVERIFY(standby);
        auto* controller = qmltest::Model<WalletController>(m_app, "walletController"); QVERIFY(controller);
        struct Gate { QSemaphore entered, release; std::atomic_bool claimed{false}, off_gui{false}, timed_out{false}; std::atomic_int calls{0}; };
        const auto gate = std::make_shared<Gate>();
        auto handler = std::make_shared<std::unique_ptr<interfaces::Handler>>(qmltest::Call(m_app, [gate](interfaces::Node* node) {
            return node->walletLoader().handleLoadWallet([gate](std::unique_ptr<interfaces::Wallet> wallet) {
                if (!wallet || wallet->getWalletName() != "accepted-original") return;
                ++gate->calls;
                if (gate->claimed.exchange(true)) return;
                gate->off_gui = QThread::currentThread() != QCoreApplication::instance()->thread();
                gate->entered.release();
                if (!gate->release.tryAcquire(1, 15'000)) gate->timed_out = true;
            });
        }));
        QVERIFY(*handler);
        const auto cleanup = qScopeGuard([&] {
            gate->release.release();
            qmltest::Call(m_app, [handler](interfaces::Node*) { handler->reset(); return true; });
        });
        QVERIFY(qmltest::Click(m_app, "walletBadge")); QVERIFY(qmltest::Click(m_app, "walletSelectAddWalletButton")); QVERIFY(qmltest::Click(m_app, "walletTypeRegular"));
        QVERIFY(qmltest::SetText(m_app, "createWalletNameInput", "accepted-original"));
        QVERIFY(qmltest::SetText(m_app, "createWalletPasswordInput", "original password"));
        QVERIFY(qmltest::SetText(m_app, "createWalletPasswordRepeatInput", "original password"));
        QVERIFY(qmltest::Click(m_app, "createWalletPasswordConfirmToggle"));
        auto* create = qmltest::Find(m_app, "createWalletFormCreateButton"); QVERIFY(create); QTRY_VERIFY(create->property("enabled").toBool());
        OutcomeSpy created{controller->creation(), OutcomeSpy::Kind::Created};
        QVERIFY(qmltest::Click(m_app, "createWalletFormCreateButton"));
        QVERIFY(QTest::qWaitFor([&] { return gate->entered.tryAcquire(); }, 15'000));
        QVERIFY(gate->off_gui.load()); QVERIFY(!gate->timed_out.load());
        const QPointer<WalletOperationModel> original_operation{controller->creation()->operation()}; QVERIFY(original_operation);
        QVERIFY(qmltest::SetText(m_app, "createWalletNameInput", "a-later-live-draft"));
        QVERIFY(qmltest::SetText(m_app, "createWalletPasswordInput", "replacement password"));
        QVERIFY(controller->walletLoadInProgress());
        auto* menu = qmltest::Find(m_app, "desktopMenuActions"); QVERIFY(menu);
        auto* menu_create = menu->property("createWallet").value<QObject*>(); QVERIFY(menu_create); QVERIFY(!menu_create->property("enabled").toBool());
        QVERIFY(!create->property("enabled").toBool());
        QVERIFY(controller->selectWallet(standby));
        QVERIFY(!controller->creation()->create(WalletCreationModel::Software, "forbidden-duplicate", {}, {}));
        QCOMPARE(controller->creation()->operation(), original_operation.data()); QCOMPARE(created.size(), 0);
        gate->release.release();
        QTRY_COMPARE_WITH_TIMEOUT(created.size(), 1, 30'000);
        QVERIFY(qmltest::WaitForVisible(m_app, "walletCreationReadyPage")); QTRY_VERIFY(!controller->walletLoadInProgress());
        QVERIFY(!gate->timed_out.load()); QCOMPARE(gate->calls.load(), 1); QCOMPARE(controller->selectedWallet(), standby);
        QVERIFY(original_operation); QCOMPARE(original_operation->submittedName(), QStringLiteral("accepted-original"));
        auto* result = original_operation->resultWallet(); QVERIFY(result); QCOMPARE(result->name(), QStringLiteral("accepted-original"));
        const auto loaded = qmltest::Rpc(m_app, "listwallets").write(); QVERIFY(loaded.find("accepted-original") != std::string::npos);
        QVERIFY(loaded.find("a-later-live-draft") == std::string::npos); QVERIFY(loaded.find("forbidden-duplicate") == std::string::npos);
        QVERIFY(qmltest::Rpc(m_app, "getwalletinfo", {}, result->name()).find_value("private_keys_enabled").get_bool());
        qmltest::Rpc(m_app, "walletpassphrase", qmltest::Json("[\"original password\",60]"), result->name());
        qmltest::Rpc(m_app, "walletlock", {}, result->name());
        bool replacement_rejected{false};
        try { qmltest::Rpc(m_app, "walletpassphrase", qmltest::Json("[\"replacement password\",60]"), result->name()); }
        catch (const std::runtime_error& error) { replacement_rejected = QString::fromUtf8(error.what()).contains("-14"); }
        QVERIFY(replacement_rejected);
    }

    void creationWarningsAppearOnTheInitiatingSuccessfulResult()
    {
        QVERIFY(qmltest::CreateWallet(m_app, "warning-standby"));
        auto* controller = qmltest::Model<WalletController>(m_app, "walletController"); QVERIFY(controller);
        QVERIFY(qmltest::Click(m_app, "walletBadge")); QVERIFY(qmltest::Click(m_app, "walletSelectAddWalletButton")); QVERIFY(qmltest::Click(m_app, "walletTypeRegular"));
        QVERIFY(qmltest::SetText(m_app, "createWalletNameInput", "warning-wallet"));
        QVERIFY(qmltest::SetText(m_app, "createWalletPasswordInput", "original password"));
        QVERIFY(qmltest::SetText(m_app, "createWalletPasswordRepeatInput", "original password"));
        QVERIFY(qmltest::Click(m_app, "createWalletPasswordConfirmToggle"));
        auto* button = qmltest::Find(m_app, "createWalletFormCreateButton"); QVERIFY(button); QTRY_VERIFY(button->property("enabled").toBool());
        QVERIFY(qmltest::Click(m_app, "createWalletFormCreateButton"));
        QVERIFY(qmltest::WaitForVisible(m_app, "walletCreationReadyPage"));
        QTRY_VERIFY(!controller->walletLoadInProgress());
        auto* operation = controller->creation()->operation(); QVERIFY(operation);
        QCOMPARE(operation->phase(), WalletOperationModel::Succeeded);
        const auto warnings = operation->result().warnings.join(QLatin1Char('\n'));
        QVERIFY(warnings.contains("fallbackfee"));
        auto* warning = qmltest::Find(m_app, "walletCreationWarnings"); QVERIFY(warning);
        QVERIFY(warning->property("visible").toBool());
        QCOMPARE(warning->property("text").toString(), warnings);
        QVERIFY(OperationError(controller->creation()).isEmpty());
        QVERIFY(qmltest::Click(m_app, "createWalletReadyDoneButton"));
        auto* created = qmltest::WaitForWallet(m_app, "warning-wallet"); QVERIFY(created);
        QCOMPARE(qmltest::Rpc(m_app, "getwalletinfo", {}, created->name()).find_value("walletname").get_str(), std::string{"warning-wallet"});
    }

    void rpcTrailingSeparatorHasOneCanonicalCatalogIdentity()
    {
        auto* controller = qmltest::Model<WalletController>(m_app, "walletController"); QVERIFY(controller);
        auto* catalog = qmltest::Model<WalletListModel>(m_app, "walletListModel"); QVERIFY(catalog);
        qmltest::Rpc(m_app, "createwallet", qmltest::Json("{\"wallet_name\":\"savings/\"}"));
        auto* original = qmltest::WaitForOpenWallet(m_app, "savings"); QVERIFY(original);
        const auto original_opening = original->opening()->identity();
        QCOMPARE(original->rpcName(), QStringLiteral("savings/"));
        QCOMPARE(qmltest::Rpc(m_app, "getwalletinfo", {}, original->rpcName()).find_value("walletname").get_str(), std::string{"savings/"});
        const auto rows = [&] { int count{0}; for (int row = 0; row < catalog->rowCount(); ++row) if (catalog->data(catalog->index(row), WalletListModel::NameRole).toString().startsWith("savings")) ++count; return count; };
        catalog->listWalletDir(); QTRY_VERIFY(catalog->walletDirLoaded() && catalog->fresh()); QTRY_COMPARE(rows(), 1);
        QVERIFY(!controller->walletForName("savings/"));
        QPointer<WalletModel> retired{original}; controller->closeWallet("savings"); QTRY_VERIFY(retired.isNull());
        catalog->listWalletDir(); QTRY_VERIFY(catalog->fresh()); QCOMPARE(rows(), 1);
        QCOMPARE(qmltest::Rpc(m_app, "listwallets").size(), 0U);
        controller->setSelectedWallet("savings");
        auto* reopened = qmltest::WaitForOpenWallet(m_app, "savings"); QVERIFY(reopened);
        QVERIFY(reopened->opening()->identity() != original_opening); QTRY_COMPARE(rows(), 1);
        // Nested relative creation is a supported control, not an absolute or
        // trailing-separator spelling rejected by the GUI admission policy.
        QTRY_VERIFY(!controller->walletLoadInProgress());
        controller->creation()->create(WalletCreationModel::Software, "sub/nested", {}, {});
        // Wallet creation writes to disk; allow the same time as the other wallet operations here.
        QTRY_VERIFY_WITH_TIMEOUT(!controller->walletLoadInProgress() && controller->walletForName("sub/nested"), 30'000);
        QVERIFY(qmltest::WaitForOpenWallet(m_app, "sub/nested"));
        QVERIFY(qmltest::Rpc(m_app, "listwalletdir").write().find("sub/nested") != std::string::npos);
    }

    // #1101: started with "trail2/" as its startup name (the restart case),
    // the picker shows one Open row, selecting it does not reopen the loaded
    // database, and the GUI create flow rejects a name ending in a separator.
    void trailingSeparatorStartupWalletIsOneOpenRowAndCreationRejectsIt()
    {
        auto* controller = qmltest::Model<WalletController>(m_app, "walletController"); QVERIFY(controller);
        auto* catalog = qmltest::Model<WalletListModel>(m_app, "walletListModel"); QVERIFY(catalog);
        auto* loaded = qmltest::WaitForOpenWallet(m_app, "trail2"); QVERIFY(loaded); QCOMPARE(loaded->rpcName(), QStringLiteral("trail2/"));
        const auto listed = [&] { std::set<std::string> names; const auto result = qmltest::Rpc(m_app, "listwallets"); for (const auto& name : result.getValues()) names.insert(name.get_str()); return names; };
        QVERIFY(listed() == (std::set<std::string>{"trail2/"}));
        const auto rows = [&] { QStringList found; for (int row = 0; row < catalog->rowCount(); ++row) { const auto index = catalog->index(row); if (catalog->data(index, WalletListModel::NameRole).toString().startsWith("trail2")) found << catalog->data(index, WalletListModel::NameRole).toString() + "=" + QString::number(catalog->data(index, WalletListModel::LoadStateRole).toInt()); } return found; };
        const auto one_open_row = QStringList{QStringLiteral("trail2=%1").arg(int(WalletListModel::LoadState::Open))};
        catalog->listWalletDir(); QTRY_VERIFY(catalog->walletDirLoaded() && catalog->fresh()); QTRY_COMPARE(rows(), one_open_row);
        QVERIFY(openSelector()); auto* row = qmltest::Find(m_app, "walletSelectItem_trail2"); QVERIFY(row); QVERIFY(!qmltest::Find(m_app, "walletSelectItem_trail2_"));
        QTRY_COMPARE(row->property("loadState").toInt(), int(WalletListModel::LoadState::Open));
        QSignalSpy offered{controller, &WalletController::walletLoadWarningsOffered}; const auto operation_before = OpenId(*controller);
        QVERIFY(qmltest::Click(m_app, "walletSelectItem_trail2")); QTRY_COMPARE(controller->selectedWallet(), loaded);
        QCOMPARE(OpenId(*controller), operation_before); QVERIFY(LoadError(*controller).isEmpty()); QCOMPARE(offered.count(), 0);
        auto* error_popup = qmltest::Find(m_app, "walletLoadErrorPopup"); QVERIFY(!error_popup || !error_popup->property("visible").toBool());
        catalog->listWalletDir(); QTRY_VERIFY(catalog->fresh()); QCOMPARE(rows(), one_open_row); QVERIFY(listed() == (std::set<std::string>{"trail2/"}));
        QVERIFY(qmltest::Click(m_app, "walletBadge")); QVERIFY(qmltest::WaitForVisible(m_app, "walletSelectPopup")); QVERIFY(qmltest::Click(m_app, "walletSelectAddWalletButton")); QVERIFY(qmltest::Click(m_app, "walletTypeRegular"));
        QVERIFY(qmltest::SetText(m_app, "createWalletPasswordInput", "trail password")); QVERIFY(qmltest::SetText(m_app, "createWalletPasswordRepeatInput", "trail password")); QVERIFY(qmltest::Click(m_app, "createWalletPasswordConfirmToggle"));
        auto* create = qmltest::Find(m_app, "createWalletFormCreateButton"); QVERIFY(create); auto* error = qmltest::Find(m_app, "createWalletFormErrorText"); QVERIFY(error);
        QVERIFY(qmltest::SetText(m_app, "createWalletNameInput", "trail3")); QTRY_VERIFY(create->property("enabled").toBool());
        for (const auto* name : {"trail3/", "trail2/"}) {
            QVERIFY(qmltest::SetText(m_app, "createWalletNameInput", name)); QTRY_VERIFY(!create->property("enabled").toBool());
            QTRY_COMPARE(error->property("text").toString(), QStringLiteral("Use a relative wallet name without a trailing separator or parent path."));
        }
        QVERIFY(!controller->walletLoadInProgress());
        QVERIFY(qmltest::Rpc(m_app, "listwalletdir").write().find("trail3") == std::string::npos); QVERIFY(listed() == (std::set<std::string>{"trail2/"}));
    }

    void externallyLoadedAbsoluteWalletRemainsReopenableInTheCatalog()
    {
        auto* controller = qmltest::Model<WalletController>(m_app, "walletController"); QVERIFY(controller);
        auto* catalog = qmltest::Model<WalletListModel>(m_app, "walletListModel"); QVERIFY(catalog);
        QTemporaryDir outside; QVERIFY(outside.isValid()); const auto name = outside.filePath("external-wallet");
        UniValue request{UniValue::VOBJ}; request.pushKV("wallet_name", name.toStdString()); qmltest::Rpc(m_app, "createwallet", request);
        QPointer<WalletModel> original{qmltest::WaitForOpenWallet(m_app, name)}; QVERIFY(original);
        const auto rowForName = [&] { for (int row = 0; row < catalog->rowCount(); ++row) if (catalog->data(catalog->index(row), WalletListModel::NameRole).toString() == name) return row; return -1; };
        catalog->listWalletDir(); QTRY_VERIFY(catalog->fresh()); QTRY_VERIFY(rowForName() >= 0);
        controller->closeWallet(name); QTRY_VERIFY(original.isNull());
        catalog->listWalletDir(); QTRY_VERIFY(catalog->fresh()); QVERIFY(rowForName() >= 0);
        QCOMPARE(catalog->data(catalog->index(rowForName()), WalletListModel::LoadStateRole).toInt(), int(WalletListModel::LoadState::Closed));
        controller->setSelectedWallet(name); QVERIFY(qmltest::WaitForOpenWallet(m_app, name));
        QPointer<WalletModel> reopened{controller->walletForName(name)}; controller->closeWallet(name); QTRY_VERIFY(reopened.isNull());
        // The external catalog fact lasts for this application lifetime. This
        // regression does not assert restart discovery of arbitrary paths.
    }

    void significantWhitespaceWalletsKeepDistinctKeysLabelsAndAliases()
    {
        auto* controller = qmltest::Model<WalletController>(m_app, "walletController"); QVERIFY(controller);
        auto* catalog = qmltest::Model<WalletListModel>(m_app, "walletListModel"); QVERIFY(catalog);
        auto* first = qmltest::CreateWallet(m_app, "Savings"); QVERIFY(first);
        auto* second = qmltest::CreateWallet(m_app, "Savings "); QVERIFY(second);
        const auto first_address = qmltest::NewAddress(m_app, "Savings");
        const auto second_address = qmltest::NewAddress(m_app, "Savings "); QVERIFY(first_address != second_address);
        UniValue address{UniValue::VARR}; address.push_back(first_address.toStdString());
        QVERIFY(qmltest::Rpc(m_app, "getaddressinfo", address, "Savings").find_value("ismine").get_bool());
        QVERIFY(!qmltest::Rpc(m_app, "getaddressinfo", address, "Savings ").find_value("ismine").get_bool());
        QCOMPARE(first->name(), QStringLiteral("Savings")); QCOMPARE(second->name(), QStringLiteral("Savings "));
        QVERIFY(first->displayName() != second->displayName());
        QVERIFY(controller->names()->setDisplayName("Savings ", "Second independent wallet")); QTRY_VERIFY(!controller->names()->nameWritePending());
        QVERIFY(controller->names()->nameWriteSucceeded()); QTRY_COMPARE(second->displayName(), QStringLiteral("Second independent wallet"));
        QCOMPARE(first->displayName(), QStringLiteral("Savings"));
        const auto label = [&](const QString& name) {
            for (int row = 0; row < catalog->rowCount(); ++row) {
                if (catalog->data(catalog->index(row), WalletListModel::NameRole).toString() == name)
                    return catalog->data(catalog->index(row), WalletListModel::DisplayNameRole).toString();
            }
            return QString{};
        };
        // The catalog can still be fresh from before this refresh; wait for the new label itself.
        catalog->refreshDisplayNames();
        QTRY_COMPARE(label("Savings "), QStringLiteral("Second independent wallet"));
        QCOMPARE(label("Savings"), QStringLiteral("Savings"));
    }

    void namesRejectLoadedClosedAndAliasedWallets()
    {
        auto* controller = qmltest::Model<WalletController>(m_app, "walletController");
        QVERIFY(controller);
        QVERIFY(qmltest::CreateWallet(m_app, "closed"));
        controller->closeWallet("closed");
        QTRY_VERIFY(!controller->isWalletOpen("closed"));
        QVERIFY(qmltest::CreateWallet(m_app, "loaded"));
        QVERIFY(qmltest::CreateWallet(m_app, "alias_target"));
        QVERIFY(controller->names()->setDisplayName("alias_target", "Savings"));
        QVERIFY(qmltest::Click(m_app, "walletBadge"));
        QVERIFY(qmltest::Click(m_app, "walletSelectAddWalletButton"));
        QVERIFY(qmltest::Click(m_app, "walletTypeRegular"));
        QVERIFY(qmltest::SetText(m_app, "createWalletPasswordInput", "test wallet password"));
        QVERIFY(qmltest::SetText(m_app, "createWalletPasswordRepeatInput", "test wallet password"));
        QVERIFY(qmltest::Click(m_app, "createWalletPasswordConfirmToggle"));
        auto* form = qmltest::Find(m_app, "createWalletFormPage"); QVERIFY(form);
        auto* editor = qobject_cast<WalletCreationModel*>(form->property("editor").value<QObject*>()); QVERIFY(editor);
        auto* button = qmltest::Find(m_app, "createWalletFormCreateButton"); QVERIFY(button);
        OutcomeSpy created{controller->creation(), OutcomeSpy::Kind::Created};
        const auto original_directories = qmltest::Rpc(m_app, "listwalletdir").write();
        for (const auto* name : {"closed", "loaded", "Savings", "savings"}) {
            QVERIFY2(AwaitWalletNameCheck(*controller, name).contains("already exists"), name);
            QVERIFY(qmltest::SetText(m_app, "createWalletNameInput", name));
            QTRY_VERIFY(editor->name()->text() == QString::fromUtf8(name) && !editor->pendingValidation());
            QVERIFY(editor->name()->error().contains("already exists"));
            QVERIFY(!button->property("enabled").toBool());
            auto* error = qmltest::Find(m_app, "createWalletFormErrorText");
            QVERIFY(error);
            QTRY_VERIFY(error->property("text").toString().contains("already exists"));
            // The declarative form rejects before submission. The Native
            // command must independently reject the same original name too.
            const QPointer<WalletOperationModel> rejected{controller->creation()->create(WalletCreationModel::Software, QString::fromUtf8(name), {}, QStringLiteral("test wallet password"))};
            QVERIFY(rejected); QCOMPARE(controller->creation()->operation(), rejected.data());
            QTRY_COMPARE(rejected->phase(), WalletOperationModel::Failed);
            QVERIFY(OperationError(controller->creation()).contains("already exists"));
            QCOMPARE(created.count(), 0);
            QCOMPARE(qmltest::Rpc(m_app, "listwalletdir").write(), original_directories);
        }
        QCOMPARE(AwaitWalletNameCheck(*controller, "My new wallet"), QString{});
        QVERIFY(qmltest::SetText(m_app, "createWalletNameInput", "My new wallet"));
        QTRY_VERIFY(editor->name()->text() == QStringLiteral("My new wallet") && !editor->pendingValidation());
        QVERIFY(editor->name()->error().isEmpty());
        QTRY_VERIFY(button->property("enabled").toBool());
        QVERIFY(qmltest::Click(m_app, "createWalletFormCreateButton"));
        QTRY_COMPARE_WITH_TIMEOUT(created.count(), 1, 30'000);
        QVERIFY(qmltest::WaitForVisible(m_app, "walletCreationReadyPage"));
        QVERIFY(qmltest::Click(m_app, "createWalletReadyDoneButton"));
        auto* wallet = qmltest::WaitForWallet(m_app, "My new wallet");
        QVERIFY(wallet);
        QTRY_VERIFY(wallet->isEncrypted());
        QTRY_VERIFY(wallet->isLocked());
        const auto info = qmltest::Rpc(m_app, "getwalletinfo", {}, wallet->name());
        QVERIFY(info.find_value("private_keys_enabled").get_bool());
        QCOMPARE(info.find_value("unlocked_until").getInt<int64_t>(), 0);
    }

    void closingAndReloadingReplacesTheModel()
    {
        auto* controller = qmltest::Model<WalletController>(m_app, "walletController");
        QVERIFY(controller);
        QPointer<WalletModel> original{qmltest::CreateWallet(m_app, "reload")};
        QVERIFY(original);
        QSignalSpy selected{controller, &WalletController::selectedWalletChanged};
        controller->closeWallet("reload");
        QTRY_VERIFY(!controller->isWalletLoaded());
        QTRY_VERIFY(original.isNull());
        QVERIFY(!selected.isEmpty());
        QCOMPARE(qmltest::Rpc(m_app, "listwallets").size(), 0U);
        controller->setSelectedWallet("reload");
        auto* reopened = qmltest::WaitForOpenWallet(m_app, "reload");
        QVERIFY(reopened);
        QVERIFY(!controller->selectedWallet()->available());
        QVERIFY(controller->selectWallet(reopened));
        QVERIFY(qmltest::WaitForWallet(m_app, "reload"));
        QCOMPARE(qmltest::Rpc(m_app, "listwallets").size(), 1U);
        auto* list = qmltest::Model<WalletListModel>(m_app, "walletListModel");
        QVERIFY(list);
        QTRY_COMPARE(list->rowCount(), 1);
        QCOMPARE(list->data(list->index(0), WalletListModel::LoadStateRole).toInt(), static_cast<int>(WalletListModel::LoadState::Open));
    }

    // #966: wallet discovery, the creation name check and creation itself
    // read Core's wallet directory off the GUI thread. While that real read is
    // stalled the GUI keeps serving events and shows pending state; the result
    // is published once the read completes, and a duplicate creation is refused.
    void stalledWalletDirectoryReadsKeepTheGuiResponsive()
    {
#ifdef WIN32
        QSKIP("The wallet directory stall needs a POSIX FIFO");
#else
        auto* controller = qmltest::Model<WalletController>(m_app, "walletController"); QVERIFY(controller);
        auto* catalog = qmltest::Model<WalletListModel>(m_app, "walletListModel"); QVERIFY(catalog);
        QVERIFY(qmltest::CreateWallet(m_app, "stall-open"));
        catalog->listWalletDir(); QTRY_VERIFY(catalog->fresh());
        const auto wallet_dir = QString::fromStdString(qmltest::Call(m_app, [](interfaces::Node* node) { return node->walletLoader().getWalletDir(); }));
        QVERIFY(QDir{wallet_dir}.mkpath("stall-fifo"));
        WalletDirectoryStall stall{QDir{wallet_dir}.filePath("stall-fifo/wallet.dat")}; QVERIFY(stall.ready());
        // While the stall is held, the action under test must return and the
        // event loop must reach a turn queued behind any GUI work it posted.
        // A GUI-thread directory read could do neither until the stall's hang
        // deadline expires, so progress plus "not expired" proves it without
        // timing how fast either happened.
        auto progressesWhileHeld = [&] {
            bool turned{false};
            QMetaObject::invokeMethod(this, [&turned] { turned = true; }, Qt::QueuedConnection);
            return QTest::qWaitFor([&turned] { return turned; }, 120'000) && stall.expired() == 0;
        };

        // Discovery: opening the selector refreshes the catalog.
        QVERIFY(openSelector()); QVERIFY2(stall.expired() == 0, "Opening the selector waited for the wallet directory");
        QVERIFY(progressesWhileHeld());
        QVERIFY(!catalog->fresh()); QCOMPARE(stall.released(), 0);
        QVERIFY(qmltest::Find(m_app, "walletSelectItem_stall_open"));
        stall.release(); QTRY_VERIFY(catalog->fresh()); QVERIFY(stall.released() > 0);
        QVERIFY(qmltest::Find(m_app, "walletSelectItem_stall_open"));
        auto* popup = qmltest::Find(m_app, "walletSelectPopup"); QVERIFY(popup); QVERIFY(QMetaObject::invokeMethod(popup, "close"));

        // Preflight: the name check reports pending, then the real conflict.
        stall.hold(); const int discovery_releases = stall.released();
        std::unique_ptr<WalletProvisioningCheck> check{controller->creation()->createProvisioningCheck()}; QVERIFY(check);
        check->setName("stall-open"); QVERIFY2(stall.expired() == 0, "The name check waited for the wallet directory");
        QVERIFY(progressesWhileHeld());
        QVERIFY(check->pending()); QCOMPARE(stall.released(), discovery_releases);
        stall.release(); QTRY_VERIFY(!check->pending()); QVERIFY(stall.released() > discovery_releases);
        QVERIFY(!check->nameError().isEmpty());
        check.reset();

        // Creation rechecks the name against the directory when it executes.
        stall.hold(); const int preflight_releases = stall.released();
        const QPointer<WalletOperationModel> operation{controller->creation()->create(WalletCreationModel::Software, "stall-created", {}, {})}; QVERIFY2(stall.expired() == 0, "Creation waited for the wallet directory");
        QVERIFY(controller->walletLoadInProgress());
        QVERIFY(operation);
        QVERIFY(progressesWhileHeld());
        QVERIFY(controller->walletLoadInProgress()); QVERIFY(!controller->walletForName("stall-created"));
        QCOMPARE(stall.released(), preflight_releases);
        QVERIFY(!controller->creation()->create(WalletCreationModel::Software, "stall-duplicate", {}, {}));
        QCOMPARE(controller->creation()->operation(), operation.data());
        stall.release();
        QVERIFY(qmltest::WaitForOpenWallet(m_app, "stall-created")); QVERIFY(stall.released() > preflight_releases);
        QTRY_VERIFY(!controller->walletLoadInProgress());
        QVERIFY(!controller->walletForName("stall-duplicate"));
        const auto loaded = qmltest::Rpc(m_app, "listwallets").write();
        QVERIFY(loaded.find("stall-created") != std::string::npos); QVERIFY(loaded.find("stall-duplicate") == std::string::npos);
#endif
    }

    // #966: closing a wallet does not wait for Core's wallet registry on the
    // GUI thread. A real load notification holds that registry lock; the close
    // returns at once, the GUI drops the wallet, a reopen is not admitted as a
    // second instance, and Core and the GUI agree once the lock is released.
    void closingWhileCoreHoldsTheWalletRegistryKeepsTheGuiResponsive()
    {
        auto* controller = qmltest::Model<WalletController>(m_app, "walletController"); QVERIFY(controller);
        QPointer<WalletModel> target{qmltest::CreateWallet(m_app, "close-target")}; QVERIFY(target);
        struct Gate { QSemaphore entered, release; std::atomic_bool timed_out{false}, rpc_finished{false}; };
        const auto gate = std::make_shared<Gate>();
        auto handler = std::make_shared<std::unique_ptr<interfaces::Handler>>(qmltest::Call(m_app, [gate](interfaces::Node* node) {
            return node->walletLoader().handleLoadWallet([gate](std::unique_ptr<interfaces::Wallet> wallet) {
                if (!wallet || wallet->getWalletName() != "close-gate") return;
                // Core runs load callbacks under its wallet registry mutex.
                // The deadline only breaks a hang: a GUI thread blocked on
                // the registry can return only once it has expired.
                gate->entered.release();
                if (!gate->release.tryAcquire(1, 60'000)) gate->timed_out = true;
            });
        }));
        QVERIFY(*handler);
        std::thread rpc;
        const auto cleanup = qScopeGuard([&] {
            gate->release.release();
            if (rpc.joinable()) rpc.join();
            qmltest::Call(m_app, [handler](interfaces::Node*) { handler->reset(); return true; });
        });
        auto* node = &m_app.node();
        rpc = std::thread{[node, gate] {
            try { UniValue params{UniValue::VOBJ}; params.pushKV("wallet_name", "close-gate"); node->executeRpc("createwallet", params, ""); } catch (...) {}
            gate->rpc_finished = true;
        }};
        QVERIFY(QTest::qWaitFor([&] { return gate->entered.tryAcquire(); }, 15'000));
        // Progress while the registry is held, never a speed limit: the event
        // loop reaches a turn queued behind the GUI work, and the gate has not
        // had to expire to let it.
        auto progressesWhileHeld = [&] {
            bool turned{false};
            QMetaObject::invokeMethod(this, [&turned] { turned = true; }, Qt::QueuedConnection);
            return QTest::qWaitFor([&turned] { return turned; }, 120'000) && !gate->timed_out.load();
        };
        controller->closeWallet("close-target");
        QVERIFY2(!gate->timed_out.load(), "Closing waited for Core's wallet registry");
        QVERIFY(!controller->isWalletOpen("close-target"));
        QVERIFY(progressesWhileHeld());
        // Core has not removed the wallet yet, so reopening it is refused
        // rather than admitted as a second instance.
        controller->setSelectedWallet("close-target");
        QVERIFY(progressesWhileHeld());
        QTRY_VERIFY(!LoadError(*controller).isEmpty()); QVERIFY(!controller->isWalletOpen("close-target"));
        QVERIFY(!gate->rpc_finished.load()); QVERIFY(!gate->timed_out.load());
        gate->release.release();
        QTRY_VERIFY_WITH_TIMEOUT(gate->rpc_finished.load(), 30'000); rpc.join();
        QVERIFY(!gate->timed_out.load());
        QTRY_VERIFY(target.isNull());
        // Core and the GUI agree that the wallet is closed, and it reopens
        // normally once removal has completed.
        QTRY_VERIFY_WITH_TIMEOUT(!controller->walletLoadInProgress(), 30'000);
        QVERIFY(!controller->isWalletOpen("close-target"));
        QVERIFY(qmltest::Rpc(m_app, "listwallets").write().find("close-target") == std::string::npos);
        controller->setSelectedWallet("close-target");
        QVERIFY(qmltest::WaitForOpenWallet(m_app, "close-target"));
        QCOMPARE(qmltest::Rpc(m_app, "listwallets").write().find("close-target") != std::string::npos, true);
    }

    // Shutdown drains settings and preferences before it interrupts Core, and a
    // rescan inside loadWallet() ends only on that interrupt.
    void quitInterruptsAWalletLoadWithQueuedSettingsAndPreferences_data()
    {
        QTest::addColumn<bool>("settings");
        QTest::newRow("settings") << true;
        QTest::newRow("preferences") << false;
    }
    void quitInterruptsAWalletLoadWithQueuedSettingsAndPreferences()
    {
        QFETCH(bool, settings);
        auto* controller = qmltest::Model<WalletController>(m_app, "walletController"); QVERIFY(controller);
        QPointer<WalletModel> target{qmltest::CreateWallet(m_app, "slow-load")}; QVERIFY(target);
        controller->closeWallet("slow-load");
        QTRY_VERIFY(target.isNull());
        QTRY_VERIFY_WITH_TIMEOUT(!controller->walletLoadInProgress(), 30'000);
        struct Gate {
            QSemaphore entered, unloading, allow_unload;
            std::atomic_bool interrupted{false}, finished{false};
            std::unique_ptr<interfaces::Handler> unload_handler;
        };
        const auto gate = std::make_shared<Gate>();
        auto* node = &m_app.node();
        auto handler = std::make_shared<std::unique_ptr<interfaces::Handler>>(qmltest::Call(m_app, [gate](interfaces::Node* core) {
            return core->walletLoader().handleLoadWallet([gate, core](std::unique_ptr<interfaces::Wallet> wallet) {
                if (!wallet || wallet->getWalletName() != "slow-load") return;
                gate->entered.release();
                for (int i = 0; i < 1500 && !core->shutdownRequested(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds{10});
                gate->interrupted = core->shutdownRequested();
                gate->unload_handler = wallet->handleUnload([gate] {
                    gate->unloading.release();
                    gate->allow_unload.tryAcquire(1, 15'000);
                });
                gate->finished = true;
            });
        }));
        QVERIFY(*handler);
        const auto cleanup = qScopeGuard([&] {
            if (*handler && !node->shutdownRequested()) {
                qmltest::Call(m_app, [handler](interfaces::Node*) { handler->reset(); return true; });
            }
            gate->unload_handler.reset();
            gate->allow_unload.release();
        });
        controller->setSelectedWallet("slow-load");
        QVERIFY(QTest::qWaitFor([&] { return gate->entered.tryAcquire(); }, 15'000));
        if (settings) {
            auto* options = qmltest::Model<QObject>(m_app, "optionsModel"); QVERIFY(options);
            QVERIFY(QMetaObject::invokeMethod(options, "refreshDisplaySettings"));
        } else {
            auto* store = qmltest::Model<UiPreferencesStore>(m_app, "uiPreferencesStore"); QVERIFY(store);
            store->refresh();
        }
        m_app.nodeModel().requestShutdown();
        QVERIFY(QTest::qWaitFor([&] { return node->shutdownRequested(); }, 10'000));
        QTRY_VERIFY_WITH_TIMEOUT(gate->finished.load(), 20'000);
        QVERIFY(gate->interrupted.load());
        // Disconnect both handlers while WalletContext and CWallet are still
        // alive inside unload, before appShutdown() tears them down.
        QVERIFY(QTest::qWaitFor([&] { return gate->unloading.tryAcquire(); }, 15'000));
        qmltest::Call(m_app, [handler](interfaces::Node*) { handler->reset(); return true; });
        gate->unload_handler.reset();
        gate->allow_unload.release();
    }

    // Closing a wallet waits for Core's wallet registry. Other results, such as
    // a Core message, do not wait behind it.
    void coreMessagesShowWhileAWalletCloseWaitsForTheRegistry()
    {
        auto* controller = qmltest::Model<WalletController>(m_app, "walletController"); QVERIFY(controller);
        QPointer<WalletModel> target{qmltest::CreateWallet(m_app, "close-target")}; QVERIFY(target);
        struct Gate { QSemaphore entered, release; std::atomic_bool timed_out{false}; };
        const auto gate = std::make_shared<Gate>();
        auto handler = std::make_shared<std::unique_ptr<interfaces::Handler>>(qmltest::Call(m_app, [gate](interfaces::Node* core) {
            return core->walletLoader().handleLoadWallet([gate](std::unique_ptr<interfaces::Wallet> wallet) {
                if (!wallet || wallet->getWalletName() != "close-gate") return;
                gate->entered.release();
                if (!gate->release.tryAcquire(1, 15'000)) gate->timed_out = true;
            });
        }));
        QVERIFY(*handler);
        auto* node = &m_app.node();
        std::thread rpc, message;
        const auto cleanup = qScopeGuard([&] {
            gate->release.release();
            if (rpc.joinable()) rpc.join();
            if (message.joinable()) message.join();
            qmltest::Call(m_app, [handler](interfaces::Node*) { handler->reset(); return true; });
        });
        rpc = std::thread{[node] {
            try { UniValue params{UniValue::VOBJ}; params.pushKV("wallet_name", "close-gate"); node->executeRpc("createwallet", params, ""); } catch (...) {}
        }};
        QVERIFY(QTest::qWaitFor([&] { return gate->entered.tryAcquire(); }, 15'000));
        controller->closeWallet("close-target");
        QVERIFY(!controller->isWalletOpen("close-target"));
        QTest::qWait(300);
        message = std::thread{[] { uiInterface.ThreadSafeMessageBox(Untranslated("Message during close"), CClientUIInterface::MSG_WARNING); }};
        QVERIFY(QTest::qWaitFor([&] { return m_app.nodeModel().runtimeDialogVisible(); }, 8'000));
        QVERIFY(target);
        QVERIFY(!gate->timed_out.load());
        m_app.nodeModel().answerRuntimeDialog(CClientUIInterface::BTN_OK);
        QTRY_VERIFY(!m_app.nodeModel().runtimeDialogVisible());
        QVERIFY(target);
        gate->release.release();
        QTRY_VERIFY_WITH_TIMEOUT(target.isNull(), 30'000);
        QVERIFY(!gate->timed_out.load());
        QCOMPARE(qmltest::Rpc(m_app, "listwallets").write().find("close-target") == std::string::npos, true);
    }

    void passwordChangesRelockAndPreserveTheWallet_data()
    {
        QTest::addColumn<QString>("original");
        QTest::addColumn<QString>("replacement");
        QTest::newRow("short") << QStringLiteral("old password") << QStringLiteral("new password");
        QTest::newRow("long_utf8")
            << QString::fromUtf8("old-\xc3\xa9-") + QString(40'000, QChar{'x'})
            << QString::fromUtf8("new-\xf0\x9f\x94\x91-") + QString(40'000, QChar{'y'});
    }

    void passwordChangesRelockAndPreserveTheWallet()
    {
        QFETCH(QString, original);
        QFETCH(QString, replacement);
        auto* wallet = qmltest::CreateWallet(m_app, QStringLiteral("password_%1").arg(QString::fromLatin1(QTest::currentDataTag())));
        QVERIFY(wallet);
        QVERIFY(wallet->encryptWallet(original));
        QTRY_COMPARE_WITH_TIMEOUT(wallet->securityState(), WalletModel::SecurityState::SecuritySucceeded, qmltest::kSecurityStateTimeoutMs);
        QTRY_VERIFY(wallet->isLocked());
        QVERIFY(wallet->changeWalletPassphrase("wrong password", replacement));
        QTRY_COMPARE_WITH_TIMEOUT(wallet->securityState(), WalletModel::SecurityState::SecurityFailed, qmltest::kSecurityStateTimeoutMs);
        QVERIFY(!wallet->settingsError().isEmpty());
        QTRY_VERIFY(wallet->isLocked());
        QVERIFY(wallet->changeWalletPassphrase(original, replacement));
        QTRY_COMPARE_WITH_TIMEOUT(wallet->securityState(), WalletModel::SecurityState::SecuritySucceeded, qmltest::kSecurityStateTimeoutMs);
        QVERIFY(wallet->settingsError().isEmpty());
        QTRY_VERIFY(wallet->isLocked());
        UniValue unlock{UniValue::VARR};
        unlock.push_back(replacement.toStdString());
        unlock.push_back(10);
        qmltest::Rpc(m_app, "walletpassphrase", unlock, wallet->name());
        QTRY_VERIFY(!wallet->isLocked());
        qmltest::Rpc(m_app, "walletlock", {}, wallet->name());
        QTRY_VERIFY(wallet->isLocked());
    }

    void passwordOutcomeReconcilesRpcChangesWhileGuiPublicationIsHeld_data()
    {
        QTest::addColumn<bool>("relock");
        QTest::newRow("rpc-unlocks") << false;
        QTest::newRow("rpc-unlocks-and-relocks") << true;
    }

    void passwordOutcomeReconcilesRpcChangesWhileGuiPublicationIsHeld()
    {
        QFETCH(bool, relock);
        auto* wallet = qmltest::CreateWallet(m_app, "security_rpc_interleaving");
        QVERIFY(wallet); QVERIFY(wallet->encryptWallet("before"));
        QTRY_COMPARE_WITH_TIMEOUT(wallet->securityState(), WalletModel::SecurityState::SecuritySucceeded, qmltest::kSecurityStateTimeoutMs);
        QTRY_VERIFY(wallet->isLocked());
        QVERIFY(wallet->changeWalletPassphrase("before", "after"));
        QCOMPARE(wallet->securityState(), WalletModel::SecurityState::SecurityPending);
        auto* node = &m_app.node();
        const std::string uri = "/wallet/" + wallet->name().toStdString();
        std::exception_ptr failure;
        QSemaphore completed;
        std::thread rpc{[node, uri, relock, &failure, &completed] {
            try {
                UniValue unlock{UniValue::VARR}; unlock.push_back("after"); unlock.push_back(60);
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{10};
                for (;;) {
                    try { node->executeRpc("walletpassphrase", unlock, uri); break; }
                    catch (const UniValue& error) {
                        // Only the known old-password window is retryable.
                        // Success proves the independent RPC observed the new
                        // password; no GUI processing is used as its barrier.
                        if (error.find_value("code").getInt<int>() != RPC_WALLET_PASSPHRASE_INCORRECT || std::chrono::steady_clock::now() >= deadline) throw;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds{10});
                }
                if (relock) node->executeRpc("walletlock", UniValue{UniValue::VARR}, uri);
            } catch (...) { failure = std::current_exception(); }
            completed.release();
        }};
        const auto join = qScopeGuard([&] { if (rpc.joinable()) rpc.join(); });
        // Intentional test-only GUI hold: Core/RPC and Domain must progress
        // independently. The normal test process watchdog owns a stuck RPC.
        const bool without_gui = completed.tryAcquire(1, 10'000);
        if (!without_gui) {
            const bool after_gui = QTest::qWaitFor([&] { return completed.available() > 0; }, 10'000);
            QVERIFY2(after_gui, "Independent wallet RPC did not drain after GUI publication resumed");
        }
        rpc.join();
        QVERIFY2(without_gui, "Public Core/RPC could not finish while GUI publication was held");
        QVERIFY2(!failure, "Independent wallet RPC failed");
        QCOMPARE(wallet->securityState(), WalletModel::SecurityState::SecurityPending);
        QTRY_COMPARE_WITH_TIMEOUT(wallet->securityState(), WalletModel::SecurityState::SecuritySucceeded, qmltest::kSecurityStateTimeoutMs);
        QTRY_COMPARE(wallet->isLocked(), relock);
        const auto expected = relock ? WalletSecurityModel::ObservedState::Locked : WalletSecurityModel::ObservedState::Unlocked;
        QCOMPARE(wallet->security()->observedState(), expected);
        if (!relock) {
            qmltest::Rpc(m_app, "walletlock", {}, wallet->name());
            QTRY_VERIFY(wallet->isLocked());
        }
    }

    void settingsExposeReadOnlyNameAndBackUpToTheChosenDirectory()
    {
        QVERIFY(qmltest::CreateWallet(m_app, "settings"));
        QVERIFY(qmltest::SelectSettings(m_app, "wallet"));
        QVERIFY(qmltest::WaitForVisible(m_app, "walletSettingsPage"));
        QCOMPARE(qmltest::Find(m_app, "walletNameRowValue")->property("text").toString(), QStringLiteral("settings"));
        QVERIFY(!qmltest::Find(m_app, "walletNameInput"));

        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        auto* path = qmltest::Find(m_app, "walletSettingsBackupPathField");
        QVERIFY(path);
        QVERIFY(path->setProperty("text", directory.path()));
        QVERIFY(qmltest::Click(m_app, "walletBackupRow"));
        QTRY_VERIFY(QFileInfo{directory.filePath("settings.bak")}.size() > 0);
        QCOMPARE(qmltest::Find(m_app, "walletSettingsPage")->property("errorText").toString(), QString{});
    }

    void wrongCurrentPasswordPreservesTheFieldAndTheWallet()
    {
        auto* wallet = qmltest::CreateWallet(m_app);
        QVERIFY(wallet);
        QVERIFY(wallet->encryptWallet("old password"));
        QTRY_COMPARE_WITH_TIMEOUT(wallet->securityState(), WalletModel::SecurityState::SecuritySucceeded, qmltest::kSecurityStateTimeoutMs);
        QTRY_VERIFY(wallet->isLocked());
        QVERIFY(qmltest::SelectSettings(m_app, "wallet"));
        QVERIFY(qmltest::Click(m_app, "walletPasswordRow"));
        QVERIFY(qmltest::WaitForVisible(m_app, "walletPasswordSettingsPage"));
        QVERIFY(qmltest::SetText(m_app, "walletPasswordCurrentField", "wrong password"));
        QVERIFY(qmltest::SetText(m_app, "walletPasswordNewField", "new integration password"));
        QVERIFY(qmltest::SetText(m_app, "walletPasswordConfirmField", "new integration password"));
        QVERIFY(qmltest::Click(m_app, "walletPasswordSaveButton"));
        QTRY_VERIFY(qmltest::Find(m_app, "walletPasswordErrorText")->property("text").toString().contains("incorrect", Qt::CaseInsensitive));
        QCOMPARE(qmltest::Find(m_app, "walletPasswordCurrentField")->property("text").toString(), QStringLiteral("wrong password"));
        QTRY_VERIFY(wallet->isLocked());
    }

    // #709: Set password from Settings explains, on the real encryption path,
    // that earlier backups may not cover the new keys, and the wallet settings
    // then keep that explanation on Back up wallet and write the fresh backup.
    void settingsSetPasswordExplainsChangedBackupCoverage()
    {
        auto* wallet = qmltest::CreateWallet(m_app, "coverage"); QVERIFY(wallet); QVERIFY(!wallet->isEncrypted());
        QVERIFY(qmltest::SelectSettings(m_app, "wallet")); QVERIFY(qmltest::WaitForVisible(m_app, "walletSettingsPage"));
        QCOMPARE(qmltest::Find(m_app, "walletBackupRow")->property("description").toString(), QString{});
        QVERIFY(qmltest::Click(m_app, "walletPasswordRow")); QVERIFY(qmltest::WaitForVisible(m_app, "walletPasswordSettingsPage"));
        const auto intro = qmltest::Find(m_app, "walletPasswordIntroTextDescription")->property("text").toString();
        QVERIFY(intro.contains("receiving and change keys")); QVERIFY(intro.contains("earlier backups")); QVERIFY(intro.contains("fresh backup"));
        QVERIFY(qmltest::SetText(m_app, "walletPasswordNewField", "coverage password"));
        QVERIFY(qmltest::SetText(m_app, "walletPasswordConfirmField", "coverage password"));
        QVERIFY(qmltest::Click(m_app, "walletPasswordConfirmToggle"));
        QVERIFY(qmltest::Click(m_app, "walletPasswordSaveButton"));
        QTRY_VERIFY_WITH_TIMEOUT(wallet->isEncrypted(), qmltest::kSecurityStateTimeoutMs);
        QTRY_VERIFY(wallet->isLocked());
        QCOMPARE(qmltest::Rpc(m_app, "getwalletinfo", {}, wallet->name()).find_value("unlocked_until").getInt<int64_t>(), 0);
        QVERIFY(qmltest::WaitForVisible(m_app, "walletSettingsPage"));
        QTRY_VERIFY(!qmltest::Find(m_app, "walletPasswordSettingsPage") || !qmltest::Find(m_app, "walletPasswordSettingsPage")->property("visible").toBool());
        auto* backup = qmltest::Find(m_app, "walletBackupRow"); QVERIFY(backup);
        const auto coverage = backup->property("description").toString();
        QVERIFY(coverage.contains("earlier backups")); QVERIFY(coverage.contains("fresh backup")); QVERIFY(coverage.contains("older backups"));
        QTemporaryDir directory; QVERIFY(directory.isValid());
        QVERIFY(qmltest::Find(m_app, "walletSettingsBackupPathField")->setProperty("text", directory.path()));
        QVERIFY(qmltest::Click(m_app, "walletBackupRow"));
        QTRY_VERIFY(PageBackup(m_app)); QTRY_COMPARE(PageBackup(m_app)->phase(), BackupFileOperation::Phase::Succeeded);
        QVERIFY(QFileInfo{directory.filePath("coverage.bak")}.size() > 0);
    }

    // D152: Core relocks an unlocked wallet before it rejects a wrong current
    // password. The failed change must publish that lock, and the same form
    // then changes the password with the correct one.
    void failedPasswordChangeAfterCoreRelocksPublishesTheLock()
    {
        auto* wallet = qmltest::CreateWallet(m_app, "relocked"); QVERIFY(wallet);
        const auto address = qmltest::NewAddress(m_app, wallet->name(), "legacy");
        // getwalletinfo keeps the RPC relock deadline after Core locks, so ask
        // Core to sign: only a locked wallet refuses.
        auto core_locked = [&] {
            UniValue params{UniValue::VARR}; params.push_back(address.toStdString()); params.push_back("probe");
            try { qmltest::Rpc(m_app, "signmessage", params, wallet->name()); return false; }
            catch (const std::runtime_error& error) { return std::string{error.what()}.find("passphrase") != std::string::npos; }
        };
        QVERIFY(wallet->encryptWallet("old password"));
        QTRY_COMPARE_WITH_TIMEOUT(wallet->securityState(), WalletModel::SecurityState::SecuritySucceeded, qmltest::kSecurityStateTimeoutMs);
        QTRY_VERIFY(wallet->isLocked());
        qmltest::Rpc(m_app, "walletpassphrase", qmltest::Json("[\"old password\",600]"), wallet->name());
        QTRY_VERIFY(!wallet->isLocked()); QVERIFY(!core_locked());
        QCOMPARE(wallet->security()->observedState(), WalletSecurityModel::ObservedState::Unlocked);
        QVERIFY(qmltest::SelectSettings(m_app, "wallet"));
        QVERIFY(qmltest::Click(m_app, "walletPasswordRow")); QVERIFY(qmltest::WaitForVisible(m_app, "walletPasswordSettingsPage"));
        QVERIFY(qmltest::SetText(m_app, "walletPasswordCurrentField", "wrong password"));
        QVERIFY(qmltest::SetText(m_app, "walletPasswordNewField", "new password"));
        QVERIFY(qmltest::SetText(m_app, "walletPasswordConfirmField", "new password"));
        QVERIFY(qmltest::Click(m_app, "walletPasswordSaveButton"));
        QTRY_VERIFY_WITH_TIMEOUT(qmltest::Find(m_app, "walletPasswordErrorText")->property("text").toString().contains("incorrect", Qt::CaseInsensitive), qmltest::kSecurityStateTimeoutMs);
        QVERIFY(core_locked());
        QTRY_VERIFY(wallet->isLocked());
        QCOMPARE(wallet->security()->observedState(), WalletSecurityModel::ObservedState::Locked);
        QVERIFY(qmltest::Find(m_app, "walletPasswordSettingsPage")->property("visible").toBool());
        QVERIFY(qmltest::SetText(m_app, "walletPasswordCurrentField", "old password"));
        QVERIFY(qmltest::Click(m_app, "walletPasswordSaveButton"));
        QTRY_COMPARE_WITH_TIMEOUT(qmltest::Find(m_app, "walletPasswordErrorText")->property("text").toString(), QStringLiteral("Password updated successfully."), qmltest::kSecurityStateTimeoutMs);
        QVERIFY(wallet->isLocked()); QVERIFY(core_locked());
        qmltest::Rpc(m_app, "walletpassphrase", qmltest::Json("[\"new password\",1]"), wallet->name());
    }

    void walletChangesUnwindPasswordSettings_data()
    {
        QTest::addColumn<bool>("close_wallet");
        QTest::newRow("close-selected-wallet") << true;
        QTest::newRow("select-another-wallet") << false;
    }

    void walletChangesUnwindPasswordSettings()
    {
        QFETCH(bool, close_wallet);
        auto* alpha = qmltest::CreateWallet(m_app, "alpha");
        QVERIFY(alpha);
        QVERIFY(alpha->encryptWallet("alpha password"));
        QTRY_COMPARE_WITH_TIMEOUT(alpha->securityState(), WalletModel::SecurityState::SecuritySucceeded, qmltest::kSecurityStateTimeoutMs);
        QVERIFY(qmltest::CreateWallet(m_app, "beta"));
        QVERIFY(selectWallet("alpha"));
        QVERIFY(qmltest::SelectSettings(m_app, "wallet"));
        QVERIFY(qmltest::Click(m_app, "walletPasswordRow"));
        QVERIFY(qmltest::WaitForVisible(m_app, "walletPasswordSettingsPage"));

        if (close_wallet) {
            QVERIFY(walletAction("alpha", "Close"));
            QVERIFY(qmltest::Click(m_app, "walletCloseConfirmationConfirmButton"));
            QTRY_COMPARE(qmltest::Rpc(m_app, "listwallets").size(), 1U);
        } else {
            QVERIFY(selectWallet("beta"));
        }
        QVERIFY(qmltest::WaitForVisible(m_app, "walletSettingsPage"));
        QVERIFY(!qmltest::Find(m_app, "walletPasswordSettingsPage") ||
                !qmltest::Find(m_app, "walletPasswordSettingsPage")->property("visible").toBool());
        QVERIFY(selectWallet(close_wallet ? "alpha" : "beta"));
        QVERIFY(qmltest::WaitForVisible(m_app, "walletNameRow"));
    }

    void selectorActionsTargetTheirOwnRowAndRequireCloseConfirmation()
    {
        QVERIFY(qmltest::CreateWallet(m_app, "alpha"));
        qmltest::Rpc(m_app, "createwallet", qmltest::Json("{\"wallet_name\":\"beta\",\"disable_private_keys\":true}"));
        QVERIFY(qmltest::WaitForOpenWallet(m_app, "beta"));
        auto* controller = qmltest::Model<WalletController>(m_app, "walletController");
        QVERIFY(controller);
        QSignalSpy loading{controller, &WalletController::walletLoadInProgressChanged};
        QVERIFY(selectWallet("alpha"));
        QVERIFY(loading.isEmpty());
        QCOMPARE(qmltest::Rpc(m_app, "listwallets").size(), 2U);

        QVERIFY(openSelector());
        QCOMPARE(qmltest::Find(m_app, "walletSelectStatus_alpha")->property("text").toString(), QStringLiteral("0.00000000 BTC"));
        QVERIFY(qmltest::Click(m_app, "walletSelectActions_beta"));
        QCOMPARE(qmltest::Find(m_app, "walletBadge")->property("text").toString(), QStringLiteral("alpha"));
        QVERIFY(qmltest::Click(m_app, "walletSelectSettings_beta"));
        QVERIFY(qmltest::WaitForWallet(m_app, "beta"));
        QVERIFY(qmltest::Find(m_app, "desktopWalletSettingsTabButton")->property("checked").toBool());

        QVERIFY(walletAction("beta", "Close"));
        QCOMPARE(qmltest::Rpc(m_app, "listwallets").size(), 2U);
        QVERIFY(qmltest::Click(m_app, "walletCloseConfirmationCancelButton"));
        QCOMPARE(qmltest::Rpc(m_app, "listwallets").size(), 2U);
        QVERIFY(walletAction("beta", "Close"));
        QVERIFY(qmltest::Click(m_app, "walletCloseConfirmationConfirmButton"));
        QTRY_COMPARE(qmltest::Rpc(m_app, "listwallets").size(), 1U);
        QVERIFY(qmltest::WaitForWallet(m_app, "alpha"));
        QVERIFY(openSelector());
        auto* row = qmltest::Find(m_app, "walletSelectItem_beta");
        QVERIFY(row);
        QTRY_COMPARE(row->property("loadState").toInt(), static_cast<int>(WalletListModel::LoadState::Closed));
        QCOMPARE(row->property("iconSource").toString(), QStringLiteral("image://images/wallet"));
        QVERIFY(!qmltest::Find(m_app, "walletSelectActionsMenu_beta")->property("visible").toBool());
    }

    void selectorLoadFailureRemainsVisibleAfterDismissingTheAlert()
    {
        QPointer<WalletModel> wallet{qmltest::CreateWallet(m_app, "broken")};
        QVERIFY(wallet);
        QCOMPARE(qmltest::Rpc(m_app, "getwalletinfo", {}, "broken").find_value("format").get_str(), std::string{"sqlite"});
        const auto database_path = qmltest::Call(m_app, [](interfaces::Node* node) -> QString {
            return QString::fromStdString(node->walletLoader().getWalletDir()) + QStringLiteral("/broken/wallet.dat");
        });
        const auto header = qmltest::Call(m_app, [database_path](interfaces::Node*) {
            QFile database{database_path};
            return database.open(QIODevice::ReadOnly) ? database.read(32) : QByteArray{};
        });
        QCOMPARE(header.size(), 32);
        QVERIFY(header.startsWith(QByteArray{"SQLite format 3\0", 16}));
        auto* controller = qmltest::Model<WalletController>(m_app, "walletController");
        QVERIFY(controller);
        auto* wallets = qmltest::Model<WalletListModel>(m_app, "walletListModel");
        QVERIFY(wallets);
        wallets->listWalletDir();
        QTRY_VERIFY(wallets->walletDirLoaded());
        controller->closeWallet("broken");
        QTRY_VERIFY(wallet.isNull());
        QTRY_COMPARE(qmltest::Rpc(m_app, "listwallets").size(), 0U);
        QVERIFY(openSelector());

        // Keep SQLite's format signature so directory discovery still lists it,
        // while opening the truncated database produces a real load error.
        const auto written = qmltest::Call(m_app, [database_path, header](interfaces::Node*) -> qint64 {
            QFile database{database_path};
            return database.open(QIODevice::WriteOnly | QIODevice::Truncate) ? database.write(header) : -1;
        });
        QCOMPARE(written, header.size());
        QVERIFY(qmltest::Click(m_app, "walletSelectItem_broken"));
        QVERIFY(qmltest::WaitForVisible(m_app, "walletLoadErrorPopup"));
        QVERIFY(!qmltest::Find(m_app, "walletLoadErrorPopupText")->property("text").toString().isEmpty());
        auto* row = qmltest::Find(m_app, "walletSelectItem_broken");
        QVERIFY(row);
        QTRY_COMPARE(row->property("loadState").toInt(), static_cast<int>(WalletListModel::LoadState::LoadError));
        QVERIFY(qmltest::Find(m_app, "walletSelectStatus_broken")->property("text").toString().contains("failed", Qt::CaseInsensitive));
        QVERIFY(qmltest::Click(m_app, "walletLoadErrorPopupDismissButton"));
        QTRY_VERIFY(!qmltest::Find(m_app, "walletLoadErrorPopup")->property("visible").toBool());
        QCOMPARE(row->property("loadState").toInt(), static_cast<int>(WalletListModel::LoadState::LoadError));
    }

    // #1004: a wallet.dat already at the wallet directory root when the GUI
    // starts (with -nowallet, so it starts closed) is listed with a usable
    // name, not a blank "Failed to open wallet" row, and opens from the picker.
    void unnamedWalletPresentAtStartupIsNamedAndOpensFromThePicker()
    {
        auto* controller = qmltest::Model<WalletController>(m_app, "walletController"); QVERIFY(controller);
        auto* catalog = qmltest::Model<WalletListModel>(m_app, "walletListModel"); QVERIFY(catalog);
        QVERIFY(QFileInfo::exists(QString::fromStdString(fs::PathToString(wallet::GetWalletDir() / "wallet.dat"))));
        QCOMPARE(qmltest::Rpc(m_app, "listwallets").size(), 0U);
        const auto label = [&] { for (int row = 0; row < catalog->rowCount(); ++row) if (catalog->data(catalog->index(row), WalletListModel::NameRole).toString().isEmpty()) return catalog->data(catalog->index(row), WalletListModel::DisplayNameRole).toString(); return QStringLiteral("<missing>"); };
        QTRY_VERIFY(catalog->walletDirLoaded()); QTRY_COMPARE(label(), QStringLiteral("Default wallet"));
        QVERIFY(QTest::qWaitFor([&] { return openSelector() && qmltest::Find(m_app, "walletSelectItem_"); }));
        auto* row = qmltest::Find(m_app, "walletSelectItem_"); QVERIFY(row); QTRY_COMPARE(row->property("loadState").toInt(), int(WalletListModel::LoadState::Closed));
        QCOMPARE(qmltest::Find(m_app, "walletSelectName_")->property("text").toString(), QStringLiteral("Default wallet"));
        QVERIFY(qmltest::Find(m_app, "walletSelectStatus_")->property("text").toString().isEmpty());
        QVERIFY(qmltest::Click(m_app, "walletSelectItem_")); QTRY_VERIFY(controller->isWalletOpen(QString{}));
        QVERIFY(qmltest::Rpc(m_app, "listwallets")[0].get_str().empty()); QCOMPARE(qmltest::Rpc(m_app, "getwalletinfo", {}, "").find_value("walletname").get_str(), std::string{});
        QVERIFY(openSelector()); row = qmltest::Find(m_app, "walletSelectItem_"); QVERIFY(row); QTRY_COMPARE(row->property("loadState").toInt(), int(WalletListModel::LoadState::Open));
        QVERIFY(qmltest::Click(m_app, "walletSelectItem_")); QTRY_VERIFY(controller->selectedWallet() && controller->selectedWallet()->name().isEmpty());
        QCOMPARE(controller->selectedWallet()->displayName(), QStringLiteral("Default wallet")); QTRY_COMPARE(qmltest::Find(m_app, "walletBadge")->property("text").toString(), QStringLiteral("Default wallet"));
    }

    void unnamedWalletOpensClosesAndReopensFromThePicker()
    {
        // Core lists a wallet.dat at the wallet directory root as the wallet
        // with an empty name. The picker must open it, close it and open it
        // again, not report "Failed to open wallet".
        auto* controller = qmltest::Model<WalletController>(m_app, "walletController"); QVERIFY(controller);
        auto* seed = qmltest::CreateWallet(m_app, "unnamed_seed"); QVERIFY(seed);
        const auto root_file = fs::PathToString(wallet::GetWalletDir() / "wallet.dat");
        QVERIFY(!QFileInfo::exists(QString::fromStdString(root_file)));
        UniValue backup{UniValue::VARR}; backup.push_back(root_file);
        qmltest::Rpc(m_app, "backupwallet", backup, seed->name());
        QVERIFY(QFileInfo::exists(QString::fromStdString(root_file)));
        // A picker click loads a closed wallet; completion does not select it
        // behind a newer choice. The next click on the loaded row selects it.
        const auto row_state = [&](WalletListModel::LoadState state) {
            // Rows are rebuilt as the catalog changes; look the row up each time.
            return QTest::qWaitFor([&] {
                auto* row = qmltest::Find(m_app, "walletSelectItem_");
                return row && row->property("enabled").toBool() && row->property("loadState").toInt() == static_cast<int>(state);
            });
        };
        const auto open_from_picker = [&] {
            if (!openSelector() || !row_state(WalletListModel::LoadState::Closed) || !qmltest::Click(m_app, "walletSelectItem_")) return false;
            if (!QTest::qWaitFor([&] { return controller->isWalletOpen(QString{}); })) return false;
            if (!openSelector() || !row_state(WalletListModel::LoadState::Open) || !qmltest::Click(m_app, "walletSelectItem_")) return false;
            return QTest::qWaitFor([&] { return controller->selectedWallet() && controller->selectedWallet()->name().isEmpty(); });
        };
        QVERIFY(QTest::qWaitFor([&] { return openSelector() && qmltest::Find(m_app, "walletSelectItem_"); }));
        QVERIFY(open_from_picker());
        // Keep the RPC result alive while iterating its values.
        const auto loaded = [&] { std::set<std::string> names; const auto result = qmltest::Rpc(m_app, "listwallets"); for (const auto& name : result.getValues()) names.insert(name.get_str()); return names; };
        QVERIFY(loaded() == (std::set<std::string>{"", "unnamed_seed"}));
        QVERIFY(openSelector()); QVERIFY(row_state(WalletListModel::LoadState::Open));
        QVERIFY(qmltest::Click(m_app, "walletSelectActions_"));
        QVERIFY(qmltest::Click(m_app, "walletSelectClose_"));
        QVERIFY(qmltest::Click(m_app, "walletCloseConfirmationConfirmButton"));
        QTRY_VERIFY(!controller->isWalletOpen(QString{}));
        QTRY_VERIFY(loaded() == (std::set<std::string>{"unnamed_seed"}));
        QVERIFY(open_from_picker());
        QVERIFY(loaded() == (std::set<std::string>{"", "unnamed_seed"}));
    }
    void rpcLoadRacingPickerOpenKeepsTheGuiIntentAndSelection()
    {
        static constexpr auto wallet_a_name = "rpc_race_a";
        constexpr auto wallet_b_name = "rpc_race_b";
        auto* controller = qmltest::Model<WalletController>(m_app, "walletController");
        QVERIFY(controller);

        QVERIFY(qmltest::CreateWallet(m_app, wallet_a_name));
        controller->closeWallet(wallet_a_name);
        QTRY_VERIFY(!controller->isWalletOpen(wallet_a_name));
        QTRY_COMPARE(qmltest::Rpc(m_app, "listwallets").size(), 0U);
        auto* wallet_b = qmltest::CreateWallet(m_app, wallet_b_name);
        QVERIFY(wallet_b);
        QVERIFY(qmltest::WaitForWallet(m_app, wallet_b_name));
        QCOMPARE(controller->selectedWallet(), wallet_b);

        QVERIFY(openSelector());
        auto* row_a = qmltest::Find(m_app, "walletSelectItem_rpc_race_a");
        QVERIFY(row_a);
        QTRY_COMPARE(row_a->property("loadState").toInt(), static_cast<int>(WalletListModel::LoadState::Closed));
        QVERIFY(row_a->property("enabled").toBool());

        struct NotificationGate {
            QSemaphore entered;
            QSemaphore release;
            std::atomic_bool claimed{false};
            std::atomic_bool callback_on_gui{true};
            std::atomic_bool callback_timed_out{false};
        };
        auto gate = std::make_shared<NotificationGate>();
        auto handler = std::make_shared<std::unique_ptr<interfaces::Handler>>(
            qmltest::Call(m_app, [gate](interfaces::Node* node) {
                return node->walletLoader().handleLoadWallet([gate](std::unique_ptr<interfaces::Wallet> wallet) {
                    if (!wallet || wallet->getWalletName() != wallet_a_name || gate->claimed.exchange(true)) return;
                    gate->callback_on_gui.store(QThread::currentThread() == QCoreApplication::instance()->thread());
                    gate->entered.release();
                    if (!gate->release.tryAcquire(1, 15'000)) gate->callback_timed_out.store(true);
                });
            }));
        QVERIFY(*handler);

        struct RpcOutcome {
            std::optional<UniValue> result;
            std::exception_ptr error;
            std::atomic_bool finished{false};
        };
        RpcOutcome outcome;
        std::thread rpc_thread;
        auto disconnectHandler = [&] {
            if (!*handler) return true;
            return qmltest::Call(m_app, [handler](interfaces::Node*) {
                handler->reset();
                return true;
            });
        };
        const auto cleanup = qScopeGuard([&] {
            gate->release.release();
            if (rpc_thread.joinable()) rpc_thread.join();
            disconnectHandler();
        });

        auto* node = &m_app.node();
        rpc_thread = std::thread([node, &outcome] {
            try {
                UniValue params{UniValue::VARR};
                params.push_back(wallet_a_name);
                outcome.result = node->executeRpc("loadwallet", params, "");
            } catch (...) {
                outcome.error = std::current_exception();
            }
            outcome.finished.store(true);
        });

        QVERIFY2(gate->entered.tryAcquire(1, 15'000), "RPC load did not reach the registered wallet notification");
        QVERIFY(!gate->callback_on_gui.load());
        QVERIFY(!gate->callback_timed_out.load());
        QVERIFY(!controller->walletForName(wallet_a_name));
        QCOMPARE(row_a->property("loadState").toInt(), static_cast<int>(WalletListModel::LoadState::Closed));

        QSignalSpy succeeded{controller, &WalletController::walletLoadSucceeded};
        QSignalSpy error_notices{controller, &WalletController::loadFailureChanged};
        QSignalSpy warnings_changed{controller, &WalletController::walletLoadWarningsChanged};
        const auto operation_before = OpenId(*controller);
        QVERIFY(QMetaObject::invokeMethod(row_a, "clicked", Qt::DirectConnection));
        QCOMPARE(OpenId(*controller), operation_before + 1);
        QCOMPARE(OpenState(*controller), WalletController::WalletOperationState::WalletOperationPending);
        QCOMPARE(OpenName(*controller), QString::fromLatin1(wallet_a_name));
        QVERIFY(controller->walletLoadInProgress());

        gate->release.release();
        const bool rpc_finished = QTest::qWaitFor([&] { return outcome.finished.load(); }, 30'000);
        if (rpc_thread.joinable()) rpc_thread.join();
        QVERIFY(rpc_finished);
        QVERIFY(!gate->callback_timed_out.load());
        if (outcome.error) {
            try {
                std::rethrow_exception(outcome.error);
            } catch (const UniValue& error) {
                QFAIL(qPrintable(QString::fromStdString(error.write())));
            } catch (const std::exception& error) {
                QFAIL(error.what());
            } catch (...) {
                QFAIL("RPC loadwallet threw an unknown exception");
            }
        }
        QVERIFY(outcome.result.has_value());
        QVERIFY(outcome.result->isObject());
        QCOMPARE(QString::fromStdString(outcome.result->find_value("name").get_str()), QString::fromLatin1(wallet_a_name));
        const auto core_wallets = qmltest::Rpc(m_app, "listwallets");
        const auto& core_wallet_names = core_wallets.getValues();
        QVERIFY(std::any_of(core_wallet_names.begin(), core_wallet_names.end(), [](const UniValue& value) {
            return value.get_str() == wallet_a_name;
        }));

        auto* wallet_a = qmltest::WaitForOpenWallet(m_app, wallet_a_name);
        QVERIFY(wallet_a);
        QTRY_COMPARE(OpenState(*controller), WalletController::WalletOperationState::WalletOperationSucceeded);
        QTRY_COMPARE(OpenAdoption(*controller), WalletController::WalletAdoptionState::AdoptionAvailable);
        QCOMPARE(OpenName(*controller), QString::fromLatin1(wallet_a_name));
        QCOMPARE(OpenResult(*controller), wallet_a);
        QVERIFY(OpenCanOpen(*controller));
        QCOMPARE(controller->walletForName(wallet_a_name), wallet_a);
        QCOMPARE(controller->selectedWallet(), wallet_b);
        QTRY_COMPARE(succeeded.size(), 1);
        QCOMPARE(qvariant_cast<WalletModel*>(succeeded.front().front()), wallet_a);

        int wallet_a_facades{0};
        for (auto* opened : controller->openWallets()) {
            if (auto* facade = qobject_cast<WalletModel*>(opened); facade && facade->name() == QString::fromLatin1(wallet_a_name)) {
                ++wallet_a_facades;
            }
        }
        QCOMPARE(wallet_a_facades, 1);
        QCOMPARE(error_notices.size(), 0);
        QVERIFY(LoadError(*controller).isEmpty());
        auto* error_popup = qmltest::Find(m_app, "walletLoadErrorPopup");
        QVERIFY(error_popup);
        QVERIFY(!error_popup->property("visible").toBool());
        QVERIFY(!OpenWarnings(*controller).isEmpty());
        QVERIFY(!warnings_changed.isEmpty());
        QVERIFY(disconnectHandler());
    }

    void rpcLoadAndUnloadBeforeGuiAdoptionKeepsPickerOpen()
    {
        constexpr auto target = "unload_before_gui";
        auto* controller = qmltest::Model<WalletController>(m_app, "walletController"); QVERIFY(controller);
        QPointer<WalletController> retained_controller{controller};
        QPointer<WalletModel> initial{qmltest::CreateWallet(m_app, target)}; QVERIFY(initial);
        controller->closeWallet(target); QTRY_VERIFY(initial.isNull());
        auto* survivor = qmltest::CreateWallet(m_app, "unload_survivor"); QVERIFY(survivor);
        QVERIFY(qmltest::WaitForWallet(m_app, "unload_survivor"));
        QVERIFY(openSelector());
        QPointer<QObject> picker{qmltest::Find(m_app, "walletSelectPopup")}; QVERIFY(picker);
        auto* row = qmltest::Find(m_app, "walletSelectItem_unload_before_gui"); QVERIFY(row);
        QTRY_COMPARE(row->property("loadState").toInt(), static_cast<int>(WalletListModel::LoadState::Closed));
        struct Gate {
            QSemaphore unloaded;
            std::unique_ptr<interfaces::Handler> observation;
            std::atomic_int load_callbacks{0}, unload_callbacks{0};
            std::atomic_bool callbacks_off_gui{true}, finished{false};
            std::optional<UniValue> load_result, unload_result;
            std::exception_ptr error;
        };
        const auto gate = std::make_shared<Gate>();
        const std::weak_ptr<Gate> weak = gate;
        auto producer = std::make_shared<std::unique_ptr<interfaces::Handler>>(qmltest::Call(m_app, [weak](interfaces::Node* node) {
            return node->walletLoader().handleLoadWallet([weak](std::unique_ptr<interfaces::Wallet> wallet) {
                if (!wallet || wallet->getWalletName() != "unload_before_gui") return;
                const auto owner = weak.lock(); if (!owner) return;
                ++owner->load_callbacks;
                if (QThread::currentThread() == QCoreApplication::instance()->thread()) owner->callbacks_off_gui = false;
                owner->observation = wallet->handleUnload([weak] {
                    if (const auto observed = weak.lock()) {
                        ++observed->unload_callbacks;
                        // Release this test-only connection before Core destroys its wallet.
                        observed->observation.reset();
                        if (QThread::currentThread() == QCoreApplication::instance()->thread()) observed->callbacks_off_gui = false;
                        observed->unloaded.release();
                    }
                });
            });
        }));
        QVERIFY(*producer);
        std::thread rpc;
        const auto cleanup = qScopeGuard([&] {
            // Recovery runs only after a failed oracle; it never supplies the
            // release required by a passing unload-with-picker-open result.
            if (rpc.joinable() && !gate->finished.load()) {
                controller->closeWallet(target);
                if (!QTest::qWaitFor([&] { return gate->finished.load(); }, 30'000)) {
                    controller->beginShutdown();
                    (void)QTest::qWaitFor([&] { return gate->finished.load(); }, 30'000);
                }
            }
            if (rpc.joinable()) rpc.join();
            qmltest::Call(m_app, [producer, gate](interfaces::Node*) { gate->observation.reset(); producer->reset(); return true; });
        });
        QSignalSpy loaded{controller, &WalletController::walletLoadSucceeded};
        QSignalSpy inserted{controller->openWalletModel(), &QAbstractItemModel::rowsInserted};
        auto* node = &m_app.node();
        rpc = std::thread([node, gate] {
            try {
                UniValue params{UniValue::VARR}; params.push_back("unload_before_gui");
                gate->load_result = node->executeRpc("loadwallet", params, "");
                gate->unload_result = node->executeRpc("unloadwallet", params, "");
            } catch (...) { gate->error = std::current_exception(); }
            gate->finished = true;
        });
        // Deliberately do not pump GUI events: both sequential public RPCs
        // reach the one unload notification before first GUI adoption.
        QVERIFY2(gate->unloaded.tryAcquire(1, 15'000), "RPC unload did not reach its public notification before GUI adoption");
        QVERIFY(!controller->walletForName(target)); QCOMPARE(inserted.size(), 0);
        QCOMPARE(controller->selectedWallet(), survivor);
        QVERIFY(picker && picker->property("opened").toBool());
        QVERIFY2(QTest::qWaitFor([&] { return gate->finished.load(); }, 30'000), "RPC unload must finish without closing the opening picker or its controller");
        rpc.join();
        if (gate->error) {
            try { std::rethrow_exception(gate->error); }
            catch (const UniValue& error) { QFAIL(qPrintable(QString::fromStdString(error.write()))); }
            catch (const std::exception& error) { QFAIL(error.what()); }
            catch (...) { QFAIL("Sequential RPC load/unload threw an unknown exception"); }
        }
        QVERIFY(gate->load_result && gate->load_result->isObject()); QVERIFY(gate->unload_result && gate->unload_result->isObject());
        QCOMPARE(gate->load_callbacks.load(), 1); QCOMPARE(gate->unload_callbacks.load(), 1); QVERIFY(gate->callbacks_off_gui.load());
        QVERIFY(retained_controller == controller); QVERIFY(controller->initialized());
        QVERIFY(picker && picker->property("opened").toBool());
        QTRY_COMPARE(row->property("loadState").toInt(), static_cast<int>(WalletListModel::LoadState::Closed));
        QVERIFY(!controller->walletForName(target)); QVERIFY(!controller->isWalletOpen(target));
        QCOMPARE(controller->selectedWallet(), survivor); QCOMPARE(controller->openWallets(), QList<QObject*>({survivor}));
        QCOMPARE(inserted.size(), 0); QCOMPARE(loaded.size(), 0);
        const auto loaded_wallets = qmltest::Rpc(m_app, "listwallets");
        QCOMPARE(loaded_wallets.size(), 1U); QCOMPARE(loaded_wallets[0].get_str(), std::string{"unload_survivor"});
        const auto directories = qmltest::Rpc(m_app, "listwalletdir").find_value("wallets");
        const auto& names = directories.getValues();
        QVERIFY(std::any_of(names.begin(), names.end(), [](const UniValue& entry) { return entry.find_value("name").get_str() == "unload_before_gui"; }));
    }

    void walletTypeSelectorRejectsInvalidBackups()
    {
        QVERIFY(qmltest::Click(m_app, "walletBadge"));
        QVERIFY(qmltest::WaitForVisible(m_app, "walletCreationTypePage"));
        QVERIFY(!qmltest::Find(m_app, "walletTypeMultiKey"));
        QVERIFY(!qmltest::Find(m_app, "walletTypeCustom"));
        auto* controller = qmltest::Model<WalletController>(m_app, "walletController");
        QVERIFY(controller);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QFile invalid{directory.filePath("invalid.bak")};
        QVERIFY(invalid.open(QIODevice::WriteOnly));
        const QByteArray original{"not a wallet backup"};
        QCOMPARE(invalid.write(original), original.size());
        invalid.close();
        auto* wizard = qmltest::Find(m_app, "createWalletWizard");
        QVERIFY(wizard);
        QVERIFY(QMetaObject::invokeMethod(wizard, "importFile", Q_ARG(QVariant, invalid.fileName())));
        QVERIFY(qmltest::WaitForVisible(m_app, "walletImportErrorPage"));
        auto* operation = controller->creation()->operation(); QVERIFY(operation);
        QTRY_VERIFY(operation->terminal());
        QVERIFY(!operation->errorText().isEmpty());
        QCOMPARE(operation->kind(), WalletOperationModel::Restore);
        QCOMPARE(operation->phase(), WalletOperationModel::Failed);
        QCOMPARE(operation->error().code, QStringLiteral("UnsupportedBackup"));
        QCOMPARE(operation->source().path(), invalid.fileName());
        QVERIFY(operation->outcomeKnown());
        QCOMPARE(operation->result().effect, WalletOperationResult::NoEffect);
        QVERIFY(!operation->resultWallet()); QVERIFY(!operation->canOpenResult());
        // Invalid database bytes are not a SingleKey/MultiKey policy claim.
        // R13 rejects the bounded format hint before any restore mutation and
        // projects that exact owned failure, preserving the original source.
        QCOMPARE(qmltest::Find(m_app, "importWalletErrorTitle")->property("text").toString(), QStringLiteral("This wallet backup could not be opened"));
        auto* help = operation->errorDetails(); QVERIFY(help);
        QTRY_VERIFY(help->rowCount() > 0);
        QString details;
        for (int row{0}; row < help->rowCount(); ++row)
            details += help->data(help->index(row, 0), Qt::UserRole + 1).toString();
        QCOMPARE(details, operation->errorText());
        QVERIFY2(details.contains("The selected file does not have a supported wallet database header. The original file was not changed."), qPrintable(details));
        QCOMPARE(BackupBytes(invalid.fileName()), original);
        QVERIFY(!QFileInfo::exists(QString::fromStdString(m_app.node().walletLoader().getWalletDir()) + "/invalid"));
        QCOMPARE(qmltest::Rpc(m_app, "listwallets").size(), 0U);
    }

    void backupRefusalPreservesSelectedAndFinalFiles_data()
    {
        QTest::addColumn<bool>("selected_exists");
        QTest::newRow("dotted-new-name-existing-final-backup") << false;
        QTest::newRow("selected-and-final-sentinels") << true;
    }

    void backupRefusalPreservesSelectedAndFinalFiles()
    {
        QFETCH(bool, selected_exists);
        const bool previous = QCoreApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
        QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs, true);
        const auto restore = qScopeGuard([previous] { QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs, previous); });
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto selected = directory.filePath("archive.v1");
        const QString final{selected + ".bak"};
        auto* original = qmltest::CreateWallet(m_app, "backup_original");
        QVERIFY(original);
        const auto original_address = qmltest::NewAddress(m_app, original->name());
        BackupPage direct{*original};
        QVERIFY(direct.begin(final));
        QTRY_VERIFY(QFileInfo{final}.size() > 0);
        QTRY_VERIFY(!direct.operation()->busy());
        const auto final_before = BackupBytes(final);
        QVERIFY(!final_before.isEmpty());
        QByteArray selected_before;
        if (selected_exists) {
            QFile sentinel{selected}; QVERIFY(sentinel.open(QIODevice::WriteOnly));
            selected_before = "selected path sentinel - retain every byte";
            QCOMPARE(sentinel.write(selected_before), qint64{selected_before.size()}); sentinel.close();
        }
        auto* replacement = qmltest::CreateWallet(m_app, "backup_replacement");
        QVERIFY(replacement);
        const auto replacement_address = qmltest::NewAddress(m_app, replacement->name());
        RefuseNativeOverwrite refuse;
        qApp->installEventFilter(&refuse);
        const auto filter_cleanup = qScopeGuard([&refuse] { qApp->removeEventFilter(&refuse); });
        QVERIFY(qmltest::Click(m_app, "desktopWalletSettingsTabButton"));
        QVERIFY(qmltest::Click(m_app, "settingsSidebar_wallet"));
        QVERIFY(qmltest::WaitForVisible(m_app, "walletSettingsPage"));
        QVERIFY(qmltest::Click(m_app, "walletBackupRow"));
        QTRY_VERIFY(VisibleBackupChooser());
        auto* chooser = VisibleBackupChooser();
        auto* name_field = chooser->findChild<QLineEdit*>("fileNameEdit");
        QVERIFY(name_field); // Actual Widgets fallback, not a fabricated accepted event.
        chooser->selectNameFilter("All files (*)");
        chooser->setDirectory(directory.path());
        name_field->clear();
        QTest::keyClicks(name_field, "archive.v1");
        QSignalSpy accepted{chooser, &QFileDialog::accepted};
        qInfo() << "Fallback typed name" << name_field->text() << "selected files before Return" << chooser->selectedFiles();
        QTest::keyClick(name_field, Qt::Key_Return);
        QTRY_VERIFY(accepted.count() == 1 || refuse.prompts > 0);
        qInfo() << "Fallback after Return selected files" << chooser->selectedFiles()
                << "bound current wallet" << replacement->name()
                << "QML error" << qmltest::Find(m_app, "walletSettingsPage")->property("errorText");
        int app_refusals{0};
        if (refuse.prompts > 0) {
            chooser->reject();
        } else if (auto* operation = PageBackup(m_app)) {
            QTRY_VERIFY(operation->property("confirmationRequired").toBool() || !operation->property("busy").toBool());
            if (operation->property("confirmationRequired").toBool()) {
                QCOMPARE(operation->property("finalPath").toString(), final);
                QVERIFY(qmltest::WaitForVisible(m_app, "walletBackupOverwritePopup"));
                QVERIFY(qmltest::Click(m_app, "walletBackupOverwriteRejectButton"));
                ++app_refusals;
                QTRY_VERIFY(!operation->property("busy").toBool());
            }
        }
        const auto final_after = BackupBytes(final);
        const bool selected_preserved = selected_exists ? BackupBytes(selected) == selected_before : !QFileInfo::exists(selected);
        // Independently restore the resulting file before checking bytes, so
        // the red run records whose real keys replaced the disposable backup.
        UniValue params{UniValue::VARR}; params.push_back("backup_restored"); params.push_back(final.toStdString());
        qmltest::Rpc(m_app, "restorewallet", params);
        auto owns = [&](const QString& address) {
            UniValue request{UniValue::VARR}; request.push_back(address.toStdString());
            return qmltest::Rpc(m_app, "getaddressinfo", request, "backup_restored").find_value("ismine").get_bool();
        };
        const bool owns_original = owns(original_address), owns_replacement = owns(replacement_address);
        qInfo() << "Backup refusal oracle native prompts" << refuse.prompts << "app refusals" << app_refusals
                << "selected bytes preserved" << selected_preserved << "final bytes preserved" << (final_after == final_before)
                << "restored original/replacement ownership" << owns_original << owns_replacement;
        QVERIFY(selected_preserved);
        QCOMPARE(final_after, final_before);
        QVERIFY(owns_original); QVERIFY(!owns_replacement);
        QCOMPARE(refuse.prompts + app_refusals, 1);
    }

    void newBackupDestinationIsTheApprovedPath_data()
    {
        QTest::addColumn<bool>("rewritten");
        QTest::newRow("known-suffix-new-file") << false;
        QTest::newRow("dotted-name-new-final-file") << true;
    }

    void newBackupDestinationIsTheApprovedPath()
    {
        QFETCH(bool, rewritten);
        const bool previous = QCoreApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
        QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs, true);
        const auto restore = qScopeGuard([previous] { QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs, previous); });
        QTemporaryDir directory; QVERIFY(directory.isValid());
        auto* wallet = qmltest::CreateWallet(m_app, "approved_source"); QVERIFY(wallet);
        const auto address = qmltest::NewAddress(m_app, wallet->name());
        const QString name = rewritten ? "new.archive" : "new.bak";
        const QString selected = directory.filePath(name);
        const QString final = rewritten ? selected + ".bak" : selected;
        QVERIFY(qmltest::Click(m_app, "desktopWalletSettingsTabButton"));
        QVERIFY(qmltest::Click(m_app, "settingsSidebar_wallet"));
        QVERIFY(qmltest::WaitForVisible(m_app, "walletSettingsPage"));
        QVERIFY(qmltest::Click(m_app, "walletBackupRow"));
        QTRY_VERIFY(VisibleBackupChooser()); auto* chooser = VisibleBackupChooser();
        chooser->selectNameFilter("All files (*)"); chooser->setDirectory(directory.path());
        auto* field = chooser->findChild<QLineEdit*>("fileNameEdit"); QVERIFY(field);
        field->clear(); QTest::keyClicks(field, name); QTest::keyClick(field, Qt::Key_Return);
        QTRY_VERIFY(PageBackup(m_app)); auto* operation = PageBackup(m_app);
        if (rewritten) {
            QTRY_VERIFY(operation->confirmationRequired());
            QCOMPARE(operation->finalPath(), final);
            QVERIFY(!QFileInfo::exists(selected)); QVERIFY(!QFileInfo::exists(final));
            QVERIFY(qmltest::WaitForVisible(m_app, "walletBackupOverwritePopup"));
            QVERIFY(qmltest::Click(m_app, "walletBackupOverwriteApproveButton"));
        }
        QTRY_COMPARE(operation->phase(), BackupFileOperation::Phase::Succeeded);
        QCOMPARE(operation->finalPath(), final); QVERIFY(QFileInfo{final}.size() > 0);
        if (rewritten) QVERIFY(!QFileInfo::exists(selected));
        UniValue restore_params{UniValue::VARR}; restore_params.push_back("approved_restored"); restore_params.push_back(final.toStdString());
        qmltest::Rpc(m_app, "restorewallet", restore_params);
        UniValue info{UniValue::VARR}; info.push_back(address.toStdString());
        QVERIFY(qmltest::Rpc(m_app, "getaddressinfo", info, "approved_restored").find_value("ismine").get_bool());
    }

    // A backup never replaces a wallet database, even one the user would
    // confirm replacing: the wallet's own file, a hard link to it, or an
    // unloaded wallet's file in the wallet folder.
    void backupRefusesWalletDatabases_data()
    {
        QTest::addColumn<int>("target");
        QTest::newRow("own-database") << 0;
        QTest::newRow("hard-link") << 1;
        QTest::newRow("unloaded-wallet") << 2;
    }

    void backupRefusesWalletDatabases()
    {
        QFETCH(int, target);
        auto* wallet = qmltest::CreateWallet(m_app, QStringLiteral("live_source_%1").arg(target)); QVERIFY(wallet);
        const QDir wallet_dir{QString::fromStdString(qmltest::Call(m_app, [](interfaces::Node* node) { return node->walletLoader().getWalletDir(); }))};
        QString owner = wallet->name();
        if (target == 2) {
            owner = QStringLiteral("unloaded_target");
            UniValue create{UniValue::VARR}; create.push_back(owner.toStdString()); qmltest::Rpc(m_app, "createwallet", create);
            UniValue unload{UniValue::VARR}; unload.push_back(owner.toStdString()); qmltest::Rpc(m_app, "unloadwallet", unload);
        }
        const QString database = wallet_dir.filePath(owner + QStringLiteral("/wallet.dat"));
        QVERIFY(QFileInfo{database}.isFile());
        // Hard links need the wallet's file system, so stay beside the wallet folder.
        QTemporaryDir directory{QFileInfo{wallet_dir.absolutePath()}.dir().filePath("backup-links-XXXXXX")}; QVERIFY(directory.isValid());
        // A second link to the database tells whether its file was replaced.
        const QString witness = directory.filePath("witness"); std::error_code error;
        fs::create_hard_link(fs::PathFromString(database.toStdString()), fs::PathFromString(witness.toStdString()), error); QVERIFY(!error);
        QString selected = database;
        if (target == 1) {
            selected = directory.filePath("alias.dat");
            fs::create_hard_link(fs::PathFromString(database.toStdString()), fs::PathFromString(selected.toStdString()), error); QVERIFY(!error);
        }
        QVERIFY(qmltest::SelectSettings(m_app, "wallet")); QVERIFY(qmltest::WaitForVisible(m_app, "walletSettingsPage"));
        QVERIFY(qmltest::Find(m_app, "walletSettingsBackupPathField")->setProperty("text", selected));
        QVERIFY(qmltest::Click(m_app, "walletBackupRow"));
        QTRY_VERIFY(PageBackup(m_app)); auto* operation = PageBackup(m_app);
        QTRY_VERIFY(!operation->busy());
        QVERIFY(!operation->confirmationRequired()); QVERIFY(!operation->saved());
        QCOMPARE(operation->error(), QStringLiteral("Choose a file that is not a wallet."));
        QCOMPARE(qmltest::Find(m_app, "walletSettingsPage")->property("errorText").toString(), operation->error());
        QVERIFY(fs::equivalent(fs::PathFromString(database.toStdString()), fs::PathFromString(witness.toStdString()), error)); QVERIFY(!error);
    }

    // D225: an existing extensionless file is a file, not a directory. The
    // backup is proposed beside it with the .bak suffix, needs approval, and
    // leaves the selected file's bytes untouched.
    void existingExtensionlessBackupTargetIsAFile()
    {
        auto* wallet = qmltest::CreateWallet(m_app, "plain_source"); QVERIFY(wallet);
        const auto address = qmltest::NewAddress(m_app, wallet->name());
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto selected = directory.filePath("archive");
        const QByteArray sentinel{"extensionless sentinel - retain every byte"};
        { QFile file{selected}; QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write(sentinel), qint64{sentinel.size()}); }
        QVERIFY(qmltest::SelectSettings(m_app, "wallet")); QVERIFY(qmltest::WaitForVisible(m_app, "walletSettingsPage"));
        QVERIFY(qmltest::Find(m_app, "walletSettingsBackupPathField")->setProperty("text", selected));
        QVERIFY(qmltest::Click(m_app, "walletBackupRow"));
        QTRY_VERIFY(PageBackup(m_app)); auto* operation = PageBackup(m_app);
        QTRY_VERIFY(operation->confirmationRequired() || !operation->busy());
        QCOMPARE(operation->error(), QString{});
        QVERIFY(operation->confirmationRequired());
        QCOMPARE(operation->finalPath(), selected + ".bak");
        QVERIFY(qmltest::WaitForVisible(m_app, "walletBackupOverwritePopup"));
        QVERIFY(qmltest::Click(m_app, "walletBackupOverwriteApproveButton"));
        QTRY_COMPARE(operation->phase(), BackupFileOperation::Phase::Succeeded);
        QVERIFY(QFileInfo{selected}.isFile()); QCOMPARE(BackupBytes(selected), sentinel);
        QVERIFY(QFileInfo{selected + ".bak"}.size() > 0);
        UniValue params{UniValue::VARR}; params.push_back("plain_restored"); params.push_back(QString{selected + ".bak"}.toStdString());
        qmltest::Rpc(m_app, "restorewallet", params);
        UniValue info{UniValue::VARR}; info.push_back(address.toStdString());
        QVERIFY(qmltest::Rpc(m_app, "getaddressinfo", info, "plain_restored").find_value("ismine").get_bool());
    }

    // D231 (#1026): the Save chooser belongs to the wallet that opened it.
    // When that wallet vanishes or another is selected, the chooser closes
    // rather than offering a Save that would return silently, and nothing is
    // written. Backing up the current wallet then opens a fresh chooser.
    void backupChooserRetiresWithItsWallet_data()
    {
        QTest::addColumn<bool>("unload");
        QTest::addColumn<bool>("only");
        QTest::newRow("rpc-unload") << true << false;
        QTest::newRow("rpc-unload-only-wallet") << true << true;
        QTest::newRow("select-another-wallet") << false << false;
    }

    void backupChooserRetiresWithItsWallet()
    {
        QFETCH(bool, unload);
        QFETCH(bool, only);
        const bool previous = QCoreApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
        QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs, true);
        const auto restore = qScopeGuard([previous] { QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs, previous); });
        auto* controller = qmltest::Model<WalletController>(m_app, "walletController"); QVERIFY(controller);
        auto* other = only ? nullptr : qmltest::CreateWallet(m_app, "chooser_other"); QVERIFY(only || other);
        QPointer<WalletModel> owner{qmltest::CreateWallet(m_app, "chooser_owner")}; QVERIFY(owner);
        QTemporaryDir directory; QVERIFY(directory.isValid());
        QVERIFY(qmltest::SelectSettings(m_app, "wallet")); QVERIFY(qmltest::WaitForVisible(m_app, "walletSettingsPage"));
        QVERIFY(qmltest::Click(m_app, "walletBackupRow"));
        QTRY_VERIFY(VisibleBackupChooser());
        QPointer<QFileDialog> chooser{VisibleBackupChooser()};
        QPointer<QObject> wrapper{qmltest::Find(m_app, "walletSettingsBackupDialog")}; QVERIFY(wrapper); QVERIFY(wrapper->property("visible").toBool());
        chooser->setDirectory(directory.path());
        auto* field = chooser->findChild<QLineEdit*>("fileNameEdit"); QVERIFY(field);
        field->clear(); QTest::keyClicks(field, "stale.bak");
        if (unload) {
            UniValue params{UniValue::VARR}; params.push_back("chooser_owner");
            qmltest::Rpc(m_app, "unloadwallet", params);
            QTRY_VERIFY(owner.isNull());
        } else {
            QVERIFY(controller->selectWallet(other)); QTRY_COMPARE(controller->selectedWallet(), other);
        }
        QTRY_VERIFY(!VisibleBackupChooser());
        QVERIFY(!wrapper || !wrapper->property("visible").toBool());
        if (chooser) QVERIFY(!chooser->isVisible());
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QTest::qWait(100);
        QVERIFY(!QFileInfo::exists(directory.filePath("stale.bak")));
        QVERIFY(QDir{directory.path()}.entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden).isEmpty());
        if (only) {
            QTRY_VERIFY(!controller->selectedWallet() || !controller->selectedWallet()->available());
            return;
        }
        QVERIFY(!other->backups()->busy());
        if (controller->selectedWallet() != other) QVERIFY(controller->selectWallet(other));
        QTRY_COMPARE(controller->selectedWallet(), other);
        QVERIFY(qmltest::WaitForVisible(m_app, "walletSettingsPage"));
        QVERIFY(qmltest::Click(m_app, "walletBackupRow"));
        QTRY_VERIFY(VisibleBackupChooser());
        VisibleBackupChooser()->reject();
        QTRY_VERIFY(!VisibleBackupChooser());
    }

    //! Starts a backup of the selected wallet to an existing file from its
    //! settings page and waits for the "Replace backup?" question.
    QObject* askToReplaceBackup(const QString& existing)
    {
        if (!qmltest::SelectSettings(m_app, "wallet") || !qmltest::WaitForVisible(m_app, "walletSettingsPage")) return nullptr;
        auto* path = qmltest::Find(m_app, "walletSettingsBackupPathField");
        if (!path || !path->setProperty("text", existing) || !qmltest::Click(m_app, "walletBackupRow")) return nullptr;
        return qmltest::WaitForVisible(m_app, "walletBackupOverwritePopup");
    }

    // Selecting another wallet hides the page; the question ends without a
    // write, the wallet is not left busy, and returning shows no question.
    void hidingTheBackupPageEndsItsPrompt()
    {
        auto* controller = qmltest::Model<WalletController>(m_app, "walletController"); QVERIFY(controller);
        auto* other = qmltest::CreateWallet(m_app, "hide_other"); QVERIFY(other);
        auto* wallet = qmltest::CreateWallet(m_app, "hide_owner"); QVERIFY(wallet);
        QVERIFY(controller->selectWallet(wallet)); QTRY_COMPARE(controller->selectedWallet(), wallet);
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto existing = directory.filePath("existing.bak");
        { QFile file{existing}; QVERIFY(file.open(QIODevice::WriteOnly)); QVERIFY(file.write("sentinel") == 8); }
        QVERIFY(askToReplaceBackup(existing));
        QPointer<BackupFileOperation> operation{PageBackup(m_app)}; QVERIFY(operation);
        QVERIFY(wallet->backups()->busy());
        QVERIFY(controller->selectWallet(other)); QTRY_COMPARE(controller->selectedWallet(), other);
        QTRY_VERIFY(!wallet->backups()->busy());
        QTRY_COMPARE(operation->phase(), BackupFileOperation::Phase::Retired);
        QCOMPARE(BackupBytes(existing), QByteArray("sentinel"));
        QVERIFY(controller->selectWallet(wallet)); QTRY_COMPARE(controller->selectedWallet(), wallet);
        auto* popup = qmltest::Find(m_app, "walletBackupOverwritePopup");
        QVERIFY(popup);
        QTRY_VERIFY(!popup->property("visible").toBool());
        QVERIFY(!popup->property("opened").toBool()); QVERIFY(!popup->property("shown").toBool());
    }

    // Loading and unloading another wallet in the background keeps this
    // wallet's view, page and question; approving writes this wallet's backup.
    void backgroundWalletLoadKeepsTheBackupPrompt()
    {
        auto* wallet = qmltest::CreateWallet(m_app, "background_owner"); QVERIFY(wallet);
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto existing = directory.filePath("existing.bak");
        { QFile file{existing}; QVERIFY(file.open(QIODevice::WriteOnly)); QVERIFY(file.write("sentinel") == 8); }
        auto* popup = askToReplaceBackup(existing); QVERIFY(popup);
        QPointer<QObject> page{qmltest::Find(m_app, "walletSettingsPage")}; QVERIFY(page);
        QPointer<BackupFileOperation> operation{PageBackup(m_app)}; QVERIFY(operation);
        UniValue create{UniValue::VARR}; create.push_back("background_other");
        qmltest::Rpc(m_app, "createwallet", create);
        QVERIFY(qmltest::WaitForOpenWallet(m_app, "background_other"));
        UniValue unload{UniValue::VARR}; unload.push_back("background_other");
        qmltest::Rpc(m_app, "unloadwallet", unload);
        QTest::qWait(250);
        QVERIFY(page); QVERIFY(operation); QVERIFY(operation->confirmationRequired());
        QVERIFY(popup->property("visible").toBool());
        QVERIFY(qmltest::Click(m_app, "walletBackupOverwriteApproveButton"));
        QTRY_COMPARE(operation->phase(), BackupFileOperation::Phase::Succeeded);
        QVERIFY(BackupBytes(existing) != QByteArray("sentinel"));
        QTRY_VERIFY(!wallet->backups()->busy());
    }

    void backupRestoresAddressesAndRejectsInvalidFiles()
    {
        auto* wallet = qmltest::CreateWallet(m_app, "original");
        QVERIFY(wallet);
        const auto address = qmltest::NewAddress(m_app, wallet->name());
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto backup = directory.filePath(QString::fromUtf8("wallet backup ü.dat"));
        BackupPage backup_1{*wallet};
        QVERIFY(backup_1.begin(backup));
        QTRY_COMPARE(backup_1.operation()->phase(), BackupFileOperation::Phase::Succeeded);
        QVERIFY(QFileInfo{backup}.size() > 0);
        auto* controller = qmltest::Model<WalletController>(m_app, "walletController");
        QVERIFY(controller);
        controller->closeWallet("original");
        QTRY_VERIFY(!controller->isWalletLoaded());
        OutcomeSpy imported{controller->creation(), OutcomeSpy::Kind::Imported};
        controller->creation()->restorePath(backup);
        QTRY_COMPARE_WITH_TIMEOUT(imported.count(), 1, 30'000);
        QVERIFY(controller->isWalletLoaded());
        UniValue params{UniValue::VARR};
        params.push_back(address.toStdString());
        QVERIFY(qmltest::Rpc(m_app, "getaddressinfo", params, controller->selectedWallet()->name()).find_value("ismine").get_bool());
        const QPointer<WalletOperationModel> missing{controller->creation()->restorePath(directory.filePath("missing.dat"))}; QVERIFY(missing);
        QTRY_VERIFY(missing->terminal()); QVERIFY(!missing->errorText().isEmpty());
        QCOMPARE(imported.count(), 1);
        QVERIFY(controller->isWalletLoaded());
    }

    void earlierBackupCoverageAfterEncryption_data()
    {
        QTest::addColumn<bool>("spend");
        QTest::newRow("later-receive") << false;
        QTest::newRow("actual-change") << true;
    }

    void earlierBackupCoverageAfterEncryption()
    {
        QFETCH(bool, spend);
        auto* miner = qmltest::CreateWallet(m_app, "recovery_miner");
        QVERIFY(miner);
        QVERIFY(qmltest::FundWallet(m_app, *miner));
        QPointer<WalletModel> live{qmltest::CreateWallet(m_app, "recovery_live")};
        QVERIFY(live);
        const auto original_address = qmltest::NewAddress(m_app, live->name());
        const auto mining_address = qmltest::NewAddress(m_app, miner->name());
        auto mine = [&] {
            UniValue params{UniValue::VARR}; params.push_back(1); params.push_back(mining_address.toStdString());
            qmltest::Rpc(m_app, "generatetoaddress", params);
        };
        auto pay = [&](const QString& from, const QString& address, const char* amount) {
            UniValue params{UniValue::VOBJ};
            params.pushKV("address", address.toStdString());
            params.pushKV("amount", UniValue{UniValue::VNUM, amount});
            params.pushKV("fee_rate", 2);
            return qmltest::Rpc(m_app, "sendtoaddress", params, from).get_str();
        };
        auto balance = [&](const QString& name) {
            return ParseMoney(qmltest::Rpc(m_app, "getbalance", {}, name).getValStr());
        };
        auto owns = [&](const QString& name, const QString& address) {
            UniValue params{UniValue::VARR}; params.push_back(address.toStdString());
            return qmltest::Rpc(m_app, "getaddressinfo", params, name).find_value("ismine").get_bool();
        };
        pay(miner->name(), original_address, "1.0"); mine();
        QCOMPARE(balance(live->name()), std::optional<CAmount>{100'000'000});
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto old_backup = directory.filePath("before-encryption.bak");
        const auto new_backup = directory.filePath("after-encryption.bak");
        BackupPage backup_2{*live};
        QVERIFY(backup_2.begin(old_backup));
        QTRY_COMPARE(backup_2.operation()->phase(), BackupFileOperation::Phase::Succeeded);
        QFile old_file{old_backup}; QVERIFY(old_file.open(QIODevice::ReadOnly));
        const auto old_bytes = old_file.readAll(); old_file.close(); QVERIFY(!old_bytes.isEmpty());
        QVERIFY(live->encryptWallet("recovery password"));
        QTRY_COMPARE_WITH_TIMEOUT(live->securityState(), WalletModel::SecurityState::SecuritySucceeded, qmltest::kSecurityStateTimeoutMs);
        // The command receipt and Session's current-state observation have
        // independent delivery. Wait for that observation before using it.
        QTRY_VERIFY(live->isLocked());
        QCOMPARE(qmltest::Rpc(m_app, "getwalletinfo", {}, live->name()).find_value("unlocked_until").getInt<int64_t>(), 0);
        const auto later_address = qmltest::NewAddress(m_app, live->name());
        QString actual_change;
        if (spend) {
            qmltest::Rpc(m_app, "walletpassphrase", qmltest::Json("[\"recovery password\",60]"), live->name());
            const auto txid = pay(live->name(), mining_address, "0.25");
            qmltest::Rpc(m_app, "walletlock", {}, live->name());
            UniValue tx_params{UniValue::VARR}; tx_params.push_back(txid);
            const auto transaction = qmltest::Rpc(m_app, "gettransaction", tx_params, live->name());
            UniValue decode{UniValue::VARR}; decode.push_back(transaction.find_value("hex").get_str());
            const auto decoded = qmltest::Rpc(m_app, "decoderawtransaction", decode);
            for (const auto& output : decoded.find_value("vout").getValues()) {
                const auto address = QString::fromStdString(output.find_value("scriptPubKey").find_value("address").get_str());
                if (owns(live->name(), address)) {
                    QVERIFY(actual_change.isEmpty()); actual_change = address;
                }
            }
            QVERIFY(!actual_change.isEmpty());
        } else {
            pay(miner->name(), later_address, "0.5");
        }
        mine();
        const auto live_balance = balance(live->name());
        QVERIFY(live_balance && *live_balance > 0);
        if (!spend) QCOMPARE(*live_balance, CAmount{150'000'000});
        else QVERIFY(*live_balance < 100'000'000);
        BackupPage backup_3{*live};
        QVERIFY(backup_3.begin(new_backup));
        QTRY_COMPARE(backup_3.operation()->phase(), BackupFileOperation::Phase::Succeeded);
        auto restore = [&](const QString& name, const QString& path) {
            UniValue params{UniValue::VARR}; params.push_back(name.toStdString()); params.push_back(path.toStdString());
            return qmltest::Rpc(m_app, "restorewallet", params).find_value("name").get_str();
        };
        QCOMPARE(restore("recovery_old", old_backup), std::string{"recovery_old"});
        QCOMPARE(restore("recovery_fresh", new_backup), std::string{"recovery_fresh"});
        QVERIFY(owns("recovery_old", original_address));
        QVERIFY(!owns("recovery_old", later_address));
        QVERIFY(owns("recovery_fresh", later_address));
        if (spend) {
            QVERIFY(!owns("recovery_old", actual_change));
            QVERIFY(owns("recovery_fresh", actual_change));
        }
        QCOMPARE(balance("recovery_old"), std::optional<CAmount>{spend ? 0 : 100'000'000});
        QCOMPARE(balance("recovery_fresh"), live_balance);
        QCOMPARE(balance("recovery_live"), live_balance); // The live wallet has not lost these funds.
        QVERIFY(live);
        auto* controller = qmltest::Model<WalletController>(m_app, "walletController");
        QVERIFY(controller);
        controller->setSelectedWallet("recovery_live");
        QCOMPARE(qmltest::WaitForWallet(m_app, "recovery_live"), live.data());
        QVERIFY(old_file.open(QIODevice::ReadOnly)); QCOMPARE(old_file.readAll(), old_bytes);
        qInfo() << "Controlled recovery" << (spend ? "actual change" : "later receive")
                << "old backup" << *balance("recovery_old") << "fresh backup and preserved live wallet" << *live_balance;
    }

    void encryptedBackupRestoresLockedAndRetainsItsSigningKeys()
    {
        auto* wallet = qmltest::CreateWallet(m_app, "encrypted_source");
        QVERIFY(wallet);
        const auto address = qmltest::NewAddress(m_app, wallet->name(), "legacy");
        QVERIFY(wallet->encryptWallet("backup password"));
        QTRY_COMPARE_WITH_TIMEOUT(wallet->securityState(), WalletModel::SecurityState::SecuritySucceeded, qmltest::kSecurityStateTimeoutMs);
        QTRY_VERIFY(wallet->isLocked());
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto backup = directory.filePath("encrypted_restore.bak");
        BackupPage backup_4{*wallet};
        QVERIFY(backup_4.begin(backup));
        QTRY_COMPARE(backup_4.operation()->phase(), BackupFileOperation::Phase::Succeeded);
        auto* controller = qmltest::Model<WalletController>(m_app, "walletController");
        QVERIFY(controller);
        controller->closeWallet(wallet->name());
        QTRY_VERIFY(!controller->isWalletLoaded());
        QTRY_COMPARE(qmltest::Rpc(m_app, "listwallets").size(), 0U);
        controller->creation()->restorePath(backup);
        auto* restored = qmltest::WaitForOpenWallet(m_app, "encrypted_restore");
        QVERIFY(restored);
        QVERIFY(controller->selectWallet(restored));
        QTRY_VERIFY(restored->isEncrypted());
        QTRY_VERIFY(restored->isLocked());
        auto* signer = restored->signVerifyMessageModel();
        QVERIFY(AwaitMessageSigning(*signer, address, "restored wallet", "backup password"));
        QVERIFY(AwaitMessageVerification(*signer, address, "restored wallet", signer->signature()));
        QTRY_VERIFY(restored->isLocked());
    }

    void completionRetainsItsOriginalResultAndNeverSelectsIt()
    {
        auto* standby = qmltest::CreateWallet(m_app, "standby-b");
        QVERIFY(standby);
        auto* controller = qmltest::Model<WalletController>(m_app, "walletController");
        QVERIFY(controller);
        OutcomeSpy created{controller->creation(), OutcomeSpy::Kind::Created};
        const QPointer<WalletOperationModel> creation{controller->creation()->create(WalletCreationModel::Software, "result-a", {}, {})}; QVERIFY(creation);
        QVERIFY(controller->walletLoadInProgress());
        QVERIFY(controller->selectWallet(standby));
        QTRY_COMPARE_WITH_TIMEOUT(created.size(), 1, 30'000);
        QTRY_VERIFY(!controller->walletLoadInProgress());
        QPointer<WalletModel> original{qvariant_cast<WalletModel*>(created.front().front())};
        QVERIFY(original);
        QCOMPARE(controller->selectedWallet(), standby);
        QVERIFY(creation); QCOMPARE(creation->resultWallet(), original.data());
        QVERIFY(creation->canOpenResult());
        const auto identity = original->opening()->identity();
        controller->closeWallet("result-a");
        QTRY_VERIFY(!controller->isWalletOpen("result-a"));
        QVERIFY(creation); QVERIFY(!creation->canOpenResult());
        QCOMPARE(controller->selectedWallet(), standby);
        // Storage survives retirement. Reopening is a new operation with a
        // different facade; the retained old result cannot open it.
        QTRY_VERIFY(original.isNull());
        const auto prior_operation = OpenId(*controller);
        const QPointer<WalletOpenOperation> prior_open{controller->commandOpening()};
        const auto prior_state = OpenState(*controller);
        controller->setSelectedWallet("result-a");
        QVERIFY2(OpenId(*controller) > prior_operation, qPrintable(LoadError(*controller)));
        // The earlier open keeps its own outcome; the new one reports its own.
        if (prior_open) { QCOMPARE(prior_open->operationId(), prior_operation); QCOMPARE(prior_open->state(), prior_state); }
        QVERIFY(controller->commandOpening() != prior_open.data());
        auto* reopened = qmltest::WaitForOpenWallet(m_app, "result-a");
        QVERIFY2(reopened, qPrintable(LoadError(*controller) + " " + OpenAdoptionError(*controller)));
        QVERIFY(reopened->opening()->identity() != identity);
        QCOMPARE(controller->selectedWallet(), standby);
        QVERIFY(!controller->selectWallet(original.data()));
        QCOMPARE(OpenResult(*controller), reopened);
    }
};

QStringList WalletTestArguments(const QString& selector)
{
    if (selector == QLatin1String("creationWarningsAppearOnTheInitiatingSuccessfulResult") || selector == QLatin1String("restoreWarningsRemainOnTheInitiatingOwnedResult") || selector == QLatin1String("openedWalletWarningsRetainTheExactOriginalTarget")) return {QStringLiteral("-fallbackfee=0.1")};
    if (selector == QLatin1String("restoreSplitKeepsStorageWhenStartupSettingsAreDisabled")) return {QStringLiteral("-nosettings")};
    if (selector == QLatin1String("prunedValidBackupKeepsItsFilesAndReportsTheActualReason")) return {QStringLiteral("-prune=1"), QStringLiteral("-fastprune")};
    if (selector == QLatin1String("trailingSeparatorStartupWalletIsOneOpenRowAndCreationRejectsIt")) return {QStringLiteral("-wallet=trail2/")};
    if (selector == QLatin1String("unnamedWalletPresentAtStartupIsNamedAndOpensFromThePicker")) return {QStringLiteral("-nowallet")};
    return {};
}

QString WalletPreparedWallet(const QString& selector)
{
    if (selector == QLatin1String("trailingSeparatorStartupWalletIsOneOpenRowAndCreationRejectsIt")) return QStringLiteral("trail2");
    // "." copies the seed's wallet.dat to the wallet directory root, Core's
    // unnamed-wallet layout, before the GUI starts.
    if (selector == QLatin1String("unnamedWalletPresentAtStartupIsNamedAndOpensFromThePicker")) return QStringLiteral(".");
    return {};
}

std::unique_ptr<QObject> CreateWalletTests(BitcoinQmlApplication& app, WalletMode)
{
    return std::make_unique<WalletIntegrationTests>(app);
}

#include <test_wallet.moc>
