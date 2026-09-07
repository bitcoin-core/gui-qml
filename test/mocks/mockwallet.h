// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_TEST_MOCKS_MOCKWALLET_H
#define BITCOIN_QML_TEST_MOCKS_MOCKWALLET_H

#include <interfaces/wallet.h>
#include <outputtype.h>
#include <test/mocks/callcounter.h>
#include <wallet/coincontrol.h>
#include <wallet/types.h>
#include <wallet/wallet.h>

#include <QtTest/qtestcase.h>

#include <functional>
#include <source_location>
#include <sstream>

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
    util::Result<wallet::CreatedTransactionResult> createTransaction(const std::vector<wallet::CRecipient>&, const wallet::CCoinControl&, bool, std::optional<unsigned int>) override { return util::Error{Untranslated("not implemented")}; }
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
    std::optional<common::PSBTError> fillPSBT(const common::PSBTFillOptions&, size_t*, PartiallySignedTransaction&, bool&) override { return std::nullopt; }
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

/**
 * Configurable wallet test double with permissive defaults.
 *
 * Unconfigured methods return their StubWallet values. Tests register explicit
 * expectations for calls that are part of the behavior under test.
 */
class MockWallet : public StubWallet
{
public:
    class Verification
    {
    public:
        explicit Verification(MockWallet& wallet)
            : m_wallet{&wallet}
        {
        }

        Verification(const Verification&) = delete;
        Verification& operator=(const Verification&) = delete;
        Verification(Verification&& other) noexcept
            : m_wallet{std::exchange(other.m_wallet, nullptr)}
        {
        }

        ~Verification()
        {
            if (!m_wallet) return;
            for (const auto& failure : m_wallet->VerificationFailures()) {
                QTest::qFail(failure.message.c_str(), failure.where.file_name(), static_cast<int>(failure.where.line()));
            }
        }

    private:
        MockWallet* m_wallet;
    };

    [[nodiscard]] Verification VerifyOnExit()
    {
        return Verification{*this};
    }

    void ExpectExactly(const CallCounter& counter, int expected, std::source_location where = std::source_location::current())
    {
        m_call_expectations.push_back(CallExpectation{&counter, counter.load(), expected, expected, where});
    }

    void ExpectNoCalls(const CallCounter& counter, std::source_location where = std::source_location::current())
    {
        ExpectExactly(counter, 0, where);
    }

    void ExpectAtLeast(const CallCounter& counter, int minimum, std::source_location where = std::source_location::current())
    {
        m_call_expectations.push_back(CallExpectation{&counter, counter.load(), minimum, std::nullopt, where});
    }

    std::function<util::Result<CTransactionRef>(const std::vector<wallet::CRecipient>&, const wallet::CCoinControl&, bool, int&, CAmount&)> create_transaction_fn;
    std::function<CTxDestination(OutputType, const std::string&)> get_new_destination_fn;
    std::function<std::set<interfaces::WalletTx>()> get_wallet_txs_fn;
    std::function<CAmount()> get_balance_fn;
    std::function<CAmount(const wallet::CCoinControl&)> get_available_balance_fn;
    std::function<CAmount(unsigned int)> get_required_fee_fn;
    std::function<CoinsList()> list_coins_fn;
    std::function<OutputType()> get_default_address_type_fn;
    std::function<std::unique_ptr<interfaces::Handler>(TransactionChangedFn)> handle_transaction_changed_fn;
    std::function<interfaces::WalletTx(const Txid&)> get_wallet_tx_fn;
    std::function<bool(const Txid&, interfaces::WalletTxStatus&, int&, int64_t&)> try_get_tx_status_fn;
    std::function<bool(const Txid&)> transaction_can_be_bumped_fn;
    std::function<bool(const Txid&, const wallet::CCoinControl&, std::vector<bilingual_str>&, CAmount&, CAmount&, CMutableTransaction&)> create_bump_transaction_fn;
    std::function<bool(CMutableTransaction&)> sign_bump_transaction_fn;
    std::function<bool(const Txid&, CMutableTransaction&&, std::vector<bilingual_str>&, Txid&)> commit_bump_transaction_fn;

    struct Calls {
        CallCounter getNewDestination{"getNewDestination"};
        CallCounter createTransaction{"createTransaction"};
        CallCounter getWalletTxs{"getWalletTxs"};
        CallCounter getWalletTx{"getWalletTx"};
        CallCounter tryGetTxStatus{"tryGetTxStatus"};
        CallCounter getBalance{"getBalance"};
        CallCounter getAvailableBalance{"getAvailableBalance"};
        CallCounter getRequiredFee{"getRequiredFee"};
        CallCounter listCoins{"listCoins"};
        CallCounter getDefaultAddressType{"getDefaultAddressType"};
        CallCounter handleTransactionChanged{"handleTransactionChanged"};
        CallCounter transactionCanBeBumped{"transactionCanBeBumped"};
        CallCounter createBumpTransaction{"createBumpTransaction"};
        CallCounter signBumpTransaction{"signBumpTransaction"};
        CallCounter commitBumpTransaction{"commitBumpTransaction"};
    } calls;

    util::Result<CTxDestination> getNewDestination(const OutputType type, const std::string& label) override
    {
        ++calls.getNewDestination;
        if (get_new_destination_fn) return get_new_destination_fn(type, label);
        return util::Error{Untranslated("no get_new_destination_fn installed")};
    }

    util::Result<wallet::CreatedTransactionResult> createTransaction(const std::vector<wallet::CRecipient>& recipients,
        const wallet::CCoinControl& coin_control,
        bool sign,
        std::optional<unsigned int>) override
    {
        ++calls.createTransaction;
        if (create_transaction_fn) {
            int change_pos{-1};
            CAmount fee{0};
            auto result = create_transaction_fn(recipients, coin_control, sign, change_pos, fee);
            if (!result) {
                return util::Error{util::ErrorString(result)};
            }
            return wallet::CreatedTransactionResult{
                *result,
                fee,
                change_pos >= 0 ? std::optional<unsigned int>{static_cast<unsigned int>(change_pos)} : std::nullopt,
                FeeCalculation{}};
        }
        return util::Error{Untranslated("no create_transaction_fn installed")};
    }

    std::set<interfaces::WalletTx> getWalletTxs() override
    {
        ++calls.getWalletTxs;
        return get_wallet_txs_fn ? get_wallet_txs_fn() : std::set<interfaces::WalletTx>{};
    }

    interfaces::WalletTx getWalletTx(const Txid& txid) override
    {
        ++calls.getWalletTx;
        return get_wallet_tx_fn ? get_wallet_tx_fn(txid) : interfaces::WalletTx{};
    }

    bool tryGetTxStatus(const Txid& txid, interfaces::WalletTxStatus& tx_status, int& num_blocks, int64_t& block_time) override
    {
        ++calls.tryGetTxStatus;
        return try_get_tx_status_fn ? try_get_tx_status_fn(txid, tx_status, num_blocks, block_time) : false;
    }

    CAmount getBalance() override
    {
        ++calls.getBalance;
        return get_balance_fn ? get_balance_fn() : 0;
    }

    CAmount getAvailableBalance(const wallet::CCoinControl& coin_control) override
    {
        ++calls.getAvailableBalance;
        return get_available_balance_fn ? get_available_balance_fn(coin_control) : 0;
    }

    CAmount getRequiredFee(unsigned int tx_bytes) override
    {
        ++calls.getRequiredFee;
        return get_required_fee_fn ? get_required_fee_fn(tx_bytes) : 0;
    }

    CoinsList listCoins() override
    {
        ++calls.listCoins;
        return list_coins_fn ? list_coins_fn() : CoinsList{};
    }

    OutputType getDefaultAddressType() override
    {
        ++calls.getDefaultAddressType;
        return get_default_address_type_fn ? get_default_address_type_fn() : OutputType::BECH32;
    }

    std::unique_ptr<interfaces::Handler> handleTransactionChanged(TransactionChangedFn fn) override
    {
        ++calls.handleTransactionChanged;
        return handle_transaction_changed_fn ? handle_transaction_changed_fn(std::move(fn)) : nullptr;
    }

    bool transactionCanBeBumped(const Txid& txid) override
    {
        ++calls.transactionCanBeBumped;
        return transaction_can_be_bumped_fn ? transaction_can_be_bumped_fn(txid) : false;
    }

    bool createBumpTransaction(const Txid& txid, const wallet::CCoinControl& coin_control, std::vector<bilingual_str>& errors,
                               CAmount& old_fee, CAmount& new_fee, CMutableTransaction& mtx) override
    {
        ++calls.createBumpTransaction;
        return create_bump_transaction_fn ? create_bump_transaction_fn(txid, coin_control, errors, old_fee, new_fee, mtx) : false;
    }

    bool signBumpTransaction(CMutableTransaction& mtx) override
    {
        ++calls.signBumpTransaction;
        return sign_bump_transaction_fn ? sign_bump_transaction_fn(mtx) : false;
    }

    bool commitBumpTransaction(const Txid& txid, CMutableTransaction&& mtx, std::vector<bilingual_str>& errors, Txid& bumped_txid) override
    {
        ++calls.commitBumpTransaction;
        return commit_bump_transaction_fn ? commit_bump_transaction_fn(txid, std::move(mtx), errors, bumped_txid) : false;
    }

private:
    struct CallExpectation {
        const CallCounter* counter;
        int baseline;
        int minimum;
        std::optional<int> maximum;
        std::source_location where;
    };

    struct VerificationFailure {
        std::string message;
        std::source_location where;
    };

    std::vector<VerificationFailure> VerificationFailures() const
    {
        std::vector<VerificationFailure> failures;
        for (const CallExpectation& expectation : m_call_expectations) {
            const int actual{expectation.counter->load() - expectation.baseline};
            if (actual < expectation.minimum || (expectation.maximum && actual > *expectation.maximum)) {
                std::ostringstream message;
                message << "Expected " << expectation.counter->Name() << " calls to be ";
                if (expectation.maximum && expectation.minimum == *expectation.maximum) {
                    message << expectation.minimum;
                } else if (expectation.maximum) {
                    message << "between " << expectation.minimum << " and " << *expectation.maximum;
                } else {
                    message << "at least " << expectation.minimum;
                }
                message << ", actual " << actual;
                failures.push_back({message.str(), expectation.where});
            }
        }
        return failures;
    }

    std::vector<CallExpectation> m_call_expectations;
};

#endif // BITCOIN_QML_TEST_MOCKS_MOCKWALLET_H
