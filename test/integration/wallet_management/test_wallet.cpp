// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/integration/runner/test_cases.h>
#include <test/integration/fixture/test_wallet.h>
#include <wallet/db.h>
#include <wallet/wallet.h>

#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QSignalSpy>
#include <QTemporaryDir>

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
        return openSelector() && qmltest::Click(m_app, qPrintable("walletSelectItem_" + name)) &&
               qmltest::WaitForWallet(m_app, name);
    }

    bool walletAction(const QString& name, const char* action)
    {
        return openSelector() && qmltest::Click(m_app, qPrintable("walletSelectActions_" + name)) &&
               qmltest::Click(m_app, qPrintable(QString{"walletSelect%1_%2"}.arg(QString::fromLatin1(action), name)));
    }

public:
    explicit WalletIntegrationTests(BitcoinQmlApplication& app) : m_app{app} {}

private Q_SLOTS:
    void namesRejectLoadedClosedAndAliasedWallets()
    {
        auto* controller = qmltest::Model<WalletQmlController>(m_app, "walletController");
        QVERIFY(controller);
        QVERIFY(qmltest::CreateWallet(m_app, "closed"));
        controller->closeWallet("closed");
        QTRY_VERIFY(!controller->isWalletOpen("closed"));
        QVERIFY(qmltest::CreateWallet(m_app, "loaded"));
        QVERIFY(qmltest::CreateWallet(m_app, "alias_target"));
        QVERIFY(controller->setWalletDisplayName("alias_target", "Savings"));
        QVERIFY(qmltest::Click(m_app, "walletBadge"));
        QVERIFY(qmltest::Click(m_app, "walletSelectAddWalletButton"));
        QVERIFY(qmltest::Click(m_app, "walletTypeRegular"));
        QVERIFY(qmltest::SetText(m_app, "createWalletPasswordInput", "test wallet password"));
        QVERIFY(qmltest::SetText(m_app, "createWalletPasswordRepeatInput", "test wallet password"));
        QVERIFY(qmltest::Click(m_app, "createWalletPasswordConfirmToggle"));
        for (const auto* name : {"closed", "loaded", "Savings", "savings"}) {
            QVERIFY2(controller->walletNameAvailabilityError(name).contains("already exists"), name);
            QVERIFY(qmltest::SetText(m_app, "createWalletNameInput", name));
            QVERIFY(qmltest::Click(m_app, "createWalletFormCreateButton"));
            auto* error = qmltest::Find(m_app, "createWalletFormErrorText");
            QVERIFY(error);
            QTRY_VERIFY(error->property("text").toString().contains("already exists"));
        }
        QCOMPARE(controller->walletNameAvailabilityError("My new wallet"), QString{});
        QSignalSpy created{controller, &WalletQmlController::walletCreateSucceeded};
        QVERIFY(qmltest::SetText(m_app, "createWalletNameInput", "My new wallet"));
        QVERIFY(qmltest::Click(m_app, "createWalletFormCreateButton"));
        QTRY_COMPARE_WITH_TIMEOUT(created.count(), 1, 30'000);
        QVERIFY(qmltest::WaitForVisible(m_app, "walletCreationReadyPage"));
        QVERIFY(qmltest::Click(m_app, "createWalletReadyDoneButton"));
        auto* wallet = qmltest::WaitForWallet(m_app, "My new wallet");
        QVERIFY(wallet);
        QVERIFY(wallet->isEncrypted());
        QVERIFY(wallet->isLocked());
        const auto info = qmltest::Rpc(m_app, "getwalletinfo", {}, wallet->name());
        QVERIFY(info.find_value("private_keys_enabled").get_bool());
        QCOMPARE(info.find_value("unlocked_until").getInt<int64_t>(), 0);
    }

    void closingAndReloadingReplacesTheModel()
    {
        auto* controller = qmltest::Model<WalletQmlController>(m_app, "walletController");
        QVERIFY(controller);
        QPointer<WalletQmlModel> original{qmltest::CreateWallet(m_app, "reload")};
        QVERIFY(original);
        QSignalSpy selected{controller, &WalletQmlController::selectedWalletChanged};
        controller->closeWallet("reload");
        QTRY_VERIFY(!controller->isWalletLoaded());
        QTRY_VERIFY(original.isNull());
        QVERIFY(!selected.isEmpty());
        QCOMPARE(qmltest::Rpc(m_app, "listwallets").size(), 0U);
        controller->setSelectedWallet("reload");
        QVERIFY(qmltest::WaitForWallet(m_app, "reload"));
        QCOMPARE(qmltest::Rpc(m_app, "listwallets").size(), 1U);
        auto* list = qmltest::Model<WalletListModel>(m_app, "walletListModel");
        QVERIFY(list);
        QTRY_COMPARE(list->rowCount(), 1);
        QCOMPARE(list->data(list->index(0), WalletListModel::LoadStateRole).toInt(), static_cast<int>(WalletListModel::LoadState::Open));
    }

    void passwordChangesRelockAndPreserveTheWallet()
    {
        auto* wallet = qmltest::CreateWallet(m_app);
        QVERIFY(wallet);
        QVERIFY(wallet->encryptWallet("old password"));
        QTRY_VERIFY(wallet->isLocked());
        QVERIFY(!wallet->changeWalletPassphrase("wrong password", "new password"));
        QVERIFY(!wallet->settingsError().isEmpty());
        QVERIFY(wallet->isLocked());
        QVERIFY(wallet->changeWalletPassphrase("old password", "new password"));
        QVERIFY(wallet->settingsError().isEmpty());
        QVERIFY(wallet->isLocked());
        qmltest::Rpc(m_app, "walletpassphrase", qmltest::Json("[\"new password\", 10]"), wallet->name());
        QTRY_VERIFY(!wallet->isLocked());
        qmltest::Rpc(m_app, "walletlock", {}, wallet->name());
        QTRY_VERIFY(wallet->isLocked());
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
        QVERIFY(wallet->isLocked());
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
        QVERIFY(qmltest::WaitForWallet(m_app, "beta"));
        auto* controller = qmltest::Model<WalletQmlController>(m_app, "walletController");
        QVERIFY(controller);
        QSignalSpy loading{controller, &WalletQmlController::walletLoadInProgressChanged};
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
        QPointer<WalletQmlModel> wallet{qmltest::CreateWallet(m_app, "broken")};
        QVERIFY(wallet);
        const auto database_path = QString::fromStdString(wallet->wallet()->wallet()->GetDatabase().Filename());
        QFile database{database_path};
        QVERIFY(database.open(QIODevice::ReadOnly));
        const auto header = database.read(32);
        database.close();
        auto* controller = qmltest::Model<WalletQmlController>(m_app, "walletController");
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
        QVERIFY(database.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QCOMPARE(database.write(header), header.size());
        database.close();
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

    void walletTypeSelectorRejectsInvalidBackups()
    {
        QVERIFY(qmltest::Click(m_app, "walletBadge"));
        QVERIFY(qmltest::WaitForVisible(m_app, "walletCreationTypePage"));
        QVERIFY(!qmltest::Find(m_app, "walletTypeMultiKey"));
        QVERIFY(!qmltest::Find(m_app, "walletTypeCustom"));
        auto* controller = qmltest::Model<WalletQmlController>(m_app, "walletController");
        QVERIFY(controller);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QFile invalid{directory.filePath("invalid.bak")};
        QVERIFY(invalid.open(QIODevice::WriteOnly));
        QVERIFY(invalid.write("not a wallet backup") > 0);
        invalid.close();
        auto* wizard = qmltest::Find(m_app, "createWalletWizard");
        QVERIFY(wizard);
        QVERIFY(QMetaObject::invokeMethod(wizard, "importFile", Q_ARG(QVariant, invalid.fileName())));
        QVERIFY(qmltest::WaitForVisible(m_app, "walletImportErrorPage"));
        QVERIFY(!controller->walletLoadError().isEmpty());
        QCOMPARE(qmltest::Find(m_app, "importWalletErrorTitle")->property("text").toString(), QStringLiteral("This wallet type is not supported"));
        QVERIFY(qmltest::Find(m_app, "importWalletErrorHelpText")->property("text").toString().contains("Data is not in recognized format."));
        QVERIFY(!QFileInfo::exists(QString::fromStdString(m_app.node().walletLoader().getWalletDir()) + "/invalid"));
        QCOMPARE(qmltest::Rpc(m_app, "listwallets").size(), 0U);
    }

    void backupRestoresAddressesAndRejectsInvalidFiles()
    {
        auto* wallet = qmltest::CreateWallet(m_app, "original");
        QVERIFY(wallet);
        const auto address = qmltest::NewAddress(m_app, wallet->name());
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto backup = directory.filePath(QString::fromUtf8("wallet backup ü.dat"));
        QVERIFY(wallet->backupWallet(backup));
        QVERIFY(QFileInfo{backup}.size() > 0);
        auto* controller = qmltest::Model<WalletQmlController>(m_app, "walletController");
        QVERIFY(controller);
        controller->closeWallet("original");
        QTRY_VERIFY(!controller->isWalletLoaded());
        QSignalSpy imported{controller, &WalletQmlController::walletImportSucceeded};
        controller->importWallet(backup);
        QTRY_COMPARE_WITH_TIMEOUT(imported.count(), 1, 30'000);
        QVERIFY(controller->isWalletLoaded());
        UniValue params{UniValue::VARR};
        params.push_back(address.toStdString());
        QVERIFY(qmltest::Rpc(m_app, "getaddressinfo", params, controller->selectedWallet()->name()).find_value("ismine").get_bool());
        controller->importWallet(directory.filePath("missing.dat"));
        QTRY_VERIFY(!controller->walletLoadError().isEmpty());
        QCOMPARE(imported.count(), 1);
        QVERIFY(controller->isWalletLoaded());
    }

    void encryptedBackupRestoresLockedAndRetainsItsSigningKeys()
    {
        auto* wallet = qmltest::CreateWallet(m_app, "encrypted_source");
        QVERIFY(wallet);
        const auto address = qmltest::NewAddress(m_app, wallet->name(), "legacy");
        QVERIFY(wallet->encryptWallet("backup password"));
        QTRY_VERIFY(wallet->isLocked());
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto backup = directory.filePath("encrypted_restore.bak");
        QVERIFY(wallet->backupWallet(backup));
        auto* controller = qmltest::Model<WalletQmlController>(m_app, "walletController");
        QVERIFY(controller);
        controller->closeWallet(wallet->name());
        QTRY_VERIFY(!controller->isWalletLoaded());
        QTRY_COMPARE(qmltest::Rpc(m_app, "listwallets").size(), 0U);
        controller->importWallet(backup);
        auto* restored = qmltest::WaitForWallet(m_app, "encrypted_restore");
        QVERIFY(restored);
        QVERIFY(restored->isEncrypted());
        QVERIFY(restored->isLocked());
        auto* signer = restored->signVerifyMessageModel();
        QVERIFY(signer->signMessageWithPassphrase(address, "restored wallet", "backup password"));
        QVERIFY(signer->verifyMessage(address, "restored wallet", signer->signature()));
        QVERIFY(restored->isLocked());
    }
};

std::unique_ptr<QObject> CreateWalletTests(BitcoinQmlApplication& app, WalletMode)
{
    return std::make_unique<WalletIntegrationTests>(app);
}

#include <test_wallet.moc>
