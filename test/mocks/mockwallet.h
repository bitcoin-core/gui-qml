// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_TEST_MOCKS_MOCKWALLET_H
#define BITCOIN_QML_TEST_MOCKS_MOCKWALLET_H

#ifdef Assert
#pragma push_macro("Assert")
#undef Assert
#define BITCOIN_QML_RESTORE_ASSERT_MACRO
#endif

#include <gmock/gmock.h>

#ifdef BITCOIN_QML_RESTORE_ASSERT_MACRO
#pragma pop_macro("Assert")
#undef BITCOIN_QML_RESTORE_ASSERT_MACRO
#endif

#include <interfaces/wallet.h>
#include <outputtype.h>
#include <wallet/coincontrol.h>
#include <wallet/types.h>
#include <wallet/wallet.h>

#include <functional>

class StubWallet : public interfaces::Wallet
{
public:
    bool encryptWallet(const SecureString&) override { return false; }
    bool isCrypted() override { return false; }
    bool lock() override { return false; }
    bool unlock(const SecureString&) override { return false; }
    bool isLocked() override { return false; }
    bool changeWalletPassphrase(const SecureString&, const SecureString&) override { return false; }
    void abortRescan() override {}
    bool backupWallet(const std::string&) override { return false; }
    std::string getWalletName() override { return {}; }
    util::Result<CTxDestination> getNewDestination(const OutputType, const std::string&) override { return util::Error{Untranslated("not implemented")}; }
    bool getPubKey(const CScript&, const CKeyID&, CPubKey&) override { return false; }
    SigningResult signMessage(const std::string&, const PKHash&, std::string&) override { return SigningResult::PRIVATE_KEY_NOT_AVAILABLE; }
    bool isSpendable(const CTxDestination&) override { return false; }
    bool setAddressBook(const CTxDestination&, const std::string&, const std::optional<wallet::AddressPurpose>&) override { return false; }
    bool delAddressBook(const CTxDestination&) override { return false; }
    bool getAddress(const CTxDestination&, std::string*, wallet::AddressPurpose*) override { return false; }
    std::vector<interfaces::WalletAddress> getAddresses() override { return {}; }
    std::vector<std::string> getAddressReceiveRequests() override { return {}; }
    bool setAddressReceiveRequest(const CTxDestination&, const std::string&, const std::string&) override { return false; }
    util::Result<void> displayAddress(const CTxDestination&) override { return {}; }
    bool lockCoin(const COutPoint&, const bool) override { return false; }
    bool unlockCoin(const COutPoint&) override { return false; }
    bool isLockedCoin(const COutPoint&) override { return false; }
    void listLockedCoins(std::vector<COutPoint>&) override {}
    util::Result<CTransactionRef> createTransaction(const std::vector<wallet::CRecipient>&, const wallet::CCoinControl&, bool, int&, CAmount&) override { return util::Error{Untranslated("not implemented")}; }
    void commitTransaction(CTransactionRef, interfaces::WalletValueMap, interfaces::WalletOrderForm) override {}
    bool transactionCanBeAbandoned(const Txid&) override { return false; }
    bool abandonTransaction(const Txid&) override { return false; }
    bool transactionCanBeBumped(const Txid&) override { return false; }
    bool createBumpTransaction(const Txid&, const wallet::CCoinControl&, std::vector<bilingual_str>&, CAmount&, CAmount&, CMutableTransaction&) override { return false; }
    bool signBumpTransaction(CMutableTransaction&) override { return false; }
    bool commitBumpTransaction(const Txid&, CMutableTransaction&&, std::vector<bilingual_str>&, Txid&) override { return false; }
    CTransactionRef getTx(const Txid&) override { return {}; }
    interfaces::WalletTx getWalletTx(const Txid&) override { return {}; }
    std::set<interfaces::WalletTx> getWalletTxs() override { return {}; }
    bool tryGetTxStatus(const Txid&, interfaces::WalletTxStatus&, int&, int64_t&) override { return false; }
    interfaces::WalletTx getWalletTxDetails(const Txid&, interfaces::WalletTxStatus&, interfaces::WalletOrderForm&, bool&, int&) override { return {}; }
    std::optional<common::PSBTError> fillPSBT(std::optional<int>, bool, bool, size_t*, PartiallySignedTransaction&, bool&) override { return std::nullopt; }
    interfaces::WalletBalances getBalances() override { return {}; }
    bool tryGetBalances(interfaces::WalletBalances&, uint256&) override { return false; }
    CAmount getBalance() override { return 0; }
    CAmount getAvailableBalance(const wallet::CCoinControl&) override { return 0; }
    bool txinIsMine(const CTxIn&) override { return false; }
    bool txoutIsMine(const CTxOut&) override { return false; }
    CAmount getDebit(const CTxIn&) override { return 0; }
    CAmount getCredit(const CTxOut&) override { return 0; }
    CoinsList listCoins() override { return {}; }
    std::vector<interfaces::WalletTxOut> getCoins(const std::vector<COutPoint>&) override { return {}; }
    CAmount getRequiredFee(unsigned int) override { return 0; }
    CAmount getMinimumFee(unsigned int, const wallet::CCoinControl&, int*, FeeReason*) override { return 0; }
    unsigned int getConfirmTarget() override { return 0; }
    bool hdEnabled() override { return false; }
    bool canGetAddresses() override { return false; }
    bool privateKeysDisabled() override { return false; }
    bool taprootEnabled() override { return false; }
    bool hasExternalSigner() override { return false; }
    OutputType getDefaultAddressType() override { return OutputType::BECH32; }
    CAmount getDefaultMaxTxFee() override { return 0; }
    void remove() override {}
    std::unique_ptr<interfaces::Handler> handleUnload(UnloadFn) override { return {}; }
    std::unique_ptr<interfaces::Handler> handleShowProgress(ShowProgressFn) override { return {}; }
    std::unique_ptr<interfaces::Handler> handleStatusChanged(StatusChangedFn) override { return {}; }
    std::unique_ptr<interfaces::Handler> handleAddressBookChanged(AddressBookChangedFn) override { return {}; }
    std::unique_ptr<interfaces::Handler> handleTransactionChanged(TransactionChangedFn) override { return {}; }
    std::unique_ptr<interfaces::Handler> handleCanGetAddressesChanged(CanGetAddressesChangedFn) override { return {}; }
};

class MockWallet : public StubWallet
{
public:
    std::function<util::Result<CTransactionRef>(const std::vector<wallet::CRecipient>&, const wallet::CCoinControl&, bool, int&, CAmount&)> createTransactionHandler;

    util::Result<CTxDestination> getNewDestination(const OutputType type, const std::string& label) override
    {
        return getNewDestinationValue(type, label);
    }

    util::Result<CTransactionRef> createTransaction(const std::vector<wallet::CRecipient>& recipients,
        const wallet::CCoinControl& coin_control,
        bool sign,
        int& change_pos,
        CAmount& fee) override
    {
        if (createTransactionHandler) {
            return createTransactionHandler(recipients, coin_control, sign, change_pos, fee);
        }
        return util::Error{Untranslated("no createTransactionHandler installed")};
    }

    MOCK_METHOD(CTxDestination, getNewDestinationValue, (OutputType, const std::string&));
    MOCK_METHOD((std::set<interfaces::WalletTx>), getWalletTxs, (), (override));
    MOCK_METHOD(CAmount, getBalance, (), (override));
    MOCK_METHOD(CAmount, getAvailableBalance, (const wallet::CCoinControl&), (override));
    MOCK_METHOD(CAmount, getRequiredFee, (unsigned int), (override));
    MOCK_METHOD(CoinsList, listCoins, (), (override));
    MOCK_METHOD(OutputType, getDefaultAddressType, (), (override));
    MOCK_METHOD((std::unique_ptr<interfaces::Handler>), handleTransactionChanged, (TransactionChangedFn), (override));
    MOCK_METHOD(bool, transactionCanBeBumped, (const Txid&), (override));
    MOCK_METHOD(bool, createBumpTransaction, (const Txid&, const wallet::CCoinControl&, std::vector<bilingual_str>&, CAmount&, CAmount&, CMutableTransaction&), (override));
    MOCK_METHOD(bool, signBumpTransaction, (CMutableTransaction&), (override));
    MOCK_METHOD(bool, commitBumpTransaction, (const Txid&, CMutableTransaction&&, std::vector<bilingual_str>&, Txid&), (override));
};

#endif // BITCOIN_QML_TEST_MOCKS_MOCKWALLET_H
