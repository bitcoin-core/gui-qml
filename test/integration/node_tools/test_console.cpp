// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <bitcoin-build-config.h> // IWYU pragma: keep
#include <test/integration/runner/test_cases.h>
#include <test/integration/fixture/test_application.h>
#ifdef ENABLE_WALLET
#include <test/integration/fixture/test_wallet.h>
#endif
#include <test/integration/fixture/test_util.h>
#include <qml/models/rpcconsolemodel.h>
#include <qml/models/monospaceoutputmodel.h>

#include <QSignalSpy>
#include <QRegularExpression>
#include <QDir>
#include <QFile>
#include <QScopeGuard>

#include <string>
#include <utility>

class ConsoleIntegrationTests : public QObject
{
    Q_OBJECT
    BitcoinQmlApplication& m_app;
    WalletMode m_mode;

    bool submitCommand(const QString& command)
    {
        if (!qmltest::SetText(m_app, "consoleInput", command)) return false;
        if (!QMetaObject::invokeMethod(qmltest::Find(m_app, "consoleInput"), "forceActiveFocus")) return false;
        QTest::keyClick(m_app.window(), Qt::Key_Return, Qt::NoModifier, 1);
        return true;
    }

    MonospaceOutputModel* projection() const
    {
        auto* list = qmltest::Find(m_app, "consoleOutputArea_list");
        return list ? qobject_cast<MonospaceOutputModel*>(list->property("model").value<QObject*>()) : nullptr;
    }

    bool waitForOutput()
    {
        auto* model = qmltest::Model<RpcConsoleModel>(m_app, "rpcConsoleModel");
        QPointer<MonospaceOutputModel> view{projection()};
        return model && view && QTest::qWaitFor([&] {
            return !model->executing() && view && !view->busy();
        }) && view && view->error().isEmpty() && !view->truncated() && view->sourceModel() == model->outputModel();
    }

    // Physical rows are bounded lines/chunks. A timestamp starts each original
    // logical message. The first wallet query additionally has one exact
    // context banner; Clear starts a fresh presentation context.
    // No events are processed during the individual role reads: no model
    // mutation can interleave this synchronous, bounded traversal.
    bool hasLogicalExchange(int reply_category, const QString& forbidden = {}, const QString& expected_context = {}) const
    {
        const auto failure = [](int line) { qInfo() << "R38 exchange projection not settled at oracle line" << line; return false; };
        auto* model = qmltest::Model<RpcConsoleModel>(m_app, "rpcConsoleModel");
        QPointer<MonospaceOutputModel> view{projection()};
        if (!model || model->executing() || !view || view->busy() || !view->error().isEmpty() || view->truncated()) return failure(__LINE__);
        QPointer<RpcOutputListModel> source{model->outputModel()};
        const int rows = source->rowCount();
        if (view->sourceModel() != source || rows <= 0 || rows > RpcOutputListModel::kMaxRows) return failure(__LINE__);
        const auto receipt = model->commandReceipt();
        if (!receipt) return failure(__LINE__);
        const auto result = receipt->result();
        if (!result || !result->value || result->value->command_id != model->commandId()) return failure(__LINE__);
        const auto& context = result->value->context;
        const auto banner = context.target == RpcCommandContext::Target::Node
            ? QStringLiteral("Executing command without any wallet")
            : context.wallet_name.isEmpty() ? QStringLiteral("Executing command using the default wallet")
            : QStringLiteral("Executing command using \"%1\" wallet").arg(context.wallet_name);
        const bool has_banner = source->data(source->index(0, 0), RpcOutputListModel::ContentRole).toString() == banner.toHtmlEscaped();
        if (!expected_context.isEmpty() && (!has_banner || expected_context != banner)) return failure(__LINE__);
        const int context_messages = has_banner ? 1 : 0;
        const int expected_starts = 2 + context_messages;
        QString first_timestamp;
        int starts{0};
        for (int row{0}; row < rows; ++row) {
            const auto original = [&] {
                return source && view && model->outputModel() == source && source->rowCount() == rows && view->sourceModel() == source;
            };
            const auto read = [&](int role) {
                if (!original()) return QVariant{};
                const auto value = source->data(source->index(row, 0), role);
                return original() ? value : QVariant{};
            };
            if (read(RpcOutputListModel::CommandIdRole).toULongLong() != result->value->command_id ||
                read(RpcOutputListModel::GenerationRole).toULongLong() != result->value->generation ||
                read(RpcOutputListModel::WalletContextRole).toBool() != (context.target == RpcCommandContext::Target::Wallet) ||
                read(RpcOutputListModel::WalletNameRole).toString() != context.wallet_name) return failure(__LINE__);
            const auto timestamp = read(RpcOutputListModel::TimestampRole);
            const auto content = read(RpcOutputListModel::ContentRole);
            const auto category = read(RpcOutputListModel::CategoryRole);
            const auto continuation = read(RpcOutputListModel::ContinuationRole);
            if (!timestamp.isValid() || !content.isValid() || !category.isValid() || !continuation.isValid()) return failure(__LINE__);
            if (!forbidden.isEmpty() && content.toString().contains(forbidden)) return failure(__LINE__);
            if (!timestamp.toString().isEmpty()) {
                if (continuation.toBool() || !QRegularExpression{QStringLiteral("^[0-9]{2}:[0-9]{2}:[0-9]{2}$")}.match(timestamp.toString()).hasMatch()) return failure(__LINE__);
                ++starts;
                if (starts == 1) first_timestamp = timestamp.toString();
            }
            if (starts < 1 || starts > expected_starts || category.toInt() !=
                (starts <= 1 + context_messages ? RpcConsoleModel::CMD_REQUEST : reply_category)) return failure(__LINE__);
            // The known short banner occupies precisely one source row.
            if (context_messages && starts == 1 && row != 0) return failure(__LINE__);
        }
        if (context_messages) {
            if (!view || view->rowCount() <= 0) return failure(__LINE__);
            const auto first = view->index(0, 0);
            if (view->data(first, MonospaceOutputModel::PlainContentRole).toString() != banner ||
                view->data(first, MonospaceOutputModel::LeftRole).toString() != first_timestamp ||
                view->data(first, MonospaceOutputModel::CategoryRole).toInt() != RpcConsoleModel::CMD_REQUEST) return failure(__LINE__);
        }
        return starts == expected_starts && source && source->rowCount() == rows && view && view->sourceModel() == source;
    }

    bool searchMatches(const QString& query, int expected)
    {
        if (!qmltest::SetText(m_app, "rpcConsoleSearchField", query)) return false;
        QPointer<MonospaceOutputModel> view{projection()};
        auto* console = qmltest::Find(m_app, "rpcConsole");
        return view && console && QTest::qWaitFor([&] {
            return view && console->property("searchText").toString() == query &&
                   view->searchText() == query && !view->busy() && !view->searchBusy() &&
                   view->searchResultCount() == expected && console->property("searchResultCount").toInt() == expected;
        }) && view && view->error().isEmpty() && !view->truncated() && !view->searchLimited() &&
            view->searchResultCount() == expected && console->property("searchResultCount").toInt() == expected;
    }

public:
    explicit ConsoleIntegrationTests(BitcoinQmlApplication& app, WalletMode mode) : m_app{app}, m_mode{mode} {}

private Q_SLOTS:
    void init()
    {
        QVERIFY(qmltest::SelectSettings(m_app, "rpc-console"));
        QVERIFY(qmltest::WaitForVisible(m_app, "rpcConsole"));
    }

    void executesCommandsAndRecoversFromErrors_data()
    {
        QTest::addColumn<QString>("command");
        QTest::addColumn<QString>("expected_reply");
        QTest::newRow("block-count") << "getblockcount" << "0";
        QTest::newRow("help") << "help getblockcount" << "Returns the height";
        QTest::newRow("unknown-command") << "thiscommanddoesnotexist" << "Method not found";
    }

    void defaultWalletRpcContextIsExplicitWithMultipleWallets()
    {
#ifndef ENABLE_WALLET
        QSKIP("Wallet support is not compiled in");
#else
        if (m_mode != WalletMode::Enabled) QSKIP("This integration case requires wallet support");

        auto* model = qmltest::Model<RpcConsoleModel>(m_app, "rpcConsoleModel");
        QVERIFY(model);
        QVERIFY(qmltest::Rpc(m_app, "listwallets").empty());

        // Core's supported creation RPC requires a name. Reproduce its own
        // unnamed-wallet startup fixture: create, unload, and move the database
        // to the default wallet location before loading it through loadwallet.
        const QString seed_name{QStringLiteral("rpc-default-wallet-seed")};
        QVERIFY(qmltest::CreateWallet(m_app, seed_name));
        UniValue unload_params{UniValue::VARR};
        unload_params.push_back(seed_name.toStdString());
        QVERIFY(qmltest::Rpc(m_app, "unloadwallet", unload_params).isObject());
        QVERIFY(QTest::qWaitFor([&] { return qmltest::Rpc(m_app, "listwallets").empty(); }, 10'000));

        const auto wallet_dir = qmltest::Call(m_app, [](interfaces::Node* node) {
            return node->walletLoader().getWalletDir();
        });
        const QDir wallet_root{QString::fromStdString(wallet_dir)};
        QVERIFY2(wallet_root.exists(), qPrintable(wallet_root.absolutePath()));
        const QString seed_directory = wallet_root.filePath(seed_name);
        const QString seed_file = QDir{seed_directory}.filePath(QStringLiteral("wallet.dat"));
        const QString unnamed_file = wallet_root.filePath(QStringLiteral("wallet.dat"));
        QVERIFY2(QFile::exists(seed_file), qPrintable(seed_file));
        QVERIFY(!QFile::exists(unnamed_file));
        QVERIFY2(QFile::rename(seed_file, unnamed_file), qPrintable(QStringLiteral("Cannot move wallet database to %1").arg(unnamed_file)));
        QVERIFY(QDir{seed_directory}.removeRecursively());

        UniValue load_params{UniValue::VARR};
        load_params.push_back("");
        const auto loaded = qmltest::Rpc(m_app, "loadwallet", load_params);
        QCOMPARE(loaded.find_value("name").get_str(), std::string{});
        QVERIFY(QTest::qWaitFor([&] { return qmltest::Rpc(m_app, "listwallets").size() == 1; }, 10'000));
        auto* controller = qmltest::Model<WalletQmlController>(m_app, "walletController");
        QVERIFY(controller);
        QVERIFY(QTest::qWaitFor([&] { return controller->walletForName(QString{}) != nullptr; }, 10'000));

        QVERIFY(qmltest::CreateWallet(m_app, QStringLiteral("rpc-secondary-wallet")));
        const auto wallets = qmltest::Rpc(m_app, "listwallets");
        QCOMPARE(wallets.size(), 2U);
        QVERIFY(wallets[0].get_str().empty() || wallets[1].get_str().empty());
        QVERIFY(QTest::qWaitFor([&] { return controller->walletForName(QStringLiteral("rpc-secondary-wallet")) != nullptr; }, 10'000));

        auto* unnamed_wallet = controller->walletForName(QString{});
        QVERIFY(unnamed_wallet);
        QVERIFY(unnamed_wallet->name().isEmpty());
        QVERIFY(controller->selectWallet(unnamed_wallet));
        QCOMPARE(controller->selectedWallet(), unnamed_wallet);

        // Use the production QML submit path here: its selected-facade binding
        // must keep wallet context true even though the wallet name is empty.
        auto* targeted_console = qmltest::Find(m_app, "rpcConsole"); QVERIFY(targeted_console);
        QTRY_VERIFY(targeted_console->property("walletContext").toBool());
        QTRY_VERIFY(targeted_console->property("targetAvailable").toBool());
        QCOMPARE(targeted_console->property("walletName").toString(), unnamed_wallet->rpcName());
        QVERIFY(submitCommand(QStringLiteral("getwalletinfo")));
        QVERIFY(waitForOutput());
        QTRY_VERIFY(hasLogicalExchange(RpcConsoleModel::CMD_REPLY, {}, QStringLiteral("Executing command using the default wallet")));
        QVERIFY(searchMatches(QStringLiteral("\"walletname\": \"\""), 1));

        // The node endpoint deliberately has no implicit wallet when two are
        // loaded. This distinguishes explicit /wallet/ routing from a default
        // wallet fallback and proves the empty name reached Core as a wallet.
        model->clear();
        QCOMPARE(model->outputModel()->rowCount(), 0);
        QVERIFY(waitForOutput());
        QVERIFY(model->submitNodeCommand(QStringLiteral("getwalletinfo")));
        QVERIFY(waitForOutput());
        QTRY_VERIFY(hasLogicalExchange(RpcConsoleModel::CMD_ERROR, {}, QStringLiteral("Executing command without any wallet")));
        QVERIFY(searchMatches(QStringLiteral("Multiple wallets are loaded"), 1));
#endif
    }

    // #1131: a wallet loaded outside the picker (another RPC client, or
    // createwallet typed in this console) or unloaded again does not become
    // the console's target; each later command runs in, and its rows record,
    // the wallet that was selected when it was typed.
    // #1131: a command recalled from history never silently runs in a wallet
    // other than the one it last ran in. The first Enter names both wallets
    // and runs nothing; a second Enter runs it in the console's current wallet.
    void recalledCommandAsksBeforeRunningInAnotherWallet()
    {
#ifndef ENABLE_WALLET
        QSKIP("Wallet support is not compiled in");
#else
        if (m_mode != WalletMode::Enabled) QSKIP("This integration case requires wallet support");
        auto* model = qmltest::Model<RpcConsoleModel>(m_app, "rpcConsoleModel"); QVERIFY(model);
        auto* console = qmltest::Find(m_app, "rpcConsole"); QVERIFY(console);
        const QString alpha{QStringLiteral("recall-alpha")}, beta{QStringLiteral("recall-beta")};
        const auto unload = qScopeGuard([&] { for (const auto& name : {alpha, beta}) { UniValue params{UniValue::VARR}; params.push_back(name.toStdString()); try { qmltest::Rpc(m_app, "unloadwallet", params); } catch (const std::exception&) {} } });
        QVERIFY(qmltest::CreateWallet(m_app, alpha));
        QTRY_COMPARE(console->property("walletName").toString(), alpha); QTRY_VERIFY(console->property("targetAvailable").toBool());
        model->clear(); QVERIFY(waitForOutput());
        auto* source = model->outputModel();
        const auto replySince = [&](int earlier) {
            QString reply;
            for (int row = earlier; row < source->rowCount(); ++row) {
                const auto index = source->index(row, 0);
                if (source->data(index, RpcOutputListModel::CategoryRole).toInt() == RpcConsoleModel::CMD_REPLY) reply += source->data(index, RpcOutputListModel::ContentRole).toString();
            }
            return reply;
        };
        int earlier = source->rowCount();
        QVERIFY(submitCommand(QStringLiteral("getwalletinfo"))); QVERIFY(waitForOutput());
        QTRY_VERIFY(replySince(earlier).contains(alpha));
        // The user switches the app to another wallet; the console follows it.
        QVERIFY(qmltest::CreateWallet(m_app, beta));
        QTRY_COMPARE(console->property("walletName").toString(), beta); QTRY_VERIFY(console->property("targetAvailable").toBool());
        auto* input = qmltest::Find(m_app, "consoleInput"); QVERIFY(input);
        QVERIFY(QMetaObject::invokeMethod(input, "forceActiveFocus"));
        QTest::keyClick(m_app.window(), Qt::Key_Up, Qt::NoModifier, 1);
        QTRY_COMPARE(input->property("text").toString(), QStringLiteral("getwalletinfo"));
        earlier = source->rowCount();
        QTest::keyClick(m_app.window(), Qt::Key_Return, Qt::NoModifier, 1);
        QTRY_VERIFY(model->completionError().contains("\"" + alpha + "\"") && model->completionError().contains("\"" + beta + "\""));
        QVERIFY(!model->executing());
        QVERIFY(!QTest::qWaitFor([&] { return source->rowCount() > earlier; }, 500));
        QCOMPARE(input->property("text").toString(), QStringLiteral("getwalletinfo"));
        // Pressing Enter again runs it in the current wallet.
        QTest::keyClick(m_app.window(), Qt::Key_Return, Qt::NoModifier, 1);
        QVERIFY(waitForOutput());
        QTRY_VERIFY(replySince(earlier).contains(beta));
        QVERIFY(!replySince(earlier).contains(alpha));
#endif
    }

    void externallyLoadedWalletDoesNotRetargetTheConsole()
    {
#ifndef ENABLE_WALLET
        QSKIP("Wallet support is not compiled in");
#else
        if (m_mode != WalletMode::Enabled) QSKIP("This integration case requires wallet support");
        auto* model = qmltest::Model<RpcConsoleModel>(m_app, "rpcConsoleModel"); QVERIFY(model);
        auto* controller = qmltest::Model<WalletQmlController>(m_app, "walletController"); QVERIFY(controller);
        auto* console = qmltest::Find(m_app, "rpcConsole"); QVERIFY(console);
        const QString target{QStringLiteral("console-target")}, outside{QStringLiteral("console-outside")}, typed{QStringLiteral("console-typed")};
        const auto unload = qScopeGuard([&] { for (const auto& name : {target, outside, typed}) { UniValue params{UniValue::VARR}; params.push_back(name.toStdString()); try { qmltest::Rpc(m_app, "unloadwallet", params); } catch (const std::exception&) {} } });
        auto* selected = qmltest::CreateWallet(m_app, target); QVERIFY(selected);
        QTRY_COMPARE(console->property("walletName").toString(), target); QTRY_VERIFY(console->property("targetAvailable").toBool());
        model->clear(); QVERIFY(waitForOutput());
        const auto banner = QStringLiteral("Executing command using \"%1\" wallet").arg(target);
        auto* source = model->outputModel();
        // Runs getwalletinfo from the console input and checks the new rows.
        const auto run = [&](bool first) -> QString {
            const int earlier = source->rowCount();
            if (!submitCommand(QStringLiteral("getwalletinfo")) || !waitForOutput() || !QTest::qWaitFor([&] { return source->rowCount() > earlier + 1; })) return QStringLiteral("getwalletinfo did not complete");
            QString reply;
            for (int row = earlier; row < source->rowCount(); ++row) {
                const auto index = source->index(row, 0);
                if (!source->data(index, RpcOutputListModel::WalletContextRole).toBool() || source->data(index, RpcOutputListModel::WalletNameRole).toString() != target) return QStringLiteral("row %1 is not tagged with %2").arg(row).arg(target);
                if (source->data(index, RpcOutputListModel::CategoryRole).toInt() == RpcConsoleModel::CMD_REPLY) reply += source->data(index, RpcOutputListModel::ContentRole).toString();
            }
            // Only a changed target is announced, so later exchanges add no banner.
            if ((source->data(source->index(earlier, 0), RpcOutputListModel::ContentRole).toString() == banner.toHtmlEscaped()) != first) return QStringLiteral("unexpected context banner at row %1").arg(earlier);
            if (!reply.contains(target) || reply.contains(outside) || reply.contains(typed)) return QStringLiteral("reply names another wallet: ") + reply;
            return QString{};
        };
        const auto still_selected = [&] { return controller->selectedWallet() == selected && console->property("walletName").toString() == target; };
        QCOMPARE(run(true), QString{});
        // Another client creates and loads a wallet while the console is open.
        UniValue params{UniValue::VOBJ}; params.pushKV("wallet_name", outside.toStdString()); qmltest::Rpc(m_app, "createwallet", params);
        QVERIFY(qmltest::WaitForOpenWallet(m_app, outside)); QTest::qWait(250); QVERIFY(still_selected());
        QCOMPARE(run(false), QString{});
        // createwallet typed in this console loads a wallet the same way.
        const int before = source->rowCount();
        QVERIFY(submitCommand(QStringLiteral("createwallet %1").arg(typed))); QVERIFY(waitForOutput()); QTRY_VERIFY(source->rowCount() > before);
        QVERIFY(qmltest::WaitForOpenWallet(m_app, typed)); QTest::qWait(250); QVERIFY(still_selected());
        QCOMPARE(run(false), QString{});
        // Unloading a wallet from outside leaves the selection where it was.
        UniValue unload_params{UniValue::VARR}; unload_params.push_back(outside.toStdString()); qmltest::Rpc(m_app, "unloadwallet", unload_params);
        QTRY_VERIFY(!controller->walletForName(outside)); QTest::qWait(250); QVERIFY(still_selected());
        QCOMPARE(run(false), QString{});
#endif
    }

    void executesCommandsAndRecoversFromErrors()
    {
        QFETCH(QString, command);
        QFETCH(QString, expected_reply);
        auto* model = qmltest::Model<RpcConsoleModel>(m_app, "rpcConsoleModel");
        QVERIFY(model);
        auto* console = qmltest::Find(m_app, "rpcConsole");
        QVERIFY(console);

        QVERIFY(submitCommand(command));
        QVERIFY(waitForOutput());
        const int reply_category = command == "thiscommanddoesnotexist" ? RpcConsoleModel::CMD_ERROR : RpcConsoleModel::CMD_REPLY;
        QVERIFY(hasLogicalExchange(reply_category));
        // Search the complete Native projection, including uninstantiated rows.
        QVERIFY(searchMatches(expected_reply, 1));

        // Searching narrows the matches without removing console history.
        QVERIFY(searchMatches(command, 1));
        QVERIFY(hasLogicalExchange(reply_category));
        QVERIFY(searchMatches("unique-native-absent-output-sentinel-92", 0));
        QVERIFY(searchMatches("", 0));
        QVERIFY(QMetaObject::invokeMethod(console, "clearInputOrOutput"));
        QTRY_COMPARE(console->property("outputCount").toInt(), 0);
        QVERIFY(waitForOutput());
        QTRY_COMPARE(projection()->rowCount(), 0);
        QVERIFY(!qmltest::Find(m_app, "rpcConsoleHelpFooter")->property("text").toString().isEmpty());

        // A successful command still works after clearing either kind of reply.
        QVERIFY(submitCommand("getblockcount"));
        QVERIFY(waitForOutput());
        QVERIFY(hasLogicalExchange(RpcConsoleModel::CMD_REPLY));
        QVERIFY(searchMatches("getblockcount", 1));
        QVERIFY(searchMatches("0", 1));
        QVERIFY(searchMatches("", 0));
    }

    void secretRequestsStayRedactedAfterClear_data()
    {
        QTest::addColumn<QString>("method"); QTest::addColumn<QString>("arguments"); QTest::addColumn<bool>("sensitive");
        QTest::newRow("createwallet-passphrase") << "createwallet" << "privacy-rpc false false unique-native-secret-92" << true;
        QTest::newRow("migratewallet-passphrase") << "migratewallet" << "privacy-rpc unique-native-secret-92" << true;
        QTest::newRow("addhdkey-private-key") << "addhdkey" << "unique-native-secret-92" << true;
        QTest::newRow("walletpassphrase-control") << "walletpassphrase" << "unique-native-secret-92 10" << true;
        QTest::newRow("ordinary-control") << "help" << "getblockcount" << false;
    }

    void secretRequestsStayRedactedAfterClear()
    {
        QFETCH(QString, method); QFETCH(QString, arguments); QFETCH(bool, sensitive);
        const bool wallet_enabled = m_mode == WalletMode::Enabled;
#ifdef ENABLE_WALLET
        WalletQmlModel* wallet{nullptr};
        UniValue loaded_before;
        if (wallet_enabled) {
            wallet = qmltest::CreateWallet(m_app, "privacy-rpc"); QVERIFY(wallet);
            QVERIFY(wallet->encryptWallet("owned test fixture password"));
            QTRY_COMPARE_WITH_TIMEOUT(wallet->securityState(), WalletQmlModel::SecurityState::SecuritySucceeded, qmltest::kSecurityStateTimeoutMs);
            // Success precedes the refreshed lock observation; wait for it.
            QTRY_VERIFY(wallet->isLocked());
            loaded_before = qmltest::Rpc(m_app, "listwallets");
            QVERIFY(qmltest::SelectSettings(m_app, "rpc-console"));
            QVERIFY(qmltest::WaitForVisible(m_app, "rpcConsole"));
        }
#endif
        auto* model = qmltest::Model<RpcConsoleModel>(m_app, "rpcConsoleModel"); QVERIFY(model);
        auto* targeted_console = qmltest::Find(m_app, "rpcConsole"); QVERIFY(targeted_console);
        QTRY_COMPARE(targeted_console->property("walletContext").toBool(), wallet_enabled);
        QTRY_VERIFY(targeted_console->property("targetAvailable").toBool());
#ifdef ENABLE_WALLET
        if (wallet_enabled) QCOMPARE(targeted_console->property("walletName").toString(), wallet->rpcName());
#endif
        const QString command = arguments.isEmpty() ? method : method + " " + arguments;
        QVERIFY(submitCommand(command));
        QVERIFY(waitForOutput());
        auto* output = model->outputModel();
        const QString expected_request = sensitive ? method + QString::fromUtf8("(…)") : command;
        QVERIFY(searchMatches(expected_request, 1));
        QVERIFY(searchMatches(sensitive ? QStringLiteral("(code ") : QStringLiteral("Returns the height"), 1));
        const bool request_safe = QTest::qWaitFor([&] { return hasLogicalExchange(sensitive ? RpcConsoleModel::CMD_ERROR : RpcConsoleModel::CMD_REPLY,
                                                     "unique-native-secret-92",
                                                     wallet_enabled ? QStringLiteral("Executing command using \"privacy-rpc\" wallet") : QString{}); });
        QVERIFY(request_safe);
        QVERIFY(searchMatches("unique-native-secret-92", 0));
        QVERIFY(searchMatches("", 0));
        auto* console = qmltest::Find(m_app, "rpcConsole"); QVERIFY(console);
        QVERIFY(QMetaObject::invokeMethod(console, "clearInputOrOutput")); QTRY_COMPARE(output->rowCount(), 0);
        QVERIFY(waitForOutput());
        QTRY_COMPARE(projection()->rowCount(), 0);
        QVERIFY(searchMatches("unique-native-secret-92", 0));
        QVERIFY(searchMatches("", 0));
        auto* input = qmltest::Find(m_app, "consoleInput"); QVERIFY(input);
        QVERIFY(QMetaObject::invokeMethod(input, "forceActiveFocus"));
        QTest::keyClick(m_app.window(), Qt::Key_Up, Qt::NoModifier, 1);
        QTRY_VERIFY(!input->property("text").toString().isEmpty());
        const QString recalled = input->property("text").toString();
        const bool recall_safe = !recalled.contains("unique-native-secret-92");
        auto* completion = qmltest::Find(m_app, "consoleAutocompletePopup");
        if (completion && completion->property("visible").toBool()) {
            QTest::keyClick(m_app.window(), Qt::Key_Escape, Qt::NoModifier, 1);
            QTRY_VERIFY(!completion->property("visible").toBool());
            QCOMPARE(input->property("text").toString(), recalled);
        }
        QTest::keyClick(m_app.window(), Qt::Key_Down, Qt::NoModifier, 1); QTRY_COMPARE(input->property("text").toString(), QString{});
#ifdef ENABLE_WALLET
        if (wallet_enabled) {
            QTRY_VERIFY(wallet->isLocked());
            QCOMPARE(qmltest::Rpc(m_app, "listwallets").write(), loaded_before.write());
        }
#endif
        qInfo() << "Native RPC privacy oracle" << method << "wallet enabled" << wallet_enabled
                << "request safe" << request_safe << "recall after Clear safe" << recall_safe;
        QVERIFY(request_safe); QVERIFY(recall_safe);
        QCOMPARE(recalled, sensitive ? method + QString::fromUtf8("(…)") : command);
    }

    void enterUsesTypedInputUnlessKeyboardSelected_data()
    {
        QTest::addColumn<bool>("selected");
        QTest::newRow("hover-only") << false;
        QTest::newRow("keyboard-selection") << true;
    }

    void enterUsesTypedInputUnlessKeyboardSelected()
    {
        QFETCH(bool, selected);
        auto* model = qmltest::Model<RpcConsoleModel>(m_app, "rpcConsoleModel"); QVERIFY(model);
        auto* input = qmltest::Find(m_app, "consoleInput"); QVERIFY(input);
        auto* console = qmltest::Find(m_app, "rpcConsole"); QVERIFY(console);
        model->clear();
        QVERIFY(qmltest::SetText(m_app, "consoleInput", QStringLiteral("getbloc")));
        QVERIFY(QMetaObject::invokeMethod(input, "forceActiveFocus"));
        QTRY_VERIFY(!model->completionBusy());
        QVERIFY(model->completionSuggestions().contains(QStringLiteral("getblockcount")));
        const auto hover_index = model->completionSuggestions().indexOf(QStringLiteral("getblockcount"));
        const auto hover_name = QStringLiteral("consoleAutocomplete_%1").arg(hover_index);
        QVERIFY(qmltest::WaitForVisible(m_app, qPrintable(hover_name)));
        auto* suggestion = qobject_cast<QQuickItem*>(qmltest::Find(m_app, qPrintable(hover_name))); QVERIFY(suggestion);
        QTest::mouseMove(m_app.window(), suggestion->mapToScene(QPointF{suggestion->width() / 2, suggestion->height() / 2}).toPoint());
        QTRY_VERIFY(suggestion->property("hovered").toBool());
        QCOMPARE(input->property("text").toString(), QStringLiteral("getbloc"));
        QString expected{QStringLiteral("getbloc")};
        if (selected) {
            QTest::keyClick(m_app.window(), Qt::Key_Down, Qt::NoModifier, 1);
            expected = model->completionSuggestions().at(0);
        }
        const auto before = model->commandId();
        QTest::keyClick(m_app.window(), Qt::Key_Return, Qt::NoModifier, 1);
        QVERIFY(waitForOutput()); QCOMPARE(model->commandId(), before + 1);
        bool saw_request{false}; auto* output = model->outputModel();
        for (int row = 0; row < output->rowCount(); ++row) {
            if (output->data(output->index(row, 0), RpcOutputListModel::CategoryRole).toInt() == RpcConsoleModel::CMD_REQUEST &&
                output->data(output->index(row, 0), RpcOutputListModel::ContentRole).toString() == expected) saw_request = true;
        }
        QVERIFY(saw_request); QCOMPARE(input->property("text").toString(), QString{});
        QTest::keyClick(m_app.window(), Qt::Key_Return, Qt::NoModifier, 1);
        QCOMPARE(model->commandId(), before + 1); // An empty second Enter cannot execute again.
    }

    void busyEnterKeepsDraftAndClearKeepsActualOutcome()
    {
        auto* model = qmltest::Model<RpcConsoleModel>(m_app, "rpcConsoleModel"); QVERIFY(model);
        auto* input = qmltest::Find(m_app, "consoleInput"); QVERIFY(input);
        const auto tip = qmltest::Call(m_app, &interfaces::Node::executeRpc, "getbestblockhash", UniValue{UniValue::VARR}, "").get_str();
        model->clear();
        QVERIFY(model->submitNodeCommand(QString::fromStdString("waitfornewblock 0 " + tip)));
        const auto receipt = model->commandReceipt(); const auto id = model->commandId();
        bool completed{false};
        const auto finish = [&] {
            if (completed) return;
            completed = true;
            UniValue params{UniValue::VARR}; params.push_back(1); params.push_back("raw(51)");
            qmltest::Call(m_app, &interfaces::Node::executeRpc, "generatetodescriptor", params, "");
        };
        const auto cleanup = qScopeGuard([&] { finish(); QVERIFY(QTest::qWaitFor([&] { return !model->executing(); }, 5000)); });
        QTRY_VERIFY(qmltest::RpcCommandRunning(m_app, "waitfornewblock"));
        QVERIFY(qmltest::SetText(m_app, "consoleInput", QStringLiteral("getblock")));
        QVERIFY(QMetaObject::invokeMethod(input, "forceActiveFocus"));
        QTRY_VERIFY(!model->completionBusy()); QVERIFY(model->completionSuggestions().size() > 1);
        QTest::keyClick(m_app.window(), Qt::Key_Down, Qt::NoModifier, 1);
        QTest::keyClick(m_app.window(), Qt::Key_Down, Qt::NoModifier, 1);
        QTest::keyClick(m_app.window(), Qt::Key_Return, Qt::NoModifier, 1);
        QCOMPARE(input->property("text").toString(), QStringLiteral("getblock"));
        QCOMPARE(model->commandId(), id); QVERIFY(model->executing());
        auto* autocomplete = qmltest::Find(m_app, "consoleAutocompletePopup"); QVERIFY(autocomplete);
        QTRY_VERIFY(autocomplete->property("opened").toBool());
        const auto generation = model->outputGeneration();
        m_app.window()->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(m_app.window()));
        QTest::keyClick(m_app.window(), Qt::Key_L,
#ifdef Q_OS_MACOS
                        Qt::MetaModifier,
#else
                        Qt::ControlModifier,
#endif
                        1);
        QTRY_COMPARE(model->outputGeneration(), generation + 1); QCOMPARE(model->outputModel()->rowCount(), 0);
        QVERIFY(model->executing()); finish(); QVERIFY(waitForOutput());
        QCOMPARE(model->outputModel()->rowCount(), 0); QCOMPARE(input->property("text").toString(), QStringLiteral("getblock"));
        const auto result = receipt->result(); QVERIFY(result); QVERIFY(result->value); QVERIFY(result->value->attempted);
        QCOMPARE(result->value->command_id, id); QCOMPARE(result->value->generation, generation);
        QCOMPARE(result->value->outcome, RpcCommandResult::Outcome::Succeeded);
    }

    void consoleLayoutAndOutputStayAlignedAcrossFontSizes()
    {
        auto* console = qmltest::Find(m_app, "rpcConsole");
        QVERIFY(console);
        const auto geometry = [&](const char* name, const char* property) {
            auto* object = qmltest::Find(m_app, name);
            return object ? object->property(property).toDouble() : -1.0;
        };

        // The input row keeps its fixed padding and prompt dimensions.
        QCOMPARE(geometry("consoleInputRow", "x"), 0.0);
        QCOMPARE(geometry("consoleInputRow", "width"), console->property("width").toDouble());
        QCOMPARE(geometry("consoleInputRow", "height"), 64.0);
        QCOMPARE(geometry("consoleInputDivider", "height"), 1.0);
        QCOMPARE(geometry("consoleInputContent", "x"), 12.0);
        QCOMPARE(geometry("consoleInputContent", "y"), 12.0);
        QCOMPARE(geometry("consoleInputContent", "height"), 40.0);
        QCOMPARE(geometry("consolePromptIcon", "width"), 16.0);
        QCOMPARE(geometry("consolePromptIcon", "height"), 16.0);
        QCOMPARE(geometry("consoleInputContent", "x") + geometry("consoleInput", "x"), 36.0);
        QVERIFY(!qmltest::Find(m_app, "rpcConsoleSearchPreviousButton")->property("enabled").toBool());
        QVERIFY(!qmltest::Find(m_app, "rpcConsoleSearchNextButton")->property("enabled").toBool());
        QVERIFY(!qmltest::Find(m_app, "rpcConsoleWarningBanner")->property("text").toString().isEmpty());

        // Both the command and its result must stay aligned at every font size.
        QVERIFY(submitCommand("getblockcount"));
        QVERIFY(waitForOutput());
        QVERIFY(hasLogicalExchange(RpcConsoleModel::CMD_REPLY));
        QTRY_COMPARE(projection()->rowCount(), 2);
        const int original_size = console->property("outputFontPixelSize").toInt();
        const int minimum = console->property("minimumOutputFontPixelSize").toInt();
        const int maximum = console->property("maximumOutputFontPixelSize").toInt();
        for (int size{original_size}; size > minimum; --size) QVERIFY(qmltest::Click(m_app, "consoleFontDecreaseButton"));
        for (int size{minimum}; size <= maximum; ++size) {
            QTRY_COMPARE(console->property("outputFontPixelSize").toInt(), size);
            for (int row{0}; row < 2; ++row) {
                const auto row_name = QString{"consoleOutputArea_row_%1"}.arg(row);
                const auto timestamp_name = QString{"consoleOutputArea_left_%1"}.arg(row);
                const auto content_name = QString{"consoleOutputArea_content_%1"}.arg(row);
                QVERIFY(qmltest::WaitForVisible(m_app, qPrintable(row_name)));
                QTRY_VERIFY(qAbs(geometry(qPrintable(row_name), "width") - console->property("width").toDouble() + 32) <= 1);
                QTRY_VERIFY(geometry(qPrintable(timestamp_name), "width") >= std::max(60.0, geometry(qPrintable(timestamp_name), "contentWidth")));
                QTRY_VERIFY(qAbs(geometry(qPrintable(content_name), "x") - geometry(qPrintable(timestamp_name), "width") - 20) <= 1);
                QCOMPARE(qmltest::Find(m_app, qPrintable(row_name))->property("rowCategory").toInt(), row);
            }
            if (size < maximum) QVERIFY(qmltest::Click(m_app, "consoleFontIncreaseButton"));
        }
        const auto timestamp = qmltest::Find(m_app, "consoleOutputArea_left_0")->property("text").toString();
        QVERIFY(QRegularExpression{QStringLiteral("^[0-9]{2}:[0-9]{2}:[0-9]{2}$")}.match(timestamp).hasMatch());
        QVERIFY(qmltest::ReturnToNode(m_app));
        QVERIFY(qmltest::WaitForVisible(m_app, "blockClock"));
    }

    void autocompleteUsesLiveRpcCommands()
    {
        auto* model = qmltest::Model<RpcConsoleModel>(m_app, "rpcConsoleModel");
        QVERIFY(model);
        QVERIFY(qmltest::SetText(m_app, "consoleInput", "getblockcou"));
        QVERIFY(qmltest::WaitForVisible(m_app, "consoleAutocompletePopup"));
        QTRY_VERIFY(!model->completionBusy());
        QVERIFY(model->completionError().isEmpty());
        QCOMPARE(model->completionSuggestions(), QStringList{QStringLiteral("getblockcount")});
        QVERIFY(qmltest::Click(m_app, "consoleAutocomplete_0"));
        QTRY_COMPARE(qmltest::Find(m_app, "consoleInput")->property("text").toString().trimmed(), QStringLiteral("getblockcount"));
        QVERIFY(qmltest::SetText(m_app, "consoleInput", "zzzznotacommand"));
        QTRY_COMPARE(model->completionPrefix(), QStringLiteral("zzzznotacommand"));
        QTRY_VERIFY(!model->completionBusy());
        QVERIFY(model->completionError().isEmpty());
        QCOMPARE(model->completionSuggestions().size(), 0);
        QTRY_VERIFY(!qmltest::Find(m_app, "consoleAutocompletePopup")->property("visible").toBool());
        QVERIFY(qmltest::SetText(m_app, "consoleInput", "help get"));
        QVERIFY(qmltest::WaitForVisible(m_app, "consoleAutocompletePopup"));
        QTRY_VERIFY(!model->completionBusy());
        QVERIFY(model->completionError().isEmpty());
        QVERIFY(!model->completionSuggestions().isEmpty());
        QVERIFY(model->completionSuggestions().first().startsWith("help get"));
    }
};

std::unique_ptr<QObject> CreateConsoleTests(BitcoinQmlApplication& app, WalletMode mode)
{
    return std::make_unique<ConsoleIntegrationTests>(app, mode);
}

#include <test_console.moc>
