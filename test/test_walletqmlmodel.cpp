// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <addresstype.h>
#include <chainparams.h>
#include <common/messages.h>
#include <common/signmessage.h>
#include <interfaces/handler.h>
#include <interfaces/wallet.h>
#include <key.h>
#include <key_io.h>
#include <outputtype.h>
#include <primitives/transaction.h>
#include <psbt.h>
#include <qml/models/activityfilterproxymodel.h>
#include <qml/models/activitylistmodel.h>
#include <qml/models/psbtqmlmodel.h>
#include <qml/models/receiverequesthistorymodel.h>
#include <qml/models/sendrecipient.h>
#include <qml/models/sendrecipientslistmodel.h>
#include <qml/models/walletqmlmodel.h>
#include <qml/models/walletqmlmodeltransaction.h>
#include <script/signingprovider.h>
#include <script/solver.h>
#include <test/mocks/mocknode.h>
#include <test/mocks/mockwallet.h>
#include <wallet/coincontrol.h>
#include <wallet/types.h>

#include <QFile>
#include <QSemaphore>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QVariantList>
#include <QVariantMap>
#include <QtTest/QtTest>

#include <algorithm>
#include <array>
#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <numeric>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <vector>

namespace {
std::unique_ptr<interfaces::Handler> MakeNoopHandler()
{
    return interfaces::MakeCleanupHandler([] {});
}

constexpr auto NON_P2PKH_ADDRESS{"bcrt1qqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqq3xueyj"};

constexpr auto FEE_ESTIMATE_TIMEOUT_MS{3'000};
const auto VALID_MAINNET_ADDRESS = QStringLiteral("1BoatSLRHtKNngkdXEeobR76b53LETtpyT");
const auto VALID_MAINNET_P2SH_ADDRESS = QStringLiteral("3J98t1WpEZ73CNmQviecrnyiWrnqRhWNLy");
const auto VALID_REGTEST_ADDRESS = QStringLiteral("bcrt1qdavt4j2sd7dlhqsavtnfxvzppw6k7qy97tmnu9");

class ChainSelectionGuard
{
public:
    explicit ChainSelectionGuard(const ChainType chain_type)
        : m_previous_chain_type{Params().GetChainType()}
    {
        SelectParams(chain_type);
    }

    ~ChainSelectionGuard()
    {
        SelectParams(m_previous_chain_type);
    }

private:
    ChainType m_previous_chain_type;
};

struct FeeEstimateCall {
    unsigned int target;
    bool sign;
    bool selected_inputs;
    std::optional<CAmount> fee_rate;
    size_t recipient_count;
};

struct BalancePreviewCall {
    unsigned int target;
    CAmount total_amount;
    bool subtracts_fee;
};

struct SelectedCoinEstimateCall {
    unsigned int target;
    bool sign;
    bool expected_coin_selected;
    bool allow_other_inputs;
    size_t selected_count;
};

template <typename T>
class ThreadSafeCallLog
{
public:
    void Append(T call)
    {
        std::lock_guard<std::mutex> lock{m_mutex};
        m_calls.push_back(std::move(call));
    }

    size_t Size() const
    {
        std::lock_guard<std::mutex> lock{m_mutex};
        return m_calls.size();
    }

    std::vector<T> Snapshot() const
    {
        std::lock_guard<std::mutex> lock{m_mutex};
        return m_calls;
    }

private:
    mutable std::mutex m_mutex;
    std::vector<T> m_calls;
};

void CompareFeeEstimateCalls(const std::vector<FeeEstimateCall>& actual, const std::vector<FeeEstimateCall>& expected)
{
    QCOMPARE(actual.size(), expected.size());
    for (size_t index{0}; index < std::min(actual.size(), expected.size()); ++index) {
        QCOMPARE(actual[index].target, expected[index].target);
        QCOMPARE(actual[index].sign, expected[index].sign);
        QCOMPARE(actual[index].selected_inputs, expected[index].selected_inputs);
        QCOMPARE(actual[index].fee_rate.has_value(), expected[index].fee_rate.has_value());
        if (actual[index].fee_rate && expected[index].fee_rate) {
            QCOMPARE(*actual[index].fee_rate, *expected[index].fee_rate);
        }
        QCOMPARE(actual[index].recipient_count, expected[index].recipient_count);
    }
}

template <typename Wallet>
struct WalletModelHarness {
    Wallet* wallet;
    std::unique_ptr<WalletQmlModel> model;
};

WalletModelHarness<MockWallet> MakeWalletModel(interfaces::Node* node = nullptr)
{
    auto wallet = std::make_unique<MockWallet>();
    MockWallet* const wallet_view{wallet.get()};

    wallet_view->get_wallet_txs_fn = [] { return std::set<interfaces::WalletTx>{}; };
    wallet_view->list_coins_fn = [] { return interfaces::Wallet::CoinsList{}; };
    wallet_view->get_balance_fn = [] { return 10 * COIN; };
    wallet_view->get_available_balance_fn = [](const wallet::CCoinControl&) { return 10 * COIN; };
    wallet_view->get_required_fee_fn = [](unsigned int) { return 1000; };
    wallet_view->get_default_address_type_fn = [] { return OutputType::BECH32; };
    wallet_view->handle_transaction_changed_fn = [](interfaces::Wallet::TransactionChangedFn) {
        return std::unique_ptr<interfaces::Handler>{};
    };
    wallet_view->get_new_destination_fn = [](OutputType, const std::string&) {
        return DecodeDestination(VALID_MAINNET_ADDRESS.toStdString());
    };

    return {wallet_view, std::make_unique<WalletQmlModel>(std::move(wallet), node)};
}

// A minimal external incoming payment to `dest`, shaped so that
// Transaction::fromWalletTx decodes it into a single RecvWithAddress row.
interfaces::WalletTx ReceiveWalletTxFor(const CTxDestination& dest, CAmount amount)
{
    CMutableTransaction mtx;
    mtx.version = 2;
    mtx.vout.emplace_back(amount, GetScriptForDestination(dest));

    interfaces::WalletTx wtx;
    wtx.tx = MakeTransactionRef(mtx);
    wtx.txin_is_mine = {false}; // an external sender: an incoming payment
    wtx.txout_is_mine = {true};
    wtx.txout_address = {dest};
    wtx.txout_address_is_mine = {true};
    wtx.txout_is_change = {false};
    wtx.credit = amount;
    wtx.debit = 0;
    wtx.time = 500;
    wtx.is_coinbase = false;
    return wtx;
}

void SetValidRecipient(WalletQmlModel& model,
                       const QString& address = VALID_MAINNET_ADDRESS,
                       const bool subtract_fee_from_amount = false)
{
    auto* recipient = model.sendRecipientList()->currentRecipient();
    QVERIFY(recipient != nullptr);

    recipient->address()->setAddress(address, 0);
    recipient->amount()->setSatoshi(50'000);
    recipient->setSubtractFeeFromAmount(subtract_fee_from_amount);

    QVERIFY2(recipient->isValid(), "Recipient must be valid before scheduling fee estimates");
}

interfaces::WalletTx MakeOutgoingActivityWalletTx(const std::vector<CAmount>& output_values,
                                                   const std::vector<bool>& output_is_mine,
                                                   const std::vector<bool>& output_is_change)
{
    CMutableTransaction transaction;
    transaction.vin.emplace_back(COutPoint{Txid::FromUint256(uint256{1}), 0});
    for (const CAmount value : output_values) {
        transaction.vout.emplace_back(value, CScript{});
    }

    interfaces::WalletTx wallet_tx;
    wallet_tx.tx = MakeTransactionRef(std::move(transaction));
    wallet_tx.txin_is_mine = {true};
    wallet_tx.txout_is_mine = output_is_mine;
    wallet_tx.txout_is_change = output_is_change;
    wallet_tx.txout_address.assign(output_values.size(), DecodeDestination(VALID_MAINNET_ADDRESS.toStdString()));
    wallet_tx.txout_address_is_mine = output_is_mine;
    wallet_tx.credit = std::inner_product(
        output_values.begin(), output_values.end(), output_is_mine.begin(), CAmount{0});
    wallet_tx.debit = std::accumulate(output_values.begin(), output_values.end(), CAmount{0}) + 1'000;
    wallet_tx.change = 0;
    wallet_tx.time = 0;
    wallet_tx.is_coinbase = false;
    return wallet_tx;
}

class FakePasswordWallet : public StubWallet
{
public:
    bool encrypted{true};
    bool locked{true};
    bool private_keys_disabled{false};
    bool external_signer{false};
    bool taproot_enabled{true};
    OutputType default_address_type{OutputType::BECH32};
    CAmount balance{50'000};
    int encrypt_calls{0};
    int change_passphrase_calls{0};
    int backup_calls{0};
    std::string last_backup_path;
    std::vector<std::pair<std::string, std::string>> changed_passphrases;
    int unlock_calls{0};
    int lock_calls{0};
    int commit_calls{0};
    int create_bump_calls{0};
    int sign_bump_calls{0};
    int commit_bump_calls{0};
    int get_new_destination_calls{0};
    int set_address_receive_request_calls{0};
    std::vector<std::string> unlock_passphrases;
    std::vector<OutputType> new_destination_types;
    std::vector<std::string> new_destination_labels;
    std::vector<interfaces::WalletAddress> wallet_addresses;
    std::vector<std::string> receive_request_ids;
    std::vector<bool> create_transaction_sign_args;
    std::vector<bool> fill_psbt_sign_args;
    bool get_address_result{false};
    std::string get_address_label;
    std::string last_set_address_book_label;
    int set_address_book_calls{0};
    int sign_message_calls{0};
    std::string last_signed_message;
    bool can_bump_transaction{true};
    bool sign_bump_result{true};
    bool commit_bump_result{true};

    std::function<util::Result<CTransactionRef>(const std::vector<wallet::CRecipient>&,
                                                const wallet::CCoinControl&,
                                                bool,
                                                int&,
                                                CAmount&)>
        create_transaction_fn = [](const std::vector<wallet::CRecipient>&,
                                   const wallet::CCoinControl&,
                                   bool,
                                   int& change_pos,
                                   CAmount& fee) {
            change_pos = -1;
            fee = 250;
            return MakeTransactionRef(CMutableTransaction{});
        };
    std::function<bool(const SecureString&)> unlock_fn = [this](const SecureString& passphrase) {
        ++unlock_calls;
        unlock_passphrases.emplace_back(passphrase.begin(), passphrase.end());
        locked = false;
        return true;
    };
    std::function<util::Result<CTxDestination>(OutputType, const std::string&)> get_new_destination_fn = [this](OutputType type, const std::string& label) -> util::Result<CTxDestination> {
        ++get_new_destination_calls;
        new_destination_types.push_back(type);
        new_destination_labels.push_back(label);
        return DecodeDestination(VALID_MAINNET_ADDRESS.toStdString());
    };
    std::function<std::optional<common::PSBTError>(const common::PSBTFillOptions&,
                                                   size_t*,
                                                   PartiallySignedTransaction&,
                                                   bool&)>
        fill_psbt_fn = [this](const common::PSBTFillOptions& options,
                              size_t*,
                              PartiallySignedTransaction&,
                              bool& complete) {
            fill_psbt_sign_args.push_back(options.sign);
            complete = options.sign;
            return std::nullopt;
        };
    std::set<Txid> known_txids;
    std::function<bool(const CTxIn&)> txin_is_mine_fn = [](const CTxIn&) {
        return false;
    };
    std::function<bool(const CTxOut&)> txout_is_mine_fn = [](const CTxOut&) {
        return false;
    };
    std::function<SigningResult(const std::string&, const PKHash&, std::string&)>
        sign_message_fn = [this](const std::string& message, const PKHash&, std::string& signature) {
            ++sign_message_calls;
            last_signed_message = message;
            signature = "fake-signature";
            return SigningResult::OK;
        };

    bool encryptWallet(const SecureString& passphrase) override
    {
        ++encrypt_calls;
        encrypted = true;
        locked = true;
        unlock_passphrases.emplace_back(passphrase.begin(), passphrase.end());
        return !passphrase.empty();
    }
    bool isCrypted() override { return encrypted; }
    bool lock() override
    {
        ++lock_calls;
        locked = true;
        return true;
    }
    bool unlock(const SecureString& wallet_passphrase) override { return unlock_fn(wallet_passphrase); }
    bool isLocked() override { return locked; }
    util::Result<CTxDestination> getNewDestination(const OutputType type, const std::string& label) override
    {
        return get_new_destination_fn(type, label);
    }
    bool changeWalletPassphrase(const SecureString& old_passphrase, const SecureString& new_passphrase) override
    {
        ++change_passphrase_calls;
        changed_passphrases.emplace_back(
            std::string(old_passphrase.begin(), old_passphrase.end()),
            std::string(new_passphrase.begin(), new_passphrase.end()));
        return !old_passphrase.empty() && !new_passphrase.empty() && old_passphrase == SecureString{"secret"};
    }
    void abortRescan() override {}
    bool backupWallet(const std::string& path) override
    {
        ++backup_calls;
        last_backup_path = path;
        return !path.empty();
    }
    std::string getWalletName() override { return "fake-wallet"; }
    bool getPubKey(const CScript&, const CKeyID&, CPubKey&) override { return false; }
    SigningResult signMessage(const std::string& message, const PKHash& pkhash, std::string& signature) override
    {
        return sign_message_fn(message, pkhash, signature);
    }
    bool isSpendable(const CTxDestination&) override { return false; }
    bool setAddressBook(const CTxDestination&, const std::string& name, const std::optional<wallet::AddressPurpose>&) override
    {
        last_set_address_book_label = name;
        ++set_address_book_calls;
        return true;
    }
    bool delAddressBook(const CTxDestination&) override { return true; }
    bool getAddress(const CTxDestination&, std::string* name, wallet::AddressPurpose*) override
    {
        if (name) {
            *name = get_address_label;
        }
        return get_address_result;
    }
    std::vector<interfaces::WalletAddress> getAddresses() override { return wallet_addresses; }
    std::vector<std::string> getAddressReceiveRequests() override { return {}; }
    bool setAddressReceiveRequest(const CTxDestination&, const std::string& id, const std::string&) override
    {
        ++set_address_receive_request_calls;
        receive_request_ids.push_back(id);
        return set_address_receive_request_result;
    }
    bool set_address_receive_request_result{true};
    util::Result<void> displayAddress(const CTxDestination&) override { return {}; }
    bool lockCoin(const COutPoint&, const bool) override { return true; }
    bool unlockCoin(const COutPoint&) override { return true; }
    bool isLockedCoin(const COutPoint&) override { return false; }
    void listLockedCoins(std::vector<COutPoint>& outputs) override { outputs.clear(); }
    util::Result<wallet::CreatedTransactionResult> createTransaction(const std::vector<wallet::CRecipient>& recipients,
                                                    const wallet::CCoinControl& coin_control,
                                                    bool sign,
                                                    std::optional<unsigned int>) override
    {
        create_transaction_sign_args.push_back(sign);
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
    void commitTransaction(CTransactionRef, interfaces::WalletValueMap, interfaces::WalletOrderForm) override
    {
        ++commit_calls;
    }
    bool transactionCanBeBumped(const Txid&) override { return can_bump_transaction; }
    bool createBumpTransaction(const Txid& txid,
                               const wallet::CCoinControl&,
                               std::vector<bilingual_str>&,
                               CAmount& old_fee,
                               CAmount& new_fee,
                               CMutableTransaction& mtx) override
    {
        ++create_bump_calls;
        old_fee = 100;
        new_fee = 200;
        mtx.vin.emplace_back(COutPoint{txid, 0});
        return true;
    }
    bool signBumpTransaction(CMutableTransaction&) override
    {
        ++sign_bump_calls;
        return sign_bump_result;
    }
    bool commitBumpTransaction(const Txid&, CMutableTransaction&&, std::vector<bilingual_str>&, Txid& bumped_txid) override
    {
        ++commit_bump_calls;
        bumped_txid = Txid::FromUint256(uint256::ONE);
        return commit_bump_result;
    }
    std::optional<common::PSBTError> fillPSBT(const common::PSBTFillOptions& options,
                                              size_t* n_signed,
                                              PartiallySignedTransaction& psbtx,
                                              bool& complete) override
    {
        return fill_psbt_fn(options, n_signed, psbtx, complete);
    }
    CAmount getBalance() override { return balance; }
    CAmount getAvailableBalance(const wallet::CCoinControl&) override { return balance; }
    bool txinIsMine(const CTxIn& txin) override { return txin_is_mine_fn(txin); }
    bool txoutIsMine(const CTxOut& txout) override { return txout_is_mine_fn(txout); }
    bool tryGetTxStatus(const Txid& txid, interfaces::WalletTxStatus&, int&, int64_t&) override
    {
        return known_txids.contains(txid);
    }
    CAmount getDebit(const CTxIn&) override { return 0; }
    CAmount getCredit(const CTxOut&) override { return 0; }
    CoinsList listCoins() override { return {}; }
    std::vector<interfaces::WalletTxOut> getCoins(const std::vector<COutPoint>&) override { return {}; }
    CAmount getRequiredFee(unsigned int) override { return 0; }
    CAmount getMinimumFee(unsigned int, const wallet::CCoinControl&, int*, FeeReason*) override { return 0; }
    unsigned int getConfirmTarget() override { return 6; }
    bool hdEnabled() override { return true; }
    bool canGetAddresses() override { return true; }
    bool privateKeysDisabled() override { return private_keys_disabled; }
    bool taprootEnabled() override { return taproot_enabled; }
    bool hasExternalSigner() override { return external_signer; }
    OutputType getDefaultAddressType() override { return default_address_type; }
    CAmount getDefaultMaxTxFee() override { return COIN; }
    void remove() override {}
    std::unique_ptr<interfaces::Handler> handleUnload(UnloadFn) override { return MakeNoopHandler(); }
    std::unique_ptr<interfaces::Handler> handleShowProgress(ShowProgressFn) override { return MakeNoopHandler(); }
    std::unique_ptr<interfaces::Handler> handleStatusChanged(StatusChangedFn) override { return MakeNoopHandler(); }
    std::unique_ptr<interfaces::Handler> handleAddressBookChanged(AddressBookChangedFn) override { return MakeNoopHandler(); }
    std::unique_ptr<interfaces::Handler> handleTransactionChanged(TransactionChangedFn) override { return MakeNoopHandler(); }
    std::unique_ptr<interfaces::Handler> handleCanGetAddressesChanged(CanGetAddressesChangedFn) override { return MakeNoopHandler(); }
};

WalletModelHarness<FakePasswordWallet> MakePasswordWalletModel(interfaces::Node* node = nullptr)
{
    auto wallet = std::make_unique<FakePasswordWallet>();
    FakePasswordWallet* const wallet_view{wallet.get()};
    return {wallet_view, std::make_unique<WalletQmlModel>(std::move(wallet), node)};
}

void SetPasswordRecipient(WalletQmlModel& model, qint64 satoshis)
{
    auto* recipient = model.sendRecipientList()->currentRecipient();
    QVERIFY(recipient != nullptr);

    recipient->address()->setAddress(VALID_MAINNET_ADDRESS, 0);
    recipient->amount()->setSatoshi(satoshis);

    QVERIFY2(recipient->isValid(), "Recipient must be valid before preparing a transaction");
}

PartiallySignedTransaction MakeReviewPsbt()
{
    CMutableTransaction previous_tx;
    previous_tx.vin.emplace_back(COutPoint{Txid::FromUint256(uint256::ONE), 0});
    previous_tx.vout.emplace_back(2'000, CScript{} << OP_DROP << OP_TRUE);
    const CTransactionRef previous{MakeTransactionRef(previous_tx)};

    CMutableTransaction tx;
    tx.vin.emplace_back(COutPoint{previous->GetHash(), 0});
    tx.vout.emplace_back(1'500, GetScriptForDestination(DecodeDestination(VALID_MAINNET_ADDRESS.toStdString())));

    PartiallySignedTransaction psbt{tx};
    psbt.inputs[0].non_witness_utxo = previous;
    return psbt;
}

CMutableTransaction UnsignedTx(const PartiallySignedTransaction& psbt)
{
    return *psbt.GetUnsignedTx();
}

COutPoint FirstInputPrevout(const PartiallySignedTransaction& psbt)
{
    return UnsignedTx(psbt).vin[0].prevout;
}

void CompleteReviewPsbt(PartiallySignedTransaction& psbt)
{
    psbt.inputs[0].final_script_sig = CScript{} << std::vector<unsigned char>{};
}

QString WritePsbt(const PartiallySignedTransaction& psbt, const QTemporaryDir& temp_dir, const QString& name)
{
    const QString path{temp_dir.filePath(name)};
    if (!PsbtQmlModel::SavePsbtToFile(psbt, path).isEmpty()) {
        return {};
    }
    return path;
}
} // namespace

class WalletQmlModelTests : public QObject
{
    Q_OBJECT

private:
    std::unique_ptr<ECC_Context> m_ecc_context;

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();
    void feeTargetIndex_mapsStandardTargets();
    void estimatedFeeForTarget_returnsEmptyWhenUnavailable();
    void scheduleFeeEstimates_populatesFormattedEstimates();
    void scheduleFeeEstimates_fallsBackWhenNetworkFeeEstimatesUnavailable();
    void scheduleFeeEstimates_usesStaticRegtestFeeOverride();
    void scheduleFeeEstimates_usesCustomFeeRateWhenEnabled();
    void scheduleFeeEstimates_estimatesWhenAmountWouldExceedBalanceWithFee();
    void sendAmountExhaustsBalance_requiresFeeBuffer();
    void sendAmountExhaustsBalance_usesCoinControlAvailableBalance();
    void scheduleFeeEstimates_usesDummyPreviewChangeDestination();
    void prepareTransaction_usesStaticRegtestFeeOverride();
    void prepareTransaction_usesCustomFeeRateWithoutRegtestOverride();
    void prepareTransaction_reassignsAmountWhenFeeIncluded();
    void walletQmlModelTransaction_reassignAmounts_excludesChangeOutput();
    void scheduleFeeEstimates_usesSelectedCoinsInCoinControl();
    void prepareTransaction_disallowsOtherInputsWhenCoinsSelected();
    void scheduleFeeEstimates_debouncesRapidRestarts();
    void scheduleFeeEstimates_invalidatesStalePreviewState();
    void transactionChangedEmitsBalanceChanged();
    void setCurrentPaymentRequestAddressUsesAddressListLabel();
    void commitPaymentRequestUsesSelectedAddressType();
    void usePaymentRequestAsTemplatePreservesAddressType();
    void commitPaymentRequestOnLockedWalletSignalsNeedsUnlock();
    void commitPaymentRequestWithPassphraseUnlocksRetriesAndRelocks();
    void commitPaymentRequestWithPassphraseWrongPasswordSurfacesError();
    void availableReceiveAddressTypesHideUnavailableTaproot();
    void receiveAddressTypeDefaultPersistsPerWallet();
    void removeReceiveRequestRemovesPendingActivityRow();
    void activityDetailsSelectLowestOutputIndex();
    void activityDetailsPreferOutgoingForSelfPayment();
    void editedReceiveRequestLabelShownInActivityRow();
    void editedRequestSyncsAddressBookLabel();
    void requestSaveLeavesUneditedAddressBookLabelAlone();
    void editedAddressBookLabelSyncsRequestLabel();
    void requestSaveLeavesSiblingRequestLabelsAlone();
    void labelSyncSkipsInMemoryUpdateWhenPersistFails();
    void usedAddressReceiveRequestRowIsFlaggedAndUntracked();
    void paidPendingRequestBecomesUsedAddressRequestLive();
    void paymentMarksEveryPendingRequestForAddressUsedLive();
    void reloadMarksEveryRequestForPaidAddressUsed();
    void prepareTransactionOnLockedWalletRequiresPassword();
    void prepareTransactionWithPrivateKeysDisabledDoesNotRequirePassword();
    void sendRecipientRejectsDustAmount();
    void sendRecipientUsesNodeDustRelayFee();
    void prepareTransactionRejectsDuplicateRecipientsBeforeUnlock();
    void prepareTransactionWithPassphraseForwardsUtf8Bytes();
    void prepareTransactionWithPassphraseRelocksWhenRecipientsInvalid();
    void prepareTransactionWithPassphraseRequiresCompleteMultiRecipient();
    void prepareTransactionWithPassphraseRelocksWhenCustomFeeInvalid();
    void prepareTransactionWithPassphraseReportsCreateErrorAndRelocks();
    void sendTransactionCommitsPreparedTransactionWithoutUnlockingAgain();
    void sendTransactionClearsSelectedCoins();
    void clearingRecipientsClearsSelectedCoins();
    void saveCurrentTransactionAsPsbt_savesUnsignedPreparedTransaction();
    void importPsbtFromFile_opensOwnedUnsignedPsbtWithoutSigning();
    void importPsbtFromFile_opensWatchOnlyUnsignedPsbtForReviewOnly();
    void saveCurrentTransactionAsPsbt_preservesImportedMetadata();
    void saveReviewOnlyPsbt_preservesOriginalWithoutDerivationMetadata();
    void discardCurrentTransaction_clearsReviewState();
    void sendImportedPsbtWithPassphraseSignsOnceAndRelocks();
    void externalSignerApprovalSignsImportedPsbtOnlyOnce();
    void externalSignerApprovalKeepsIncompleteSignedPsbt();
    void importPsbtFromFile_opensForeignUnsignedPsbtForReviewOnly();
    void importPsbtFromFile_broadcastsCompleteForeignMultisigPsbt();
    void saveCompleteBroadcastableImportedPsbt_preservesOriginalPsbt();
    void importPsbtFromFile_skipsZeroValueOpReturnOutputs();
    void importPsbtFromFile_opensUnsignedMultisigPsbtForReviewOnly();
    void importPsbtFromFile_blocksBroadcastWhenFeeIsInvalid();
    void importPsbtFromFile_returnsTransactionAlreadyKnownWhenTxIsInWallet();
    void sendTransactionWithPrivateKeysDisabledDoesNotCommit();
    void bumpTransactionOnLockedWalletRequiresPassword();
    void bumpTransactionWithPassphraseUnlocksCommitsAndRelocks();
    void bumpTransactionWithWrongPassphraseDoesNotSign();
    void displayNameDefaultsToWalletName();
    void detailPropertiesReflectWalletCapabilities();
    void encryptWalletUpdatesSecurityState();
    void changeWalletPassphraseForwardsPasswords();
    void backupWalletForwardsPath();
    void signVerifyMessageRejectsNonP2PKHAddress();
    void signVerifyMessageSignsWithLegacyP2PKHAddress();
    void signVerifyMessageSignsEmptyMessage();
    void signVerifyMessageWithPassphraseUnlocksSignsAndRelocks();
    void signVerifyMessageWrongPassphraseSurfacesErrorAndDoesNotRelock();
    void signVerifyMessageSurfacesSigningFailure();
    void signVerifyMessageWithoutWalletSurfacesError();
    void signVerifyMessageClearResetsState();
    void signVerifyMessageVerifiesValidSignature();
    void signVerifyMessageVerifyRejectsEmptySignature();
};

void WalletQmlModelTests::initTestCase()
{
    SelectParams(ChainType::MAIN);
    m_ecc_context = std::make_unique<ECC_Context>();
}

void WalletQmlModelTests::cleanupTestCase()
{
    m_ecc_context.reset();
}

void WalletQmlModelTests::displayNameDefaultsToWalletName()
{
    auto [wallet, model] = MakePasswordWalletModel();

    QCOMPARE(model->displayName(), QString("fake-wallet"));
    model->setDisplayName("Personal");
    QCOMPARE(model->displayName(), QString("Personal"));
}

void WalletQmlModelTests::detailPropertiesReflectWalletCapabilities()
{
    auto [wallet, model] = MakePasswordWalletModel();

    wallet->private_keys_disabled = true;
    wallet->external_signer = true;
    QCOMPARE(model->keyScheme(), QString("Watch-only"));
    QCOMPARE(model->privateKeysStatus(), QString("Disabled"));
    QCOMPARE(model->externalSignerStatus(), QString("Enabled"));
    QVERIFY(!model->canManagePassphrase());

    wallet->private_keys_disabled = false;

    QCOMPARE(model->keyScheme(), QString("Single-key"));
    QCOMPARE(model->privateKeysStatus(), QString("Enabled"));
    QVERIFY(model->canManagePassphrase());
}

void WalletQmlModelTests::encryptWalletUpdatesSecurityState()
{
    auto wallet = std::make_unique<FakePasswordWallet>();
    wallet->encrypted = false;
    wallet->locked = false;
    FakePasswordWallet* raw_wallet = wallet.get();
    auto model = std::make_unique<WalletQmlModel>(std::move(wallet));

    QVERIFY(model->encryptWallet("secret"));
    QCOMPARE(raw_wallet->encrypt_calls, 1);
    QVERIFY(model->isEncrypted());
    QVERIFY(model->isLocked());
    QVERIFY(model->settingsError().isEmpty());
}

void WalletQmlModelTests::changeWalletPassphraseForwardsPasswords()
{
    auto [wallet, model] = MakePasswordWalletModel();

    QVERIFY(model->changeWalletPassphrase("secret", "new-secret"));
    QCOMPARE(wallet->change_passphrase_calls, 1);
    QCOMPARE(wallet->changed_passphrases.size(), size_t{1});
    QCOMPARE(wallet->changed_passphrases.front().first, std::string("secret"));
    QCOMPARE(wallet->changed_passphrases.front().second, std::string("new-secret"));

    QVERIFY(!model->changeWalletPassphrase("wrong", "new-secret"));
    QCOMPARE(model->settingsError(), QString("The current wallet password was incorrect."));
}

void WalletQmlModelTests::backupWalletForwardsPath()
{
    auto [wallet, model] = MakePasswordWalletModel();

    QVERIFY(model->backupWallet("/tmp/fake-wallet.bak"));
    QCOMPARE(wallet->backup_calls, 1);
    QCOMPARE(wallet->last_backup_path, std::string("/tmp/fake-wallet.bak"));
    QVERIFY(model->settingsError().isEmpty());
}

void WalletQmlModelTests::availableReceiveAddressTypesHideUnavailableTaproot()
{
    auto [wallet, model] = MakePasswordWalletModel();

    QVariantList types = model->availableReceiveAddressTypes();
    QCOMPARE(types.size(), 4);
    QCOMPARE(types.front().toMap().value("id").toString(), QString("bech32m"));

    wallet->taproot_enabled = false;
    types = model->availableReceiveAddressTypes();
    QCOMPARE(types.size(), 3);
    for (const QVariant& type : types) {
        QVERIFY(type.toMap().value("id").toString() != QString("bech32m"));
    }
}

void WalletQmlModelTests::receiveAddressTypeDefaultPersistsPerWallet()
{
    QSettings settings;
    settings.remove("receiveAddressTypes/fake-wallet");
    settings.sync();

    auto [wallet, model] = MakePasswordWalletModel();
    wallet->default_address_type = OutputType::BECH32;

    QCOMPARE(model->defaultReceiveAddressType(), QString("bech32"));

    model->setDefaultReceiveAddressType("p2sh-segwit");
    QCOMPARE(model->defaultReceiveAddressType(), QString("p2sh-segwit"));

    model->setDefaultReceiveAddressType("bech32m");
    QCOMPARE(model->defaultReceiveAddressType(), QString("bech32m"));

    wallet->taproot_enabled = false;
    QCOMPARE(model->defaultReceiveAddressType(), QString("bech32"));

    settings.remove("receiveAddressTypes/fake-wallet");
    settings.sync();
}

void WalletQmlModelTests::feeTargetIndex_mapsStandardTargets()
{
    WalletQmlModel model;

    QCOMPARE(model.feeTargetIndex(1), 0);
    QCOMPARE(model.feeTargetIndex(2), 1);
    QCOMPARE(model.feeTargetIndex(6), 2);
    QCOMPARE(model.feeTargetIndex(42), 1);
}

void WalletQmlModelTests::estimatedFeeForTarget_returnsEmptyWhenUnavailable()
{
    WalletQmlModel model;

    QCOMPARE(model.estimatedFeeForTarget(1), QString());
    QCOMPARE(model.estimatedFeeForTarget(2), QString());
    QCOMPARE(model.estimatedFee(), QString());
}

void WalletQmlModelTests::scheduleFeeEstimates_populatesFormattedEstimates()
{
    auto [wallet, model] = MakeWalletModel();
    SetValidRecipient(*model);

    QSemaphore release_first_call;
    std::atomic<bool> first_call_started{false};
    std::atomic<bool> first_call_blocked{false};
    ThreadSafeCallLog<FeeEstimateCall> calls;

    [[maybe_unused]] auto verify_wallet = wallet->VerifyOnExit();
    wallet->ExpectNoCalls(wallet->calls.getNewDestination);
    wallet->create_transaction_fn = [&](const std::vector<wallet::CRecipient>& recipients,
                                        const wallet::CCoinControl& coin_control,
                                        bool sign,
                                        int& change_pos,
                                        CAmount& fee) -> util::Result<CTransactionRef> {
        calls.Append({
            coin_control.m_confirm_target.value_or(0),
            sign,
            coin_control.HasSelected() || !coin_control.ListSelected().empty(),
            coin_control.m_feerate ? std::optional<CAmount>{coin_control.m_feerate->GetFeePerK()} : std::nullopt,
            recipients.size(),
        });
        change_pos = -1;
        fee = coin_control.m_confirm_target.value_or(0) * 100;
        if (!first_call_blocked.exchange(true)) {
            first_call_started = true;
            release_first_call.acquire();
        }
        return util::Result<CTransactionRef>{MakeTransactionRef(CMutableTransaction{})};
    };

    model->scheduleFeeEstimates();

    QTRY_VERIFY_WITH_TIMEOUT(first_call_started.load(), FEE_ESTIMATE_TIMEOUT_MS);
    QTRY_VERIFY_WITH_TIMEOUT(model->feeEstimatePending(), FEE_ESTIMATE_TIMEOUT_MS);
    QCOMPARE(model->estimatedFeeForTarget(2), QString());

    release_first_call.release();

    QTRY_VERIFY_WITH_TIMEOUT(calls.Size() == 3, FEE_ESTIMATE_TIMEOUT_MS);
    CompareFeeEstimateCalls(calls.Snapshot(), {
                                                  {1, false, false, std::nullopt, 1},
                                                  {2, false, false, std::nullopt, 1},
                                                  {6, false, false, std::nullopt, 1},
                                              });
    QTRY_VERIFY_WITH_TIMEOUT(!model->feeEstimatePending(), FEE_ESTIMATE_TIMEOUT_MS);
    QCOMPARE(model->estimatedFeeForTarget(1), QStringLiteral("0.00000100 ₿"));
    QCOMPARE(model->estimatedFeeForTarget(2), QStringLiteral("0.00000200 ₿"));
    QCOMPARE(model->estimatedFeeForTarget(6), QStringLiteral("0.00000600 ₿"));
    QCOMPARE(model->estimatedFee(), QStringLiteral("0.00000200 ₿"));
}

void WalletQmlModelTests::scheduleFeeEstimates_fallsBackWhenNetworkFeeEstimatesUnavailable()
{
    auto [wallet, model] = MakeWalletModel();
    SetValidRecipient(*model);

    ThreadSafeCallLog<FeeEstimateCall> fallback_calls;

    [[maybe_unused]] auto verify_wallet = wallet->VerifyOnExit();
    wallet->ExpectNoCalls(wallet->calls.getNewDestination);
    wallet->ExpectAtLeast(wallet->calls.getRequiredFee, 3);
    wallet->create_transaction_fn = [&](const std::vector<wallet::CRecipient>& recipients,
                                        const wallet::CCoinControl& coin_control,
                                        bool sign,
                                        int& change_pos,
                                        CAmount& fee) -> util::Result<CTransactionRef> {
        change_pos = -1;

        if (!coin_control.m_feerate.has_value()) {
            return util::Error{Untranslated("fee estimation unavailable")};
        }

        fallback_calls.Append({
            coin_control.m_confirm_target.value_or(0),
            sign,
            coin_control.HasSelected() || !coin_control.ListSelected().empty(),
            coin_control.m_feerate->GetFeePerK(),
            recipients.size(),
        });
        fee = coin_control.m_feerate->GetFee(250);
        return util::Result<CTransactionRef>{MakeTransactionRef(CMutableTransaction{})};
    };

    model->scheduleFeeEstimates();

    QTRY_COMPARE_WITH_TIMEOUT(model->estimatedFeeForTarget(2), QStringLiteral("0.00000500 ₿"), FEE_ESTIMATE_TIMEOUT_MS);
    QTRY_VERIFY_WITH_TIMEOUT(!model->feeEstimatePending(), FEE_ESTIMATE_TIMEOUT_MS);
    QTRY_VERIFY_WITH_TIMEOUT(fallback_calls.Size() == 3, FEE_ESTIMATE_TIMEOUT_MS);
    QCOMPARE(model->estimatedFeeForTarget(1), QStringLiteral("0.00000750 ₿"));
    QCOMPARE(model->estimatedFeeForTarget(2), QStringLiteral("0.00000500 ₿"));
    QCOMPARE(model->estimatedFeeForTarget(6), QStringLiteral("0.00000250 ₿"));
    QCOMPARE(model->estimatedFee(), QStringLiteral("0.00000500 ₿"));
    CompareFeeEstimateCalls(fallback_calls.Snapshot(), {
                                                           {0, false, false, CAmount{3000}, 1},
                                                           {0, false, false, CAmount{2000}, 1},
                                                           {0, false, false, CAmount{1000}, 1},
                                                       });
}

void WalletQmlModelTests::scheduleFeeEstimates_usesStaticRegtestFeeOverride()
{
    ChainSelectionGuard chain_guard{ChainType::REGTEST};
    auto [wallet, model] = MakeWalletModel();
    SetValidRecipient(*model, VALID_REGTEST_ADDRESS);

    ThreadSafeCallLog<FeeEstimateCall> calls;

    [[maybe_unused]] auto verify_wallet = wallet->VerifyOnExit();
    wallet->ExpectNoCalls(wallet->calls.getNewDestination);
    wallet->ExpectNoCalls(wallet->calls.getRequiredFee);
    wallet->create_transaction_fn = [&](const std::vector<wallet::CRecipient>& recipients,
                                        const wallet::CCoinControl& coin_control,
                                        bool sign,
                                        int& change_pos,
                                        CAmount& fee) -> util::Result<CTransactionRef> {
        calls.Append({
            coin_control.m_confirm_target.value_or(0),
            sign,
            coin_control.HasSelected() || !coin_control.ListSelected().empty(),
            coin_control.m_feerate ? std::optional<CAmount>{coin_control.m_feerate->GetFeePerK()} : std::nullopt,
            recipients.size(),
        });
        change_pos = -1;

        if (!coin_control.m_feerate.has_value()) return util::Error{Untranslated("missing regtest fee override")};

        fee = coin_control.m_feerate->GetFee(250);
        return util::Result<CTransactionRef>{MakeTransactionRef(CMutableTransaction{})};
    };

    model->scheduleFeeEstimates();

    QTRY_COMPARE_WITH_TIMEOUT(model->estimatedFeeForTarget(2), QStringLiteral("0.00000250 ₿"), FEE_ESTIMATE_TIMEOUT_MS);
    QCOMPARE(model->estimatedFeeForTarget(1), QStringLiteral("0.00000250 ₿"));
    QCOMPARE(model->estimatedFeeForTarget(2), QStringLiteral("0.00000250 ₿"));
    QCOMPARE(model->estimatedFeeForTarget(6), QStringLiteral("0.00000250 ₿"));
    CompareFeeEstimateCalls(calls.Snapshot(), {
                                                  {0, false, false, CAmount{wallet::DEFAULT_TRANSACTION_MINFEE}, 1},
                                                  {0, false, false, CAmount{wallet::DEFAULT_TRANSACTION_MINFEE}, 1},
                                                  {0, false, false, CAmount{wallet::DEFAULT_TRANSACTION_MINFEE}, 1},
                                              });
}

void WalletQmlModelTests::scheduleFeeEstimates_usesCustomFeeRateWhenEnabled()
{
    auto [wallet, model] = MakeWalletModel();
    SetValidRecipient(*model);

    ThreadSafeCallLog<FeeEstimateCall> calls;

    [[maybe_unused]] auto verify_wallet = wallet->VerifyOnExit();
    wallet->ExpectNoCalls(wallet->calls.getNewDestination);
    wallet->create_transaction_fn = [&](const std::vector<wallet::CRecipient>& recipients,
                                        const wallet::CCoinControl& coin_control,
                                        bool sign,
                                        int& change_pos,
                                        CAmount& fee) -> util::Result<CTransactionRef> {
        calls.Append({
            coin_control.m_confirm_target.value_or(0),
            sign,
            coin_control.HasSelected() || !coin_control.ListSelected().empty(),
            coin_control.m_feerate ? std::optional<CAmount>{coin_control.m_feerate->GetFeePerK()} : std::nullopt,
            recipients.size(),
        });
        change_pos = -1;
        fee = coin_control.m_feerate.has_value()
            ? coin_control.m_feerate->GetFee(250)
            : coin_control.m_confirm_target.value_or(0) * 100;
        return util::Result<CTransactionRef>{MakeTransactionRef(CMutableTransaction{})};
    };

    model->setCustomFeeEnabled(true);
    model->setCustomFeeRate(QStringLiteral("2"));
    model->scheduleFeeEstimates();

    QTRY_COMPARE_WITH_TIMEOUT(model->estimatedFee(), QStringLiteral("0.00000500 ₿"), FEE_ESTIMATE_TIMEOUT_MS);
    const auto recorded_calls{calls.Snapshot()};
    QCOMPARE(recorded_calls.size(), 4U);
    for (const FeeEstimateCall& call : recorded_calls) {
        QVERIFY(!call.sign);
        QVERIFY(!call.selected_inputs);
        QCOMPARE(call.recipient_count, 1U);
        if (call.target == 0) {
            QVERIFY(call.fee_rate.has_value());
            QCOMPARE(*call.fee_rate, CAmount{2000});
        } else {
            QVERIFY(!call.fee_rate.has_value());
        }
    }
    const auto called_target = [&](unsigned int target) {
        return std::ranges::any_of(recorded_calls, [&](const FeeEstimateCall& call) { return call.target == target; });
    };
    QVERIFY(called_target(0));
    QVERIFY(called_target(1));
    QVERIFY(called_target(2));
    QVERIFY(called_target(6));
}

void WalletQmlModelTests::scheduleFeeEstimates_estimatesWhenAmountWouldExceedBalanceWithFee()
{
    auto [wallet, model] = MakeWalletModel();
    wallet->get_balance_fn = [] { return CAmount{50'000}; };
    wallet->get_available_balance_fn = [](const wallet::CCoinControl&) { return CAmount{50'000}; };
    SetValidRecipient(*model);
    auto* recipient = model->sendRecipientList()->currentRecipient();
    QVERIFY(recipient != nullptr);
    recipient->amount()->setSatoshi(49'850);
    QVERIFY(!model->sendAmountExhaustsBalance());
    QSignalSpy balance_exhausted_spy{model.get(), &WalletQmlModel::sendAmountExhaustsBalanceChanged};

    ThreadSafeCallLog<BalancePreviewCall> calls;

    [[maybe_unused]] auto verify_wallet = wallet->VerifyOnExit();
    wallet->ExpectNoCalls(wallet->calls.getRequiredFee);
    wallet->create_transaction_fn = [&](const std::vector<wallet::CRecipient>& recipients,
                                        const wallet::CCoinControl& coin_control,
                                        bool,
                                        int& change_pos,
                                        CAmount& fee) -> util::Result<CTransactionRef> {
        change_pos = -1;
        fee = coin_control.m_feerate.has_value()
            ? 50
            : coin_control.m_confirm_target.value_or(0) * 100;

        const CAmount total_amount = std::accumulate(recipients.begin(), recipients.end(), CAmount{0}, [](const CAmount total, const wallet::CRecipient& recipient) {
            return total + recipient.nAmount;
        });
        const bool subtracts_fee = std::any_of(recipients.begin(), recipients.end(), [](const wallet::CRecipient& recipient) {
            return recipient.fSubtractFeeFromAmount;
        });

        calls.Append({coin_control.m_confirm_target.value_or(0), total_amount, subtracts_fee});
        if (subtracts_fee) {
            return util::Result<CTransactionRef>{MakeTransactionRef(CMutableTransaction{})};
        }

        if (total_amount + fee <= 50'000) {
            return util::Result<CTransactionRef>{MakeTransactionRef(CMutableTransaction{})};
        }
        return util::Error{Untranslated("amount plus fee exceeds balance")};
    };

    model->scheduleFeeEstimates();

    QTRY_COMPARE_WITH_TIMEOUT(model->estimatedFeeForTarget(2), QStringLiteral("0.00000200 ₿"), FEE_ESTIMATE_TIMEOUT_MS);
    QTRY_VERIFY_WITH_TIMEOUT(model->sendAmountExhaustsBalance(), FEE_ESTIMATE_TIMEOUT_MS);
    QVERIFY(model->sendAmountExhaustsBalance());
    QVERIFY(balance_exhausted_spy.count() > 0);
    QTRY_VERIFY_WITH_TIMEOUT(calls.Size() >= 5, FEE_ESTIMATE_TIMEOUT_MS);
    const auto preview_calls{calls.Snapshot()};
    QVERIFY(std::ranges::any_of(preview_calls, [](const BalancePreviewCall& call) { return !call.subtracts_fee; }));
    QVERIFY(std::ranges::any_of(preview_calls, [](const BalancePreviewCall& call) { return call.subtracts_fee; }));
    QVERIFY(std::ranges::all_of(preview_calls, [](const BalancePreviewCall& call) { return call.total_amount == 49'850; }));
    QCOMPARE(model->sendRecipientList()->currentRecipient()->subtractFeeFromAmount(), false);
    QCOMPARE(model->estimatedFeeForTarget(1), QStringLiteral("0.00000100 ₿"));
    QCOMPARE(model->estimatedFeeForTarget(2), QStringLiteral("0.00000200 ₿"));
    QCOMPARE(model->estimatedFeeForTarget(6), QStringLiteral("0.00000600 ₿"));

    model->setFeeTargetBlocks(1);
    QCOMPARE(model->estimatedFee(), QStringLiteral("0.00000100 ₿"));
    QVERIFY(!model->sendAmountExhaustsBalance());

    model->setFeeTargetBlocks(6);
    QCOMPARE(model->estimatedFee(), QStringLiteral("0.00000600 ₿"));
    QVERIFY(model->sendAmountExhaustsBalance());

    model->setCustomFeeEnabled(true);
    model->setCustomFeeRate(QStringLiteral("1"));
    QTRY_COMPARE_WITH_TIMEOUT(model->estimatedFee(), QStringLiteral("0.00000050 ₿"), FEE_ESTIMATE_TIMEOUT_MS);
    QVERIFY(!model->sendAmountExhaustsBalance());
}

void WalletQmlModelTests::sendAmountExhaustsBalance_requiresFeeBuffer()
{
    auto [wallet, model] = MakePasswordWalletModel();
    wallet->balance = 50'000;
    SetPasswordRecipient(*model, 50'000);

    QVERIFY(model->sendAmountExhaustsBalance());

    auto* recipient = model->sendRecipientList()->currentRecipient();
    QVERIFY(recipient != nullptr);
    QSignalSpy spy{model.get(), &WalletQmlModel::sendAmountExhaustsBalanceChanged};
    recipient->amount()->setSatoshi(49'999);
    QVERIFY(!model->sendAmountExhaustsBalance());
    QVERIFY(spy.count() > 0);

    recipient->amount()->setSatoshi(50'000);
    QVERIFY(model->sendAmountExhaustsBalance());
    recipient->setSubtractFeeFromAmount(true);
    QVERIFY(!model->sendAmountExhaustsBalance());

    recipient->setSubtractFeeFromAmount(false);
    recipient->amount()->setSatoshi(49'000);
    QVERIFY(!model->sendAmountExhaustsBalance());
    model->sendRecipientList()->add();
    auto* second_recipient = model->sendRecipientList()->currentRecipient();
    QVERIFY(second_recipient != nullptr);
    second_recipient->address()->setAddress(VALID_MAINNET_P2SH_ADDRESS, 0);
    second_recipient->amount()->setSatoshi(1'000);
    QVERIFY(model->sendAmountExhaustsBalance());
    model->sendRecipientList()->remove();
    QVERIFY(!model->sendAmountExhaustsBalance());
}

void WalletQmlModelTests::sendAmountExhaustsBalance_usesCoinControlAvailableBalance()
{
    auto [wallet, model] = MakeWalletModel();
    SetValidRecipient(*model);

    const COutPoint selected_outpoint{Txid::FromUint256(uint256::ONE), 1};
    bool create_transaction_called{false};

    [[maybe_unused]] auto verify_wallet = wallet->VerifyOnExit();
    wallet->ExpectNoCalls(wallet->calls.getBalance);
    bool selected_coin_control_valid{true};
    bool unselected_coin_control_valid{true};
    wallet->get_available_balance_fn = [&](const wallet::CCoinControl& coin_control) {
        if (coin_control.HasSelected()) {
            selected_coin_control_valid &= coin_control.IsSelected(selected_outpoint);
            selected_coin_control_valid &= !coin_control.m_allow_other_inputs;
            return CAmount{20'000};
        }

        unselected_coin_control_valid &= coin_control.m_allow_other_inputs;
        return CAmount{100'000};
    };

    wallet->create_transaction_fn = [&](const std::vector<wallet::CRecipient>&,
                                        const wallet::CCoinControl&,
                                        bool,
                                        int& change_pos,
                                        CAmount& fee) -> util::Result<CTransactionRef> {
        create_transaction_called = true;
        change_pos = -1;
        fee = 200;
        return util::Result<CTransactionRef>{MakeTransactionRef(CMutableTransaction{})};
    };

    QVERIFY(!model->sendAmountExhaustsBalance());

    QSignalSpy spy{model.get(), &WalletQmlModel::sendAmountExhaustsBalanceChanged};
    model->selectCoin(selected_outpoint);
    QCOMPARE(spy.count(), 1);
    QVERIFY(model->sendAmountExhaustsBalance());
    QVERIFY(!model->prepareTransaction());
    QVERIFY(!create_transaction_called);
    QCOMPARE(model->transactionError(), QStringLiteral("Selected inputs do not cover the amount plus fee"));

    model->unselectCoin(selected_outpoint);
    QCOMPARE(spy.count(), 2);
    QVERIFY(!model->sendAmountExhaustsBalance());
    QVERIFY(model->prepareTransaction());
    QVERIFY(create_transaction_called);

    model->selectCoin(selected_outpoint);
    QCOMPARE(spy.count(), 3);
    QVERIFY(model->sendAmountExhaustsBalance());
    model->clearSelectedCoins();
    QCOMPARE(spy.count(), 4);
    QVERIFY(!model->sendAmountExhaustsBalance());

    model->clearSelectedCoins();
    QCOMPARE(spy.count(), 4);
    QVERIFY(selected_coin_control_valid);
    QVERIFY(unselected_coin_control_valid);
}

void WalletQmlModelTests::scheduleFeeEstimates_usesDummyPreviewChangeDestination()
{
    auto [wallet, model] = MakeWalletModel();
    SetValidRecipient(*model);

    std::atomic<bool> saw_missing_change{false};
    std::atomic<bool> saw_wrong_change_type{false};

    [[maybe_unused]] auto verify_wallet = wallet->VerifyOnExit();
    wallet->ExpectNoCalls(wallet->calls.getNewDestination);
    wallet->create_transaction_fn = [&](const std::vector<wallet::CRecipient>&,
                                        const wallet::CCoinControl& coin_control,
                                        bool,
                                        int& change_pos,
                                        CAmount& fee) -> util::Result<CTransactionRef> {
        if (std::get_if<CNoDestination>(&coin_control.destChange)) {
            saw_missing_change = true;
        }
        if (!std::get_if<WitnessV0KeyHash>(&coin_control.destChange)) {
            saw_wrong_change_type = true;
        }
        change_pos = -1;
        fee = coin_control.m_confirm_target.value_or(0) * 100;
        return util::Result<CTransactionRef>{MakeTransactionRef(CMutableTransaction{})};
    };

    model->scheduleFeeEstimates();

    QTRY_COMPARE_WITH_TIMEOUT(wallet->calls.createTransaction.load(), 3, FEE_ESTIMATE_TIMEOUT_MS);
    QVERIFY(!saw_missing_change.load());
    QVERIFY(!saw_wrong_change_type.load());
}

void WalletQmlModelTests::prepareTransaction_usesStaticRegtestFeeOverride()
{
    ChainSelectionGuard chain_guard{ChainType::REGTEST};
    auto [wallet, model] = MakeWalletModel();
    SetValidRecipient(*model, VALID_REGTEST_ADDRESS);

    std::optional<FeeEstimateCall> call;

    [[maybe_unused]] auto verify_wallet = wallet->VerifyOnExit();
    wallet->ExpectNoCalls(wallet->calls.getRequiredFee);
    wallet->create_transaction_fn = [&](const std::vector<wallet::CRecipient>& recipients,
                                        const wallet::CCoinControl& coin_control,
                                        bool sign,
                                        int& change_pos,
                                        CAmount& fee) -> util::Result<CTransactionRef> {
        call = FeeEstimateCall{
            coin_control.m_confirm_target.value_or(0),
            sign,
            coin_control.HasSelected() || !coin_control.ListSelected().empty(),
            coin_control.m_feerate ? std::optional<CAmount>{coin_control.m_feerate->GetFeePerK()} : std::nullopt,
            recipients.size(),
        };
        change_pos = -1;

        if (!coin_control.m_feerate.has_value()) return util::Error{Untranslated("missing regtest fee override")};

        fee = coin_control.m_feerate->GetFee(250);
        return util::Result<CTransactionRef>{MakeTransactionRef(CMutableTransaction{})};
    };

    QVERIFY(model->prepareTransaction());
    QVERIFY(model->currentTransaction() != nullptr);
    QCOMPARE(model->currentTransaction()->feeAmount()->satoshi(), CAmount{250});
    QVERIFY(call.has_value());
    CompareFeeEstimateCalls({*call}, {
                                         {0, true, false, CAmount{wallet::DEFAULT_TRANSACTION_MINFEE}, 1},
                                     });
}

void WalletQmlModelTests::prepareTransaction_usesCustomFeeRateWithoutRegtestOverride()
{
    ChainSelectionGuard chain_guard{ChainType::REGTEST};
    auto [wallet, model] = MakeWalletModel();
    SetValidRecipient(*model, VALID_REGTEST_ADDRESS);

    std::optional<FeeEstimateCall> call;

    [[maybe_unused]] auto verify_wallet = wallet->VerifyOnExit();
    wallet->ExpectNoCalls(wallet->calls.getRequiredFee);
    wallet->create_transaction_fn = [&](const std::vector<wallet::CRecipient>& recipients,
                                        const wallet::CCoinControl& coin_control,
                                        bool sign,
                                        int& change_pos,
                                        CAmount& fee) -> util::Result<CTransactionRef> {
        call = FeeEstimateCall{
            coin_control.m_confirm_target.value_or(0),
            sign,
            coin_control.HasSelected() || !coin_control.ListSelected().empty(),
            coin_control.m_feerate ? std::optional<CAmount>{coin_control.m_feerate->GetFeePerK()} : std::nullopt,
            recipients.size(),
        };
        change_pos = -1;

        if (!coin_control.m_feerate.has_value()) {
            return util::Error{Untranslated("missing custom fee override")};
        }

        fee = coin_control.m_feerate->GetFee(250);
        return util::Result<CTransactionRef>{MakeTransactionRef(CMutableTransaction{})};
    };

    model->setCustomFeeEnabled(true);
    model->setCustomFeeRate(QStringLiteral("2"));

    QVERIFY(model->prepareTransaction());
    QVERIFY(model->currentTransaction() != nullptr);
    QCOMPARE(model->currentTransaction()->feeAmount()->satoshi(), CAmount{500});
    QVERIFY(call.has_value());
    CompareFeeEstimateCalls({*call}, {
                                         {0, true, false, CAmount{2000}, 1},
                                     });
}

void WalletQmlModelTests::prepareTransaction_reassignsAmountWhenFeeIncluded()
{
    auto [wallet, model] = MakeWalletModel();
    SetValidRecipient(*model, VALID_MAINNET_ADDRESS, /*subtract_fee_from_amount=*/true);

    bool saw_subtract_fee_from_amount{false};
    bool saw_wrong_recipient_count{false};
    wallet->create_transaction_fn = [&](const std::vector<wallet::CRecipient>& recipients,
                                        const wallet::CCoinControl&,
                                        bool,
                                        int& change_pos,
                                        CAmount& fee) -> util::Result<CTransactionRef> {
        if (recipients.size() != 1U) {
            saw_wrong_recipient_count = true;
            return util::Error{Untranslated("unexpected recipient count")};
        }
        saw_subtract_fee_from_amount = recipients.at(0).fSubtractFeeFromAmount;
        change_pos = -1;
        fee = 200;

        CMutableTransaction tx;
        tx.vout.emplace_back(/*nValue=*/49'800, CScript{});
        return util::Result<CTransactionRef>{MakeTransactionRef(std::move(tx))};
    };

    QVERIFY(model->prepareTransaction());
    QVERIFY(!saw_wrong_recipient_count);
    QVERIFY(saw_subtract_fee_from_amount);
    QVERIFY(model->currentTransaction() != nullptr);
    QCOMPARE(model->currentTransaction()->amountAmount()->satoshi(), CAmount{49'800});
    QCOMPARE(model->currentTransaction()->feeAmount()->satoshi(), CAmount{200});
    QCOMPARE(model->currentTransaction()->totalAmount()->satoshi(), CAmount{50'000});
    QCOMPARE(model->currentTransaction()->amount(), QString::fromUtf8("0.00049800 \xe2\x82\xbf"));
    QCOMPARE(model->currentTransaction()->fee(), QString::fromUtf8("0.00000200 \xe2\x82\xbf"));
    QCOMPARE(model->currentTransaction()->total(), QString::fromUtf8("0.00050000 \xe2\x82\xbf"));
    QCOMPARE(model->currentTransaction()->getTotalTransactionAmount(), CAmount{50'000});
}

void WalletQmlModelTests::walletQmlModelTransaction_reassignAmounts_excludesChangeOutput()
{
    SendRecipientsListModel recipients;
    auto* recipient = recipients.currentRecipient();
    QVERIFY(recipient != nullptr);

    recipient->address()->setAddress(VALID_MAINNET_ADDRESS, 0);
    recipient->amount()->setSatoshi(50'000);

    WalletQmlModelTransaction transaction{&recipients};
    QSignalSpy amount_changed_spy{transaction.amountAmount(), &BitcoinAmount::amountChanged};
    QSignalSpy total_changed_spy{transaction.totalAmount(), &BitcoinAmount::amountChanged};
    QSignalSpy fee_changed_spy{transaction.feeAmount(), &BitcoinAmount::amountChanged};

    CMutableTransaction tx;
    tx.vout.emplace_back(/*nValue=*/49'800, CScript{});
    tx.vout.emplace_back(/*nValue=*/1'000, CScript{}); // change
    transaction.setWtx(MakeTransactionRef(std::move(tx)));

    transaction.setTransactionFee(200);
    QCOMPARE(fee_changed_spy.count(), 1);
    QCOMPARE(total_changed_spy.count(), 1);
    QCOMPARE(transaction.totalAmount()->satoshi(), CAmount{50'200});
    QCOMPARE(transaction.total(), QString::fromUtf8("0.00050200 \xe2\x82\xbf"));

    transaction.reassignAmounts(/*nChangePosRet=*/1);

    QCOMPARE(amount_changed_spy.count(), 1);
    QCOMPARE(total_changed_spy.count(), 2);
    QCOMPARE(transaction.amountAmount()->satoshi(), CAmount{49'800});
    QCOMPARE(transaction.feeAmount()->satoshi(), CAmount{200});
    QCOMPARE(transaction.totalAmount()->satoshi(), CAmount{50'000});
    QCOMPARE(transaction.amount(), QString::fromUtf8("0.00049800 \xe2\x82\xbf"));
    QCOMPARE(transaction.fee(), QString::fromUtf8("0.00000200 \xe2\x82\xbf"));
    QCOMPARE(transaction.total(), QString::fromUtf8("0.00050000 \xe2\x82\xbf"));
    QCOMPARE(transaction.getTotalTransactionAmount(), CAmount{50'000});
}

void WalletQmlModelTests::scheduleFeeEstimates_usesSelectedCoinsInCoinControl()
{
    auto [wallet, model] = MakeWalletModel();
    SetValidRecipient(*model);

    const COutPoint selected_outpoint{Txid{}, 5};
    ThreadSafeCallLog<SelectedCoinEstimateCall> calls;

    [[maybe_unused]] auto verify_wallet = wallet->VerifyOnExit();
    wallet->ExpectNoCalls(wallet->calls.getNewDestination);
    wallet->create_transaction_fn = [&](const std::vector<wallet::CRecipient>&,
                                        const wallet::CCoinControl& coin_control,
                                        bool sign,
                                        int& change_pos,
                                        CAmount& fee) -> util::Result<CTransactionRef> {
        const auto selected = coin_control.ListSelected();
        calls.Append({
            coin_control.m_confirm_target.value_or(0),
            sign,
            coin_control.HasSelected() && coin_control.IsSelected(selected_outpoint) && selected.size() == 1U && selected.front() == selected_outpoint,
            coin_control.m_allow_other_inputs,
            selected.size(),
        });
        change_pos = -1;
        fee = coin_control.m_confirm_target.value_or(0) * 200;
        return util::Result<CTransactionRef>{MakeTransactionRef(CMutableTransaction{})};
    };

    model->selectCoin(selected_outpoint);

    QTRY_COMPARE_WITH_TIMEOUT(calls.Size(), size_t{3}, FEE_ESTIMATE_TIMEOUT_MS);
    const auto selected_calls{calls.Snapshot()};
    constexpr std::array expected_targets{1U, 2U, 6U};
    for (size_t index{0}; index < selected_calls.size(); ++index) {
        QCOMPARE(selected_calls[index].target, expected_targets[index]);
        QVERIFY(!selected_calls[index].sign);
        QVERIFY(selected_calls[index].expected_coin_selected);
        QVERIFY(!selected_calls[index].allow_other_inputs);
        QCOMPARE(selected_calls[index].selected_count, 1U);
    }
}

void WalletQmlModelTests::prepareTransaction_disallowsOtherInputsWhenCoinsSelected()
{
    auto [wallet, model] = MakeWalletModel();
    SetValidRecipient(*model);

    const COutPoint selected_outpoint{Txid::FromUint256(uint256::ONE), 1};
    std::optional<bool> available_balance_allow_other_inputs;
    bool saw_selected_coin{false};
    bool saw_other_inputs_allowed{true};

    [[maybe_unused]] auto verify_wallet = wallet->VerifyOnExit();
    wallet->ExpectExactly(wallet->calls.getAvailableBalance, 1);
    wallet->ExpectExactly(wallet->calls.createTransaction, 1);
    wallet->get_available_balance_fn = [&](const wallet::CCoinControl& coin_control) {
        available_balance_allow_other_inputs = coin_control.m_allow_other_inputs;
        return 10 * COIN;
    };

    wallet->create_transaction_fn = [&](const std::vector<wallet::CRecipient>&,
                                        const wallet::CCoinControl& coin_control,
                                        bool,
                                        int& change_pos,
                                        CAmount& fee) -> util::Result<CTransactionRef> {
        saw_selected_coin = coin_control.HasSelected() && coin_control.IsSelected(selected_outpoint);
        saw_other_inputs_allowed = coin_control.m_allow_other_inputs;
        change_pos = -1;
        fee = 200;
        return util::Result<CTransactionRef>{MakeTransactionRef(CMutableTransaction{})};
    };

    model->selectCoin(selected_outpoint);

    QVERIFY(model->prepareTransaction());
    QVERIFY(saw_selected_coin);
    QVERIFY(available_balance_allow_other_inputs.has_value());
    QVERIFY(!*available_balance_allow_other_inputs);
    QVERIFY(!saw_other_inputs_allowed);
}

void WalletQmlModelTests::scheduleFeeEstimates_debouncesRapidRestarts()
{
    auto [wallet, model] = MakeWalletModel();
    SetValidRecipient(*model);

    [[maybe_unused]] auto verify_wallet = wallet->VerifyOnExit();
    wallet->ExpectNoCalls(wallet->calls.getNewDestination);
    std::atomic<bool> saw_sign_true{false};
    wallet->create_transaction_fn = [&](const std::vector<wallet::CRecipient>&,
                                        const wallet::CCoinControl& coin_control,
                                        bool sign,
                                        int& change_pos,
                                        CAmount& fee) -> util::Result<CTransactionRef> {
        if (sign) saw_sign_true = true;
        change_pos = -1;
        fee = coin_control.m_confirm_target.value_or(0) * 250;
        return util::Result<CTransactionRef>{MakeTransactionRef(CMutableTransaction{})};
    };

    model->scheduleFeeEstimates();
    model->scheduleFeeEstimates();
    model->scheduleFeeEstimates();

    QTRY_COMPARE_WITH_TIMEOUT(wallet->calls.createTransaction.load(), 3, FEE_ESTIMATE_TIMEOUT_MS);
    QVERIFY(!saw_sign_true.load());
    QTRY_VERIFY_WITH_TIMEOUT(!model->feeEstimatePending(), FEE_ESTIMATE_TIMEOUT_MS);
    QCOMPARE(model->estimatedFeeForTarget(1), QStringLiteral("0.00000250 ₿"));
    QCOMPARE(model->estimatedFeeForTarget(2), QStringLiteral("0.00000500 ₿"));
    QCOMPARE(model->estimatedFeeForTarget(6), QStringLiteral("0.00001500 ₿"));
}

void WalletQmlModelTests::scheduleFeeEstimates_invalidatesStalePreviewState()
{
    auto [wallet, model] = MakeWalletModel();
    wallet->get_available_balance_fn = [](const wallet::CCoinControl&) { return CAmount{50'000}; };
    SetValidRecipient(*model);

    auto* recipient = model->sendRecipientList()->currentRecipient();
    QVERIFY(recipient != nullptr);
    recipient->amount()->setSatoshi(49'000);

    std::atomic<bool> stale_request_started{false};
    std::atomic<bool> fresh_request_started{false};
    QSemaphore release_stale_request;
    QSemaphore release_fresh_request;

    wallet->create_transaction_fn = [&](const std::vector<wallet::CRecipient>& recipients,
                                        const wallet::CCoinControl& coin_control,
                                        bool,
                                        int& change_pos,
                                        CAmount& fee) -> util::Result<CTransactionRef> {
        const CAmount total_amount = std::accumulate(recipients.begin(), recipients.end(), CAmount{0}, [](const CAmount total, const wallet::CRecipient& recipient) {
            return total + recipient.nAmount;
        });

        if (total_amount == 49'000 && !stale_request_started.exchange(true)) {
            release_stale_request.acquire();
        }
        if (total_amount == 49'900 && !fresh_request_started.exchange(true)) {
            release_fresh_request.acquire();
        }

        change_pos = -1;
        fee = total_amount == 49'900
            ? coin_control.m_confirm_target.value_or(0) * 100
            : 50;
        return util::Result<CTransactionRef>{MakeTransactionRef(CMutableTransaction{})};
    };

    model->scheduleFeeEstimates();
    QTRY_VERIFY_WITH_TIMEOUT(stale_request_started.load(), FEE_ESTIMATE_TIMEOUT_MS);
    const auto release_stale_on_exit{interfaces::MakeCleanupHandler([&] { release_stale_request.release(); })};
    QVERIFY(model->feeEstimatePending());
    QCOMPARE(model->estimatedFeeForTarget(2), QString());

    recipient->amount()->setSatoshi(49'900);
    model->scheduleFeeEstimates();
    QCOMPARE(model->estimatedFeeForTarget(2), QString());
    QVERIFY(model->feeEstimatePending());

    release_stale_request.release();
    QTRY_VERIFY_WITH_TIMEOUT(fresh_request_started.load(), FEE_ESTIMATE_TIMEOUT_MS);
    const auto release_fresh_on_exit{interfaces::MakeCleanupHandler([&] { release_fresh_request.release(); })};
    QCOMPARE(model->estimatedFeeForTarget(2), QString());
    QVERIFY(model->feeEstimatePending());
    QVERIFY(!model->sendAmountExhaustsBalance());

    release_fresh_request.release();
    QTRY_COMPARE_WITH_TIMEOUT(model->estimatedFeeForTarget(2), QStringLiteral("0.00000200 ₿"), FEE_ESTIMATE_TIMEOUT_MS);
    QTRY_VERIFY_WITH_TIMEOUT(!model->feeEstimatePending(), FEE_ESTIMATE_TIMEOUT_MS);
    QVERIFY(model->sendAmountExhaustsBalance());
    QVERIFY(wallet->calls.createTransaction.load() >= 6);
}

void WalletQmlModelTests::transactionChangedEmitsBalanceChanged()
{
    auto wallet = std::make_unique<MockWallet>();
    auto* wallet_ptr = wallet.get();

    CAmount balance{50 * COIN};
    interfaces::Wallet::TransactionChangedFn transaction_changed;
    wallet_ptr->get_balance_fn = [&] { return balance; };
    wallet_ptr->handle_transaction_changed_fn = [&](interfaces::Wallet::TransactionChangedFn fn) {
        transaction_changed = std::move(fn);
        return std::unique_ptr<interfaces::Handler>{};
    };

    WalletQmlModel model{std::move(wallet)};
    QSignalSpy balance_spy{&model, &WalletQmlModel::balanceChanged};

    QCOMPARE(model.balance(), QStringLiteral("50.00000000"));
    QVERIFY(transaction_changed);

    balance = 75 * COIN;
    transaction_changed(Txid::FromUint256(uint256{}), CT_UPDATED);

    QTRY_COMPARE(balance_spy.count(), 1);
    QCOMPARE(model.balance(), QStringLiteral("75.00000000"));
}

void WalletQmlModelTests::commitPaymentRequestUsesSelectedAddressType()
{
    auto [wallet, model] = MakePasswordWalletModel();

    model->currentPaymentRequest()->setLabel(QStringLiteral("typed receive"));
    model->currentPaymentRequest()->setAddressType(QStringLiteral("bech32m"));

    QVERIFY(model->commitPaymentRequest());
    QCOMPARE(wallet->get_new_destination_calls, 1);
    QCOMPARE(wallet->new_destination_types.size(), size_t{1});
    QCOMPARE(wallet->new_destination_types.front(), OutputType::BECH32M);
    QCOMPARE(wallet->new_destination_labels.size(), size_t{1});
    QCOMPARE(wallet->new_destination_labels.front(), std::string{"typed receive"});
}

void WalletQmlModelTests::setCurrentPaymentRequestAddressUsesAddressListLabel()
{
    auto [wallet, model] = MakePasswordWalletModel();
    wallet->wallet_addresses.emplace_back(
        DecodeDestination(VALID_MAINNET_ADDRESS.toStdString()),
        true,
        wallet::AddressPurpose::RECEIVE,
        "invoice 1024");
    wallet->get_address_result = true;

    QVERIFY(model->setCurrentPaymentRequestAddress(VALID_MAINNET_ADDRESS));
    QCOMPARE(model->currentPaymentRequest()->address(), VALID_MAINNET_ADDRESS);
    QCOMPARE(model->currentPaymentRequest()->label(), QStringLiteral("invoice 1024"));
}

void WalletQmlModelTests::usePaymentRequestAsTemplatePreservesAddressType()
{
    auto [wallet, model] = MakePasswordWalletModel();
    wallet->get_new_destination_fn = [wallet](OutputType type, const std::string& label) -> util::Result<CTxDestination> {
        ++wallet->get_new_destination_calls;
        wallet->new_destination_types.push_back(type);
        wallet->new_destination_labels.push_back(label);
        return DecodeDestination(VALID_MAINNET_P2SH_ADDRESS.toStdString());
    };

    model->currentPaymentRequest()->setLabel(QStringLiteral("typed template"));
    model->currentPaymentRequest()->setAddressType(QStringLiteral("p2sh-segwit"));
    QVERIFY(model->commitPaymentRequest());

    model->usePaymentRequestAsTemplate(QStringLiteral("1"));

    QVERIFY(model->currentPaymentRequest()->address().isEmpty());
    QCOMPARE(model->currentPaymentRequest()->addressType(), QStringLiteral("p2sh-segwit"));
    QCOMPARE(model->currentPaymentRequest()->label(), QStringLiteral("typed template"));
}

void WalletQmlModelTests::commitPaymentRequestOnLockedWalletSignalsNeedsUnlock()
{
    auto [wallet, model] = MakePasswordWalletModel();
    wallet->get_new_destination_fn = [wallet](OutputType, const std::string& label) -> util::Result<CTxDestination> {
        ++wallet->get_new_destination_calls;
        wallet->new_destination_labels.push_back(label);
        if (wallet->locked) {
            return util::Error{Untranslated("keypool empty")};
        }
        return DecodeDestination(VALID_MAINNET_ADDRESS.toStdString());
    };

    QVERIFY(!model->commitPaymentRequest());
    QVERIFY(model->currentPaymentRequest()->needsUnlock());
    QVERIFY(model->currentPaymentRequest()->unlockError().isEmpty());
    QCOMPARE(wallet->get_new_destination_calls, 1);
    QCOMPARE(wallet->unlock_calls, 0);
    QVERIFY(wallet->locked);
}

void WalletQmlModelTests::commitPaymentRequestWithPassphraseUnlocksRetriesAndRelocks()
{
    auto [wallet, model] = MakePasswordWalletModel();
    wallet->get_new_destination_fn = [wallet](OutputType, const std::string& label) -> util::Result<CTxDestination> {
        ++wallet->get_new_destination_calls;
        wallet->new_destination_labels.push_back(label);
        if (wallet->locked) {
            return util::Error{Untranslated("keypool empty")};
        }
        return DecodeDestination(VALID_MAINNET_ADDRESS.toStdString());
    };

    QVERIFY(!model->commitPaymentRequest());
    QVERIFY(model->currentPaymentRequest()->needsUnlock());

    const QString passphrase{QString::fromUtf8("secret")};
    QVERIFY(model->commitPaymentRequestWithPassphrase(passphrase));
    QCOMPARE(wallet->unlock_calls, 1);
    QCOMPARE(wallet->unlock_passphrases.size(), size_t{1});
    QCOMPARE(wallet->unlock_passphrases.back(), std::string{"secret"});
    QCOMPARE(wallet->lock_calls, 1);
    QVERIFY(wallet->locked);
    QCOMPARE(wallet->get_new_destination_calls, 2);
    QVERIFY(!model->currentPaymentRequest()->needsUnlock());
    QVERIFY(model->currentPaymentRequest()->unlockError().isEmpty());
    QVERIFY(!model->currentPaymentRequest()->address().isEmpty());
}

void WalletQmlModelTests::commitPaymentRequestWithPassphraseWrongPasswordSurfacesError()
{
    auto [wallet, model] = MakePasswordWalletModel();
    wallet->get_new_destination_fn = [wallet](OutputType, const std::string& label) -> util::Result<CTxDestination> {
        ++wallet->get_new_destination_calls;
        wallet->new_destination_labels.push_back(label);
        if (wallet->locked) {
            return util::Error{Untranslated("keypool empty")};
        }
        return DecodeDestination(VALID_MAINNET_ADDRESS.toStdString());
    };
    wallet->unlock_fn = [wallet](const SecureString& passphrase) {
        ++wallet->unlock_calls;
        wallet->unlock_passphrases.emplace_back(passphrase.begin(), passphrase.end());
        return false;
    };

    QVERIFY(!model->commitPaymentRequest());
    QVERIFY(!model->commitPaymentRequestWithPassphrase(QStringLiteral("wrong")));
    QCOMPARE(wallet->unlock_calls, 1);
    QCOMPARE(wallet->lock_calls, 0);
    QCOMPARE(wallet->get_new_destination_calls, 1);
    QCOMPARE(model->currentPaymentRequest()->unlockError(),
             QStringLiteral("The wallet password you entered was incorrect."));
    QVERIFY(model->currentPaymentRequest()->address().isEmpty());
}

void WalletQmlModelTests::removeReceiveRequestRemovesPendingActivityRow()
{
    auto [wallet, model] = MakePasswordWalletModel();
    model->currentPaymentRequest()->setLabel(QStringLiteral("request label"));

    QCOMPARE(model->activityListModel()->rowCount(), 0);
    QVERIFY(model->commitPaymentRequest());
    QCOMPARE(wallet->set_address_receive_request_calls, 1);
    QCOMPARE(wallet->receive_request_ids.back(), std::string{"1"});
    QCOMPARE(model->activityListModel()->rowCount(), 1);

    QVERIFY(model->removeReceiveRequest(QStringLiteral("1")));
    QCOMPARE(wallet->set_address_receive_request_calls, 2);
    QCOMPARE(wallet->receive_request_ids.back(), std::string{"1"});
    QCOMPARE(model->activityListModel()->rowCount(), 0);
}

void WalletQmlModelTests::activityDetailsSelectLowestOutputIndex()
{
    auto [wallet, model] = MakeWalletModel();
    const interfaces::WalletTx wallet_tx = MakeOutgoingActivityWalletTx(
        {10'000, 20'000, 30'000},
        {true, false, false},
        {true, false, false});
    wallet->get_wallet_txs_fn = [wallet_tx] {
        return std::set<interfaces::WalletTx>{wallet_tx};
    };
    model->activityListModel()->reload();

    const QString txid = QString::fromStdString(wallet_tx.tx->GetHash().ToString());
    const QVariantMap first = model->activityListModel()->firstTransactionDetails(txid);
    QCOMPARE(first.value("outputIndex").toInt(), 1);

    const QVariantMap exact = model->activityListModel()->transactionDetails(txid, 2);
    QCOMPARE(exact.value("outputIndex").toInt(), 2);
}

void WalletQmlModelTests::activityDetailsPreferOutgoingForSelfPayment()
{
    auto [wallet, model] = MakeWalletModel();
    const interfaces::WalletTx wallet_tx = MakeOutgoingActivityWalletTx(
        {20'000},
        {true},
        {false});
    wallet->get_wallet_txs_fn = [wallet_tx] {
        return std::set<interfaces::WalletTx>{wallet_tx};
    };
    model->activityListModel()->reload();

    const QString txid = QString::fromStdString(wallet_tx.tx->GetHash().ToString());
    const QVariantMap details = model->activityListModel()->firstTransactionDetails(txid);
    QCOMPARE(details.value("outputIndex").toInt(), 0);
    QVERIFY(details.value("amount").toString().startsWith('-'));
}

void WalletQmlModelTests::editedReceiveRequestLabelShownInActivityRow()
{
    auto [wallet, model] = MakePasswordWalletModel();

    // The address book reports a different label for the request address. A
    // pending request row must show the request's own label, not the address
    // book label, so an edited label is not overwritten when Activity renders.
    wallet->get_address_result = true;
    wallet->get_address_label = "address book label";

    model->currentPaymentRequest()->setLabel(QStringLiteral("Old label"));
    QVERIFY(model->commitPaymentRequest());

    ActivityListModel* activity = model->activityListModel();
    QCOMPARE(activity->rowCount(), 1);
    const QModelIndex row = activity->index(0);
    QVERIFY(activity->data(row, ActivityListModel::IsPendingRequestRole).toBool());
    QCOMPARE(activity->data(row, ActivityListModel::LabelRole).toString(), QStringLiteral("Old label"));

    // Editing the label commits an update for the same request id.
    model->currentPaymentRequest()->setLabel(QStringLiteral("New label"));
    QVERIFY(model->commitPaymentRequest());

    QCOMPARE(activity->rowCount(), 1);
    QCOMPARE(activity->data(activity->index(0), ActivityListModel::LabelRole).toString(),
             QStringLiteral("New label"));
}

void WalletQmlModelTests::editedRequestSyncsAddressBookLabel()
{
    auto [wallet, model] = MakePasswordWalletModel();
    // getAddress must succeed for the label sync to reach setAddressBook.
    wallet->get_address_result = true;

    // Creating the request labels its address.
    model->currentPaymentRequest()->setLabel(QStringLiteral("Old label"));
    QVERIFY(model->commitPaymentRequest());
    QCOMPARE(wallet->last_set_address_book_label, std::string{"Old label"});

    // Editing the label writes the new label back to the address book, so the
    // Addresses page reflects it instead of keeping the stale label.
    const int calls_before = wallet->set_address_book_calls;
    model->currentPaymentRequest()->setLabel(QStringLiteral("New label"));
    QVERIFY(model->commitPaymentRequest());
    QVERIFY(wallet->set_address_book_calls > calls_before);
    QCOMPARE(wallet->last_set_address_book_label, std::string{"New label"});
}

void WalletQmlModelTests::requestSaveLeavesUneditedAddressBookLabelAlone()
{
    auto [wallet, model] = MakePasswordWalletModel();
    wallet->get_address_result = true;
    // The address book already carries a label, e.g. set on the Addresses page.
    wallet->get_address_label = "Book label";

    // Saving a request whose label is empty must not clear the book label.
    QVERIFY(model->commitPaymentRequest());
    QCOMPARE(wallet->set_address_book_calls, 0);

    // Saving with an unchanged label (e.g. an amount-only edit) must not
    // rewrite the book label either.
    model->currentPaymentRequest()->setLabel(QStringLiteral("Book label"));
    QVERIFY(model->commitPaymentRequest());
    QCOMPARE(wallet->set_address_book_calls, 0);
}

void WalletQmlModelTests::editedAddressBookLabelSyncsRequestLabel()
{
    auto [wallet, model] = MakePasswordWalletModel();
    wallet->get_address_result = true;

    // Create a request; its address carries the request label.
    model->currentPaymentRequest()->setLabel(QStringLiteral("Old label"));
    QVERIFY(model->commitPaymentRequest());
    const QString address = model->currentPaymentRequest()->address();
    QVERIFY(!address.isEmpty());

    ActivityListModel* activity = model->activityListModel();
    QCOMPARE(activity->data(activity->index(0), ActivityListModel::LabelRole).toString(),
             QStringLiteral("Old label"));

    // Editing the label on the Addresses page must propagate to the matching
    // request, so the request record and its Activity row do not diverge from
    // the address book (the reverse of editedRequestSyncsAddressBookLabel).
    QVERIFY(model->setAddressLabel(address, QStringLiteral("New label")));

    QCOMPARE(activity->data(activity->index(0), ActivityListModel::LabelRole).toString(),
             QStringLiteral("New label"));
    const QVariantList matches = model->receiveRequests()->matchingEntriesForAddress(address);
    QCOMPARE(matches.size(), 1);
    QCOMPARE(matches.at(0).toMap().value(QStringLiteral("label")).toString(),
             QStringLiteral("New label"));
}

// Core supports multiple receive requests for one address, each with its own
// label. Saving one request writes its label to the address book but must not
// fan it out to the sibling requests; only an Addresses page edit
// (setAddressLabel) deliberately rewrites every request for the address.
void WalletQmlModelTests::requestSaveLeavesSiblingRequestLabelsAlone()
{
    auto [wallet, model] = MakePasswordWalletModel();
    wallet->get_address_result = true;

    // Two requests on the same address (the fake wallet hands out one
    // destination), with their own labels.
    model->currentPaymentRequest()->setLabel(QStringLiteral("Request A"));
    QVERIFY(model->commitPaymentRequest());
    const QString request_a_id = model->currentPaymentRequest()->id();
    const QString address = model->currentPaymentRequest()->address();

    model->currentPaymentRequest()->clear();
    model->currentPaymentRequest()->setLabel(QStringLiteral("Request B"));
    QVERIFY(model->commitPaymentRequest());
    const QString request_b_id = model->currentPaymentRequest()->id();
    QVERIFY(request_a_id != request_b_id);
    QCOMPARE(model->currentPaymentRequest()->address(), address);

    // Editing request A's label updates the address book but leaves request
    // B's persisted label alone.
    QVERIFY(model->loadPaymentRequest(request_a_id));
    model->currentPaymentRequest()->setLabel(QStringLiteral("Edited A"));
    const int b_writes_before = static_cast<int>(
        std::count(wallet->receive_request_ids.begin(), wallet->receive_request_ids.end(),
                   request_b_id.toStdString()));
    QVERIFY(model->commitPaymentRequest());
    QCOMPARE(wallet->last_set_address_book_label, std::string{"Edited A"});

    const int b_writes_after = static_cast<int>(
        std::count(wallet->receive_request_ids.begin(), wallet->receive_request_ids.end(),
                   request_b_id.toStdString()));
    QCOMPARE(b_writes_after, b_writes_before);
    const auto sibling = model->receiveRequests()->entryById(request_b_id);
    QVERIFY(sibling.has_value());
    QCOMPARE(QString::fromStdString(sibling->recipient.label), QStringLiteral("Request B"));
}

// A label sync write that fails to persist must not update the in-memory
// request models: they would show a label the wallet does not hold, and a
// reload would silently revert it.
void WalletQmlModelTests::labelSyncSkipsInMemoryUpdateWhenPersistFails()
{
    auto [wallet, model] = MakePasswordWalletModel();
    wallet->get_address_result = true;

    model->currentPaymentRequest()->setLabel(QStringLiteral("Old label"));
    QVERIFY(model->commitPaymentRequest());
    const QString request_id = model->currentPaymentRequest()->id();
    const QString address = model->currentPaymentRequest()->address();

    wallet->set_address_receive_request_result = false;
    QVERIFY(model->setAddressLabel(address, QStringLiteral("New label")));

    const auto entry = model->receiveRequests()->entryById(request_id);
    QVERIFY(entry.has_value());
    QCOMPARE(QString::fromStdString(entry->recipient.label), QStringLiteral("Old label"));
    ActivityListModel* activity = model->activityListModel();
    QCOMPARE(activity->data(activity->index(0), ActivityListModel::LabelRole).toString(),
             QStringLiteral("Old label"));
}

void WalletQmlModelTests::usedAddressReceiveRequestRowIsFlaggedAndUntracked()
{
    auto [wallet, model] = MakePasswordWalletModel();
    ActivityListModel* activity = model->activityListModel();
    QCOMPARE(activity->rowCount(), 0);

    // An unused-address request is a normal pending row.
    activity->addReceiveRequest(QStringLiteral("bcrt1qunused"), QStringLiteral("Unused"),
                                1000, 100, QStringLiteral("1"), /*used_address=*/false);
    QCOMPARE(activity->rowCount(), 1);
    QVERIFY(activity->data(activity->index(0), ActivityListModel::IsPendingRequestRole).toBool());
    QVERIFY(!activity->data(activity->index(0), ActivityListModel::IsUsedAddressRequestRole).toBool());

    // A used-address request is still a payment-request row but is flagged so the
    // proxy can keep it out of every view except the Payment request filter.
    activity->addReceiveRequest(QStringLiteral("bcrt1qused"), QStringLiteral("Used"),
                                2000, 200, QStringLiteral("2"), /*used_address=*/true);
    QCOMPARE(activity->rowCount(), 2);
    const QModelIndex used_row = activity->index(0); // rows are pushed to the front
    QVERIFY(activity->data(used_row, ActivityListModel::IsPendingRequestRole).toBool());
    QVERIFY(activity->data(used_row, ActivityListModel::IsUsedAddressRequestRole).toBool());
    QCOMPARE(activity->data(used_row, ActivityListModel::LabelRole).toString(), QStringLiteral("Used"));
}

void WalletQmlModelTests::paidPendingRequestBecomesUsedAddressRequestLive()
{
    auto wallet = std::make_unique<MockWallet>();
    auto* wallet_ptr = wallet.get();

    const CTxDestination dest{WitnessV0KeyHash{uint160{std::vector<unsigned char>(20, 7)}}};
    const QString address = QString::fromStdString(EncodeDestination(dest));
    const interfaces::WalletTx received = ReceiveWalletTxFor(dest, 50 * COIN);

    std::vector<interfaces::Wallet::TransactionChangedFn> callbacks;
    wallet_ptr->get_wallet_txs_fn = [] { return std::set<interfaces::WalletTx>{}; };
    wallet_ptr->get_balance_fn = [] { return 10 * COIN; };
    wallet_ptr->handle_transaction_changed_fn = [&](interfaces::Wallet::TransactionChangedFn fn) {
        callbacks.push_back(std::move(fn));
        return std::unique_ptr<interfaces::Handler>{};
    };
    wallet_ptr->get_wallet_tx_fn = [received](const Txid&) { return received; };
    wallet_ptr->try_get_tx_status_fn = [](const Txid&, interfaces::WalletTxStatus&, int&, int64_t&) { return true; };

    WalletQmlModel model{std::move(wallet)};
    ActivityListModel* activity = model.activityListModel();

    // A pending request for the address that is about to be paid.
    activity->addReceiveRequest(address, QStringLiteral("req"), 0, 100, QStringLiteral("1"));
    QCOMPARE(activity->rowCount(), 1);
    QVERIFY(activity->data(activity->index(0), ActivityListModel::IsPendingRequestRole).toBool());
    QVERIFY(!activity->data(activity->index(0), ActivityListModel::IsUsedAddressRequestRole).toBool());

    // The payment arrives while the wallet is open (no reload).
    QVERIFY(!callbacks.empty());
    for (const auto& cb : callbacks) {
        cb(received.tx->GetHash(), CT_NEW);
    }

    // The request is kept as a used-address request (Payment request filter only)
    // and the received transaction is added as its own row, matching the reloaded
    // state instead of consuming the request in place.
    QCOMPARE(activity->rowCount(), 2);
    QVERIFY(!activity->data(activity->index(0), ActivityListModel::IsPendingRequestRole).toBool());
    QVERIFY(!activity->data(activity->index(0), ActivityListModel::IsUsedAddressRequestRole).toBool());
    QCOMPARE(activity->data(activity->index(0), ActivityListModel::TypeRole).toInt(),
             static_cast<int>(Transaction::RecvWithAddress));

    const QModelIndex request_row = activity->index(1);
    QVERIFY(activity->data(request_row, ActivityListModel::IsPendingRequestRole).toBool());
    QVERIFY(activity->data(request_row, ActivityListModel::IsUsedAddressRequestRole).toBool());
    QCOMPARE(activity->data(request_row, ActivityListModel::LabelRole).toString(), QStringLiteral("req"));
}

namespace {
// Collect (label, isPendingRequest, isUsedAddressRequest) per row so the
// assertions do not depend on the model's insertion order.
struct RequestRowState {
    bool pending{false};
    bool used{false};
};
std::map<QString, RequestRowState> RequestRowStates(ActivityListModel* activity)
{
    std::map<QString, RequestRowState> states;
    for (int i = 0; i < activity->rowCount(); ++i) {
        const QModelIndex row = activity->index(i);
        states[activity->data(row, ActivityListModel::LabelRole).toString()] = RequestRowState{
            activity->data(row, ActivityListModel::IsPendingRequestRole).toBool(),
            activity->data(row, ActivityListModel::IsUsedAddressRequestRole).toBool()};
    }
    return states;
}
} // namespace

// Core supports multiple receive requests for one address. When a payment
// arrives, every pending request for the paid address must become a
// used-address request, not just the row the address lookup found first:
// a single marked row would strand its siblings as pending for the rest of
// the session while a reload marks them all used.
void WalletQmlModelTests::paymentMarksEveryPendingRequestForAddressUsedLive()
{
    auto wallet = std::make_unique<MockWallet>();
    auto* wallet_ptr = wallet.get();

    const CTxDestination dest{WitnessV0KeyHash{uint160{std::vector<unsigned char>(20, 7)}}};
    const QString address = QString::fromStdString(EncodeDestination(dest));
    const CTxDestination other_dest{WitnessV0KeyHash{uint160{std::vector<unsigned char>(20, 8)}}};
    const QString other_address = QString::fromStdString(EncodeDestination(other_dest));
    const interfaces::WalletTx received = ReceiveWalletTxFor(dest, 50 * COIN);

    std::vector<interfaces::Wallet::TransactionChangedFn> callbacks;
    wallet_ptr->get_wallet_txs_fn = [] { return std::set<interfaces::WalletTx>{}; };
    wallet_ptr->get_balance_fn = [] { return 10 * COIN; };
    wallet_ptr->handle_transaction_changed_fn = [&](interfaces::Wallet::TransactionChangedFn fn) {
        callbacks.push_back(std::move(fn));
        return std::unique_ptr<interfaces::Handler>{};
    };
    wallet_ptr->get_wallet_tx_fn = [received](const Txid&) { return received; };
    wallet_ptr->try_get_tx_status_fn = [](const Txid&, interfaces::WalletTxStatus&, int&, int64_t&) { return true; };

    WalletQmlModel model{std::move(wallet)};
    ActivityListModel* activity = model.activityListModel();
    ActivityFilterProxyModel proxy;
    proxy.setSourceModel(activity);

    // Two pending requests for the address about to be paid, and one for an
    // unrelated address that must stay pending.
    activity->addReceiveRequest(address, QStringLiteral("req-a1"), 0, 100, QStringLiteral("1"));
    activity->addReceiveRequest(address, QStringLiteral("req-a2"), 0, 110, QStringLiteral("2"));
    activity->addReceiveRequest(other_address, QStringLiteral("req-b"), 0, 120, QStringLiteral("3"));
    QCOMPARE(activity->rowCount(), 3);
    QCOMPARE(proxy.rowCount(), 3);

    QVERIFY(!callbacks.empty());
    for (const auto& cb : callbacks) {
        cb(received.tx->GetHash(), CT_NEW);
    }

    QCOMPARE(activity->rowCount(), 4);
    const auto states = RequestRowStates(activity);
    QVERIFY(states.at(QStringLiteral("req-a1")).used);
    QVERIFY(states.at(QStringLiteral("req-a2")).used);
    QVERIFY(states.at(QStringLiteral("req-b")).pending);
    QVERIFY(!states.at(QStringLiteral("req-b")).used);

    // The default view hides both used rows: the real transaction and the
    // unrelated pending request remain.
    QCOMPARE(proxy.rowCount(), 2);
    proxy.setTypeFilter(ActivityFilterProxyModel::PaymentRequest);
    QCOMPARE(proxy.rowCount(), 3);
}

// The reload half of the same rule: rebuilding the model from the wallet
// marks every request whose address appears in a real transaction as a
// used-address request, so a restart shows the same state as the live
// update path.
void WalletQmlModelTests::reloadMarksEveryRequestForPaidAddressUsed()
{
    auto wallet = std::make_unique<MockWallet>();
    auto* wallet_ptr = wallet.get();

    const CTxDestination dest{WitnessV0KeyHash{uint160{std::vector<unsigned char>(20, 7)}}};
    const QString address = QString::fromStdString(EncodeDestination(dest));
    const CTxDestination other_dest{WitnessV0KeyHash{uint160{std::vector<unsigned char>(20, 8)}}};
    const QString other_address = QString::fromStdString(EncodeDestination(other_dest));
    const interfaces::WalletTx received = ReceiveWalletTxFor(dest, 50 * COIN);

    wallet_ptr->get_wallet_txs_fn = [received] { return std::set<interfaces::WalletTx>{received}; };
    wallet_ptr->get_balance_fn = [] { return 10 * COIN; };
    wallet_ptr->try_get_tx_status_fn = [](const Txid&, interfaces::WalletTxStatus&, int&, int64_t&) { return true; };

    WalletQmlModel model{std::move(wallet)};
    ActivityListModel* activity = model.activityListModel();

    auto make_entry = [](int64_t id, const QString& addr, const char* label) {
        QmlRecentRequestEntry entry;
        entry.id = id;
        entry.date = QDateTime::fromSecsSinceEpoch(100 + id);
        entry.recipient.address = addr.toStdString();
        entry.recipient.label = label;
        return entry;
    };
    std::vector<QmlRecentRequestEntry> entries;
    entries.push_back(make_entry(1, address, "req-a1"));
    entries.push_back(make_entry(2, address, "req-a2"));
    entries.push_back(make_entry(3, other_address, "req-b"));
    model.receiveRequests()->setEntries(std::move(entries));

    activity->reload();

    QCOMPARE(activity->rowCount(), 4);
    const auto states = RequestRowStates(activity);
    QVERIFY(states.at(QStringLiteral("req-a1")).used);
    QVERIFY(states.at(QStringLiteral("req-a2")).used);
    QVERIFY(states.at(QStringLiteral("req-b")).pending);
    QVERIFY(!states.at(QStringLiteral("req-b")).used);
}

void WalletQmlModelTests::prepareTransactionOnLockedWalletRequiresPassword()
{
    auto [wallet, model] = MakePasswordWalletModel();
    SetPasswordRecipient(*model, 1'000);

    QVERIFY(!model->prepareTransaction());
    QVERIFY(model->isEncrypted());
    QVERIFY(model->isLocked());
    QVERIFY(model->transactionNeedsUnlock());
    QCOMPARE(model->transactionError(), QString("Enter your wallet password to prepare this transaction."));
    QVERIFY(wallet->create_transaction_sign_args.empty());
    QCOMPARE(wallet->unlock_calls, 0);
    QCOMPARE(wallet->lock_calls, 0);
}

void WalletQmlModelTests::prepareTransactionWithPrivateKeysDisabledDoesNotRequirePassword()
{
    auto [wallet, model] = MakePasswordWalletModel();
    SetPasswordRecipient(*model, 1'000);
    wallet->private_keys_disabled = true;

    QVERIFY(model->prepareTransaction());
    QVERIFY(model->isEncrypted());
    QVERIFY(model->isLocked());
    QVERIFY(!model->transactionNeedsUnlock());
    QVERIFY(wallet->create_transaction_sign_args == std::vector<bool>{false});
    QCOMPARE(wallet->unlock_calls, 0);
    QCOMPARE(wallet->lock_calls, 0);
}

void WalletQmlModelTests::sendRecipientRejectsDustAmount()
{
    auto [wallet, model] = MakePasswordWalletModel();
    auto* recipient = model->sendRecipientList()->currentRecipient();
    QVERIFY(recipient != nullptr);

    recipient->address()->setAddress(VALID_MAINNET_ADDRESS, 0);
    recipient->amount()->setSatoshi(1);

    QVERIFY(!recipient->isValid());
    QCOMPARE(recipient->amountError(), QStringLiteral("Amount is too small to send."));
    QVERIFY(!model->sendRecipientList()->allValid());
}

void WalletQmlModelTests::sendRecipientUsesNodeDustRelayFee()
{
    MockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    node.get_dust_relay_fee_fn = [] { return CFeeRate{10'000}; };
    node.ExpectAtLeast(node.calls.getDustRelayFee, 1);
    auto [wallet, model] = MakePasswordWalletModel(&node);
    auto* recipient = model->sendRecipientList()->currentRecipient();
    QVERIFY(recipient != nullptr);

    recipient->address()->setAddress(VALID_MAINNET_ADDRESS, 0);
    recipient->amount()->setSatoshi(600);

    QVERIFY(!recipient->isValid());
    QCOMPARE(recipient->amountError(), QStringLiteral("Amount is too small to send."));
    QVERIFY(!model->sendRecipientList()->allValid());
}

void WalletQmlModelTests::prepareTransactionRejectsDuplicateRecipientsBeforeUnlock()
{
    auto [wallet, model] = MakePasswordWalletModel();
    SetPasswordRecipient(*model, 1'000);

    model->sendRecipientList()->add();
    auto* second = model->sendRecipientList()->currentRecipient();
    QVERIFY(second != nullptr);
    second->address()->setAddress(VALID_MAINNET_ADDRESS, 0);
    second->amount()->setSatoshi(2'000);
    QVERIFY(second->isValid());

    QVERIFY(!model->sendRecipientList()->allValid());
    QCOMPARE(model->sendRecipientList()->validationError(), QString("Recipient addresses must be unique."));

    QVERIFY(!model->prepareTransactionWithPassphrase("secret"));
    QCOMPARE(model->transactionError(), QString("Recipient addresses must be unique."));
    QCOMPARE(wallet->unlock_calls, 0);
    QCOMPARE(wallet->lock_calls, 0);
    QVERIFY(wallet->create_transaction_sign_args.empty());
}

void WalletQmlModelTests::prepareTransactionWithPassphraseForwardsUtf8Bytes()
{
    auto [wallet, model] = MakePasswordWalletModel();
    SetPasswordRecipient(*model, 1'000);

    const QString passphrase{QString::fromUtf8("pässwörd-₿")};
    const std::string expected_passphrase{passphrase.toUtf8().toStdString()};

    QVERIFY(model->prepareTransactionWithPassphrase(passphrase));
    QCOMPARE(wallet->unlock_calls, 1);
    QCOMPARE(wallet->unlock_passphrases.size(), size_t{1});
    QCOMPARE(wallet->unlock_passphrases.front(), expected_passphrase);
    QCOMPARE(wallet->lock_calls, 1);
}

void WalletQmlModelTests::prepareTransactionWithPassphraseRelocksWhenRecipientsInvalid()
{
    auto [wallet, model] = MakePasswordWalletModel();
    auto* recipient = model->sendRecipientList()->currentRecipient();
    QVERIFY(recipient != nullptr);
    recipient->amount()->setSatoshi(1'000);
    QVERIFY(!recipient->isValid());

    QVERIFY(!model->sendRecipientList()->allValid());
    QVERIFY(model->sendRecipientList()->validationError().isEmpty());

    QVERIFY(!model->prepareTransactionWithPassphrase("secret"));
    QVERIFY(wallet->locked);
    QVERIFY(model->transactionError().isEmpty());
    QCOMPARE(wallet->unlock_calls, 0);
    QCOMPARE(wallet->lock_calls, 0);
    QVERIFY(wallet->create_transaction_sign_args.empty());
}

void WalletQmlModelTests::prepareTransactionWithPassphraseRequiresCompleteMultiRecipient()
{
    auto [wallet, model] = MakePasswordWalletModel();
    SetPasswordRecipient(*model, 1'000);

    model->sendRecipientList()->add();
    auto* second = model->sendRecipientList()->currentRecipient();
    QVERIFY(second != nullptr);
    QVERIFY(!second->isValid());

    QVERIFY(!model->sendRecipientList()->allValid());
    QCOMPARE(model->sendRecipientList()->validationError(), QString("Complete every recipient before continuing."));

    QVERIFY(!model->prepareTransactionWithPassphrase("secret"));
    QVERIFY(wallet->locked);
    QCOMPARE(model->transactionError(), QString("Complete every recipient before continuing."));
    QCOMPARE(wallet->unlock_calls, 0);
    QCOMPARE(wallet->lock_calls, 0);
    QVERIFY(wallet->create_transaction_sign_args.empty());
}

void WalletQmlModelTests::prepareTransactionWithPassphraseRelocksWhenCustomFeeInvalid()
{
    auto [wallet, model] = MakePasswordWalletModel();
    SetPasswordRecipient(*model, 1'000);
    model->setCustomFeeEnabled(true);
    model->setCustomFeeRate(QStringLiteral("not-a-fee"));

    QVERIFY(!model->prepareTransactionWithPassphrase("secret"));
    QVERIFY(wallet->locked);
    QCOMPARE(wallet->unlock_calls, 1);
    QCOMPARE(wallet->lock_calls, 1);
    QVERIFY(wallet->create_transaction_sign_args.empty());
}

void WalletQmlModelTests::prepareTransactionWithPassphraseReportsCreateErrorAndRelocks()
{
    auto [wallet, model] = MakePasswordWalletModel();
    SetPasswordRecipient(*model, 1'000);

    wallet->create_transaction_fn = [](const std::vector<wallet::CRecipient>&,
                                       const wallet::CCoinControl&,
                                       bool,
                                       int&,
                                       CAmount&) -> util::Result<CTransactionRef> {
        return util::Error{Untranslated("Transaction needs a change address, but we can't generate it. Error: Keypool ran out, please call keypoolrefill first")};
    };

    QVERIFY(!model->prepareTransactionWithPassphrase("secret"));
    QVERIFY(model->isEncrypted());
    QVERIFY(model->isLocked());
    QVERIFY(!model->transactionNeedsUnlock());
    QCOMPARE(model->transactionError(), QString("Transaction needs a change address, but we can't generate it. Error: Keypool ran out, please call keypoolrefill first"));
    QVERIFY(wallet->create_transaction_sign_args == std::vector<bool>{true});
    QCOMPARE(wallet->unlock_calls, 1);
    QCOMPARE(wallet->lock_calls, 1);
}

void WalletQmlModelTests::sendTransactionCommitsPreparedTransactionWithoutUnlockingAgain()
{
    auto [wallet, model] = MakePasswordWalletModel();
    SetPasswordRecipient(*model, 1'000);

    QVERIFY(model->prepareTransactionWithPassphrase("secret"));
    QVERIFY(wallet->locked);
    QCOMPARE(wallet->unlock_calls, 1);
    QCOMPARE(wallet->lock_calls, 1);

    QVERIFY(model->sendTransaction());
    QCOMPARE(wallet->unlock_calls, 1);
    QCOMPARE(wallet->lock_calls, 1);
    QCOMPARE(wallet->commit_calls, 1);
    QVERIFY(wallet->locked);
    QVERIFY(wallet->fill_psbt_sign_args.empty());
    QVERIFY(model->transactionError().isEmpty());
    QVERIFY(!model->transactionNeedsUnlock());
}

void WalletQmlModelTests::sendTransactionClearsSelectedCoins()
{
    auto [wallet, model] = MakePasswordWalletModel();
    SetPasswordRecipient(*model, 1'000);
    const COutPoint selected_outpoint{Txid::FromUint256(uint256::ONE), 1};

    model->selectCoin(selected_outpoint);
    QCOMPARE(model->listSelectedCoins().size(), size_t{1});

    QVERIFY(model->prepareTransactionWithPassphrase("secret"));
    QVERIFY(model->sendTransaction());
    QVERIFY(model->listSelectedCoins().empty());
}

void WalletQmlModelTests::clearingRecipientsClearsSelectedCoins()
{
    auto [wallet, model] = MakePasswordWalletModel();
    const COutPoint selected_outpoint{Txid::FromUint256(uint256::ONE), 1};

    model->selectCoin(selected_outpoint);
    QCOMPARE(model->listSelectedCoins().size(), size_t{1});

    model->sendRecipientList()->clear();
    QVERIFY(model->listSelectedCoins().empty());
}

void WalletQmlModelTests::saveCurrentTransactionAsPsbt_savesUnsignedPreparedTransaction()
{
    auto [wallet, model] = MakePasswordWalletModel();
    SetPasswordRecipient(*model, 1'000);

    const std::vector<unsigned char> script_sig_bytes{0x51};
    const std::vector<unsigned char> witness_bytes{0x02, 0x03};
    wallet->create_transaction_fn = [script_sig_bytes, witness_bytes](const std::vector<wallet::CRecipient>&,
                                                                      const wallet::CCoinControl&,
                                                                      bool,
                                                                      int& change_pos,
                                                                      CAmount& fee) -> util::Result<CTransactionRef> {
        CMutableTransaction mtx;
        mtx.vin.emplace_back(COutPoint{Txid::FromUint256(uint256::ONE), 0});
        mtx.vin[0].scriptSig = CScript{script_sig_bytes.begin(), script_sig_bytes.end()};
        mtx.vin[0].scriptWitness.stack.push_back(witness_bytes);
        mtx.vout.emplace_back(900, CScript{});
        change_pos = -1;
        fee = 100;
        return MakeTransactionRef(std::move(mtx));
    };

    QVERIFY(model->prepareTransactionWithPassphrase("secret"));

    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());
    const QString path{temp_dir.filePath(QStringLiteral("prepared.psbt"))};
    QCOMPARE(model->saveCurrentTransactionAsPsbt(path), QString());

    QFile file{path};
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray bytes{file.readAll()};
    std::vector<std::byte> raw;
    raw.reserve(bytes.size());
    for (const char byte : bytes) {
        raw.push_back(static_cast<std::byte>(static_cast<unsigned char>(byte)));
    }

    const auto decoded{DecodeRawPSBT(std::span<const std::byte>{raw.data(), raw.size()})};
    QVERIFY(decoded.has_value());
    const auto decoded_tx{decoded->GetUnsignedTx()};
    QVERIFY(decoded_tx.has_value());
    QCOMPARE(decoded_tx->vin.size(), size_t{1});
    QVERIFY(decoded_tx->vin[0].scriptSig.empty());
    QVERIFY(decoded_tx->vin[0].scriptWitness.IsNull());
    QCOMPARE(decoded->inputs.size(), size_t{1});
    QVERIFY(decoded->inputs[0].final_script_sig.empty());
    QVERIFY(decoded->inputs[0].final_script_witness.IsNull());
}

void WalletQmlModelTests::importPsbtFromFile_opensOwnedUnsignedPsbtWithoutSigning()
{
    auto [wallet, model] = MakePasswordWalletModel();

    const COutPoint owned_outpoint{Txid::FromUint256(uint256::ONE), 0};
    wallet->txin_is_mine_fn = [owned_outpoint](const CTxIn& txin) {
        return txin.prevout == owned_outpoint;
    };
    wallet->fill_psbt_fn = [wallet](const common::PSBTFillOptions& options,
                                    size_t* n_signed,
                                    PartiallySignedTransaction&,
                                    bool& complete) {
        const bool sign{options.sign};
        wallet->fill_psbt_sign_args.push_back(sign);
        if (n_signed) {
            *n_signed = 1;
        }
        complete = sign;
        return std::nullopt;
    };

    CMutableTransaction mtx;
    mtx.vin.emplace_back(owned_outpoint);
    mtx.vout.emplace_back(1'500, GetScriptForDestination(DecodeDestination(VALID_MAINNET_ADDRESS.toStdString())));
    PartiallySignedTransaction psbt{mtx};
    psbt.inputs[0].witness_utxo = CTxOut{2'000, CScript{}};

    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());
    const QString path{temp_dir.filePath(QStringLiteral("unsigned.psbt"))};
    QFile file{path};
    QVERIFY(file.open(QIODevice::WriteOnly));
    const QByteArray raw{PsbtQmlModel::SerializePsbtRaw(psbt)};
    QCOMPARE(file.write(raw), raw.size());
    file.close();

    QVERIFY(!model->broadcastCurrentTransaction());
    QVERIFY(!model->transactionError().isEmpty());
    QCOMPARE(model->importPsbtFromFile(path), WalletQmlModel::PsbtImportResult::WalletCanSign);
    QVERIFY(model->transactionError().isEmpty());
    QVERIFY(!wallet->fill_psbt_sign_args.empty());
    QVERIFY(std::none_of(wallet->fill_psbt_sign_args.begin(), wallet->fill_psbt_sign_args.end(), [](bool sign) {
        return sign;
    }));
    QVERIFY(model->currentTransaction() != nullptr);
    QVERIFY(model->currentTransaction()->getWtx() != nullptr);
    QCOMPARE(model->currentTransaction()->getWtx()->vin.size(), size_t{1});
    QVERIFY(model->currentTransaction()->getWtx()->vin[0].scriptSig.empty());
    QVERIFY(model->currentTransaction()->getWtx()->vin[0].scriptWitness.IsNull());
    QCOMPARE(model->sendRecipientList()->count(), 1);
    QCOMPARE(model->sendRecipientList()->currentRecipient()->address()->address(), VALID_MAINNET_ADDRESS);
    QCOMPARE(model->sendRecipientList()->currentRecipient()->amount()->satoshi(), 1'500);
    QVERIFY(model->currentTransactionCanSend());
    QVERIFY(!model->currentTransactionCanBroadcast());
    QVERIFY(model->currentTransactionReviewMessage().isEmpty());
}

void WalletQmlModelTests::importPsbtFromFile_opensForeignUnsignedPsbtForReviewOnly()
{
    auto [wallet, model] = MakePasswordWalletModel();

    wallet->fill_psbt_fn = [wallet](const common::PSBTFillOptions& options,
                                    size_t* n_signed,
                                    PartiallySignedTransaction&,
                                    bool& complete) {
        const bool sign{options.sign};
        wallet->fill_psbt_sign_args.push_back(sign);
        if (n_signed) {
            *n_signed = 1;
        }
        complete = false;
        return std::nullopt;
    };

    CMutableTransaction mtx;
    mtx.vin.emplace_back(COutPoint{Txid::FromUint256(uint256::ONE), 0});
    mtx.vout.emplace_back(1'500, GetScriptForDestination(DecodeDestination(VALID_MAINNET_ADDRESS.toStdString())));
    PartiallySignedTransaction psbt{mtx};
    psbt.inputs[0].witness_utxo = CTxOut{2'000, CScript{}};

    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());
    const QString path{temp_dir.filePath(QStringLiteral("foreign-unsigned.psbt"))};
    QFile file{path};
    QVERIFY(file.open(QIODevice::WriteOnly));
    const QByteArray raw{PsbtQmlModel::SerializePsbtRaw(psbt)};
    QCOMPARE(file.write(raw), raw.size());
    file.close();

    QCOMPARE(model->importPsbtFromFile(path), WalletQmlModel::PsbtImportResult::WalletCannotSign);
    QVERIFY(!wallet->fill_psbt_sign_args.empty());
    QVERIFY(std::none_of(wallet->fill_psbt_sign_args.begin(), wallet->fill_psbt_sign_args.end(), [](bool sign) {
        return sign;
    }));
    QVERIFY(model->currentTransaction() != nullptr);
    QVERIFY(model->currentTransaction()->getWtx() != nullptr);
    QCOMPARE(model->sendRecipientList()->count(), 1);
    QCOMPARE(model->sendRecipientList()->currentRecipient()->address()->address(), VALID_MAINNET_ADDRESS);
    QCOMPARE(model->sendRecipientList()->currentRecipient()->amount()->satoshi(), 1'500);
    QVERIFY(!model->currentTransactionCanSend());
    QVERIFY(!model->currentTransactionCanBroadcast());
    QCOMPARE(model->currentTransactionReviewMessage(), QString("This wallet does not have the keys to sign this transaction."));
    QVERIFY(!model->importedPsbt()->loaded());

    QVERIFY(!model->sendTransaction());
    QCOMPARE(wallet->commit_calls, 0);
    QCOMPARE(model->transactionError(), QString("This wallet does not have the keys to sign this transaction."));
}

void WalletQmlModelTests::saveReviewOnlyPsbt_preservesOriginalWithoutDerivationMetadata()
{
    auto [wallet, model] = MakePasswordWalletModel();

    PartiallySignedTransaction psbt{MakeReviewPsbt()};
    psbt.unknown[{0x50}] = {0x01};
    const QByteArray original{PsbtQmlModel::SerializePsbtRaw(psbt)};
    std::vector<bool> bip32_derivation_requests;
    wallet->fill_psbt_fn = [wallet, &bip32_derivation_requests](const common::PSBTFillOptions& options,
                                                               size_t* n_signed,
                                                               PartiallySignedTransaction& psbtx,
                                                               bool& complete) {
        const bool sign{options.sign};
        wallet->fill_psbt_sign_args.push_back(sign);
        const bool bip32derivs{options.bip32_derivs};
        bip32_derivation_requests.push_back(bip32derivs);
        if (bip32derivs) {
            psbtx.unknown[{0x53}] = {0x04};
        }
        if (n_signed) {
            *n_signed = 1;
        }
        complete = false;
        return std::nullopt;
    };

    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());
    const QString source_path{WritePsbt(psbt, temp_dir, QStringLiteral("review-only-source.psbt"))};
    QVERIFY(!source_path.isEmpty());

    QCOMPARE(model->importPsbtFromFile(source_path), WalletQmlModel::PsbtImportResult::WalletCannotSign);
    QCOMPARE(bip32_derivation_requests, std::vector<bool>({false}));

    const QString saved_path{temp_dir.filePath(QStringLiteral("review-only-saved.psbt"))};
    QCOMPARE(model->saveCurrentTransactionAsPsbt(saved_path), QString{});
    QCOMPARE(bip32_derivation_requests, std::vector<bool>({false}));

    QFile saved_file{saved_path};
    QVERIFY(saved_file.open(QIODevice::ReadOnly));
    QCOMPARE(saved_file.readAll(), original);
}

void WalletQmlModelTests::discardCurrentTransaction_clearsReviewState()
{
    auto [wallet, model] = MakePasswordWalletModel();
    wallet->fill_psbt_fn = [wallet](const common::PSBTFillOptions& options,
                                    size_t* n_signed,
                                    PartiallySignedTransaction&,
                                    bool& complete) {
        const bool sign{options.sign};
        wallet->fill_psbt_sign_args.push_back(sign);
        if (n_signed) {
            *n_signed = 0;
        }
        complete = false;
        return std::nullopt;
    };

    const PartiallySignedTransaction psbt{MakeReviewPsbt()};
    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());
    const QString path{WritePsbt(psbt, temp_dir, QStringLiteral("discard-review.psbt"))};
    QVERIFY(!path.isEmpty());
    QCOMPARE(model->importPsbtFromFile(path), WalletQmlModel::PsbtImportResult::WalletCannotSign);
    QVERIFY(!model->sendTransaction());
    QVERIFY(!model->transactionError().isEmpty());

    QSignalSpy transaction_changed{model.get(), &WalletQmlModel::currentTransactionChanged};
    model->discardCurrentTransaction();

    QCOMPARE(transaction_changed.count(), 1);
    QVERIFY(model->currentTransaction() == nullptr);
    QVERIFY(!model->currentTransactionCanSend());
    QVERIFY(model->currentTransactionReviewMessage().isEmpty());
    QVERIFY(model->transactionError().isEmpty());
    QCOMPARE(model->sendRecipientList()->count(), 1);
    QVERIFY(model->sendRecipientList()->currentRecipient()->address()->address().isEmpty());
    QCOMPARE(model->sendRecipientList()->currentRecipient()->amount()->satoshi(), CAmount{0});
}

void WalletQmlModelTests::importPsbtFromFile_opensWatchOnlyUnsignedPsbtForReviewOnly()
{
    auto [wallet, model] = MakePasswordWalletModel();
    wallet->private_keys_disabled = true;

    const PartiallySignedTransaction psbt{MakeReviewPsbt()};
    const COutPoint owned_outpoint{FirstInputPrevout(psbt)};
    wallet->txin_is_mine_fn = [owned_outpoint](const CTxIn& txin) {
        return txin.prevout == owned_outpoint;
    };
    wallet->fill_psbt_fn = [wallet](const common::PSBTFillOptions& options,
                                    size_t* n_signed,
                                    PartiallySignedTransaction&,
                                    bool& complete) {
        const bool sign{options.sign};
        wallet->fill_psbt_sign_args.push_back(sign);
        if (n_signed) {
            *n_signed = 0;
        }
        complete = false;
        return std::nullopt;
    };

    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());
    const QString path{WritePsbt(psbt, temp_dir, QStringLiteral("watch-only.psbt"))};
    QVERIFY(!path.isEmpty());

    QCOMPARE(model->importPsbtFromFile(path), WalletQmlModel::PsbtImportResult::WalletCannotSign);
    QVERIFY(model->currentTransaction() != nullptr);
    QVERIFY(!model->currentTransactionCanSend());
    QCOMPARE(model->currentTransactionReviewMessage(), QString("This wallet does not have the keys to sign this transaction."));
}

void WalletQmlModelTests::saveCurrentTransactionAsPsbt_preservesImportedMetadata()
{
    auto [wallet, model] = MakePasswordWalletModel();

    PartiallySignedTransaction psbt{MakeReviewPsbt()};
    psbt.unknown[{0x50}] = {0x01};
    psbt.inputs[0].unknown[{0x51}] = {0x02};
    psbt.outputs[0].unknown[{0x52}] = {0x03};
    const QByteArray original{PsbtQmlModel::SerializePsbtRaw(psbt)};
    const COutPoint owned_outpoint{FirstInputPrevout(psbt)};
    wallet->txin_is_mine_fn = [owned_outpoint](const CTxIn& txin) {
        return txin.prevout == owned_outpoint;
    };
    wallet->fill_psbt_fn = [wallet](const common::PSBTFillOptions& options,
                                    size_t* n_signed,
                                    PartiallySignedTransaction&,
                                    bool& complete) {
        const bool sign{options.sign};
        wallet->fill_psbt_sign_args.push_back(sign);
        if (n_signed) {
            *n_signed = 1;
        }
        complete = false;
        return std::nullopt;
    };

    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());
    const QString source_path{WritePsbt(psbt, temp_dir, QStringLiteral("source.psbt"))};
    QVERIFY(!source_path.isEmpty());
    QCOMPARE(model->importPsbtFromFile(source_path), WalletQmlModel::PsbtImportResult::WalletCanSign);
    const auto fill_calls_after_import{wallet->fill_psbt_sign_args.size()};

    const QString saved_path{temp_dir.filePath(QStringLiteral("saved.psbt"))};
    QCOMPARE(model->saveCurrentTransactionAsPsbt(saved_path), QString{});
    QCOMPARE(wallet->fill_psbt_sign_args.size(), fill_calls_after_import);

    QFile saved_file{saved_path};
    QVERIFY(saved_file.open(QIODevice::ReadOnly));
    const QByteArray bytes{saved_file.readAll()};
    QCOMPARE(bytes, original);
    std::vector<std::byte> raw;
    raw.reserve(bytes.size());
    for (const char byte : bytes) {
        raw.push_back(static_cast<std::byte>(static_cast<unsigned char>(byte)));
    }

    const auto saved{DecodeRawPSBT(std::span<const std::byte>{raw.data(), raw.size()})};
    QVERIFY(saved.has_value());
    QCOMPARE(saved->unknown, psbt.unknown);
    QCOMPARE(saved->inputs[0].unknown, psbt.inputs[0].unknown);
    QCOMPARE(saved->outputs[0].unknown, psbt.outputs[0].unknown);
}

void WalletQmlModelTests::sendImportedPsbtWithPassphraseSignsOnceAndRelocks()
{
    auto [wallet, model] = MakePasswordWalletModel();

    const PartiallySignedTransaction psbt{MakeReviewPsbt()};
    const COutPoint owned_outpoint{FirstInputPrevout(psbt)};
    wallet->txin_is_mine_fn = [owned_outpoint](const CTxIn& txin) {
        return txin.prevout == owned_outpoint;
    };
    wallet->fill_psbt_fn = [wallet](const common::PSBTFillOptions& options,
                                    size_t* n_signed,
                                    PartiallySignedTransaction& psbt,
                                    bool& complete) {
        const bool sign{options.sign};
        wallet->fill_psbt_sign_args.push_back(sign);
        if (n_signed) {
            *n_signed = 1;
        }
        if (sign) {
            CompleteReviewPsbt(psbt);
        }
        complete = sign;
        return std::nullopt;
    };

    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());
    const QString path{WritePsbt(psbt, temp_dir, QStringLiteral("locked.psbt"))};
    QVERIFY(!path.isEmpty());
    QCOMPARE(model->importPsbtFromFile(path), WalletQmlModel::PsbtImportResult::WalletCanSign);

    QVERIFY(!model->sendTransaction());
    QVERIFY(model->transactionNeedsUnlock());
    QCOMPARE(wallet->unlock_calls, 0);
    QCOMPARE(std::count(wallet->fill_psbt_sign_args.begin(), wallet->fill_psbt_sign_args.end(), true), 0);

    QVERIFY(model->sendTransactionWithPassphrase(QStringLiteral("secret")));
    QCOMPARE(wallet->unlock_calls, 1);
    QCOMPARE(wallet->lock_calls, 1);
    QVERIFY(wallet->locked);
    QCOMPARE(wallet->commit_calls, 1);
    QCOMPARE(std::count(wallet->fill_psbt_sign_args.begin(), wallet->fill_psbt_sign_args.end(), true), 1);
}

void WalletQmlModelTests::externalSignerApprovalSignsImportedPsbtOnlyOnce()
{
    auto [wallet, model] = MakePasswordWalletModel();
    wallet->private_keys_disabled = true;
    wallet->external_signer = true;

    const PartiallySignedTransaction psbt{MakeReviewPsbt()};
    const COutPoint owned_outpoint{FirstInputPrevout(psbt)};
    wallet->txin_is_mine_fn = [owned_outpoint](const CTxIn& txin) {
        return txin.prevout == owned_outpoint;
    };
    wallet->fill_psbt_fn = [wallet](const common::PSBTFillOptions& options,
                                    size_t* n_signed,
                                    PartiallySignedTransaction& psbt,
                                    bool& complete) {
        const bool sign{options.sign};
        wallet->fill_psbt_sign_args.push_back(sign);
        if (n_signed) {
            *n_signed = 1;
        }
        if (sign) {
            CompleteReviewPsbt(psbt);
        }
        complete = false;
        return std::nullopt;
    };

    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());
    const QString path{WritePsbt(psbt, temp_dir, QStringLiteral("external.psbt"))};
    QVERIFY(!path.isEmpty());
    QCOMPARE(model->importPsbtFromFile(path), WalletQmlModel::PsbtImportResult::WalletCanSign);

    QSignalSpy succeeded_spy{model.get(), &WalletQmlModel::externalSignerApprovalSucceeded};
    model->approveExternalSignerTransaction();
    QCOMPARE(succeeded_spy.count(), 1);
    QCOMPARE(std::count(wallet->fill_psbt_sign_args.begin(), wallet->fill_psbt_sign_args.end(), true), 1);

    QVERIFY(model->sendTransaction());
    QCOMPARE(wallet->commit_calls, 1);
    QCOMPARE(std::count(wallet->fill_psbt_sign_args.begin(), wallet->fill_psbt_sign_args.end(), true), 1);
}

void WalletQmlModelTests::externalSignerApprovalKeepsIncompleteSignedPsbt()
{
    auto [wallet, model] = MakePasswordWalletModel();
    wallet->private_keys_disabled = true;
    wallet->external_signer = true;

    const PartiallySignedTransaction psbt{MakeReviewPsbt()};
    const COutPoint owned_outpoint{FirstInputPrevout(psbt)};
    wallet->txin_is_mine_fn = [owned_outpoint](const CTxIn& txin) {
        return txin.prevout == owned_outpoint;
    };

    const std::vector<unsigned char> signer_key{0x53};
    const std::vector<unsigned char> signer_value{0x99};
    wallet->fill_psbt_fn = [wallet, signer_key, signer_value](const common::PSBTFillOptions& options,
                                                              size_t* n_signed,
                                                              PartiallySignedTransaction& psbt,
                                                              bool& complete) {
        const bool sign{options.sign};
        wallet->fill_psbt_sign_args.push_back(sign);
        if (n_signed) {
            *n_signed = 1;
        }
        if (sign) {
            psbt.inputs[0].unknown[signer_key] = signer_value;
        }
        complete = false;
        return std::nullopt;
    };

    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());
    const QString path{WritePsbt(psbt, temp_dir, QStringLiteral("incomplete-external.psbt"))};
    QVERIFY(!path.isEmpty());
    QCOMPARE(model->importPsbtFromFile(path), WalletQmlModel::PsbtImportResult::WalletCanSign);

    QSignalSpy succeeded_spy{model.get(), &WalletQmlModel::externalSignerApprovalSucceeded};
    QSignalSpy partially_succeeded_spy{model.get(), &WalletQmlModel::externalSignerApprovalPartiallySucceeded};
    QSignalSpy failed_spy{model.get(), &WalletQmlModel::externalSignerApprovalFailed};
    model->approveExternalSignerTransaction();

    QCOMPARE(succeeded_spy.count(), 0);
    QCOMPARE(partially_succeeded_spy.count(), 1);
    QCOMPARE(failed_spy.count(), 0);
    QVERIFY(!model->currentTransactionCanSend());
    QVERIFY(!model->currentTransactionCanBroadcast());
    QCOMPARE(model->currentTransactionReviewMessage(), QString("Signed on external signer. More signatures are required."));
    QCOMPARE(std::count(wallet->fill_psbt_sign_args.begin(), wallet->fill_psbt_sign_args.end(), true), 1);

    const QString saved_path{temp_dir.filePath(QStringLiteral("saved-incomplete-external.psbt"))};
    QCOMPARE(model->saveCurrentTransactionAsPsbt(saved_path), QString{});

    CMutableTransaction empty_tx;
    PartiallySignedTransaction saved{empty_tx};
    QCOMPARE(PsbtQmlModel::LoadPsbtFromFile(saved_path, saved), QString{});
    QVERIFY(saved.inputs[0].unknown.contains(signer_key));
    QCOMPARE(saved.inputs[0].unknown.at(signer_key), signer_value);
}

void WalletQmlModelTests::importPsbtFromFile_broadcastsCompleteForeignMultisigPsbt()
{
    MockNode node;
    auto [wallet, model] = MakePasswordWalletModel(&node);

    wallet->fill_psbt_fn = [wallet](const common::PSBTFillOptions& options,
                                    size_t* n_signed,
                                    PartiallySignedTransaction&,
                                    bool& complete) {
        const bool sign{options.sign};
        wallet->fill_psbt_sign_args.push_back(sign);
        if (n_signed) {
            *n_signed = 0;
        }
        complete = false;
        return std::nullopt;
    };

    CKey key1;
    CKey key2;
    key1.MakeNewKey(true);
    key2.MakeNewKey(true);
    const CScript witness_script{GetScriptForMultisig(2, {key1.GetPubKey(), key2.GetPubKey()})};

    CMutableTransaction mtx;
    mtx.vin.emplace_back(COutPoint{Txid::FromUint256(uint256::ONE), 0});
    mtx.vout.emplace_back(1'500, GetScriptForDestination(DecodeDestination(VALID_MAINNET_ADDRESS.toStdString())));
    PartiallySignedTransaction psbt{mtx};
    psbt.inputs[0].witness_utxo = CTxOut{2'000, GetScriptForDestination(WitnessV0ScriptHash(witness_script))};
    psbt.inputs[0].witness_script = witness_script;
    QVERIFY(PsbtQmlModel::IsMultisigPsbtInput(psbt, 0));

    FillableSigningProvider provider;
    QVERIFY(provider.AddCScript(witness_script));
    QVERIFY(provider.AddKey(key1));
    QVERIFY(provider.AddKey(key2));
    const auto txdata{PrecomputePSBTData(psbt)};
    QVERIFY(txdata.has_value());
    QCOMPARE(SignPSBTInput(provider, psbt, 0, &*txdata, /*options=*/{}), PSBTError::OK);
    QVERIFY(FinalizePSBT(psbt));

    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());
    const QString path{temp_dir.filePath(QStringLiteral("complete-multisig.psbt"))};
    QFile file{path};
    QVERIFY(file.open(QIODevice::WriteOnly));
    const QByteArray raw{PsbtQmlModel::SerializePsbtRaw(psbt)};
    QCOMPARE(file.write(raw), raw.size());
    file.close();

    QCOMPARE(model->importPsbtFromFile(path), WalletQmlModel::PsbtImportResult::WalletCannotSign);
    QVERIFY(!model->currentTransactionCanSend());
    QVERIFY(model->currentTransactionCanBroadcast());
    QVERIFY(model->currentTransactionReviewMessage().isEmpty());
    QCOMPARE(wallet->commit_calls, 0);

    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    node.broadcast_transaction_fn = [](CTransactionRef, CAmount, std::string&) { return node::TransactionError::OK; };
    node.ExpectExactly(node.calls.broadcastTransaction, 1);
    QVERIFY(model->broadcastCurrentTransaction());
    QCOMPARE(wallet->commit_calls, 0);
    QVERIFY(!model->currentTransactionCanBroadcast());
    QVERIFY(model->transactionError().isEmpty());
}

void WalletQmlModelTests::saveCompleteBroadcastableImportedPsbt_preservesOriginalPsbt()
{
    MockNode node;
    auto [wallet, model] = MakePasswordWalletModel(&node);

    wallet->fill_psbt_fn = [wallet](const common::PSBTFillOptions& options,
                                    size_t* n_signed,
                                    PartiallySignedTransaction&,
                                    bool& complete) {
        const bool sign{options.sign};
        wallet->fill_psbt_sign_args.push_back(sign);
        if (n_signed) {
            *n_signed = 0;
        }
        complete = false;
        return std::nullopt;
    };

    CKey key1;
    CKey key2;
    key1.MakeNewKey(true);
    key2.MakeNewKey(true);
    const CScript witness_script{GetScriptForMultisig(2, {key1.GetPubKey(), key2.GetPubKey()})};

    CMutableTransaction mtx;
    mtx.vin.emplace_back(COutPoint{Txid::FromUint256(uint256::ONE), 0});
    mtx.vout.emplace_back(1'500, GetScriptForDestination(DecodeDestination(VALID_MAINNET_ADDRESS.toStdString())));

    PartiallySignedTransaction psbt{mtx};
    psbt.inputs[0].witness_utxo = CTxOut{2'000, GetScriptForDestination(WitnessV0ScriptHash(witness_script))};
    psbt.inputs[0].witness_script = witness_script;

    FillableSigningProvider provider;
    QVERIFY(provider.AddCScript(witness_script));
    QVERIFY(provider.AddKey(key1));
    QVERIFY(provider.AddKey(key2));

    const auto txdata{PrecomputePSBTData(psbt)};
    QVERIFY(txdata.has_value());
    QCOMPARE(
        SignPSBTInput(provider, psbt, 0, &*txdata, {.finalize = false}),
        PSBTError::OK);

    QVERIFY(!psbt.inputs[0].partial_sigs.empty());
    QVERIFY(!psbt.inputs[0].witness_script.empty());
    QVERIFY(psbt.inputs[0].final_script_witness.IsNull());

    PartiallySignedTransaction finalized_copy{psbt};
    QVERIFY(FinalizePSBT(finalized_copy));
    QVERIFY(finalized_copy.inputs[0].partial_sigs.empty());
    QVERIFY(finalized_copy.inputs[0].witness_script.empty());
    QVERIFY(!finalized_copy.inputs[0].final_script_witness.IsNull());

    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());
    const QString source_path{WritePsbt(psbt, temp_dir, QStringLiteral("complete-unfinalized.psbt"))};
    QVERIFY(!source_path.isEmpty());

    QCOMPARE(model->importPsbtFromFile(source_path), WalletQmlModel::PsbtImportResult::WalletCannotSign);
    QVERIFY(!model->currentTransactionCanSend());
    QVERIFY(model->currentTransactionCanBroadcast());

    const QString saved_path{temp_dir.filePath(QStringLiteral("saved.psbt"))};
    QCOMPARE(model->saveCurrentTransactionAsPsbt(saved_path), QString{});

    CMutableTransaction empty_tx;
    PartiallySignedTransaction saved{empty_tx};
    QCOMPARE(PsbtQmlModel::LoadPsbtFromFile(saved_path, saved), QString{});

    QCOMPARE(saved.inputs[0].partial_sigs.size(), psbt.inputs[0].partial_sigs.size());
    QVERIFY(saved.inputs[0].witness_script == psbt.inputs[0].witness_script);
    QVERIFY(saved.inputs[0].final_script_witness.IsNull());
    QCOMPARE(PsbtQmlModel::SerializePsbtRaw(saved), PsbtQmlModel::SerializePsbtRaw(psbt));
}

void WalletQmlModelTests::importPsbtFromFile_skipsZeroValueOpReturnOutputs()
{
    auto [wallet, model] = MakePasswordWalletModel();

    wallet->fill_psbt_fn = [wallet](const common::PSBTFillOptions& options,
                                    size_t* n_signed,
                                    PartiallySignedTransaction&,
                                    bool& complete) {
        const bool sign{options.sign};
        wallet->fill_psbt_sign_args.push_back(sign);
        if (n_signed) {
            *n_signed = 0;
        }
        complete = false;
        return std::nullopt;
    };

    const PartiallySignedTransaction base_psbt{MakeReviewPsbt()};
    CMutableTransaction psbt_tx{UnsignedTx(base_psbt)};
    psbt_tx.vout.emplace_back(0, CScript{} << OP_RETURN << std::vector<unsigned char>{0x01, 0x02});
    PartiallySignedTransaction psbt{psbt_tx};
    psbt.inputs[0] = base_psbt.inputs[0];
    CompleteReviewPsbt(psbt);
    const QByteArray original{PsbtQmlModel::SerializePsbtRaw(psbt)};

    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());
    const QString source_path{WritePsbt(psbt, temp_dir, QStringLiteral("op-return.psbt"))};
    QVERIFY(!source_path.isEmpty());

    QCOMPARE(model->importPsbtFromFile(source_path), WalletQmlModel::PsbtImportResult::WalletCannotSign);
    QVERIFY(model->currentTransaction() != nullptr);
    QVERIFY(model->currentTransactionCanBroadcast());
    QCOMPARE(model->sendRecipientList()->count(), 1);
    QCOMPARE(model->sendRecipientList()->currentRecipient()->address()->address(), VALID_MAINNET_ADDRESS);
    QCOMPARE(model->sendRecipientList()->currentRecipient()->amount()->satoshi(), CAmount{1'500});

    const QString saved_path{temp_dir.filePath(QStringLiteral("op-return-saved.psbt"))};
    QCOMPARE(model->saveCurrentTransactionAsPsbt(saved_path), QString{});

    QFile saved_file{saved_path};
    QVERIFY(saved_file.open(QIODevice::ReadOnly));
    QCOMPARE(saved_file.readAll(), original);
}

void WalletQmlModelTests::importPsbtFromFile_opensUnsignedMultisigPsbtForReviewOnly()
{
    auto [wallet, model] = MakePasswordWalletModel();

    wallet->fill_psbt_fn = [wallet](const common::PSBTFillOptions& options,
                                    size_t* n_signed,
                                    PartiallySignedTransaction&,
                                    bool& complete) {
        const bool sign{options.sign};
        wallet->fill_psbt_sign_args.push_back(sign);
        if (n_signed) {
            *n_signed = 0;
        }
        complete = false;
        return std::nullopt;
    };

    CKey key1;
    CKey key2;
    CKey key3;
    key1.MakeNewKey(true);
    key2.MakeNewKey(true);
    key3.MakeNewKey(true);
    const CScript witness_script{GetScriptForMultisig(2, {key1.GetPubKey(), key2.GetPubKey(), key3.GetPubKey()})};

    CMutableTransaction mtx;
    mtx.vin.emplace_back(COutPoint{Txid::FromUint256(uint256::ONE), 0});
    mtx.vout.emplace_back(1'500, GetScriptForDestination(DecodeDestination(VALID_MAINNET_ADDRESS.toStdString())));
    PartiallySignedTransaction psbt{mtx};
    psbt.inputs[0].witness_utxo = CTxOut{2'000, GetScriptForDestination(WitnessV0ScriptHash(witness_script))};
    psbt.inputs[0].witness_script = witness_script;

    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());
    const QString path{WritePsbt(psbt, temp_dir, QStringLiteral("unsigned-multisig.psbt"))};
    QVERIFY(!path.isEmpty());

    QCOMPARE(model->importPsbtFromFile(path), WalletQmlModel::PsbtImportResult::WalletCannotSign);
    QVERIFY(model->currentTransaction() != nullptr);
    QVERIFY(!model->currentTransactionCanSend());
    QVERIFY(!model->currentTransactionCanBroadcast());
    QCOMPARE(model->currentTransactionReviewMessage(), QString("This transaction requires 2 of 3 signatures."));
}

void WalletQmlModelTests::importPsbtFromFile_blocksBroadcastWhenFeeIsInvalid()
{
    MockNode node;
    auto [wallet, model] = MakePasswordWalletModel(&node);

    CKey key1;
    CKey key2;
    key1.MakeNewKey(true);
    key2.MakeNewKey(true);
    const CScript witness_script{GetScriptForMultisig(2, {key1.GetPubKey(), key2.GetPubKey()})};

    CMutableTransaction mtx;
    mtx.vin.emplace_back(COutPoint{Txid::FromUint256(uint256::ONE), 0});
    mtx.vout.emplace_back(2'500, GetScriptForDestination(DecodeDestination(VALID_MAINNET_ADDRESS.toStdString())));
    PartiallySignedTransaction psbt{mtx};
    psbt.inputs[0].witness_utxo = CTxOut{2'000, GetScriptForDestination(WitnessV0ScriptHash(witness_script))};
    psbt.inputs[0].witness_script = witness_script;

    FillableSigningProvider provider;
    QVERIFY(provider.AddCScript(witness_script));
    QVERIFY(provider.AddKey(key1));
    QVERIFY(provider.AddKey(key2));
    const auto txdata{PrecomputePSBTData(psbt)};
    QVERIFY(txdata.has_value());
    QCOMPARE(SignPSBTInput(provider, psbt, 0, &*txdata, /*options=*/{}), PSBTError::OK);
    QVERIFY(FinalizePSBT(psbt));

    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());
    const QString path{WritePsbt(psbt, temp_dir, QStringLiteral("complete-with-invalid-fee.psbt"))};
    QVERIFY(!path.isEmpty());

    QCOMPARE(model->importPsbtFromFile(path), WalletQmlModel::PsbtImportResult::WalletCannotSign);
    QVERIFY(!model->currentTransactionCanSend());
    QVERIFY(!model->currentTransactionCanBroadcast());
    QCOMPARE(
        model->currentTransactionReviewMessage(),
        QString("The transaction fee is missing or invalid. Add valid input information before broadcasting."));

    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    node.ExpectNoCalls(node.calls.broadcastTransaction);
    QVERIFY(!model->broadcastCurrentTransaction());
    QCOMPARE(model->transactionError(), QString("This transaction is not ready to broadcast."));
}

void WalletQmlModelTests::importPsbtFromFile_returnsTransactionAlreadyKnownWhenTxIsInWallet()
{
    auto [wallet, model] = MakePasswordWalletModel();

    CMutableTransaction mtx;
    mtx.vin.emplace_back(COutPoint{Txid::FromUint256(uint256::ONE), 0});
    mtx.vout.emplace_back(1'500, GetScriptForDestination(DecodeDestination(VALID_MAINNET_ADDRESS.toStdString())));
    PartiallySignedTransaction psbt{mtx};
    psbt.inputs[0].witness_utxo = CTxOut{2'000, CScript{}};

    const Txid psbt_txid{UnsignedTx(psbt).GetHash()};
    wallet->known_txids.insert(psbt_txid);
    wallet->fill_psbt_sign_args.clear();

    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());
    const QString path{WritePsbt(psbt, temp_dir, QStringLiteral("already-known.psbt"))};
    QVERIFY(!path.isEmpty());

    QCOMPARE(model->importPsbtFromFile(path), WalletQmlModel::PsbtImportResult::TransactionAlreadyKnown);
    QCOMPARE(model->importedPsbt()->matchedTxid(), QString::fromStdString(psbt_txid.GetHex()));

    // The review/SendReview flow must be skipped entirely.
    QVERIFY(wallet->fill_psbt_sign_args.empty());
    QVERIFY(model->currentTransaction() == nullptr);
    QCOMPARE(model->sendRecipientList()->count(), 1);
    QVERIFY(model->sendRecipientList()->currentRecipient()->address()->address().isEmpty());
}

void WalletQmlModelTests::bumpTransactionOnLockedWalletRequiresPassword()
{
    auto [wallet, model] = MakePasswordWalletModel();
    auto* bump_model = model->bumpModel();

    bump_model->prepareFeeBump(QString::fromStdString(Txid::FromUint256(uint256::ONE).GetHex()), 1);
    QCOMPARE(bump_model->state(), BumpTransactionModel::NeedsConfirmation);

    QVERIFY(!bump_model->confirmFeeBump());
    QCOMPARE(bump_model->state(), BumpTransactionModel::NeedsConfirmation);
    QVERIFY(bump_model->needsUnlock());
    QCOMPARE(bump_model->errorText(), QString("Enter your wallet password to update this transaction."));
    QCOMPARE(wallet->unlock_calls, 0);
    QCOMPARE(wallet->sign_bump_calls, 0);
    QCOMPARE(wallet->commit_bump_calls, 0);
}

void WalletQmlModelTests::bumpTransactionWithPassphraseUnlocksCommitsAndRelocks()
{
    auto [wallet, model] = MakePasswordWalletModel();
    auto* bump_model = model->bumpModel();

    bump_model->prepareFeeBump(QString::fromStdString(Txid::FromUint256(uint256::ONE).GetHex()), 1);
    QCOMPARE(bump_model->state(), BumpTransactionModel::NeedsConfirmation);

    QVERIFY(bump_model->confirmFeeBumpWithPassphrase(QStringLiteral("secret")));
    QCOMPARE(bump_model->state(), BumpTransactionModel::Succeeded);
    QVERIFY(!bump_model->newTxid().isEmpty());
    QVERIFY(!bump_model->needsUnlock());
    QCOMPARE(wallet->unlock_calls, 1);
    QCOMPARE(wallet->unlock_passphrases.size(), size_t{1});
    QCOMPARE(wallet->unlock_passphrases.front(), std::string{"secret"});
    QCOMPARE(wallet->lock_calls, 1);
    QCOMPARE(wallet->sign_bump_calls, 1);
    QCOMPARE(wallet->commit_bump_calls, 1);
    QVERIFY(wallet->locked);
}

void WalletQmlModelTests::bumpTransactionWithWrongPassphraseDoesNotSign()
{
    auto [wallet, model] = MakePasswordWalletModel();
    wallet->unlock_fn = [wallet](const SecureString& passphrase) {
        ++wallet->unlock_calls;
        wallet->unlock_passphrases.emplace_back(passphrase.begin(), passphrase.end());
        return false;
    };
    auto* bump_model = model->bumpModel();

    bump_model->prepareFeeBump(QString::fromStdString(Txid::FromUint256(uint256::ONE).GetHex()), 1);
    QCOMPARE(bump_model->state(), BumpTransactionModel::NeedsConfirmation);

    QVERIFY(!bump_model->confirmFeeBumpWithPassphrase(QStringLiteral("wrong")));
    QCOMPARE(bump_model->state(), BumpTransactionModel::NeedsConfirmation);
    QVERIFY(bump_model->needsUnlock());
    QCOMPARE(bump_model->errorText(), QString("The wallet password you entered was incorrect."));
    QCOMPARE(wallet->unlock_calls, 1);
    QCOMPARE(wallet->lock_calls, 0);
    QCOMPARE(wallet->sign_bump_calls, 0);
    QCOMPARE(wallet->commit_bump_calls, 0);
    QVERIFY(wallet->locked);
}

void WalletQmlModelTests::signVerifyMessageRejectsNonP2PKHAddress()
{
    auto [wallet, model] = MakePasswordWalletModel();
    auto* sign_verify = model->signVerifyMessageModel();

    QVERIFY(!sign_verify->isLegacyP2PKHAddress(QString::fromLatin1(NON_P2PKH_ADDRESS)));
    QVERIFY(!sign_verify->signMessage(QString::fromLatin1(NON_P2PKH_ADDRESS), "message"));
    QCOMPARE(sign_verify->signingError(), QString("Enter a legacy P2PKH bitcoin address."));
    QCOMPARE(wallet->sign_message_calls, 0);
}

void WalletQmlModelTests::signVerifyMessageSignsWithLegacyP2PKHAddress()
{
    auto [wallet, model] = MakePasswordWalletModel();
    wallet->encrypted = false;
    wallet->locked = false;
    auto* sign_verify = model->signVerifyMessageModel();

    QVERIFY(sign_verify->isLegacyP2PKHAddress(VALID_MAINNET_ADDRESS));
    QVERIFY(sign_verify->signMessage(VALID_MAINNET_ADDRESS, "hello"));
    QCOMPARE(wallet->sign_message_calls, 1);
    QCOMPARE(wallet->last_signed_message, std::string("hello"));
    QCOMPARE(sign_verify->signature(), QString("fake-signature"));
    QVERIFY(sign_verify->signingError().isEmpty());
    QVERIFY(!sign_verify->signingNeedsUnlock());
}

void WalletQmlModelTests::signVerifyMessageWithPassphraseUnlocksSignsAndRelocks()
{
    auto [wallet, model] = MakePasswordWalletModel();
    auto* sign_verify = model->signVerifyMessageModel();

    QVERIFY(!sign_verify->signMessage(VALID_MAINNET_ADDRESS, "hello"));
    QVERIFY(sign_verify->signingNeedsUnlock());
    QCOMPARE(wallet->unlock_calls, 0);

    QVERIFY(sign_verify->signMessageWithPassphrase(VALID_MAINNET_ADDRESS, "hello", "secret"));
    QCOMPARE(wallet->unlock_calls, 1);
    QCOMPARE(wallet->lock_calls, 1);
    QVERIFY(wallet->locked);
    QCOMPARE(sign_verify->signature(), QString("fake-signature"));
}

void WalletQmlModelTests::signVerifyMessageSurfacesSigningFailure()
{
    auto [wallet, model] = MakePasswordWalletModel();
    wallet->encrypted = false;
    wallet->locked = false;
    wallet->sign_message_fn = [](const std::string&, const PKHash&, std::string&) {
        return SigningResult::PRIVATE_KEY_NOT_AVAILABLE;
    };
    auto* sign_verify = model->signVerifyMessageModel();

    QVERIFY(!sign_verify->signMessage(VALID_MAINNET_ADDRESS, "hello"));
    QVERIFY(sign_verify->signature().isEmpty());
    QCOMPARE(sign_verify->signingError(), QString("Private key not available"));
}

void WalletQmlModelTests::signVerifyMessageVerifiesValidSignature()
{
    CKey key;
    key.MakeNewKey(true);
    const QString address{QString::fromStdString(EncodeDestination(PKHash(key.GetPubKey())))};
    const QString message{"hello"};
    std::string signature;
    QVERIFY(MessageSign(key, message.toStdString(), signature));

    auto [wallet, model] = MakePasswordWalletModel();
    auto* sign_verify = model->signVerifyMessageModel();

    QVERIFY(sign_verify->isLegacyP2PKHAddress(address));
    QVERIFY(sign_verify->verifyMessage(address, message, QString::fromStdString(signature)));
    QVERIFY(!sign_verify->verifyMessage(address, message + "!", QString::fromStdString(signature)));
    QVERIFY(!sign_verify->verifyMessage(QString::fromLatin1(NON_P2PKH_ADDRESS), message, QString::fromStdString(signature)));
}

void WalletQmlModelTests::signVerifyMessageSignsEmptyMessage()
{
    auto [wallet, model] = MakePasswordWalletModel();
    wallet->encrypted = false;
    wallet->locked = false;
    auto* sign_verify = model->signVerifyMessageModel();

    QVERIFY(sign_verify->signMessage(VALID_MAINNET_ADDRESS, QString()));
    QCOMPARE(wallet->sign_message_calls, 1);
    QCOMPARE(wallet->last_signed_message, std::string{});
    QCOMPARE(sign_verify->signature(), QString("fake-signature"));
    QVERIFY(sign_verify->signingError().isEmpty());
}

void WalletQmlModelTests::signVerifyMessageWrongPassphraseSurfacesErrorAndDoesNotRelock()
{
    auto [wallet, model] = MakePasswordWalletModel();
    wallet->unlock_fn = [wallet](const SecureString& passphrase) {
        ++wallet->unlock_calls;
        wallet->unlock_passphrases.emplace_back(passphrase.begin(), passphrase.end());
        return false;
    };
    auto* sign_verify = model->signVerifyMessageModel();

    QVERIFY(!sign_verify->signMessageWithPassphrase(VALID_MAINNET_ADDRESS, "hello", "wrong"));
    QCOMPARE(wallet->unlock_calls, 1);
    QCOMPARE(wallet->lock_calls, 0);
    QCOMPARE(wallet->sign_message_calls, 0);
    QCOMPARE(sign_verify->signingError(), QString("The wallet password you entered was incorrect."));
    QVERIFY(sign_verify->signature().isEmpty());
}

void WalletQmlModelTests::signVerifyMessageWithoutWalletSurfacesError()
{
    SignVerifyMessageModel sign_verify{nullptr};

    QVERIFY(!sign_verify.signMessage(VALID_MAINNET_ADDRESS, "hello"));
    QCOMPARE(sign_verify.signingError(), QString("No wallet is selected."));
    QVERIFY(sign_verify.signature().isEmpty());
}

void WalletQmlModelTests::signVerifyMessageClearResetsState()
{
    auto [wallet, model] = MakePasswordWalletModel();
    wallet->encrypted = false;
    wallet->locked = false;
    auto* sign_verify = model->signVerifyMessageModel();

    QVERIFY(sign_verify->signMessage(VALID_MAINNET_ADDRESS, "hello"));
    QVERIFY(!sign_verify->signature().isEmpty());

    QVERIFY(!sign_verify->signMessage(QString::fromLatin1(NON_P2PKH_ADDRESS), "hello"));
    QVERIFY(!sign_verify->signingError().isEmpty());
    QVERIFY(sign_verify->signature().isEmpty());

    sign_verify->clear();
    QVERIFY(sign_verify->signature().isEmpty());
    QVERIFY(sign_verify->signingError().isEmpty());
    QVERIFY(!sign_verify->signingNeedsUnlock());
}

void WalletQmlModelTests::signVerifyMessageVerifyRejectsEmptySignature()
{
    auto [wallet, model] = MakePasswordWalletModel();
    auto* sign_verify = model->signVerifyMessageModel();

    QVERIFY(!sign_verify->verifyMessage(VALID_MAINNET_ADDRESS, "hello", QString()));
    QVERIFY(!sign_verify->verifyMessage(VALID_MAINNET_ADDRESS, "hello", QStringLiteral("   ")));
}

void WalletQmlModelTests::sendTransactionWithPrivateKeysDisabledDoesNotCommit()
{
    auto [wallet, model] = MakePasswordWalletModel();
    SetPasswordRecipient(*model, 1'000);
    wallet->private_keys_disabled = true;

    QVERIFY(model->prepareTransaction());
    QVERIFY(wallet->locked);

    QVERIFY(!model->sendTransaction());
    QCOMPARE(model->transactionError(), QString("This wallet cannot sign transactions."));
    QCOMPARE(wallet->commit_calls, 0);
    QVERIFY(wallet->fill_psbt_sign_args.empty());
}

#ifdef BITCOINQML_NO_TEST_MAIN
#include <test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(WalletQmlModelTests)
#else
QTEST_MAIN(WalletQmlModelTests)
#endif
#include "test_walletqmlmodel.moc"
