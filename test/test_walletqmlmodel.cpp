// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QtTest/QtTest>

#include <test/mocks/mocknode.h>
#include <test/mocks/mockwallet.h>
#include <qml/models/activitylistmodel.h>
#include <qml/models/sendrecipient.h>
#include <qml/models/sendrecipientslistmodel.h>
#include <qml/models/walletqmlmodel.h>
#include <qml/models/walletqmlmodeltransaction.h>

#include <chainparams.h>
#include <addresstype.h>
#include <common/messages.h>
#include <common/signmessage.h>
#include <interfaces/handler.h>
#include <interfaces/wallet.h>
#include <key.h>
#include <key_io.h>
#include <outputtype.h>
#include <psbt.h>
#include <primitives/transaction.h>
#include <wallet/coincontrol.h>
#include <wallet/types.h>

#include <QSettings>
#include <QSignalSpy>
#include <QSemaphore>
#include <QVariantList>
#include <QVariantMap>

#include <algorithm>
#include <atomic>
#include <functional>
#include <memory>
#include <numeric>
#include <optional>
#include <string>
#include <vector>

namespace {
std::unique_ptr<interfaces::Handler> MakeNoopHandler()
{
    return interfaces::MakeCleanupHandler([] {});
}

constexpr auto NON_P2PKH_ADDRESS{"bcrt1qqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqq3xueyj"};

using ::testing::AtLeast;
using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::Return;

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

std::unique_ptr<WalletQmlModel> MakeWalletModel(NiceMock<MockWallet>*& wallet_out, interfaces::Node* node = nullptr)
{
    auto wallet = std::make_unique<NiceMock<MockWallet>>();
    wallet_out = wallet.get();

    ON_CALL(*wallet_out, getWalletTxs()).WillByDefault(Return(std::set<interfaces::WalletTx>{}));
    ON_CALL(*wallet_out, listCoins()).WillByDefault(Return(interfaces::Wallet::CoinsList{}));
    ON_CALL(*wallet_out, getBalance()).WillByDefault(Return(10 * COIN));
    ON_CALL(*wallet_out, getAvailableBalance(testing::_)).WillByDefault(Return(10 * COIN));
    ON_CALL(*wallet_out, getRequiredFee(testing::_)).WillByDefault(Return(1000));
    ON_CALL(*wallet_out, getDefaultAddressType()).WillByDefault(Return(OutputType::BECH32));
    ON_CALL(*wallet_out, handleTransactionChanged(testing::_)).WillByDefault(Invoke([](interfaces::Wallet::TransactionChangedFn) {
        return std::unique_ptr<interfaces::Handler>{};
    }));
    ON_CALL(*wallet_out, getNewDestinationValue(testing::_, testing::_)).WillByDefault(Invoke([](OutputType, const std::string&) {
        return DecodeDestination(VALID_MAINNET_ADDRESS.toStdString());
    }));

    return std::make_unique<WalletQmlModel>(std::move(wallet), node);
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
    std::function<std::optional<common::PSBTError>(std::optional<int>,
                                                   bool,
                                                   bool,
                                                   size_t*,
                                                   PartiallySignedTransaction&,
                                                   bool&)>
        fill_psbt_fn = [this](std::optional<int>,
                              bool sign,
                              bool,
                              size_t*,
                              PartiallySignedTransaction&,
                              bool& complete) {
            fill_psbt_sign_args.push_back(sign);
            complete = sign;
            return std::nullopt;
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
    bool setAddressBook(const CTxDestination&, const std::string&, const std::optional<wallet::AddressPurpose>&) override { return true; }
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
        return true;
    }
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
    std::optional<common::PSBTError> fillPSBT(std::optional<int> sighash_type,
                                              bool sign,
                                              bool bip32derivs,
                                              size_t* n_signed,
                                              PartiallySignedTransaction& psbtx,
                                              bool& complete) override
    {
        return fill_psbt_fn(sighash_type, sign, bip32derivs, n_signed, psbtx, complete);
    }
    CAmount getBalance() override { return balance; }
    CAmount getAvailableBalance(const wallet::CCoinControl&) override { return balance; }
    bool txinIsMine(const CTxIn&) override { return false; }
    bool txoutIsMine(const CTxOut&) override { return false; }
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

std::unique_ptr<WalletQmlModel> MakeWalletModel(FakePasswordWallet*& wallet_out, interfaces::Node* node = nullptr)
{
    auto wallet = std::make_unique<FakePasswordWallet>();
    wallet_out = wallet.get();
    return std::make_unique<WalletQmlModel>(std::move(wallet), node);
}

void SetPasswordRecipient(WalletQmlModel& model, qint64 satoshis)
{
    auto* recipient = model.sendRecipientList()->currentRecipient();
    QVERIFY(recipient != nullptr);

    recipient->address()->setAddress(VALID_MAINNET_ADDRESS, 0);
    recipient->amount()->setSatoshi(satoshis);

    QVERIFY2(recipient->isValid(), "Recipient must be valid before preparing a transaction");
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
    FakePasswordWallet* wallet{nullptr};
    auto model = MakeWalletModel(wallet);

    QCOMPARE(model->displayName(), QString("fake-wallet"));
    model->setDisplayName("Personal");
    QCOMPARE(model->displayName(), QString("Personal"));
}

void WalletQmlModelTests::detailPropertiesReflectWalletCapabilities()
{
    FakePasswordWallet* wallet{nullptr};
    auto model = MakeWalletModel(wallet);

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
    FakePasswordWallet* wallet{nullptr};
    auto model = MakeWalletModel(wallet);

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
    FakePasswordWallet* wallet{nullptr};
    auto model = MakeWalletModel(wallet);

    QVERIFY(model->backupWallet("/tmp/fake-wallet.bak"));
    QCOMPARE(wallet->backup_calls, 1);
    QCOMPARE(wallet->last_backup_path, std::string("/tmp/fake-wallet.bak"));
    QVERIFY(model->settingsError().isEmpty());
}

void WalletQmlModelTests::availableReceiveAddressTypesHideUnavailableTaproot()
{
    FakePasswordWallet* wallet{nullptr};
    auto model = MakeWalletModel(wallet);

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

    FakePasswordWallet* wallet{nullptr};
    auto model = MakeWalletModel(wallet);
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
    NiceMock<MockWallet>* wallet{nullptr};
    auto model = MakeWalletModel(wallet);
    SetValidRecipient(*model);

    QSemaphore release_first_call;
    std::atomic<bool> first_call_started{false};
    std::atomic<bool> first_call_blocked{false};
    std::atomic<bool> saw_sign_true{false};
    std::atomic<bool> saw_wrong_recipient_count{false};
    std::atomic<bool> saw_selected_inputs{false};
    std::atomic<bool> saw_nonempty_feerate{false};
    std::vector<unsigned int> requested_targets;

    EXPECT_CALL(*wallet, getNewDestinationValue(testing::_, testing::_)).Times(0);
    wallet->createTransactionHandler = [&](const std::vector<wallet::CRecipient>& recipients,
                                           const wallet::CCoinControl& coin_control,
                                           bool sign,
                                           int& change_pos,
                                           CAmount& fee) -> util::Result<CTransactionRef> {
        if (sign) saw_sign_true = true;
        if (recipients.size() != 1U) saw_wrong_recipient_count = true;
        if (coin_control.HasSelected() || !coin_control.ListSelected().empty()) saw_selected_inputs = true;
        if (coin_control.m_feerate.has_value()) saw_nonempty_feerate = true;

        requested_targets.push_back(coin_control.m_confirm_target.value_or(0));
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

    QTRY_VERIFY_WITH_TIMEOUT(requested_targets.size() == 3, FEE_ESTIMATE_TIMEOUT_MS);
    QVERIFY(!saw_sign_true.load());
    QVERIFY(!saw_wrong_recipient_count.load());
    QVERIFY(!saw_selected_inputs.load());
    QVERIFY(!saw_nonempty_feerate.load());
    QCOMPARE(requested_targets.at(0), 1U);
    QCOMPARE(requested_targets.at(1), 2U);
    QCOMPARE(requested_targets.at(2), 6U);
    QTRY_VERIFY_WITH_TIMEOUT(!model->feeEstimatePending(), FEE_ESTIMATE_TIMEOUT_MS);
    QCOMPARE(model->estimatedFeeForTarget(1), QStringLiteral("0.00000100 ₿"));
    QCOMPARE(model->estimatedFeeForTarget(2), QStringLiteral("0.00000200 ₿"));
    QCOMPARE(model->estimatedFeeForTarget(6), QStringLiteral("0.00000600 ₿"));
    QCOMPARE(model->estimatedFee(), QStringLiteral("0.00000200 ₿"));
}

void WalletQmlModelTests::scheduleFeeEstimates_fallsBackWhenNetworkFeeEstimatesUnavailable()
{
    NiceMock<MockWallet>* wallet{nullptr};
    auto model = MakeWalletModel(wallet);
    SetValidRecipient(*model);

    std::atomic<bool> saw_fallback_feerate{false};
    std::vector<CAmount> fallback_fee_rates;

    EXPECT_CALL(*wallet, getNewDestinationValue(testing::_, testing::_)).Times(0);
    EXPECT_CALL(*wallet, getRequiredFee(1000)).Times(AtLeast(3)).WillRepeatedly(Return(1000));
    wallet->createTransactionHandler = [&](const std::vector<wallet::CRecipient>&,
                                           const wallet::CCoinControl& coin_control,
                                           bool,
                                           int& change_pos,
                                           CAmount& fee) -> util::Result<CTransactionRef> {
        change_pos = -1;

        if (!coin_control.m_feerate.has_value()) {
            return util::Error{Untranslated("fee estimation unavailable")};
        }

        saw_fallback_feerate = true;
        fallback_fee_rates.push_back(coin_control.m_feerate->GetFeePerK());
        fee = coin_control.m_feerate->GetFee(250);
        return util::Result<CTransactionRef>{MakeTransactionRef(CMutableTransaction{})};
    };

    model->scheduleFeeEstimates();

    QTRY_COMPARE_WITH_TIMEOUT(model->estimatedFeeForTarget(2), QStringLiteral("0.00000500 ₿"), FEE_ESTIMATE_TIMEOUT_MS);
    QTRY_VERIFY_WITH_TIMEOUT(!model->feeEstimatePending(), FEE_ESTIMATE_TIMEOUT_MS);
    QVERIFY(saw_fallback_feerate.load());
    QVERIFY(!fallback_fee_rates.empty());
    QCOMPARE(model->estimatedFeeForTarget(1), QStringLiteral("0.00000750 ₿"));
    QCOMPARE(model->estimatedFeeForTarget(2), QStringLiteral("0.00000500 ₿"));
    QCOMPARE(model->estimatedFeeForTarget(6), QStringLiteral("0.00000250 ₿"));
    QCOMPARE(model->estimatedFee(), QStringLiteral("0.00000500 ₿"));
    QCOMPARE(fallback_fee_rates.size(), 3U);
    QCOMPARE(fallback_fee_rates.at(0), CAmount{3000});
    QCOMPARE(fallback_fee_rates.at(1), CAmount{2000});
    QCOMPARE(fallback_fee_rates.at(2), CAmount{1000});
}

void WalletQmlModelTests::scheduleFeeEstimates_usesStaticRegtestFeeOverride()
{
    ChainSelectionGuard chain_guard{ChainType::REGTEST};
    NiceMock<MockWallet>* wallet{nullptr};
    auto model = MakeWalletModel(wallet);
    SetValidRecipient(*model, VALID_REGTEST_ADDRESS);

    bool saw_sign_true{false};
    std::vector<unsigned int> requested_targets;
    std::vector<CAmount> requested_fee_rates;

    EXPECT_CALL(*wallet, getNewDestinationValue(testing::_, testing::_)).Times(0);
    EXPECT_CALL(*wallet, getRequiredFee(1000)).Times(0);
    wallet->createTransactionHandler = [&](const std::vector<wallet::CRecipient>&,
                                           const wallet::CCoinControl& coin_control,
                                           bool sign,
                                           int& change_pos,
                                           CAmount& fee) -> util::Result<CTransactionRef> {
        if (sign) saw_sign_true = true;
        requested_targets.push_back(coin_control.m_confirm_target.value_or(0));
        requested_fee_rates.push_back(coin_control.m_feerate.has_value() ? coin_control.m_feerate->GetFeePerK() : 0);
        change_pos = -1;

        if (!coin_control.m_feerate.has_value()) return util::Error{Untranslated("missing regtest fee override")};

        fee = coin_control.m_feerate->GetFee(250);
        return util::Result<CTransactionRef>{MakeTransactionRef(CMutableTransaction{})};
    };

    model->scheduleFeeEstimates();

    QTRY_COMPARE_WITH_TIMEOUT(model->estimatedFeeForTarget(2), QStringLiteral("0.00000250 ₿"), FEE_ESTIMATE_TIMEOUT_MS);
    QVERIFY(!saw_sign_true);
    QCOMPARE(model->estimatedFeeForTarget(1), QStringLiteral("0.00000250 ₿"));
    QCOMPARE(model->estimatedFeeForTarget(2), QStringLiteral("0.00000250 ₿"));
    QCOMPARE(model->estimatedFeeForTarget(6), QStringLiteral("0.00000250 ₿"));
    QCOMPARE(requested_targets.size(), 3U);
    QCOMPARE(requested_targets.at(0), 0U);
    QCOMPARE(requested_targets.at(1), 0U);
    QCOMPARE(requested_targets.at(2), 0U);
    QCOMPARE(requested_fee_rates.size(), 3U);
    QCOMPARE(requested_fee_rates.at(0), CAmount{wallet::DEFAULT_TRANSACTION_MINFEE});
    QCOMPARE(requested_fee_rates.at(1), CAmount{wallet::DEFAULT_TRANSACTION_MINFEE});
    QCOMPARE(requested_fee_rates.at(2), CAmount{wallet::DEFAULT_TRANSACTION_MINFEE});
}

void WalletQmlModelTests::scheduleFeeEstimates_usesCustomFeeRateWhenEnabled()
{
    NiceMock<MockWallet>* wallet{nullptr};
    auto model = MakeWalletModel(wallet);
    SetValidRecipient(*model);

    std::vector<unsigned int> requested_targets;
    std::vector<CAmount> requested_fee_rates;

    EXPECT_CALL(*wallet, getNewDestinationValue(testing::_, testing::_)).Times(0);
    wallet->createTransactionHandler = [&](const std::vector<wallet::CRecipient>&,
                                           const wallet::CCoinControl& coin_control,
                                           bool,
                                           int& change_pos,
                                           CAmount& fee) -> util::Result<CTransactionRef> {
        requested_targets.push_back(coin_control.m_confirm_target.value_or(0));
        requested_fee_rates.push_back(coin_control.m_feerate.has_value() ? coin_control.m_feerate->GetFeePerK() : 0);
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
    QCOMPARE(requested_targets.size(), 4U);
    QCOMPARE(requested_fee_rates.size(), 4U);
    QVERIFY(std::find(requested_fee_rates.begin(), requested_fee_rates.end(), CAmount{2000}) != requested_fee_rates.end());
    QVERIFY(std::find(requested_targets.begin(), requested_targets.end(), 1U) != requested_targets.end());
    QVERIFY(std::find(requested_targets.begin(), requested_targets.end(), 2U) != requested_targets.end());
    QVERIFY(std::find(requested_targets.begin(), requested_targets.end(), 6U) != requested_targets.end());
    QVERIFY(std::find(requested_targets.begin(), requested_targets.end(), 0U) != requested_targets.end());
}

void WalletQmlModelTests::scheduleFeeEstimates_estimatesWhenAmountWouldExceedBalanceWithFee()
{
    NiceMock<MockWallet>* wallet{nullptr};
    auto model = MakeWalletModel(wallet);
    ON_CALL(*wallet, getBalance()).WillByDefault(Return(50'000));
    ON_CALL(*wallet, getAvailableBalance(testing::_)).WillByDefault(Return(50'000));
    SetValidRecipient(*model);
    auto* recipient = model->sendRecipientList()->currentRecipient();
    QVERIFY(recipient != nullptr);
    recipient->amount()->setSatoshi(49'850);
    QVERIFY(!model->sendAmountExhaustsBalance());
    QSignalSpy balance_exhausted_spy{model.get(), &WalletQmlModel::sendAmountExhaustsBalanceChanged};

    bool saw_regular_recipient{false};
    bool saw_fee_subtracted_recipient{false};
    std::vector<unsigned int> requested_targets;

    EXPECT_CALL(*wallet, getRequiredFee(testing::_)).Times(0);
    wallet->createTransactionHandler = [&](const std::vector<wallet::CRecipient>& recipients,
                                           const wallet::CCoinControl& coin_control,
                                           bool,
                                           int& change_pos,
                                           CAmount& fee) -> util::Result<CTransactionRef> {
        requested_targets.push_back(coin_control.m_confirm_target.value_or(0));
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

        if (subtracts_fee) {
            saw_fee_subtracted_recipient = true;
            return util::Result<CTransactionRef>{MakeTransactionRef(CMutableTransaction{})};
        }

        saw_regular_recipient = true;
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
    QVERIFY(saw_regular_recipient);
    QVERIFY(saw_fee_subtracted_recipient);
    QVERIFY(requested_targets.size() >= 5);
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
    FakePasswordWallet* wallet{nullptr};
    auto model = MakeWalletModel(wallet);
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
    NiceMock<MockWallet>* wallet{nullptr};
    auto model = MakeWalletModel(wallet);
    SetValidRecipient(*model);

    const COutPoint selected_outpoint{Txid::FromUint256(uint256::ONE), 1};
    bool create_transaction_called{false};

    EXPECT_CALL(*wallet, getBalance()).Times(0);
    EXPECT_CALL(*wallet, getAvailableBalance(testing::_))
        .WillRepeatedly(Invoke([&](const wallet::CCoinControl& coin_control) {
            if (coin_control.HasSelected()) {
                EXPECT_TRUE(coin_control.IsSelected(selected_outpoint));
                EXPECT_FALSE(coin_control.m_allow_other_inputs);
                return CAmount{20'000};
            }

            EXPECT_TRUE(coin_control.m_allow_other_inputs);
            return CAmount{100'000};
        }));

    wallet->createTransactionHandler = [&](const std::vector<wallet::CRecipient>&,
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
}

void WalletQmlModelTests::scheduleFeeEstimates_usesDummyPreviewChangeDestination()
{
    NiceMock<MockWallet>* wallet{nullptr};
    auto model = MakeWalletModel(wallet);
    SetValidRecipient(*model);

    std::atomic<int> create_transaction_calls{0};
    std::atomic<bool> saw_missing_change{false};
    std::atomic<bool> saw_wrong_change_type{false};

    EXPECT_CALL(*wallet, getNewDestinationValue(testing::_, testing::_)).Times(0);
    wallet->createTransactionHandler = [&](const std::vector<wallet::CRecipient>&,
                                           const wallet::CCoinControl& coin_control,
                                           bool,
                                           int& change_pos,
                                           CAmount& fee) -> util::Result<CTransactionRef> {
        ++create_transaction_calls;
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

    QTRY_COMPARE_WITH_TIMEOUT(create_transaction_calls.load(), 3, FEE_ESTIMATE_TIMEOUT_MS);
    QVERIFY(!saw_missing_change.load());
    QVERIFY(!saw_wrong_change_type.load());
}

void WalletQmlModelTests::prepareTransaction_usesStaticRegtestFeeOverride()
{
    ChainSelectionGuard chain_guard{ChainType::REGTEST};
    NiceMock<MockWallet>* wallet{nullptr};
    auto model = MakeWalletModel(wallet);
    SetValidRecipient(*model, VALID_REGTEST_ADDRESS);

    bool saw_sign_false{false};
    std::vector<unsigned int> requested_targets;
    std::vector<CAmount> requested_fee_rates;

    EXPECT_CALL(*wallet, getRequiredFee(1000)).Times(0);
    wallet->createTransactionHandler = [&](const std::vector<wallet::CRecipient>&,
                                           const wallet::CCoinControl& coin_control,
                                           bool sign,
                                           int& change_pos,
                                           CAmount& fee) -> util::Result<CTransactionRef> {
        if (!sign) saw_sign_false = true;
        requested_targets.push_back(coin_control.m_confirm_target.value_or(0));
        requested_fee_rates.push_back(coin_control.m_feerate.has_value() ? coin_control.m_feerate->GetFeePerK() : 0);
        change_pos = -1;

        if (!coin_control.m_feerate.has_value()) return util::Error{Untranslated("missing regtest fee override")};

        fee = coin_control.m_feerate->GetFee(250);
        return util::Result<CTransactionRef>{MakeTransactionRef(CMutableTransaction{})};
    };

    QVERIFY(model->prepareTransaction());
    QVERIFY(!saw_sign_false);
    QVERIFY(model->currentTransaction() != nullptr);
    QCOMPARE(model->currentTransaction()->feeAmount()->satoshi(), CAmount{250});
    QCOMPARE(requested_targets.size(), 1U);
    QCOMPARE(requested_targets.at(0), 0U);
    QCOMPARE(requested_fee_rates.size(), 1U);
    QCOMPARE(requested_fee_rates.at(0), CAmount{wallet::DEFAULT_TRANSACTION_MINFEE});
}

void WalletQmlModelTests::prepareTransaction_usesCustomFeeRateWithoutRegtestOverride()
{
    ChainSelectionGuard chain_guard{ChainType::REGTEST};
    NiceMock<MockWallet>* wallet{nullptr};
    auto model = MakeWalletModel(wallet);
    SetValidRecipient(*model, VALID_REGTEST_ADDRESS);

    std::vector<unsigned int> requested_targets;
    std::vector<CAmount> requested_fee_rates;

    EXPECT_CALL(*wallet, getRequiredFee(1000)).Times(0);
    wallet->createTransactionHandler = [&](const std::vector<wallet::CRecipient>&,
                                           const wallet::CCoinControl& coin_control,
                                           bool,
                                           int& change_pos,
                                           CAmount& fee) -> util::Result<CTransactionRef> {
        requested_targets.push_back(coin_control.m_confirm_target.value_or(0));
        requested_fee_rates.push_back(coin_control.m_feerate.has_value() ? coin_control.m_feerate->GetFeePerK() : 0);
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
    QCOMPARE(requested_targets.size(), 1U);
    QCOMPARE(requested_targets.at(0), 0U);
    QCOMPARE(requested_fee_rates.size(), 1U);
    QCOMPARE(requested_fee_rates.at(0), CAmount{2000});
}

void WalletQmlModelTests::prepareTransaction_reassignsAmountWhenFeeIncluded()
{
    NiceMock<MockWallet>* wallet{nullptr};
    auto model = MakeWalletModel(wallet);
    SetValidRecipient(*model, VALID_MAINNET_ADDRESS, /*subtract_fee_from_amount=*/true);

    bool saw_subtract_fee_from_amount{false};
    bool saw_wrong_recipient_count{false};
    wallet->createTransactionHandler = [&](const std::vector<wallet::CRecipient>& recipients,
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
    NiceMock<MockWallet>* wallet{nullptr};
    auto model = MakeWalletModel(wallet);
    SetValidRecipient(*model);

    const COutPoint selected_outpoint{Txid{}, 5};
    std::atomic<int> create_transaction_calls{0};
    std::atomic<bool> saw_sign_true{false};
    std::atomic<bool> saw_missing_selection{false};
    std::atomic<bool> saw_other_inputs_allowed{false};
    std::atomic<int> selected_count{-1};
    std::vector<unsigned int> selected_targets;

    EXPECT_CALL(*wallet, getNewDestinationValue(testing::_, testing::_)).Times(0);
    wallet->createTransactionHandler = [&](const std::vector<wallet::CRecipient>&,
                                           const wallet::CCoinControl& coin_control,
                                           bool sign,
                                           int& change_pos,
                                           CAmount& fee) -> util::Result<CTransactionRef> {
        if (sign) saw_sign_true = true;
        if (!coin_control.HasSelected() || !coin_control.IsSelected(selected_outpoint)) saw_missing_selection = true;
        if (coin_control.m_allow_other_inputs) saw_other_inputs_allowed = true;
        const auto selected = coin_control.ListSelected();
        selected_count = static_cast<int>(selected.size());
        if (selected.size() != 1U || selected.front() != selected_outpoint) saw_missing_selection = true;

        selected_targets.push_back(coin_control.m_confirm_target.value_or(0));
        ++create_transaction_calls;
        change_pos = -1;
        fee = coin_control.m_confirm_target.value_or(0) * 200;
        return util::Result<CTransactionRef>{MakeTransactionRef(CMutableTransaction{})};
    };

    model->selectCoin(selected_outpoint);

    QTRY_COMPARE_WITH_TIMEOUT(create_transaction_calls.load(), 3, FEE_ESTIMATE_TIMEOUT_MS);
    QVERIFY(!saw_sign_true.load());
    QVERIFY(!saw_missing_selection.load());
    QVERIFY(!saw_other_inputs_allowed.load());
    QCOMPARE(selected_count.load(), 1);
    QCOMPARE(selected_targets.size(), 3U);
    QCOMPARE(selected_targets.at(0), 1U);
    QCOMPARE(selected_targets.at(1), 2U);
    QCOMPARE(selected_targets.at(2), 6U);
}

void WalletQmlModelTests::prepareTransaction_disallowsOtherInputsWhenCoinsSelected()
{
    NiceMock<MockWallet>* wallet{nullptr};
    auto model = MakeWalletModel(wallet);
    SetValidRecipient(*model);

    const COutPoint selected_outpoint{Txid::FromUint256(uint256::ONE), 1};
    std::optional<bool> available_balance_allow_other_inputs;
    bool saw_selected_coin{false};
    bool saw_other_inputs_allowed{true};
    int create_transaction_calls{0};

    EXPECT_CALL(*wallet, getAvailableBalance(testing::_))
        .WillOnce(Invoke([&](const wallet::CCoinControl& coin_control) {
            available_balance_allow_other_inputs = coin_control.m_allow_other_inputs;
            return 10 * COIN;
        }));

    wallet->createTransactionHandler = [&](const std::vector<wallet::CRecipient>&,
                                           const wallet::CCoinControl& coin_control,
                                           bool,
                                           int& change_pos,
                                           CAmount& fee) -> util::Result<CTransactionRef> {
        ++create_transaction_calls;
        saw_selected_coin = coin_control.HasSelected() && coin_control.IsSelected(selected_outpoint);
        saw_other_inputs_allowed = coin_control.m_allow_other_inputs;
        change_pos = -1;
        fee = 200;
        return util::Result<CTransactionRef>{MakeTransactionRef(CMutableTransaction{})};
    };

    model->selectCoin(selected_outpoint);

    QVERIFY(model->prepareTransaction());
    QCOMPARE(create_transaction_calls, 1);
    QVERIFY(saw_selected_coin);
    QVERIFY(available_balance_allow_other_inputs.has_value());
    QVERIFY(!*available_balance_allow_other_inputs);
    QVERIFY(!saw_other_inputs_allowed);
}

void WalletQmlModelTests::scheduleFeeEstimates_debouncesRapidRestarts()
{
    NiceMock<MockWallet>* wallet{nullptr};
    auto model = MakeWalletModel(wallet);
    SetValidRecipient(*model);

    std::atomic<int> create_transaction_calls{0};

    EXPECT_CALL(*wallet, getNewDestinationValue(testing::_, testing::_)).Times(0);
    std::atomic<bool> saw_sign_true{false};
    wallet->createTransactionHandler = [&](const std::vector<wallet::CRecipient>&,
                                           const wallet::CCoinControl& coin_control,
                                           bool sign,
                                           int& change_pos,
                                           CAmount& fee) -> util::Result<CTransactionRef> {
        if (sign) saw_sign_true = true;
        ++create_transaction_calls;
        change_pos = -1;
        fee = coin_control.m_confirm_target.value_or(0) * 250;
        return util::Result<CTransactionRef>{MakeTransactionRef(CMutableTransaction{})};
    };

    model->scheduleFeeEstimates();
    model->scheduleFeeEstimates();
    model->scheduleFeeEstimates();

    QTRY_COMPARE_WITH_TIMEOUT(create_transaction_calls.load(), 3, FEE_ESTIMATE_TIMEOUT_MS);
    QVERIFY(!saw_sign_true.load());
    QTRY_VERIFY_WITH_TIMEOUT(!model->feeEstimatePending(), FEE_ESTIMATE_TIMEOUT_MS);
    QCOMPARE(model->estimatedFeeForTarget(1), QStringLiteral("0.00000250 ₿"));
    QCOMPARE(model->estimatedFeeForTarget(2), QStringLiteral("0.00000500 ₿"));
    QCOMPARE(model->estimatedFeeForTarget(6), QStringLiteral("0.00001500 ₿"));
}

void WalletQmlModelTests::scheduleFeeEstimates_invalidatesStalePreviewState()
{
    NiceMock<MockWallet>* wallet{nullptr};
    auto model = MakeWalletModel(wallet);
    ON_CALL(*wallet, getAvailableBalance(testing::_)).WillByDefault(Return(50'000));
    SetValidRecipient(*model);

    auto* recipient = model->sendRecipientList()->currentRecipient();
    QVERIFY(recipient != nullptr);
    recipient->amount()->setSatoshi(49'000);

    std::atomic<int> create_transaction_calls{0};
    std::atomic<bool> stale_request_started{false};
    std::atomic<bool> fresh_request_started{false};
    QSemaphore release_stale_request;
    QSemaphore release_fresh_request;

    wallet->createTransactionHandler = [&](const std::vector<wallet::CRecipient>& recipients,
                                           const wallet::CCoinControl& coin_control,
                                           bool,
                                           int& change_pos,
                                           CAmount& fee) -> util::Result<CTransactionRef> {
        ++create_transaction_calls;
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
    QVERIFY(create_transaction_calls.load() >= 6);
}

void WalletQmlModelTests::transactionChangedEmitsBalanceChanged()
{
    auto wallet = std::make_unique<NiceMock<MockWallet>>();
    auto* wallet_ptr = wallet.get();

    CAmount balance{50 * COIN};
    interfaces::Wallet::TransactionChangedFn transaction_changed;
    ON_CALL(*wallet_ptr, getBalance()).WillByDefault(Invoke([&] { return balance; }));
    ON_CALL(*wallet_ptr, handleTransactionChanged(testing::_)).WillByDefault(Invoke([&](interfaces::Wallet::TransactionChangedFn fn) {
        transaction_changed = std::move(fn);
        return std::unique_ptr<interfaces::Handler>{};
    }));

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
    FakePasswordWallet* wallet{nullptr};
    auto model = MakeWalletModel(wallet);

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
    FakePasswordWallet* wallet{nullptr};
    auto model = MakeWalletModel(wallet);
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
    FakePasswordWallet* wallet{nullptr};
    auto model = MakeWalletModel(wallet);
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
    FakePasswordWallet* wallet{nullptr};
    auto model = MakeWalletModel(wallet);
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
    FakePasswordWallet* wallet{nullptr};
    auto model = MakeWalletModel(wallet);
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
    FakePasswordWallet* wallet{nullptr};
    auto model = MakeWalletModel(wallet);
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
    FakePasswordWallet* wallet{nullptr};
    auto model = MakeWalletModel(wallet);
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

void WalletQmlModelTests::prepareTransactionOnLockedWalletRequiresPassword()
{
    FakePasswordWallet* wallet{nullptr};
    auto model = MakeWalletModel(wallet);
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
    FakePasswordWallet* wallet{nullptr};
    auto model = MakeWalletModel(wallet);
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
    FakePasswordWallet* wallet{nullptr};
    auto model = MakeWalletModel(wallet);
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
    FakePasswordWallet* wallet{nullptr};
    NiceMock<MockNode> node;
    EXPECT_CALL(node, getDustRelayFee()).Times(AtLeast(1)).WillRepeatedly(Return(CFeeRate{10'000}));
    auto model = MakeWalletModel(wallet, &node);
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
    FakePasswordWallet* wallet{nullptr};
    auto model = MakeWalletModel(wallet);
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
    FakePasswordWallet* wallet{nullptr};
    auto model = MakeWalletModel(wallet);
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
    FakePasswordWallet* wallet{nullptr};
    auto model = MakeWalletModel(wallet);
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
    FakePasswordWallet* wallet{nullptr};
    auto model = MakeWalletModel(wallet);
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
    FakePasswordWallet* wallet{nullptr};
    auto model = MakeWalletModel(wallet);
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
    FakePasswordWallet* wallet{nullptr};
    auto model = MakeWalletModel(wallet);
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
    FakePasswordWallet* wallet{nullptr};
    auto model = MakeWalletModel(wallet);
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
    FakePasswordWallet* wallet{nullptr};
    auto model = MakeWalletModel(wallet);
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
    FakePasswordWallet* wallet{nullptr};
    auto model = MakeWalletModel(wallet);
    const COutPoint selected_outpoint{Txid::FromUint256(uint256::ONE), 1};

    model->selectCoin(selected_outpoint);
    QCOMPARE(model->listSelectedCoins().size(), size_t{1});

    model->sendRecipientList()->clear();
    QVERIFY(model->listSelectedCoins().empty());
}

void WalletQmlModelTests::bumpTransactionOnLockedWalletRequiresPassword()
{
    FakePasswordWallet* wallet{nullptr};
    auto model = MakeWalletModel(wallet);
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
    FakePasswordWallet* wallet{nullptr};
    auto model = MakeWalletModel(wallet);
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
    FakePasswordWallet* wallet{nullptr};
    auto model = MakeWalletModel(wallet);
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
    FakePasswordWallet* wallet{nullptr};
    auto model = MakeWalletModel(wallet);
    auto* sign_verify = model->signVerifyMessageModel();

    QVERIFY(!sign_verify->isLegacyP2PKHAddress(QString::fromLatin1(NON_P2PKH_ADDRESS)));
    QVERIFY(!sign_verify->signMessage(QString::fromLatin1(NON_P2PKH_ADDRESS), "message"));
    QCOMPARE(sign_verify->signingError(), QString("Enter a legacy P2PKH bitcoin address."));
    QCOMPARE(wallet->sign_message_calls, 0);
}

void WalletQmlModelTests::signVerifyMessageSignsWithLegacyP2PKHAddress()
{
    FakePasswordWallet* wallet{nullptr};
    auto model = MakeWalletModel(wallet);
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
    FakePasswordWallet* wallet{nullptr};
    auto model = MakeWalletModel(wallet);
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
    FakePasswordWallet* wallet{nullptr};
    auto model = MakeWalletModel(wallet);
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

    FakePasswordWallet* wallet{nullptr};
    auto model = MakeWalletModel(wallet);
    auto* sign_verify = model->signVerifyMessageModel();

    QVERIFY(sign_verify->isLegacyP2PKHAddress(address));
    QVERIFY(sign_verify->verifyMessage(address, message, QString::fromStdString(signature)));
    QVERIFY(!sign_verify->verifyMessage(address, message + "!", QString::fromStdString(signature)));
    QVERIFY(!sign_verify->verifyMessage(QString::fromLatin1(NON_P2PKH_ADDRESS), message, QString::fromStdString(signature)));
}

void WalletQmlModelTests::signVerifyMessageSignsEmptyMessage()
{
    FakePasswordWallet* wallet{nullptr};
    auto model = MakeWalletModel(wallet);
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
    FakePasswordWallet* wallet{nullptr};
    auto model = MakeWalletModel(wallet);
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
    FakePasswordWallet* wallet{nullptr};
    auto model = MakeWalletModel(wallet);
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
    FakePasswordWallet* wallet{nullptr};
    auto model = MakeWalletModel(wallet);
    auto* sign_verify = model->signVerifyMessageModel();

    QVERIFY(!sign_verify->verifyMessage(VALID_MAINNET_ADDRESS, "hello", QString()));
    QVERIFY(!sign_verify->verifyMessage(VALID_MAINNET_ADDRESS, "hello", QStringLiteral("   ")));
}

void WalletQmlModelTests::sendTransactionWithPrivateKeysDisabledDoesNotCommit()
{
    FakePasswordWallet* wallet{nullptr};
    auto model = MakeWalletModel(wallet);
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
