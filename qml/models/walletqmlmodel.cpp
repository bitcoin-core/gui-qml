
// Copyright (c) 2024-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/walletqmlmodel.h>
#include <qml/walletreceiveadapter.h>
#include <qml/walletidentity.h>
#include <qml/walletsend.h>
#include <qml/wallettransaction.h>
#include <stdexcept>
#include <qml/walletprepare.h>
#include <qml/retainedvalue.h>
#include <qml/guipresentation.h>
#include <qml/psbtwork.h>
#include <qml/paymentimportreview.h>
#include <qml/models/signertransaction.h>
#include <qml/models/psbtfee.h>
#include <qml/psbtpolicy.h>
#include <qml/backendexecutor.h>
#include <qml/walletbalanceadapter.h>

#include <common/messages.h>
#include <qml/bitcoinamount.h>
#include <qml/models/addresslistmodel.h>
#include <qml/models/paymentrequest.h>
#include <qml/models/psbtqmlmodel.h>
#include <qml/models/receiverequestentry.h>
#include <qml/models/receiverequesthistorymodel.h>
#include <qml/models/sendrecipient.h>
#include <qml/models/sendrecipientslistmodel.h>
#include <qml/models/signverifymessagemodel.h>
#include <qml/models/walletqmlmodeltransaction.h>
#include <qml/models/transactionflow.h>
#include <qml/util.h>

#include <chainparams.h>
#include <common/types.h>
#include <consensus/amount.h>
#include <interfaces/chain.h>
#include <interfaces/node.h>
#include <interfaces/wallet.h>
#include <key_io.h>
#include <node/psbt.h>
#include <node/transaction.h>
#include <node/types.h>
#include <addresstype.h>
#include <outputtype.h>
#include <policy/feerate.h>
#include <policy/policy.h>
#include <policy/truc_policy.h>
#include <primitives/transaction.h>
#include <psbt.h>
#include <qml/bitcoinunits.h>
#include <script/solver.h>
#include <univalue.h>
#include <util/result.h>
#include <util/rbf.h>
#include <util/threadnames.h>
#include <util/translation.h>
#include <wallet/coincontrol.h>
#include <wallet/types.h>
#include <wallet/wallet.h> // Public CRecipient value required by interfaces::Wallet.

#include <QDateTime>
#include <QMetaObject>
#include <QScopedValueRollback>
#include <QScopeGuard>
#include <QQmlEngine>
#include <QVariantList>
#include <QVariantMap>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <tuple>
#include <variant>
#include <vector>

namespace {
constexpr unsigned int DEFAULT_STANDARD_FEE_TARGET{6};
constexpr int FEE_ESTIMATE_DEBOUNCE_MS{250};
constexpr unsigned int FEE_RATE_BASIS_VBYTES{1000};
constexpr std::array<unsigned int, 3> STANDARD_FEE_TARGETS{2, DEFAULT_STANDARD_FEE_TARGET, 10};
constexpr std::array<unsigned int, 7> CUSTOM_FEE_TARGETS{2, 3, 4, 6, 10, 25, 50};

struct PsbtExportActionSlot {
    mutable std::mutex mutex;
    std::shared_ptr<std::shared_ptr<const walletopening::Session::Action>> retained;

    std::shared_ptr<const walletopening::Session::Action> get() const
    {
        std::lock_guard lock{mutex};
        if (!retained || !*retained) return {};
        return {retained, retained->get()};
    }
    void clear()
    {
        std::shared_ptr<std::shared_ptr<const walletopening::Session::Action>> released;
        {
            std::lock_guard lock{mutex};
            released = std::move(retained);
        }
        released.reset();
    }
};



QString FormatFeeEstimate(CAmount amount)
{
    BitcoinAmount bitcoin_amount;
    bitcoin_amount.setSatoshi(amount);
    return bitcoin_amount.displayWithUnit();
}

bool AmountPlusFeeExceedsBalance(const CAmount amount, const CAmount fee, const CAmount balance)
{
    if (amount > balance) {
        return true;
    }
    return fee > balance - amount;
}

void ApplySelectedInputsPolicy(wallet::CCoinControl& coin_control, const coinselection::InputMode mode)
{
    coinselection::ApplyInputPolicy(coin_control, mode);
}

std::optional<CTxDestination> PreviewChangeDestination(const OutputType change_type)
{
    const uint160 dummy_key_hash{};
    switch (change_type) {
    case OutputType::BECH32M:
        return WitnessV1Taproot{XOnlyPubKey::NUMS_H};
    case OutputType::BECH32:
        return WitnessV0KeyHash{dummy_key_hash};
    case OutputType::P2SH_SEGWIT:
        return ScriptHash{GetScriptForDestination(WitnessV0KeyHash{dummy_key_hash})};
    case OutputType::LEGACY:
        return PKHash{dummy_key_hash};
    case OutputType::UNKNOWN:
        return std::nullopt;
    }
    return std::nullopt;
}

void ApplyPreviewChangeDestination(wallet::CCoinControl& coin_control, const OutputType change_type)
{
    if (const auto destination = PreviewChangeDestination(change_type)) {
        coin_control.destChange = *destination;
    }
}


std::optional<bool> TransactionSweepsAvailableFunds(interfaces::Wallet& wallet, const CTransaction& transaction,
                                     const wallet::CCoinControl& control)
{
    return wallettransaction::Sweeps(wallet, transaction, control);
}

util::Result<wallet::CreatedTransactionResult> CreateSendAllTransaction(
    interfaces::Wallet& wallet, const std::vector<wallet::CRecipient>& recipients,
    const wallet::CCoinControl& control, int remainder, bool sign)
{
    return wallettransaction::Create(wallet, recipients, control, remainder, sign);
}

std::optional<SendFeePreview> EstimateInsufficientSelectedInputsFee(
    interfaces::Wallet& wallet, const std::vector<wallet::CRecipient>& recipients,
    const wallet::CCoinControl& control)
{
    const auto probe = wallettransaction::InsufficientFeeProbe(wallet, recipients, control);
    if (!probe) return {};
    const auto rate = control.m_feerate ? control.m_feerate->GetFeePerK()
        : wallet.getMinimumFee(FEE_RATE_BASIS_VBYTES, control, nullptr, nullptr);
    return SendFeePreview{probe->fee, static_cast<int>(probe->tx->vin.size()), rate, std::nullopt, false};
}

std::optional<SendFeePreview> TryPreviewFee(interfaces::Wallet& wallet,
    const std::vector<wallet::CRecipient>& recipients, const wallet::CCoinControl& control,
    int remainder_index = -1, QString* error = nullptr)
{
    const auto result = remainder_index >= 0
        ? CreateSendAllTransaction(wallet, recipients, control, remainder_index, false)
        : wallet.createTransaction(recipients, control, false, std::nullopt);
    if (!result) {
        if (error) { const auto text = util::ErrorString(result); *error = QString::fromStdString(text.translated.empty() ? text.original : text.translated).left(4096); }
        return remainder_index < 0 ? EstimateInsufficientSelectedInputsFee(wallet, recipients, control) : std::nullopt;
    }
    std::optional<int> target; FeeReason observed_reason{FeeReason::REQUIRED};
    const auto rate = control.m_feerate ? control.m_feerate->GetFeePerK() : wallet.getMinimumFee(FEE_RATE_BASIS_VBYTES, control, &target, &observed_reason);
    if (!result->tx || (remainder_index >= 0 && static_cast<size_t>(remainder_index) >= result->tx->vout.size())) throw std::runtime_error{"The fee preview transaction is incomplete"};
    const auto sweeps = result->change_pos ? std::optional<bool>{false} : TransactionSweepsAvailableFunds(wallet, *result->tx, control);
    if (!sweeps) throw std::runtime_error{"The wallet's remaining spendable balance could not be confirmed"};
    auto source = feeestimate::ProvenanceFor(result->fee_reason);
    const int observed_target = source == feeestimate::Provenance::Estimated && observed_reason == result->fee_reason ? target.value_or(0) : 0;
    return SendFeePreview{result->fee, static_cast<int>(result->tx->vin.size()), rate,
        remainder_index >= 0 ? std::make_optional(result->tx->vout[remainder_index].nValue) : std::nullopt, *sweeps, source, observed_target};
}

std::optional<SendFeePreview> EstimatePreviewFee(interfaces::Wallet& wallet,
    const std::vector<wallet::CRecipient>& recipients, const wallet::CCoinControl& base, coinselection::InputMode input_mode,
    OutputType preview_change_type, unsigned int target, int remainder_index = -1, QString* error = nullptr)
{
    wallet::CCoinControl control{base}; ApplySelectedInputsPolicy(control, input_mode);
    control.m_feerate.reset(); control.m_confirm_target = target;
    ApplyPreviewChangeDestination(control, preview_change_type); //048 remains held pending a supported nonmutating policy port.
    feeestimate::ApplyAutomaticPolicy(wallet, control);
    auto result = TryPreviewFee(wallet, recipients, control, remainder_index, error);
    if (result && Params().GetChainType() == ChainType::REGTEST) result->provenance = feeestimate::Provenance::Static;
    return result; // No synthetic rate can masquerade as a successful Core estimate.
}

std::optional<SendFeePreview> EstimateCustomPreviewFee(interfaces::Wallet& wallet,
    const std::vector<wallet::CRecipient>& recipients, const wallet::CCoinControl& base, coinselection::InputMode input_mode,
    OutputType preview_change_type, CAmount rate, int remainder_index = -1, QString* error = nullptr)
{
    wallet::CCoinControl control{base}; ApplySelectedInputsPolicy(control, input_mode);
    control.m_confirm_target.reset(); control.m_feerate = CFeeRate{rate}; control.fOverrideFeeRate = true;
    ApplyPreviewChangeDestination(control, preview_change_type);
    auto result = TryPreviewFee(wallet, recipients, control, remainder_index, error);
    if (result) result->provenance = feeestimate::Provenance::Manual;
    return result;
}

bool ReadyForFeePreview(const std::shared_ptr<const senddraft::Snapshot>& draft, quint64 maximum)
{
    if (!draft || draft->recipients.empty() || draft->duplicate_destinations || draft->pending_count) return false;
    if (!maximum) return draft->all_valid && draft->amount_total_valid;
    const auto position = draft->positions.find(maximum);
    if (position == draft->positions.end()) return false;
    const auto& recipient = draft->recipients[position->second];
    return recipient.address_valid && draft->invalid_count == (recipient.valid ? 0 : 1);
}


QString OutputTypeId(OutputType type)
{
    return QString::fromStdString(FormatOutputType(type));
}

QString OutputTypeLabel(OutputType type)
{
    switch (type) {
    case OutputType::BECH32M:
        return QObject::tr("Bech32m (Taproot)");
    case OutputType::BECH32:
        return QObject::tr("Bech32 (SegWit)");
    case OutputType::P2SH_SEGWIT:
        return QObject::tr("Base58 (P2SH-SegWit)");
    case OutputType::LEGACY:
        return QObject::tr("Base58 (Legacy)");
    case OutputType::UNKNOWN:
        return {};
    }
    return {};
}

QString OutputTypeDescription(OutputType type)
{
    switch (type) {
    case OutputType::BECH32M:
        return QObject::tr("Lower fees · Better privacy");
    case OutputType::BECH32:
        return QObject::tr("Lower fees · Widely supported");
    case OutputType::P2SH_SEGWIT:
        return QObject::tr("Higher fees · Backward compatible");
    case OutputType::LEGACY:
        return QObject::tr("Higher fees · Not recommended");
    case OutputType::UNKNOWN:
        return {};
    }
    return {};
}

} // namespace

WalletQmlModel::WalletQmlModel(std::unique_ptr<interfaces::Wallet> wallet, interfaces::Node* node, QObject* parent)
    : WalletQmlModel(std::move(wallet), node, FeeReadServices{}, parent)
{
}

WalletQmlModel::WalletQmlModel(std::unique_ptr<interfaces::Wallet> wallet, interfaces::Node* node,
                             FeeReadServices fee_services, QObject* parent)
    : WalletQmlModel(std::move(wallet), node, std::move(fee_services), psbtfee::Services{}, parent)
{
}

WalletQmlModel::WalletQmlModel(std::unique_ptr<interfaces::Wallet> wallet, interfaces::Node* node,
                             FeeReadServices fee_services, psbtfee::Services psbt_services, QObject* parent)
    : WalletQmlModel(std::move(wallet), node, std::move(fee_services), std::move(psbt_services), backup::Services{}, parent)
{
}

WalletQmlModel::WalletQmlModel(std::unique_ptr<interfaces::Wallet> wallet, interfaces::Node* node,
                             FeeReadServices fee_services, psbtfee::Services psbt_services, backup::Services backup_services,
                             QObject* parent)
    : WalletQmlModel(SharedWallet{std::move(wallet)}, node,
                     std::move(fee_services), std::move(psbt_services), std::move(backup_services), parent)
{
}

WalletQmlModel::WalletQmlModel(SharedWallet wallet, interfaces::Node* node,
                             FeeReadServices fee_services, psbtfee::Services psbt_services, backup::Services backup_services,
                             QObject* parent)
    : QObject(parent)
    , m_admission_closed(std::move(wallet.shutdown_gate))
    , m_opening_preferences(std::move(wallet.preferences))
    , m_opening_native_events(std::move(wallet.native_events))
    , m_fee_policy(std::move(wallet.fee_policy))
    , m_name(std::move(wallet.captured_name))
    , m_canonical_identity(wallet.canonical_identity)
    , m_balance_scheduler(std::move(wallet.observations))
    , m_wallet(std::move(wallet.wallet))
    , m_node(node)
    , m_backup_services(std::move(backup_services))
    , m_psbt_fee_services(std::move(psbt_services))
    , m_custom_psbt_lookup(bool(m_psbt_fee_services.lookup))
    , m_fee_read_services(std::move(fee_services))
{
    connect(this, &WalletQmlModel::signerApprovalChanged, this, &WalletQmlModel::unverifiedSigningResultChanged);
    connect(this, &WalletQmlModel::currentTransactionChanged, this, &WalletQmlModel::unverifiedSigningResultChanged);
    m_receive_requests = new ReceiveRequestHistoryModel(this);
    m_address_list_model = new AddressListModel(this);
    m_bump_transaction_model = new BumpTransactionModel(m_wallet.get(), this);
    m_bump_transaction_model->setSecurityStateChangedFn([this]() { refreshSecurityState(); });
    m_coins_list_model = new CoinsListModel(this);
    m_send_recipients = new SendRecipientsListModel(this);
    m_fees = new FeeSelectionModel(this);
    connect(m_send_recipients, &SendRecipientsListModel::totalAmountChanged,
            this, &WalletQmlModel::sendAmountExhaustsBalanceChanged);
    connect(m_send_recipients, &SendRecipientsListModel::validationChanged,
            this, &WalletQmlModel::sendAmountExhaustsBalanceChanged);
    m_sign_verify_message_model = new SignVerifyMessageModel(m_wallet.get(), this);
    m_sign_verify_message_model->setSecurityStateChangedFn([this]() { refreshSecurityState(); });
    m_current_payment_request = new PaymentRequest(this);
    m_receiving_address = new PaymentRequest(this);
    m_detail_payment_request = new PaymentRequest(this);
    m_imported_psbt_model = new PsbtQmlModel(m_wallet.get(), m_node, this);
    initializeBackend();
    initializeOpening();
    const QPointer<WalletQmlModel> receipt_observer{this};
    if (m_opening) {
        const PaymentRequest::PayloadPreparer prepare_payload = [opening = m_opening, bridge = m_notification_bridge](auto input, auto done) {
            const auto deliver = [bridge, done = std::move(done)](QString payload) {
                QMetaObject::invokeMethod(bridge.get(), [done, payload = std::move(payload)] { done(payload); }, Qt::QueuedConnection);
            };
            constexpr size_t MAX_INPUT_BYTES{65'536};
            size_t bytes{sizeof(input) + 256};
            for (const auto* text : {&input.address, &input.label, &input.message}) {
                if (static_cast<size_t>(text->size()) > (MAX_INPUT_BYTES - bytes) / sizeof(QChar)) { deliver({}); return; }
                bytes += static_cast<size_t>(text->size()) * sizeof(QChar);
            }
            if (!opening->postDomain([input = std::move(input), deliver] {
                deliver(walletrequests::BuildBitcoinUri(input.address, input.amount, input.label, input.message));
            })) deliver({});
        };
        for (auto* request : {m_current_payment_request, m_receiving_address, m_detail_payment_request}) request->setPayloadPreparer(prepare_payload);
        m_receipts = std::make_shared<walletreceipts::Reader>(m_opening,
            [bridge = m_notification_bridge, receipt_observer] {
                QMetaObject::invokeMethod(bridge.get(), [receipt_observer] {
                    if (receipt_observer && receipt_observer->observationsAllowed()) receipt_observer->publishReceipts();
                }, Qt::QueuedConnection);
            });
        const auto weak_reader = std::weak_ptr<walletreceipts::Reader>{m_receipts};
        // These bounded GUI ports never transfer native ownership. This
        // facade retains Reader/Session until the projection is closed below.
        const auto weak_opening = std::weak_ptr<walletopening::Session>{m_opening};
        ReceiptReconciliationModel::Ports receipt_ports;
        receipt_ports.opening = m_opening->identity();
        receipt_ports.take = [weak_reader] { const auto reader = weak_reader.lock(); return reader ? reader->take() : std::optional<walletreceipts::Publication>{}; };
        receipt_ports.retire = [weak_reader](auto snapshot) { if (const auto reader = weak_reader.lock()) reader->retire(std::move(snapshot)); };
        receipt_ports.current = [weak_reader] { const auto reader = weak_reader.lock(); return reader && reader->settled(); };
        receipt_ports.nativeRevision = [weak_opening] { const auto opening = weak_opening.lock(); return opening ? opening->transactionRevision() : uint64_t{0}; };
        receipt_ports.poll = [weak_reader] { if (const auto reader = weak_reader.lock()) reader->poll(); };
        receipt_ports.retry = [weak_reader] { if (const auto reader = weak_reader.lock()) reader->retry(); };
        receipt_ports.acknowledge = [weak_opening] { if (const auto opening = weak_opening.lock()) opening->acknowledgeTransactions(); };
        receipt_ports.transactions = [receipt_observer](const auto& changes) {
            for (const auto& [id, change] : changes) {
                if (!receipt_observer || !receipt_observer->observationsAllowed()) return;
                Q_EMIT receipt_observer->transactionChanged(QString::fromStdString(id.ToString()), int(change));
            }
        };
        m_receipt_projection = new ReceiptReconciliationModel(std::move(receipt_ports), this);
        connect(m_receipt_projection, &ReceiptReconciliationModel::changed, this, [this] {
            if (!observationsAllowed()) return;
            const QPointer<WalletQmlModel> guard{this};
            if (m_receiving_address && m_receipt_projection->observed(m_receiving_address->address())) {
                m_receiving_address->setPaymentReceived(true); if (!guard || m_stopping) return;
            }
            updateReceivedPaymentRequestAmounts(); if (!guard || m_stopping) return;
        });
        connect(m_receipt_projection, &ReceiptReconciliationModel::committed, this, &WalletQmlModel::receiveRequestReconciliationPendingChanged);
        connect(m_receipt_projection, &ReceiptReconciliationModel::rescanRequested, this, [this] {
            if (observationsAllowed() && m_transaction_activity_model) m_transaction_activity_model->reload();
        });
        m_request_store = std::make_shared<walletrequests::Store>(m_opening,
            [bridge = m_notification_bridge, receipt_observer] {
                QMetaObject::invokeMethod(bridge.get(), [receipt_observer] {
                    if (receipt_observer && receipt_observer->observationsAllowed()) receipt_observer->publishRequestHistory();
                }, Qt::QueuedConnection);
            }, [weak_reader](QSet<QString> addresses) {
                if (const auto reader = weak_reader.lock()) reader->reload(std::move(addresses));
            }, QDateTime::currentDateTimeUtc, [weak_reader] {
                const auto reader = weak_reader.lock(); return reader && reader->settled();
            }, walletrequests::SettingsReservationStorage(m_canonical_identity ? std::optional<QString>{m_name} : std::nullopt).clear,
            walletrequests::SettingsReservationStorage(m_canonical_identity ? std::optional<QString>{m_name} : std::nullopt));
        m_receipts->onSnapshot([weak = std::weak_ptr<walletrequests::Store>{m_request_store}](auto snapshot) {
            if (const auto store = weak.lock()) store->observed(snapshot);
        });
        m_opening->onTransactions([weak = std::weak_ptr<walletreceipts::Reader>{m_receipts}](uint64_t revision, std::map<Txid, ChangeType> changes, bool rescan) {
            if (const auto owner = weak.lock()) owner->notice(revision, std::move(changes), rescan);
        });
        if (!m_backup_services.coordinator) m_backup_services.coordinator = std::make_shared<backup::InstallCoordinator>();
        auto receive_ports = MakeReceivePorts(m_opening, m_request_store,
            {m_opening->scheduler(), m_fee_read_services.post}, m_backup_services.coordinator);
        receive_ports.receipts = m_receipt_projection;
        m_receive = new WalletReceiveModel(2, std::move(receive_ports), this);
        connect(m_receive->reservation(), &ReceiveReservationModel::changed, this, [this] {
            if (!m_receive->reservation()->generationPersisted()) return;
            const auto type = m_receive->reservation()->lastGeneratedAddressType();
            if ((type != QStringLiteral("legacy") && type != QStringLiteral("p2sh-segwit") && type != QStringLiteral("bech32") && type != QStringLiteral("bech32m")) || m_proven_receive_types.contains(type)) return;
            m_proven_receive_types.insert(type); Q_EMIT openingMetadataChanged();
        });
    }
    connect(m_receiving_address, &PaymentRequest::addressChanged, this, &WalletQmlModel::refreshReceiveRequestPayments);
    reloadReceiveRequests();
    initializeFeeEstimator();
    initializePsbtFeeReader();
    initializeBalanceReader();
    m_sign_verify_message_model->setOpening(m_opening, {m_opening->scheduler(), m_fee_read_services.post});
    m_bump_transaction_model->setOpening(m_opening, {m_opening->scheduler(), m_fee_read_services.post},
        {psbtpolicy::ReadNode(m_node, nullptr)});
    m_imported_psbt_model->configureSession(m_opening, m_psbt_fee_services);
}

WalletQmlModel::WalletQmlModel(interfaces::Node* node, QObject* parent)
    : QObject(parent)
    , m_node(node)
{
    connect(this, &WalletQmlModel::signerApprovalChanged, this, &WalletQmlModel::unverifiedSigningResultChanged);
    connect(this, &WalletQmlModel::currentTransactionChanged, this, &WalletQmlModel::unverifiedSigningResultChanged);
    m_address_list_model = new AddressListModel(this);
    m_bump_transaction_model = new BumpTransactionModel(nullptr, this);
    m_bump_transaction_model->setSecurityStateChangedFn([this]() { refreshSecurityState(); });
    m_coins_list_model = new CoinsListModel(this);
    m_send_recipients = new SendRecipientsListModel(this);
    m_fees = new FeeSelectionModel(this);
    connect(m_send_recipients, &SendRecipientsListModel::totalAmountChanged,
            this, &WalletQmlModel::sendAmountExhaustsBalanceChanged);
    connect(m_send_recipients, &SendRecipientsListModel::validationChanged,
            this, &WalletQmlModel::sendAmountExhaustsBalanceChanged);
    m_sign_verify_message_model = new SignVerifyMessageModel(nullptr, this);
    m_sign_verify_message_model->setSecurityStateChangedFn([this]() { refreshSecurityState(); });
    m_current_payment_request = new PaymentRequest(this);
    m_receiving_address = new PaymentRequest(this);
    m_detail_payment_request = new PaymentRequest(this);
    m_receive_requests = new ReceiveRequestHistoryModel(this);
    m_imported_psbt_model = new PsbtQmlModel(nullptr, m_node, this);
    initializeBackend();
    initializeFeeEstimator();
    initializePsbtFeeReader();
    m_sign_verify_message_model->setOpening({}, {m_backend_executor->scheduler(), m_fee_read_services.post});
    m_bump_transaction_model->setOpening({}, {m_backend_executor->scheduler(), m_fee_read_services.post});
    m_imported_psbt_model->configureSession({}, m_psbt_fee_services);
}

WalletQmlModel::WalletQmlModel(QObject* parent)
    : WalletQmlModel(nullptr, parent)
{
}

struct WalletQmlModel::ReviewRetirement : std::enable_shared_from_this<ReviewRetirement> {
    std::map<WalletQmlModelTransaction*, QPointer<WalletQmlModelTransaction>> reviews;
    std::shared_ptr<QObject> bridge{presentation::MakeGuiObject()};
    QPointer<WalletQmlModel> observer;
    std::function<void(std::function<void()>)> post{presentation::ReliablePost(
        [bridge = bridge](auto callback) { QMetaObject::invokeMethod(bridge.get(), std::move(callback), Qt::QueuedConnection); })};
    bool closing{false}, scheduled{false};
    void add(WalletQmlModelTransaction* review) {
        reviews.emplace(review, review);
        // This registry retires views in bounded turns. QObject parenting
        // would also delete an in-flight view if its destroyed handler closes
        // the facade and releases the bridge before that destructor returns.
        review->setParent(nullptr);
        QQmlEngine::setObjectOwnership(review,QQmlEngine::CppOwnership);
        const auto weak = weak_from_this();
        QObject::connect(review, &QObject::destroyed, bridge.get(), [weak, review] { if (const auto state = weak.lock()) state->reviews.erase(review); });
        if (closing) enqueue();
    }
    void enqueue() {
        if (std::exchange(scheduled, true)) return;
        const auto self = shared_from_this();
        post([self] { self->scheduled = false; self->pump(); });
    }
    void pump() {
        if (!closing) return;
        for (int budget = 0; budget < 32 && !reviews.empty(); ++budget) {
            const auto review = reviews.begin()->second; reviews.erase(reviews.begin());
            if (review) { review->close(); delete review; }
        }
        if (!reviews.empty()) { enqueue(); return; }
        if (observer) { observer->m_reviews_drained = true; observer->checkShutdownFinished(); }
    }
    void close() { closing = true; enqueue(); }
};
void WalletQmlModel::setReviewRetirementPostForTesting(std::function<void(std::function<void()>)> post)
{
    if (m_admission_closed || !post) return;
    if (!m_review_retirement) { m_review_retirement = std::make_shared<ReviewRetirement>(); m_review_retirement->observer = this; }
    m_review_retirement->post = presentation::ReliablePost(std::move(post));
}
void WalletQmlModel::registerReview(WalletQmlModelTransaction* review)
{
    if (!m_review_retirement) { m_review_retirement = std::make_shared<ReviewRetirement>(); m_review_retirement->observer = this; }
    m_reviews_drained = false; m_review_retirement->add(review);
    if (m_admission_closed) m_review_retirement->close();
}

WalletQmlModel::~WalletQmlModel()
{
    blockSignals(true);
    beginShutdown();
    if (m_fee_read_services.timer) {
        m_fee_read_services.timer->stop();
    }
    delete m_transaction_activity_model;
    delete m_address_list_model;
    delete m_coins_list_model;
    delete m_send_recipients;
    delete m_sign_verify_message_model;
    delete m_current_payment_request;
    delete m_detail_payment_request;
    delete m_receive_requests;
    delete m_imported_psbt_model;
    if (m_current_transaction) {
        delete m_current_transaction;
    }
}

TransactionActivityModel* WalletQmlModel::transactionActivityModel()
{
    // Instantiate when the activity screen first asks for it.
    if (!m_transaction_activity_model) m_transaction_activity_model = new TransactionActivityModel(this);
    return m_transaction_activity_model;
}

void WalletQmlModel::setNode(interfaces::Node* node)
{
    const QPointer<WalletQmlModel> observer{this};
    const bool recheck_fee = m_psbt_fee_pending || (m_current_psbt &&
        (!m_psbt_fee_proof || !m_psbt_fee_proof->amount.has_value()));
    m_psbt_fee_request.reset();
    m_psbt_fee_pending = false;
    if (m_node != node) m_psbt_fee_services.policy = {};
    m_node = node;
    if (!m_custom_psbt_lookup) m_psbt_fee_services.lookup = psbtfee::NodeLookup(node);
    if (m_imported_psbt_model) {
        m_imported_psbt_model->setFeeServices(m_psbt_fee_services);
        m_imported_psbt_model->setNode(node);
    }
    if (observer && recheck_fee && observationsAllowed() && m_current_psbt) requestPsbtFeeProof();
}

void WalletQmlModel::initializeOpening()
{
    if (!m_wallet) return;
    m_opening_drained = false;
    m_available_published = true;
    const QPointer<WalletQmlModel> observer{this};
    auto preferences = std::move(m_opening_preferences);
    if (!preferences.read && m_node) preferences.read = [node = m_node](const std::string& name) {
        const auto values = node->getPersistentSetting("qml_receive_address_types");
        const auto& value = values[name];
        return value.isStr() ? value.get_str() : std::string{};
    };
    // A Node void write cannot establish persistence. Production injects the
    // public Chain adapter, which supplies atomic update and a disk outcome.
    m_opening = std::make_shared<walletopening::Session>(
        m_balance_scheduler ? m_balance_scheduler : m_backend_executor->scheduler(),
        m_backend_executor->scheduler(), m_unload_requested,
        [bridge = m_notification_bridge, observer] {
            QMetaObject::invokeMethod(bridge.get(), [observer] { if (observer) observer->publishOpening(); }, Qt::QueuedConnection);
        }, std::move(preferences), std::chrono::steady_clock::now, m_admission_closed.shared(), m_opening_native_events);
    const QPointer<WalletQmlModel> approval_observer{this};
    const auto approval_post = presentation::ReliablePost(m_fee_read_services.post);
    const auto approval_gate = m_approval_running;
    m_signer_approval_operation = std::make_shared<walletsigning::ApprovalOwner>(m_opening,
        [approval_observer, approval_post, approval_gate](walletsigning::ApprovalSnapshot snapshot) mutable {
            if (!snapshot.in_flight && snapshot.attempt_id) {
                auto original = static_cast<quint64>(snapshot.attempt_id);
                approval_gate->compare_exchange_strong(original, 0);
            }
            approval_post([approval_observer, snapshot = std::move(snapshot)]() mutable {
                if (approval_observer) approval_observer->publishApproval(std::move(snapshot));
            });
        });
    m_security->bind(m_opening, m_notification_bridge, [this] {
        return available() && !preparationBusy() && !sendBusy() && !signerApprovalBusy();
    });
    auto started = m_opening->start(m_wallet);
    if (started.borrower) m_wallet = std::move(started.borrower);
    if (!started) m_opening->retire();
    if (started && m_address_list_model) m_address_list_model->refresh();
    if (started && m_coins_list_model) m_coins_list_model->update();
}

QString WalletQmlModel::closeQuestionToken() const
{
    return m_close_question ? m_opening_token + QLatin1Char(':') + QString::number(m_close_question->number) : QString{};
}

void WalletQmlModel::requestClose()
{
    if (m_opening && available()) m_opening->requestClose();
}

void WalletQmlModel::answerClose(const QString& token, bool accept)
{
    if (m_opening && m_close_question && token == closeQuestionToken()) m_opening->answerClose(*m_close_question, accept);
}

void WalletQmlModel::publishOpening()
{
    auto update = m_opening->take();
    if (!update) return;
    const QPointer<WalletQmlModel> guard{this};
    if (update->phase == walletopening::Phase::Closed) m_opening_drained = true;
    if (m_stopping || m_admission_closed) { checkShutdownFinished(); return; }
    if (update->metadata && available()) {
        auto metadata = std::move(*update->metadata);
        const QString name = QString::fromStdString(metadata.name);
        const bool name_changed = !m_canonical_identity && m_name != name;
        if (!m_canonical_identity) m_name = name;
        m_rpc_name = name;
        m_metadata_known = true;
        m_private_keys_disabled = metadata.private_keys_disabled;
        m_external_signer = metadata.external_signer;
        m_can_get_addresses = metadata.can_get_addresses;
        m_taproot = metadata.taproot;
        m_receive_preference = QString::fromStdString(metadata.receive_preference);
        m_configured_receive_type = QString::fromStdString(metadata.configured_receive_type);
        m_unavailable_receive_types.clear();
        for (const auto& type : metadata.unavailable_receive_types) m_unavailable_receive_types.insert(QString::fromStdString(type));
        m_security->observe(metadata); if (!guard || m_stopping) return;
        if (name_changed) { Q_EMIT nameChanged(); if (!guard || m_stopping) return; }
        Q_EMIT openingMetadataChanged(); if (!guard || m_stopping) return;
        if (m_receive) m_receive->refresh();
        if (!guard || m_stopping) return;
        if (m_current_transaction) { Q_EMIT currentTransactionChanged(); if (!guard || m_stopping) return; }
    }
    if (update->preference_saved && update->preference_revision == m_opening->preferenceRevision()) {
        m_receive_preference_pending = false;
        m_receive_preference_error = *update->preference_saved ? QString{} : tr("The address preference could not be confirmed.");
        Q_EMIT receivePreferenceChanged(); if (!guard || m_stopping) return;
    }
    const bool question_changed = bool(m_close_question) != bool(update->question)
        || (m_close_question && update->question && m_close_question->number != update->question->number);
    m_close_question = std::move(update->question);
    if (question_changed) { Q_EMIT closeQuestionChanged(); if (!guard || m_stopping) return; }
    if (update->unloaded || (update->failed && !available())) {
        if (std::exchange(m_available_published, false)) { Q_EMIT availableChanged(); if (!guard || m_stopping) return; }
        Q_EMIT walletUnloaded(); if (!guard || m_stopping) return;
        beginShutdown();
        return;
    }
    if (update->phase != walletopening::Phase::Open) {
        if (std::exchange(m_available_published, false)) { Q_EMIT availableChanged(); if (!guard || m_stopping) return; }
        if (!m_close_confirmed) { m_close_confirmed = true; Q_EMIT closeConfirmed(); if (!guard || m_stopping) return; }
        if (observationsAllowed()) beginShutdown();
        return;
    }
    if (update->addresses) {
        Q_EMIT addressInventoryChanged(); if (!guard || !observationsAllowed()) return;
        Q_EMIT addressListChanged(); if (!guard || !observationsAllowed()) return;
    }
    const bool transactions_changed = !update->transactions.empty() || update->rescan;
    if (transactions_changed) {
        // Backend ingress normally arrives first. The revision also makes
        // startup replay and this presentation handoff idempotent.
        m_receipts->notice(update->transaction_revision, std::move(update->transactions), update->rescan);
        // Ready can reach GUI before this Session mailbox. Acknowledge the
        // current revision after taking it, without waiting for another poll.
        if (m_receipt_projection) m_receipt_projection->invalidate();
        if (!guard || m_stopping) return;
        Q_EMIT receiveRequestReconciliationPendingChanged(); if (!guard || m_stopping) return;
    }
    if (update->status || transactions_changed) {
        refreshBalance();
        requestSendFacts();
        if (!guard || m_stopping) return;
        Q_EMIT walletContentsChanged(); if (!guard || m_stopping) return;
        Q_EMIT sendAmountExhaustsBalanceChanged();
    }
}

void WalletQmlModel::initializeBackend()
{
    connect(m_security, &WalletSecurityModel::observedChanged, this, &WalletQmlModel::securityStateChanged);
    connect(this, &WalletQmlModel::preparationChanged, m_security, &WalletSecurityModel::capabilitiesChanged);
    connect(this, &WalletQmlModel::sendOperationChanged, m_security, &WalletSecurityModel::capabilitiesChanged);
    connect(this, &WalletQmlModel::signerApprovalChanged, m_security, &WalletSecurityModel::capabilitiesChanged);
    connect(m_security, &WalletSecurityModel::operationChanged, this, [this] {
        if (!observationsAllowed()) return;
        const QPointer<WalletQmlModel> observer{this};
        const auto error = securityError();
        const bool changed = m_settings_error != error;
        m_settings_error = error;
        if (changed) { Q_EMIT settingsErrorChanged(); if (!observer || m_stopping) return; }
        Q_EMIT securityOperationChanged();
    });
    connect(this, &WalletQmlModel::currentTransactionChanged, this, &WalletQmlModel::requestCurrentFlow);
    connect(this, &WalletQmlModel::displayUnitChanged, this, &WalletQmlModel::requestCurrentFlow);
    connect(this, &WalletQmlModel::displayUnitChanged, m_bump_transaction_model, &BumpTransactionModel::setDisplayUnit);
    m_bump_transaction_model->setDisplayUnit(m_display_unit);
    // Presentation-only role updates (formatted address, amount unit, label
    // projection) do not change the prepared transaction. Draft and intent
    // signals below own semantic invalidation.
    connect(m_send_recipients, &SendRecipientsListModel::recipientDraftChanged, this, &WalletQmlModel::invalidatePreparation);
    connect(m_send_recipients, &QAbstractItemModel::rowsInserted, this, [this] { invalidatePreparation(); });
    connect(m_send_recipients, &QAbstractItemModel::rowsRemoved, this, [this] { invalidatePreparation(); });
    connect(m_send_recipients, &QAbstractItemModel::modelReset, this, &WalletQmlModel::invalidatePreparation);
    connect(this, &WalletQmlModel::maximumRecipientChanged, this, &WalletQmlModel::invalidatePreparation);
    connect(this, &WalletQmlModel::customFeeEnabledChanged, this, &WalletQmlModel::invalidatePreparation);
    connect(this, &WalletQmlModel::customFeeRateChanged, this, &WalletQmlModel::invalidatePreparation);
    connect(this, &WalletQmlModel::feeTargetBlocksChanged, this, &WalletQmlModel::invalidatePreparation);
    connect(this, &WalletQmlModel::currentTransactionChanged, this, [this] {
        const bool had_approval = signerApprovalBusy() || m_approval_state != ApprovalState::ApprovalInitial;
        if (m_signer_approval_operation) {
            m_signer_approval_operation->reviewChanged(m_current_review_identity, m_psbt_view_epoch);
        }
        if (m_approval_local_pending && !m_approval_command_admitted &&
            (m_approval_attempt_review != m_current_review_identity || m_approval_attempt_view != m_psbt_view_epoch)) {
            m_approval_local_pending = false;
            m_approval_state = ApprovalState::ApprovalInitial;
            m_approval_error.clear(); m_approval_signer_not_found = false;
        }
        if (had_approval) Q_EMIT signerApprovalChanged();
    });
    m_backend_executor->setObjectName(QStringLiteral("wallet"));
    m_notification_bridge = presentation::MakeGuiObject();
    connect(m_backend_executor.get(), &BackendExecutor::drained, this, [this] {
        m_backend_drained = true;
        checkShutdownFinished();
    });
}

void WalletQmlModel::checkShutdownFinished()
{
    if (m_stopping && m_backend_drained && m_balance_drained && m_fee_drained && m_opening_drained && m_reviews_drained && !m_shutdown_finished) {
        if (m_fees) m_fees->finishStop();
        m_shutdown_finished = true;
        Q_EMIT shutdownFinished();
    }
}

void WalletQmlModel::close()
{
    const bool admission_changed = !std::exchange(m_admission_closed, true);
    if (m_receive) m_receive->retire();
    if (m_transaction_attempt) m_transaction_attempt->close({});
    m_preparation_projection.phase = transactionattempt::Phase::Closed;
    m_preparation_projection.candidate.reset();
    m_preparation_projection.candidate_current = false;
    m_psbt_action_state = PsbtActionState::PsbtClosed;
    if (m_flow_details) m_flow_details->close();
    ++m_flow_revision;
    m_current_flow.clear(); m_flow_lifetime.reset(); m_flow_document.reset(); m_flow_proof.reset(); m_flow_transaction.reset();
    { std::lock_guard lock{m_send_facts_input->mutex}; m_send_facts_input->selected.reset(); m_send_facts_input->draft.reset(); }
    {
        std::lock_guard lock{m_flow_input->mutex};
        m_flow_input->transaction.reset(); m_flow_input->document.reset(); m_flow_input->proof.reset();
        m_flow_input->recipients.clear(); m_flow_input->notes.clear(); ++m_flow_input->revision;
    }
    m_current_psbt.reset(); m_psbt_fee_proof.reset();
    if (m_signer_approval_operation) m_signer_approval_operation->close();
    m_signer_approval_operation.reset();
    m_approval_projection.reset();
    m_approval_local_pending = false;
    m_approval_command_admitted = false;
    m_approval_state = ApprovalState::ApprovalClosed;
    m_approval_error.clear(); m_approval_signer_not_found = false;
    setCurrentTransaction(nullptr); // Review aliases retire through the independent bounded registry.
    if (m_review_retirement) m_review_retirement->close();
    m_send_operation.reset();
    m_psbt_action.reset(); m_psbt_action_lease.reset(); m_psbt_document_lease.reset();
    m_preparation.reset(); m_preparation_lease.reset();
    if (m_fees) m_fees->close();
    if (m_fee_reader) m_fee_reader->close();
    m_balance_timer.stop();
    if (m_receipt_projection) m_receipt_projection->close();
    if (m_fee_read_services.timer) m_fee_read_services.timer->stop();
    if (m_opening) m_opening->close();
    if (m_address_list_model) m_address_list_model->close();
    if (m_coins_list_model) m_coins_list_model->close();
    if (m_send_recipients) m_send_recipients->close();
    if (m_send_facts_query) m_send_facts_query->close();
    if (m_flow_query) { m_flow_query->close(); m_flow_query.reset(); }
    if (m_transaction_activity_model) m_transaction_activity_model->close();
    if (m_sign_verify_message_model) m_sign_verify_message_model->close();
    if (m_bump_transaction_model) m_bump_transaction_model->close();
    if (m_imported_psbt_model) m_imported_psbt_model->close();
    if (admission_changed) Q_EMIT preparationChanged();
}

void WalletQmlModel::beginShutdown(bool remove_wallet)
{
    if (m_stopping) return;
    close();
    m_stopping = true;
    const QPointer<WalletQmlModel> observer{this};
    const bool was_available{std::exchange(m_available_published, false)};
    m_unload_requested->store(true);
    m_balance_timer.stop();
    if (m_receipt_projection) m_receipt_projection->close();
    if (m_fee_read_services.timer) m_fee_read_services.timer->stop();
    if (m_backup_operation) m_backup_operation->retire();
    if (m_balance_reader) m_balance_reader->close();
    m_balance_known = false;
    m_balance_state = BalanceState::Closed;
    // Complete the resource handoff before any presentation signal can
    // reenter or destroy this facade. No Core effect depends on its lifetime.
    ++m_fee_estimate_request_id;
    if (m_fees) m_fees->close();
    m_fee_estimate_pending = false;
    ++m_fee_estimate_revision;
    invalidateFeeRead(true);
    m_psbt_fee_request.reset();
    m_psbt_fee_pending = false;
    if (m_request_store) {
        m_request_store->close();
        m_request_store->retire(m_receive_requests->releaseSnapshot());
    }
    if (m_receipts) {
        m_receipts->close();
    }
    if (m_opening) m_opening->retire(remove_wallet);
    if (m_bump_transaction_model) m_bump_transaction_model->detachWallet();
    if (m_imported_psbt_model) m_imported_psbt_model->detachWallet();
    m_sign_verify_message_model->setWallet(nullptr);
    m_sign_verify_message_model->setSecurityStateChangedFn({});
    auto wallet = std::move(m_wallet);
    const auto backend = m_backend_executor;
    backend->submit(this, [wallet = std::move(wallet), receipts = std::move(m_receipts), requests = std::move(m_request_store)]() mutable {
        requests.reset(); receipts.reset(); wallet.reset();
    }, [] {});
    backend->shutdown();
    if (!observer) return;
    if (m_transaction_activity_model) { m_transaction_activity_model->stop(); if (!observer) return; }
    m_security->close(); if (!observer) return;
    if (was_available) { Q_EMIT availableChanged(); if (!observer) return; }
    Q_EMIT balanceObservationChanged();
    if (observer) observer->checkShutdownFinished();
}

namespace {
class QtFeeReadTimer final : public FeeReadTimer
{
public:
    explicit QtFeeReadTimer(QObject* parent) : m_timer(new QTimer(parent))
    {
        m_timer->setSingleShot(true);
        m_timer->setInterval(FEE_ESTIMATE_DEBOUNCE_MS);
        QObject::connect(m_timer, &QTimer::timeout, m_timer, [this] {
            auto expired = std::exchange(m_expired, {});
            if (expired) expired();
        });
    }
    ~QtFeeReadTimer() override { delete m_timer; }
    void restart(std::function<void()> expired) override
    {
        m_expired = std::move(expired);
        m_timer->start();
    }
    void stop() override
    {
        m_timer->stop();
        m_expired = {};
    }
private:
    QTimer* m_timer;
    std::function<void()> m_expired;
};
}

void WalletQmlModel::initializeFeeEstimator()
{
    if (!m_fee_read_services.scheduler) m_fee_read_services.scheduler = m_opening ? m_opening->scheduler() : m_backend_executor->scheduler();
    initializePreparationOwner();
    if (!m_fee_read_services.timer) m_fee_read_services.timer = std::make_shared<QtFeeReadTimer>(this);
    if (!m_fee_read_services.post) {
        m_fee_read_services.post = [bridge = m_notification_bridge](std::function<void()> publication) {
            QMetaObject::invokeMethod(bridge.get(), std::move(publication), Qt::QueuedConnection);
        };
    }
    const auto fee_retirement_post = presentation::ReliablePost(m_fee_read_services.post);
    m_flow_details = new FlowDetailsModel(this);
    if (const auto gate = m_admission_closed.shared()) {
        auto post = std::move(m_fee_read_services.post);
        m_fee_read_services.post = [gate, post = std::move(post)](std::function<void()> callback) {
            post([gate, callback = std::move(callback)] { if (!gate->load()) callback(); });
        };
    }
    m_flow_details->setServices({m_fee_read_services.scheduler, m_fee_read_services.post});
    if (!m_fee_read_services.read) {
        m_fee_read_services.read = [opening = m_opening, weak = std::weak_ptr<interfaces::Wallet>{m_wallet}](const FeeReadInput& input) {
            FeeReadResult result;
            const auto wallet = opening ? opening->borrow() : weak.lock();
            if (!wallet) return result;
            std::vector<wallet::CRecipient> recipients;
            recipients.reserve(input.recipients.size());
            for (const auto& recipient : input.recipients) {
                const auto destination = DecodeDestination(recipient.address);
                if (!IsValidDestination(destination)) return result;
                recipients.push_back({destination, recipient.amount, false});
            }
            if (recipients.empty()) return result;
            const auto change_type = input.coin_control.m_change_type.value_or(wallet->getDefaultAddressType());
            const auto custom_rate = input.custom_rate_per_kvb;
            for (const unsigned int target : STANDARD_FEE_TARGETS) {
                QString error;
                if (const auto estimate = EstimatePreviewFee(*wallet, recipients, input.coin_control, input.input_mode, change_type, target, input.remainder_index, &error)) result.estimates.insert(target, *estimate);
                else result.errors.insert(target, error.isEmpty() ? tr("Fee estimation is unavailable. Choose Custom to enter a rate.") : error);
            }
            if (input.custom_fee_enabled && custom_rate) result.custom = EstimateCustomPreviewFee(*wallet, recipients, input.coin_control, input.input_mode, change_type, *custom_rate, input.remainder_index, &result.custom_error);
            if (result.estimates.isEmpty() && !result.custom) { result.error = feeestimate::Error::Unavailable; result.message = input.custom_fee_enabled ? result.custom_error : result.errors.value(input.coin_control.m_confirm_target.value_or(DEFAULT_STANDARD_FEE_TARGET)); }
            return result;
        };
    }
    m_fees->setServices({m_fee_read_services.scheduler, m_fee_read_services.post});
    const QPointer<WalletQmlModel> fee_observer{this};
    m_fee_drained = false;
    m_fee_reader = std::make_shared<feeestimate::Reader>(m_fee_read_services.scheduler, m_fee_read_services.read,
        [post = presentation::ReliablePost(m_fee_read_services.post), fee_observer] { post([fee_observer] { if (fee_observer) fee_observer->publishFeeEstimates(); }); },
        [post = fee_retirement_post, fee_observer] { post([fee_observer] {
            if (!fee_observer) return;
            fee_observer->m_fee_drained = true;
            if (fee_observer->m_fees) fee_observer->m_fees->finishStop();
            fee_observer->checkShutdownFinished();
        }); });
    connect(m_fees, &FeeSelectionModel::refreshRequested, this, [this] {
        ++m_fee_backend_revision;
        scheduleFeeEstimates();
    });
    connect(m_fees, &FeeSelectionModel::intentChanged, this, [this] { invalidatePreparation(); clearFeeEstimates(); });
    connect(m_fees, &FeeSelectionModel::policyChanged, this,
        [this, target = feeTargetBlocks(), manual = customFeeEnabled(), rate = customFeeRate(),
         valid = customFeeRateValid(), pending = customFeeTargetPending()]() mutable {
        if (!observationsAllowed() || m_stopping) return;
        const QPointer<WalletQmlModel> observer{this};
        const auto generation = m_fees->generation();
        const bool target_changed = target != feeTargetBlocks(), mode_changed = manual != customFeeEnabled(),
                   rate_changed = rate != customFeeRate(), validity_changed = valid != customFeeRateValid(),
                   pending_changed = pending != customFeeTargetPending();
        target = feeTargetBlocks(); manual = customFeeEnabled(); rate = customFeeRate(); valid = customFeeRateValid(); pending = customFeeTargetPending();
        m_coin_control.m_confirm_target = target;
        const auto owns = [&] { return observer && !m_stopping && observationsAllowed() && m_fees->generation() == generation; };
        if (mode_changed) { Q_EMIT customFeeEnabledChanged(); if (!owns()) return; }
        if (rate_changed) { Q_EMIT customFeeRateChanged(); if (!owns()) return; }
        if (validity_changed) { Q_EMIT customFeeRateValidChanged(); if (!owns()) return; }
        if (target_changed) { Q_EMIT feeTargetBlocksChanged(); if (!owns()) return; }
        if (pending_changed) { Q_EMIT customFeeTargetPendingChanged(); if (!owns()) return; }
        if (owns()) scheduleFeeEstimates();
    });
    connect(m_fees, &FeeSelectionModel::estimateChanged, this, [this] {
        if (!observationsAllowed() || m_stopping) return;
        const QPointer<WalletQmlModel> observer{this};
        const auto preview = selectedFeePreview();
        const bool changed = preview != m_last_fee_projection;
        const bool pending = feeEstimatePending(), pending_changed = pending != m_fee_estimate_pending;
        m_last_fee_projection = preview; m_fee_estimate_pending = pending; ++m_fee_estimate_revision;
        if (changed) { Q_EMIT estimatedFeeChanged(); if (!observer || m_stopping) return; Q_EMIT sendAmountExhaustsBalanceChanged(); if (!observer || m_stopping) return; }
        if (pending_changed) { Q_EMIT feeEstimatePendingChanged(); if (!observer || m_stopping) return; }
        Q_EMIT feeEstimateRevisionChanged();
    });
    if (m_send_recipients) {
        m_send_recipients->setOpening(m_opening, {m_fee_read_services.scheduler, m_fee_read_services.post});
        connect(m_send_recipients, &SendRecipientsListModel::maximumRecipientChanged, this, &WalletQmlModel::projectMaximumRecipient);
        connect(m_send_recipients, &SendRecipientsListModel::intentSubmitted, this, &WalletQmlModel::invalidatePreparation);
        connect(m_send_recipients, &SendRecipientsListModel::intentSubmitted, this, &WalletQmlModel::clearFeeEstimates);
        connect(m_send_recipients, &SendRecipientsListModel::recipientDraftChanged, this, [this](quint64 token, quint64 revision) {
            if (m_automatic_maximum && (token != m_automatic_maximum->token || revision != m_automatic_maximum->revision))
                invalidateAutomaticMaximum();
        });
        connect(m_send_recipients, &SendRecipientsListModel::validationChanged, this, &WalletQmlModel::scheduleFeesForRecipientValidation);
        const auto changed_intent = [this] { invalidateAutomaticMaximum(); };
        connect(this, &WalletQmlModel::openingMetadataChanged, this, [this] {
            if (m_automatic_maximum && m_automatic_maximum->capabilities != automaticMaximumCapabilities())
                invalidateAutomaticMaximum();
        });
        connect(this, &WalletQmlModel::coinSelectionChanged, this, changed_intent);
        connect(this, &WalletQmlModel::feeTargetBlocksChanged, this, changed_intent);
        connect(this, &WalletQmlModel::customFeeEnabledChanged, this, changed_intent);
        connect(this, &WalletQmlModel::customFeeRateChanged, this, changed_intent);
        connect(this, &WalletQmlModel::maximumRecipientChanged, this, changed_intent);
    }
    if (m_coins_list_model) {
        m_coins_list_model->setOpening(m_opening, {m_fee_read_services.scheduler, m_fee_read_services.post});
        connect(m_coins_list_model, &CoinsListModel::selectionSnapshotChanged, this, [this] {
            invalidatePreparation();
            if (coinSelectionPending()) m_send_facts_known = false;
            const QPointer<WalletQmlModel> observer{this};
            Q_EMIT coinSelectionChanged();
            if (!observer || m_admission_closed) return;
            Q_EMIT sendAmountExhaustsBalanceChanged();
            if (observer) scheduleFeeEstimates();
        });
    }
    requestSendFacts();
}

void WalletQmlModel::initializePreparationOwner()
{
    if (m_transaction_attempt) return;
    m_transaction_attempt = std::make_shared<transactionattempt::Owner>(
        m_opening ? m_opening->scheduler() : m_backend_executor->scheduler());
}

void WalletQmlModel::initializePsbtFeeReader()
{
    if (!m_psbt_fee_services.scheduler) m_psbt_fee_services.scheduler = m_backend_executor->scheduler();
    if (!m_psbt_fee_services.post) m_psbt_fee_services.post = m_fee_read_services.post;
    if (!m_custom_psbt_lookup) m_psbt_fee_services.lookup = psbtfee::NodeLookup(m_node);
    m_imported_psbt_model->setFeeServices(m_psbt_fee_services);
}

bool WalletQmlModel::requestPsbtFeeProof()
{
    const auto token = std::make_shared<const psbtfee::RequestIdentity>();
    m_psbt_fee_request = token;
    m_psbt_fee_pending = true;
    m_current_transaction_review_message = tr("Checking transaction fee.");
    const QPointer<WalletQmlModel> observer{this};
    const auto admission = psbtfee::Request(m_psbt_fee_services, m_current_psbt, m_psbt_fee_proof,
        [observer, token](psbtfee::Resolution result) {
            if (observer) observer->applyPsbtFeeProof(token, std::move(result));
        });
    if (!admission) applyPsbtFeeProof(token, {{}, false, true}, false);
    Q_EMIT currentTransactionChanged();
    return admission && observer && observer->m_psbt_fee_request == token;
}

void WalletQmlModel::applyPsbtFeeProof(const std::shared_ptr<const psbtfee::RequestIdentity>& token, psbtfee::Resolution result, bool publish)
{
    if (m_stopping || !m_current_transaction || m_psbt_fee_request != token) return;
    m_psbt_fee_request.reset();
    m_psbt_fee_pending = false;
    if (!result.failed && result.proof) m_psbt_fee_proof = std::move(result.proof);
    const bool known = !result.failed && m_psbt_fee_proof && m_psbt_fee_proof->amount.has_value();
    QPointer<WalletQmlModelTransaction> previous;
    bool attempt_changed{false};
    if (known) {
        // Publish a complete value projection. Observers of the old document
        // cannot close or replace it halfway through fee-amount notifications.
        auto* authenticated = new WalletQmlModelTransaction(*m_current_transaction, *m_psbt_fee_proof->amount, this);
        if (result.analysis) {
            authenticated->setAssessedPsbt(std::shared_ptr<const PartiallySignedTransaction>{result.analysis, result.analysis->assessment.get()});
            authenticated->setNodePolicyStatus(result.node_policy_status, result.analysis->node_state == psbtpolicy::State::Blocked);
        }
        if (result.transaction) authenticated->setWtx(result.transaction);
        previous = m_current_transaction;
        attempt_changed = setCurrentTransaction(authenticated);
    }
    const bool invalid_analysis = !result.analysis || result.analysis->metadata_failed;
    const bool node_blocked = m_current_transaction->nodePolicyBlocked() || m_psbt_requires_recheck || invalid_analysis;
    m_current_transaction_can_send = known && !node_blocked && (m_psbt_key_can_send || (result.complete && m_psbt_can_commit_complete));
    m_current_transaction_can_broadcast = known && result.analysis && result.analysis->can_broadcast && !node_blocked;
    m_current_transaction_review_message = !known
        ? tr("The transaction fee is missing or invalid. Add valid input information before broadcasting.")
        : invalid_analysis && result.analysis ? result.analysis->error
        : node_blocked ? m_current_transaction->nodePolicyStatus()
        : m_current_transaction_can_send || m_current_transaction_can_broadcast ? QString{}
        : tr("This wallet does not have the keys to sign this transaction.");
    if (publish) Q_EMIT currentTransactionChanged();
    if (attempt_changed) Q_EMIT preparationChanged();
    if (previous) previous->deleteLater();
}

void WalletQmlModel::invalidateFeeRead(bool close, bool scheduled)
{
    if (!m_fee_reader) return;
    if (close) m_fee_reader->close(); else m_fee_reader->invalidate(m_fee_estimate_request_id, scheduled);
}

QString WalletQmlModel::balance() const
{
    if (!m_wallet) {
        return "0";
    }
    return QmlBitcoinUnits::formatForDisplay(QmlBitcoinUnits::fromDisplayUnit(m_display_unit), m_balance);
}

qint64 WalletQmlModel::balanceSatoshi() const
{
    return displayedBalanceSatoshi();
}

void WalletQmlModel::initializeBalanceReader()
{
    m_balance_drained = false;
    m_balance_state = BalanceState::Unknown;
    const QPointer<WalletQmlModel> observer{this};
    const auto scheduler = m_balance_scheduler ? m_balance_scheduler : m_backend_executor->scheduler();
    m_balance_reader = std::make_shared<walletbalance::Reader>(scheduler,
        walletbalance::WalletRead(m_wallet), [bridge = m_notification_bridge, observer] {
            QMetaObject::invokeMethod(bridge.get(), [observer] {
                if (observer && observer->observationsAllowed()) observer->publishBalance();
            }, Qt::QueuedConnection);
        }, m_unload_requested, [bridge = m_notification_bridge, observer, lifetime = m_opening ? m_opening->borrow() : m_wallet] {
            (void)lifetime;
            QMetaObject::invokeMethod(bridge.get(), [observer] {
                if (!observer) return;
                observer->m_balance_drained = true;
                observer->checkShutdownFinished();
            }, Qt::QueuedConnection);
        });
    m_balance_timer.setInterval(1000);
    connect(&m_balance_timer, &QTimer::timeout, this, &WalletQmlModel::refreshBalance);
    m_balance_timer.start();
    refreshBalance();
}

void WalletQmlModel::refreshBalance()
{
    if (m_stopping || m_admission_closed) return;
    if (m_opening) m_opening->retryNotifications();
    if (m_receipts) m_receipts->retry();
    if (m_balance_reader) m_balance_reader->refresh();
    // Recover a retained local outcome if its presentation wakeup was lost.
    // This reads no Core state and cannot repeat the accepted device effect.
    const QPointer<WalletQmlModel> observer{this};
    if (m_preparation && preparationBusy()) publishPreparation();
    if (!observer) return;
    if (!observer || m_stopping) return;
    m_sign_verify_message_model->retryPublications();
    if (!observer || m_stopping) return;
    m_bump_transaction_model->retryPublications();
    if (!observer || m_stopping) return;
    m_imported_psbt_model->retryPublication();
    if (!observer || m_stopping) return;
    m_coins_list_model->retryPublications();
    if (!observer || m_stopping) return;
    m_send_recipients->retryPublications();
}

void WalletQmlModel::publishBalance()
{
    if (!observationsAllowed()) return;
    if (m_admission_closed) return;
    const auto result = m_balance_reader->take();
    if (!result) return;
    BalanceState state{BalanceState::Unknown};
    switch (result->status) {
    case walletbalance::Status::Ready: state = BalanceState::Ready; break;
    case walletbalance::Status::Busy: state = BalanceState::Busy; break;
    case walletbalance::Status::Unavailable: state = BalanceState::Unavailable; break;
    case walletbalance::Status::Failed: state = BalanceState::Failed; break;
    case walletbalance::Status::Rejected: state = BalanceState::Rejected; break;
    case walletbalance::Status::Closed: state = BalanceState::Closed; break;
    }
    const bool block_changed = result->snapshot && m_fee_observed_block != result->snapshot->observed_block;
    if (block_changed) { m_fee_observed_block = result->snapshot->observed_block; ++m_fee_backend_revision; }
    const bool changed = result->snapshot && (!m_balance_known || m_balance != result->snapshot->trusted);
    const bool observation_changed = state != m_balance_state || (result->snapshot && !m_balance_known);
    m_balance_state = state;
    if (result->snapshot) {
        m_balance = result->snapshot->trusted;
        m_balance_known = true;
    }
    const QPointer<WalletQmlModel> observer{this};
    if (observation_changed) Q_EMIT balanceObservationChanged();
    if (observer && observationsAllowed() && changed) Q_EMIT balanceChanged();
    if (observer && observationsAllowed() && block_changed && !m_stopping) scheduleFeeEstimates();
}

QString WalletQmlModel::estimatedFee() const
{
    const auto preview = selectedFeePreview(); return preview ? FormatFeeEstimate(preview->fee) : QString{};
}

std::optional<SendFeePreview> WalletQmlModel::selectedFeePreview() const
{
    return m_fees ? m_fees->selectedPreview() : std::nullopt;
}

std::optional<CAmount> WalletQmlModel::selectedFeeEstimate() const
{
    const auto preview = selectedFeePreview();
    return preview ? std::make_optional(preview->fee) : std::nullopt;
}

qint64 WalletQmlModel::estimatedFeeSatoshi() const
{
    return selectedFeeEstimate().value_or(-1);
}

QString WalletQmlModel::estimatedFeeRate() const
{
    const auto preview = selectedFeePreview();
    if (!preview || preview->rate_per_kvb <= 0) return {};
    auto text = feepolicy::FormatRate(preview->rate_per_kvb);
    while (text.endsWith(QLatin1Char{'0'})) text.chop(1);
    if (text.endsWith(QLatin1Char{'.'})) text.chop(1);
    return text;
}

int WalletQmlModel::estimatedInputCount() const
{
    const auto preview = selectedFeePreview();
    return preview ? preview->inputs : 0;
}

qint64 WalletQmlModel::sendTotalSatoshi() const
{
    if (!m_send_recipients) return 0;
    const auto fee = selectedFeeEstimate();
    return m_send_recipients->totalAmountSatoshi()
        + (fee ? *fee : 0);
}

qint64 WalletQmlModel::availableSendBalanceSatoshi() const
{
    return m_available_send_balance;
}

bool WalletQmlModel::sendDraftSweepsWallet() const
{
    const auto preview = selectedFeePreview();
    return preview && preview->sweeps_wallet;
}

void WalletQmlModel::useMaximum()
{
    if (m_send_recipients) useMaximumFor(m_send_recipients->currentRecipient());
}
void WalletQmlModel::useMaximumFor(SendRecipient* recipient)
{
    if (!available() || !m_send_recipients) return;
    m_send_recipients->chooseAmountMode(recipient, SendRecipient::AmountMode::Maximum);
}
void WalletQmlModel::setMaximumRecipient(SendRecipient* recipient)
{
    if (!m_send_recipients) return;
    if (recipient) m_send_recipients->chooseAmountMode(recipient, SendRecipient::AmountMode::Maximum);
    else if (m_maximum_recipient) m_maximum_recipient->chooseFixedAmount();
}
void WalletQmlModel::projectMaximumRecipient()
{
    auto* recipient = m_send_recipients->maximumRecipient();
    if (m_maximum_recipient == recipient) return;
    QObject::disconnect(m_maximum_recipient_destroyed_connection);
    m_maximum_recipient = recipient;
    if (recipient) m_maximum_recipient_destroyed_connection = connect(recipient, &QObject::destroyed, this, [this] {
        m_maximum_recipient.clear(); Q_EMIT maximumRecipientChanged();
    });
    Q_EMIT maximumRecipientChanged();
}

void WalletQmlModel::setCustomFeeTarget(unsigned int target)
{
    if (available() && m_fees) m_fees->chooseCustomTarget(static_cast<int>(target));
}

CFeeRate WalletQmlModel::dustRelayFee() const
{
    return CFeeRate{m_dust_relay_fee.value_or(DUST_RELAY_TX_FEE)};
}

bool WalletQmlModel::sendAmountExhaustsBalance() const
{
    if (!m_send_facts_known || !m_wallet || !m_send_recipients || !m_send_recipients->allValid()) {
        return false;
    }

    const CAmount balance{m_selected_send_balance};
    const CAmount total_amount{m_send_recipients->totalAmountSatoshi()};
    if (total_amount > balance) {
        return true;
    }
    if (const auto fee = selectedFeeEstimate()) {
        return AmountPlusFeeExceedsBalance(total_amount, *fee, balance);
    }

    // Without an estimate, a fixed-amount send cannot safely spend the full
    // balance because prepareTransaction() will still need to add a fee.
    return total_amount >= balance;
}

bool WalletQmlModel::customFeeRateValid() const { return m_fees && m_fees->ratePerKvB().has_value(); }

double WalletQmlModel::minimumCustomFeeRate() const
{
    return static_cast<double>(m_minimum_relay_fee.value_or(DEFAULT_MIN_RELAY_TX_FEE)) / FEE_RATE_BASIS_VBYTES;
}

QString WalletQmlModel::estimatedFeeForTarget(unsigned int target) const
{
    const auto preview = m_fees ? m_fees->preview(target) : std::nullopt; return preview ? FormatFeeEstimate(preview->fee) : QString{};
}

int WalletQmlModel::feeTargetIndex(const unsigned int target_blocks) const
{
    for (size_t i = 0; i < STANDARD_FEE_TARGETS.size(); ++i) {
        if (STANDARD_FEE_TARGETS[i] == target_blocks) {
            return static_cast<int>(i);
        }
    }

    return 1;
}

QString WalletQmlModel::name() const
{
    return m_name;
}

QString WalletQmlModel::displayName() const
{
    if (!m_display_name.isEmpty()) {
        return m_display_name;
    }
    return m_wallet ? walletidentity::Display(m_name) : name();
}

void WalletQmlModel::setDisplayName(const QString& display_name)
{
    if (m_display_name != display_name) {
        m_display_name = display_name;
        Q_EMIT displayNameChanged();
    }
}

QString WalletQmlModel::backupFileName() const
{
    // Bounded presentation of an observed identity; the Native operation
    // validates the actual final path. This reads neither Core nor the disk.
    auto name = m_name.isEmpty() ? QStringLiteral("wallet") : m_name;
    name.replace(QLatin1Char{'/'}, QLatin1Char{'_'});
    name.replace(QLatin1Char{'\\'}, QLatin1Char{'_'});
    return name + QStringLiteral(".bak");
}

QString WalletQmlModel::keyScheme() const
{
    return keySchemeDisplayText(keySchemeKind());
}

WalletQmlModel::KeyScheme WalletQmlModel::keySchemeKind() const
{
    if (!m_metadata_known) return KeyScheme::UnknownScheme;
    if (m_external_signer) return KeyScheme::ExternalSigner;
    return m_private_keys_disabled ? KeyScheme::WatchOnly : KeyScheme::Software;
}

WalletQmlModel::KeyScheme WalletQmlModel::keySchemeForWallet(interfaces::Wallet& wallet)
{
    if (wallet.hasExternalSigner()) return KeyScheme::ExternalSigner;
    return wallet.privateKeysDisabled() ? KeyScheme::WatchOnly : KeyScheme::Software;
}

QString WalletQmlModel::keySchemeDisplayText(KeyScheme scheme)
{
    switch (scheme) {
    case KeyScheme::WatchOnly: return tr("Watch-only");
    case KeyScheme::MultiKey: return tr("Multi-key");
    case KeyScheme::ExternalSigner: return tr("External signer");
    case KeyScheme::SingleKey: return tr("Single-key");
    case KeyScheme::Software: return tr("Software wallet");
    case KeyScheme::UnknownScheme: return tr("Unknown");
    }
    return tr("Unknown");
}

QString WalletQmlModel::privateKeysStatus() const
{
    return !m_metadata_known ? tr("Unknown") : m_private_keys_disabled ? tr("Disabled") : tr("Enabled");
}

QString WalletQmlModel::externalSignerStatus() const
{
    return !m_metadata_known ? tr("Unknown") : m_external_signer ? tr("Enabled") : tr("None");
}

bool WalletQmlModel::canManagePassphrase() const
{
    return m_security->canManagePassphrase();
}

WalletQmlModel::SecurityState WalletQmlModel::securityState() const
{
    const auto* operation = m_security->activeOperation();
    if (!operation) return SecurityState::SecurityIdle;
    switch (operation->phase()) {
    case SecurityOperationModel::Phase::AwaitingCredentials:
    case SecurityOperationModel::Phase::Cancelled: return SecurityState::SecurityIdle;
    case SecurityOperationModel::Phase::Pending: return SecurityState::SecurityPending;
    case SecurityOperationModel::Phase::Succeeded: return SecurityState::SecuritySucceeded;
    case SecurityOperationModel::Phase::Failed: return SecurityState::SecurityFailed;
    case SecurityOperationModel::Phase::Uncertain: return SecurityState::SecurityUncertain;
    }
    return SecurityState::SecurityIdle;
}

bool WalletQmlModel::securityBusy() const
{
    const auto* operation = m_security->activeOperation();
    return operation && operation->busy();
}

QString WalletQmlModel::securityError() const
{
    const auto* operation = m_security->activeOperation();
    return operation ? operation->error() : QString{};
}

WalletQmlModel::ApprovalState WalletQmlModel::signerApprovalState() const
{
    const bool same_view = m_approval_attempt_review == m_current_review_identity &&
        m_approval_attempt_view == m_psbt_view_epoch;
    if (!same_view && signerApprovalBusy()) return ApprovalState::ApprovalOtherReview;
    if (!same_view && m_approval_state != ApprovalState::ApprovalInitial) return ApprovalState::ApprovalInitial;
    return m_approval_state;
}

bool WalletQmlModel::signerApprovalBusy() const
{
    return m_approval_running->load() || m_approval_local_pending || (m_approval_projection && m_approval_projection->in_flight);
}

QString WalletQmlModel::signerUnverifiedResultToken() const
{
    if (observationsAllowed() && available() && m_send_sign_only && m_send_operation &&
        m_send_review && m_send_review == m_current_transaction && m_send_review_id == m_current_review_identity) {
        const auto result = m_send_operation->result();
        if (result && result->quarantined_document)
            return QStringLiteral("local:") + QString::number(m_send_request_id) + QLatin1Char(':') + QString::number(m_send_review_id);
    }
    const auto& snapshot = m_approval_projection;
    if (!observationsAllowed() || !available() || !snapshot || snapshot->in_flight || !snapshot->publishable ||
        snapshot->attempt_id != m_approval_id || snapshot->attempt_review != m_current_review_identity ||
        snapshot->attempt_view != m_psbt_view_epoch || snapshot->current_review != m_current_review_identity ||
        snapshot->current_view != m_psbt_view_epoch || !snapshot->result || !snapshot->result->quarantined_document) return {};
    return QStringLiteral("external:") + QString::number(snapshot->attempt_id) + QLatin1Char(':') + QString::number(snapshot->attempt_review) +
        QLatin1Char(':') + QString::number(snapshot->attempt_view);
}

QString WalletQmlModel::signerApprovalError() const
{
    return m_approval_attempt_review == m_current_review_identity && m_approval_attempt_view == m_psbt_view_epoch
        ? m_approval_error : QString{};
}

bool WalletQmlModel::signerApprovalSignerNotFound() const
{
    return m_approval_attempt_review == m_current_review_identity && m_approval_attempt_view == m_psbt_view_epoch &&
        m_approval_signer_not_found;
}

bool WalletQmlModel::startSecurity(walletsecurity::Kind kind, const QString& current, const QString& replacement)
{
    auto* operation = kind == walletsecurity::Kind::Encrypt ? m_security->beginEncryption() : m_security->beginPassphraseChange();
    return operation && operation->submitCredentials(operation->operationId(), current, replacement, replacement);
}

bool WalletQmlModel::encryptWallet(const QString& passphrase)
{
    return startSecurity(walletsecurity::Kind::Encrypt, {}, passphrase);
}

bool WalletQmlModel::changeWalletPassphrase(const QString& old_passphrase, const QString& new_passphrase)
{
    return startSecurity(walletsecurity::Kind::ChangePassphrase, old_passphrase, new_passphrase);
}

bool WalletQmlModel::backupWallet(const QString& path)
{
    clearSettingsError();
    if (!m_wallet) {
        setSettingsError(tr("No wallet is selected."));
        return false;
    }
    if (!available()) {
        setSettingsError(tr("This wallet is closing."));
        return false;
    }
    if (path.size() > 32'768 || path.trimmed().isEmpty()) {
        setSettingsError(tr("Choose a location for the wallet backup."));
        return false;
    }

    if (m_backup_operation && m_backup_operation->pending()) {
        setSettingsError(tr("A backup operation is already pending."));
        return false;
    }
    auto services = m_backup_services;
    if (services.scheduler && (!services.wallet_name || !services.write)) {
        setSettingsError(tr("The backup service has no owned wallet adapter."));
        return false;
    }
    if (!services.scheduler) {
        if (!m_opening) { setSettingsError(tr("This wallet is closing.")); return false; }
        services.scheduler = m_opening->scheduler();
        services.available = [weak = std::weak_ptr<walletopening::Session>{m_opening}] {
            const auto opening = weak.lock(); return opening && opening->available();
        };
        services.submit = [weak = std::weak_ptr<walletopening::Session>{m_opening}](std::function<void()> work) {
            const auto opening = weak.lock();
            const auto action = opening ? opening->startAction() : nullptr;
            if (!action) return execution::Admission{{}, execution::Rejection::Closed};
            return opening->perform(action, [work = std::move(work)](interfaces::Wallet&) { work(); return true; }, [](bool) {});
        };
    }
    if (!services.post) services.post = m_fee_read_services.post;
    if (!services.wallet_name) services.wallet_name = [name = m_name] { return name.toStdString(); };
    if (!services.inspect) services.inspect = [](const std::string& raw, const std::string& name) {
        return backup::Inspect(raw, name);
    };
    if (!m_backup_services.coordinator) m_backup_services.coordinator = std::make_shared<backup::InstallCoordinator>();
    if (!services.write) {
        services.write = [wallet = m_wallet.get(), coordinator = m_backup_services.coordinator](const backup::Proposal& target, bool replace) {
            return backup::Write(target, replace, [wallet](const std::string& staged) { return wallet->backupWallet(staged); }, *coordinator);
        };
    }
    auto* operation = new BackupFileOperation(path, std::move(services), this);
    const QPointer<BackupFileOperation> previous{m_backup_operation};
    m_backup_operation = operation;
    const bool admitted = operation->start();
    Q_EMIT backupOperationChanged();
    if (previous) previous->deleteLater();
    return admitted;
}

ImageExportOperation* WalletQmlModel::savePaymentRequestImage(PaymentRequest* request, const QImage& image, const QUrl& path)
{
    if (!available() || m_stopping || !m_opening || !m_request_storage_loaded || receiveRequestReconciliationPending() ||
        !request || (request != m_current_payment_request && request != m_detail_payment_request && request != m_receiving_address) ||
        request->paymentReceived() || request->qrPayload().isEmpty()) return nullptr;
    const auto* record = m_receive_requests->preparedEntry(request->id());
    if (!record || record->entry.payment_received || !request->matchesStoredRevision(record->revision)) return nullptr;
    const auto publication = m_receive_requests->snapshot();
    if (!publication || !m_request_store->presentationCurrent() || publication->revision != m_request_store->revision()) return nullptr;
    imageexport::Identity identity{m_opening->identity(), request->id(), request->qrPayload(), request->generation(), request->revision()};
    imageexport::Services services{m_balance_scheduler ? m_balance_scheduler : m_backend_executor->scheduler(), m_unload_requested, {}, {}};
    services.write = [weak = std::weak_ptr<walletrequests::Store>{m_request_store}, version = publication->revision](const QImage& pixels, const QUrl& target) {
        const auto store = weak.lock();
        if (!store || !store->presentationCurrent() || store->revision() != version) return imageexport::Error::Retired;
        return imageexport::WritePng(pixels, target);
    };
    services.submit = [weak = std::weak_ptr<walletopening::Session>{m_opening}](auto work, auto done, auto failed) {
        const auto opening = weak.lock();
        const auto action = opening ? opening->startAction() : nullptr;
        if (!action) return execution::Admission{{}, execution::Rejection::Closed};
        // The existing Action lease joins physical opening retirement. It is
        // held only by the background task, never until GUI acknowledgement.
        return opening->perform(action, [work = std::move(work)](interfaces::Wallet&) { return work(); },
                                std::move(done), std::move(failed));
    };
    auto* operation = new ImageExportOperation(image, path, std::move(identity), std::move(services),
        [bridge = m_notification_bridge](auto done) { QMetaObject::invokeMethod(bridge.get(), std::move(done), Qt::QueuedConnection); });
    connect(request, &PaymentRequest::generationChanged, operation, &ImageExportOperation::retire);
    connect(request, &PaymentRequest::draftChanged, operation, &ImageExportOperation::retire);
    connect(request, &PaymentRequest::qrPayloadChanged, operation, &ImageExportOperation::retire);
    connect(request, &PaymentRequest::paymentReceivedChanged, operation, &ImageExportOperation::retire);
    connect(request, &QObject::destroyed, operation, &ImageExportOperation::retire);
    connect(this, &WalletQmlModel::availableChanged, operation, [operation] { operation->retire(); });
    operation->start();
    return operation;
}

void WalletQmlModel::clearSettingsError()
{
    setSettingsError(QString());
}

QVariantList WalletQmlModel::availableReceiveAddressTypes() const
{
    if (!available() || !m_metadata_known) {
        return {};
    }

    QVariantList types;
    const std::array ordered_types{
        OutputType::BECH32M,
        OutputType::BECH32,
        OutputType::P2SH_SEGWIT,
        OutputType::LEGACY,
    };

    for (const OutputType type : ordered_types) {
        if (type == OutputType::BECH32M && !m_taproot) {
            continue;
        }
        // #1088 (partial): drop types Core has reported it cannot generate.
        // Types not yet tried stay listed as Unknown until Core offers a
        // per-type capability query.
        if (m_unavailable_receive_types.contains(OutputTypeId(type))) continue;
        QVariantMap item;
        item.insert(QStringLiteral("id"), OutputTypeId(type));
        item.insert(QStringLiteral("label"), OutputTypeLabel(type));
        item.insert(QStringLiteral("description"), OutputTypeDescription(type));
        const bool proven = m_proven_receive_types.contains(OutputTypeId(type)) || (type == OutputType::BECH32M && m_taproot);
        item.insert(QStringLiteral("capability"), static_cast<int>(proven ? ReceiveTypeStatus::Support::Proven : ReceiveTypeStatus::Support::Unknown));
        item.insert(QStringLiteral("generationAvailability"), static_cast<int>(m_can_get_addresses ? ReceiveTypeStatus::Availability::Unknown : ReceiveTypeStatus::Availability::Unavailable));
        if (!proven) item.insert(QStringLiteral("description"), QString{OutputTypeDescription(type) + QLatin1Char(' ') + tr("Support for this type will be checked when generating an address.")});
        types.append(item);
    }
    return types;
}

QString WalletQmlModel::defaultReceiveAddressType() const
{
    const QVariantList available_types = availableReceiveAddressTypes();
    // A type the user chose wins; until then an explicit -addresstype does.
    for (const QString& wanted : {m_receive_preference, m_configured_receive_type}) {
        if (wanted.isEmpty()) continue;
        for (const QVariant& item : available_types) {
            if (item.toMap().value(QStringLiteral("id")).toString() == wanted) return wanted;
        }
    }

    // The list starts with Taproot, falling back to SegWit when unavailable.
    return available_types.empty()
        ? QString{}
        : available_types.front().toMap().value(QStringLiteral("id")).toString();
}

bool WalletQmlModel::setDefaultReceiveAddressType(const QString& address_type)
{
    if (!available() || !m_opening || address_type.size() > 16) return false;
    const auto admission = m_opening->setReceivePreference(address_type.toStdString());
    m_receive_preference_pending = bool(admission) || m_receive_preference_pending;
    m_receive_preference_error = admission ? QString{} : tr("The address preference could not be started.");
    Q_EMIT receivePreferenceChanged();
    return bool(admission);
}

QString WalletQmlModel::receiveAddressTypeLabel(const QString& address_type) const
{
    for (const QVariant& item : availableReceiveAddressTypes()) {
        const QVariantMap type{item.toMap()};
        if (type.value(QStringLiteral("id")).toString() == address_type) {
            return type.value(QStringLiteral("label")).toString();
        }
    }
    return {};
}

void WalletQmlModel::removeWallet()
{
    beginShutdown(true);
}

ReceiveRequestOperation* WalletQmlModel::setCurrentPaymentRequestAddress(const QString& address)
{
    if (!available() || !m_current_payment_request || address.isEmpty()) return nullptr;
    walletrequests::Command command; command.kind = walletrequests::Kind::SelectAddress;
    command.view = 1; command.view_revision = m_receiving_address->revision();
    command.draft.address = address;
    return submitRequest(std::move(command), m_current_payment_request);
}

ReceiveRequestOperation* WalletQmlModel::ensureReceivingAddress(bool next, const QString& address_type)
{
    walletrequests::Command command; command.kind = walletrequests::Kind::ReserveAddress;
    command.view = 1; command.view_revision = m_receiving_address->revision();
    command.draft.address = m_receiving_address->address();
    command.draft.address_type = address_type.isEmpty() ? defaultReceiveAddressType() : address_type;
    command.next = next; command.explicit_type = !address_type.isEmpty();
    return submitRequest(std::move(command), m_receiving_address);
}

ReceiveRequestOperation* WalletQmlModel::resumeReceivingAddress(ReceiveRequestOperation* original, const QString& passphrase)
{
    if (!available() || !m_opening || !original || !original->addressIntent() || original->addressIntent()->create ||
        original->addressIntent()->opening != m_opening->identity() || original->viewRevision() != m_receiving_address->revision() ||
        original->addressIntent() != m_receiving_intent || m_receiving_address_pending) return nullptr;
    walletrequests::Command command; command.kind = walletrequests::Kind::ResumeAddress;
    command.resume = original->addressIntent();
    auto captured = secretinput::Capture(m_opening->scheduler(), passphrase);
    command.captured_secret = std::move(captured.input); command.secret_capture_error = captured.error;
    return submitRequest(std::move(command), m_receiving_address);
}

ReceiveRequestOperation* WalletQmlModel::retryReceivingAddress(ReceiveRequestOperation* original)
{
    if (!available() || !m_opening || !original || !original->addressIntent() || original->addressIntent()->create ||
        original->addressIntent()->opening != m_opening->identity() || original->viewRevision() != m_receiving_address->revision() ||
        original->addressIntent() != m_receiving_intent || m_receiving_address_pending) return nullptr;
    walletrequests::Command command; command.kind = walletrequests::Kind::ResumeAddress;
    command.resume = original->addressIntent();
    return submitRequest(std::move(command), m_receiving_address);
}

ReceiveRequestOperation* WalletQmlModel::commitReceivingPaymentRequest()
{
    if (!available() || !m_receiving_address_ready || !m_receiving_intent || !m_receiving_address || !m_current_payment_request ||
        !m_current_payment_request->id().isEmpty() || m_receiving_address->address().isEmpty()) return nullptr;
    // The owner checks receipt readiness and the captured address on Native.
    // Admission is represented by a pending operation, never by a saved ID.
    m_current_payment_request->setPreparedDestination(m_receiving_address->destination(), m_receiving_address->address());
    return savePaymentRequest(m_current_payment_request, true);
}

ReceiveRequestOperation* WalletQmlModel::submitRequest(walletrequests::Command command, PaymentRequest* draft, bool consume_address)
{
    auto* operation = new ReceiveRequestOperation;
    QQmlEngine::setObjectOwnership(operation, QQmlEngine::JavaScriptOwnership);
    const QPointer<WalletQmlModel> observer{this};
    const QPointer<PaymentRequest> target{draft};
    const auto revision = draft ? draft->revision() : 0;
    const auto address_revision = m_receiving_address->revision();
    const auto consumed_intent = command.reservation;
    const bool label_only = command.kind == walletrequests::Kind::Edit || command.kind == walletrequests::Kind::Label;
    const auto* original_opening = m_opening.get();
    connect(operation, &ReceiveRequestOperation::changed, this, [observer, operation, target, revision, consume_address, address_revision, consumed_intent, original_opening, label_only, label_refresh_published = false]() mutable {
        if (!observer || observer->m_stopping || operation->pending()) return;
        const auto result = *operation->result();
        const auto intent = operation->addressIntent();
        const bool ready = operation->addressReady();
        const auto error_text = operation->errorText();
        observer->publishRequestHistory();
        if (!observer || observer->m_stopping) return;
        if (result.label_saved && !label_refresh_published && observer->observationsAllowed() && observer->m_opening.get() == original_opening) {
            // The actual acknowledged write owns this refresh even when the
            // public wallet adapter does not repeat an address notification.
            label_refresh_published = true;
            if (label_only) Q_EMIT observer->addressLabelsChanged();
            else Q_EMIT observer->addressInventoryChanged();
            if (!observer || !observer->observationsAllowed() || observer->m_opening.get() != original_opening) return;
            Q_EMIT observer->addressListChanged();
            if (!observer || !observer->observationsAllowed()) return;
        }
        if (!result.paid_address.isEmpty() && observer->m_receiving_address->address() == result.paid_address) {
            observer->m_receiving_address_ready = false;
            observer->m_receiving_address->setPaymentReceived(true);
            if (!observer || observer->m_stopping) return;
            Q_EMIT observer->receivingAddressStateChanged();
            if (!observer || observer->m_stopping) return;
        }
        if (intent && !intent->create && observer->m_receiving_intent != intent) return;
        const bool apply = target && target->revision() == revision;
        if (apply && intent && intent->select && (result.address || result.request_loaded)) {
            target->clear();
            if (!observer || !target || observer->m_stopping) return;
        }
        if (apply && result.address) {
            const auto& address = *result.address;
            target->setPreparedDestination(address.destination, address.address);
            if (!observer || !target || observer->m_stopping) return;
            target->setAddressType(address.address_type);
            if (!observer || !target || observer->m_stopping) return;
            target->setPaymentReceived(false);
            if (!observer || !target || observer->m_stopping) return;
            if (intent && intent->select) {
                target->setNoteSelfBaseline(address.label, true);
                if (!observer || !target || observer->m_stopping) return;
                target->setIsEditing(true);
                if (!observer || !target || observer->m_stopping) return;
                if (observer->m_receiving_address->revision() == address_revision) {
                    observer->m_receiving_address->clear();
                    if (!observer || observer->m_stopping) return;
                    observer->m_receiving_address->setPreparedDestination(address.destination, address.address);
                    if (!observer || observer->m_stopping) return;
                    observer->m_receiving_address->setAddressType(address.address_type);
                    if (!observer || observer->m_stopping) return;
                }
            }
        }
        if (apply && intent) {
            target->setNeedsUnlock(result.error == walletrequests::Error::NeedsUnlock || result.error == walletrequests::Error::LockedAttemptFailed || result.error == walletrequests::Error::IncorrectPassphrase);
            if (!observer || !target || observer->m_stopping) return;
            target->setUnlockError(result.error == walletrequests::Error::IncorrectPassphrase ? error_text : QString{});
            if (!observer || !target || observer->m_stopping) return;
        }
        if ((result.request_saved || result.request_loaded) && result.record && apply) {
            observer->applyRequest(target, result.record->display);
            if (!observer || !target || observer->m_stopping) return;
            if (result.request_loaded) {
                target->setIsEditing(true);
                if (!observer || observer->m_stopping) return;
            }
        }
        if (intent && !intent->create) {
            observer->m_receiving_address_pending = false;
            observer->m_receiving_address_ready = ready && apply;
            // Only a type the user chose is remembered. The starting default
            // (Taproot, or -addresstype) is not written on the first visit.
            if (ready && result.address && intent->explicit_type) observer->setDefaultReceiveAddressType(result.address->address_type);
            if (!observer || observer->m_stopping) return;
            Q_EMIT observer->receivingAddressStateChanged();
            if (!observer || observer->m_stopping) return;
            Q_EMIT observer->receiveRequestReconciliationPendingChanged();
            if (!observer || observer->m_stopping) return;
        }
        if (consume_address && result.request_saved && result.reservation_released &&
            observer->m_receiving_intent == consumed_intent && observer->m_receiving_address->revision() == address_revision) {
            observer->m_receiving_address_ready = false;
            observer->m_receiving_address->clear();
            if (observer && observer->observationsAllowed()) Q_EMIT observer->receivingAddressStateChanged();
        }
    });
    operation->start(available() ? m_request_store : nullptr, std::move(command), m_notification_bridge);
    const auto intent = operation->addressIntent();
    if (intent && !intent->create) {
        m_receiving_intent = intent;
        m_receiving_address_ready = false; m_receiving_address_pending = operation->pending();
        Q_EMIT receivingAddressStateChanged();
        if (observer && observer->observationsAllowed()) Q_EMIT receiveRequestReconciliationPendingChanged();
    }
    return operation;
}

ReceiveRequestOperation* WalletQmlModel::savePaymentRequest(PaymentRequest* request, bool consume_address)
{
    if (!request || !m_request_store) return nullptr;
    walletrequests::Command command;
    command.kind = request->id().isEmpty() ? walletrequests::Kind::Create : walletrequests::Kind::Edit;
    if (const auto record = m_receive_requests->preparedEntry(request->id())) {
        command.id = record->entry.id; command.revision = record->revision;
    } else if (!request->id().isEmpty()) {
        bool valid{false}; command.id = request->id().toLongLong(&valid);
        if (!valid) return nullptr;
    }
    command.draft = {request->address(), request->label(), request->message(), request->noteSelf(),
                     request->addressType(), request->amount()->satoshi()};
    command.preserve_current_note = command.kind == walletrequests::Kind::Create && request->notePrefilled() && !request->noteDirty();
    command.require_unused = consume_address;
    command.consume_address = consume_address;
    if (consume_address) command.reservation = m_receiving_intent;
    if (command.draft.address_type.isEmpty()) command.draft.address_type = defaultReceiveAddressType();
    command.view = consume_address ? 1 : 2;
    command.view_revision = request->revision();
    return submitRequest(std::move(command), request, consume_address);
}

ReceiveRequestOperation* WalletQmlModel::commitPaymentRequest()
{
    if (!available() || !m_current_payment_request) return nullptr;
    return savePaymentRequest(m_current_payment_request);
}

ReceiveRequestOperation* WalletQmlModel::commitPaymentRequestWithPassphrase(const QString& passphrase)
{
    if (!available() || !m_current_payment_request) return nullptr;
    auto* request = m_current_payment_request;
    walletrequests::Command command;
    command.kind = request->id().isEmpty() ? walletrequests::Kind::Create : walletrequests::Kind::Edit;
    if (const auto record = m_receive_requests->preparedEntry(request->id())) { command.id = record->entry.id; command.revision = record->revision; }
    command.draft = {request->address(), request->label(), request->message(), request->noteSelf(),
        request->addressType().isEmpty() ? defaultReceiveAddressType() : request->addressType(), request->amount()->satoshi()};
    command.view = 2; command.view_revision = request->revision();
    auto captured = secretinput::Capture(m_opening ? m_opening->scheduler() : nullptr, passphrase);
    command.captured_secret = std::move(captured.input); command.secret_capture_error = captured.error;
    return submitRequest(std::move(command), request);
}

void WalletQmlModel::reloadReceiveRequests()
{
    if (!m_request_store || m_stopping) return;
    walletrequests::Command command; command.kind = walletrequests::Kind::Reload;
    const auto admission = m_request_store->submit(std::move(command));
    m_request_storage_busy = bool(admission);
    m_request_storage_error = admission ? QString{} : tr("Payment requests could not be loaded. Try again.");
    if (m_receipt_projection && !m_receipt_projection->errorText().isEmpty()) m_receipt_projection->retry();
    const QPointer<WalletQmlModel> observer{this};
    Q_EMIT receiveRequestReconciliationPendingChanged();
    if (!observer || m_stopping) return;
    Q_EMIT receiveRequestStorageChanged();
}

ReceiveRequestOperation* WalletQmlModel::removeReceiveRequest(const QString& request_id)
{
    walletrequests::Command command; command.kind = walletrequests::Kind::Erase;
    const auto record = m_receive_requests->preparedEntry(request_id);
    if (!record) return nullptr;
    command.id = record->entry.id; command.revision = record->revision;
    return submitRequest(std::move(command));
}

bool WalletQmlModel::applyRequest(PaymentRequest* target, walletrequests::PreparedRequest data, bool as_template)
{
    // A property observer can close the opening or destroy either projection.
    // Keep values independent of the model's replaceable snapshot throughout.
    const QPointer<WalletQmlModel> observer{this};
    const QPointer<PaymentRequest> held{target};
    const auto generation = target->generation();
    const auto current = [&] {
        if (!observer || !held || observer->m_stopping || held->generation() != generation) return false;
        // A template consumes captured values, not the source row's identity.
        if (as_template) return true;
        const auto* latest = observer->m_receive_requests->preparedEntry(data.id);
        return latest && latest->revision == data.revision;
    };
    // Our setters produce bounded property notifications. Additional input
    // revisions belong to a reentrant receiver and stop this snapshot update.
    const auto update = [&](uint64_t own_signals, auto effect) {
        if (!current()) return false;
        const auto before = target->revision();
        effect();
        if (!current()) return false;
        const auto after = target->revision();
        return after >= before && after - before <= own_signals;
    };
    if (!as_template && !update(2, [&] { target->setPreparedDestination(data.destination, data.address); })) return false;
    if (!update(1, [&] { target->setAddressType(data.address_type); })) return false;
    if (!update(1, [&] { target->setLabel(data.label); })) return false;
    if (!update(1, [&] { target->setMessage(data.message); })) return false;
    if (!update(1, [&] { if (as_template) target->setNoteSelf(data.note); else target->setNoteSelfBaseline(data.note); })) return false;
    if (!update(1, [&] { target->amount()->setSatoshi(data.amount_sats); })) return false;
    if (as_template) {
        if (!update(0, [&] { target->setIsEditing(false); })) return false;
        return update(0, [&] { target->setIsEditing(true); });
    }
    if (!update(1, [&] { target->setId(data.numeric_id); })) return false;
    if (!update(0, [&] { target->setCreated(data.created); })) return false;
    if (!update(0, [&] { target->setReceivedAmountSatoshi(receivedPaymentRequestAmount(data.address)); })) return false;
    if (!update(0, [&] { target->setPaymentReceived(data.payment_received); })) return false;
    if (!update(0, [&] { target->setNeedsUnlock(false); })) return false;
    if (!update(0, [&] { target->setUnlockError({}); })) return false;
    if (!update(0, [&] { target->setIsEditing(false); })) return false;
    if (!update(0, [&] { target->setPreparedPayload(data.uri); })) return false;
    target->setStoredRevision(data.revision);
    return true;
}

bool WalletQmlModel::receiveRequestReconciliationPending() const
{
    const auto snapshot = m_receive_requests ? m_receive_requests->snapshot() : nullptr;
    return m_request_storage_busy || m_receiving_address_pending || (m_receipt_projection && (m_receipt_projection->pending() || m_receipt_projection->applying())) ||
        (m_opening && m_opening->notificationsPending()) || (m_receipts && !m_receipts->settled()) ||
        (m_request_store && (!m_request_store->presentationCurrent() || !snapshot || snapshot->revision != m_request_store->revision()));
}

void WalletQmlModel::publishRequestHistory()
{
    if (!observationsAllowed()) return;
    if (m_admission_closed) return;
    if (!m_request_store || m_stopping) return;
    auto update = m_request_store->take();
    if (!update || (update->snapshot && update->snapshot->opening != m_opening->identity())) return;
    const QPointer<WalletQmlModel> observer{this};
    m_request_storage_loaded = update->loaded;
    m_request_storage_busy = update->busy;
    m_request_storage_error = ReceiveRequestOperation::ErrorText(update->error);
    if (m_receive) m_receive->refresh();
    Q_EMIT receiveRequestReconciliationPendingChanged();
    if (!observer || m_stopping) return;
    if (!update->snapshot) { Q_EMIT receiveRequestStorageChanged(); return; }
    const auto store = m_request_store;
    store->retire(m_receive_requests->setSnapshot(std::move(update->snapshot)));
    if (!observer || m_stopping) return;
    // Only the bounded set of held projections is refreshed. Unsaved drafts
    // retain their text; paid public fields are always restored from authority.
    for (auto* held : {m_current_payment_request, m_detail_payment_request}) {
        const auto record = m_receive_requests->preparedEntry(held->id());
        if (!record) continue;
        if (!held->isEditing()) applyRequest(held, record->display);
        else if (record->display.payment_received) {
            const auto data = record->display;
            const QPointer<PaymentRequest> target{held};
            const auto generation = held->generation();
            const auto current = [&] {
                if (!observer || !target || observer->m_stopping || target->generation() != generation) return false;
                const auto* latest = observer->m_receive_requests->preparedEntry(data.id);
                return latest && latest->revision == data.revision;
            };
            held->setPaymentReceived(true);
            if (!current()) return;
            held->amount()->setSatoshi(data.amount_sats);
            if (!current()) return;
            held->setLabel(data.label);
            if (!current()) return;
            held->setMessage(data.message);
        }
        if (!observer || m_stopping) return;
    }
    Q_EMIT receiveRequestReconciliationPendingChanged();
    if (!observer || m_stopping) return;
    Q_EMIT receiveRequestStorageChanged();
}

bool WalletQmlModel::loadPaymentRequest(const QString& request_id)
{
    if (!available()) return false;
    const auto record = m_receive_requests->preparedEntry(request_id);
    if (!record || !m_current_payment_request) return false;
    const auto data = record->display;
    const QPointer<WalletQmlModel> observer{this};
    const QPointer<PaymentRequest> target{m_current_payment_request};
    const auto generation = target->generation();
    target->clear();
    if (!observer || !target || observer->m_stopping || target->generation() != generation + 1 ||
        !target->address().isEmpty() || !target->label().isEmpty() || !target->message().isEmpty() ||
        !target->noteSelf().isEmpty() || target->amount()->isSet()) return false;
    return applyRequest(target, data);
}

bool WalletQmlModel::loadPaymentRequestDetail(const QString& request_id)
{
    if (!available()) return false;
    const auto record = m_receive_requests->preparedEntry(request_id);
    if (!record || !m_detail_payment_request) return false;
    const auto data = record->display;
    const QPointer<WalletQmlModel> observer{this};
    const QPointer<PaymentRequest> target{m_detail_payment_request};
    const auto generation = target->generation();
    target->clear();
    if (!observer || !target || observer->m_stopping || target->generation() != generation + 1 ||
        !target->address().isEmpty() || !target->label().isEmpty() || !target->message().isEmpty() ||
        !target->noteSelf().isEmpty() || target->amount()->isSet()) return false;
    return applyRequest(target, data);
}

ReceiveRequestOperation* WalletQmlModel::updatePaymentRequest(const QString& request_id, qint64 amount,
                                                           const QString& label, const QString& message, const QString& note)
{
    const auto record = m_receive_requests->preparedEntry(request_id);
    if (!record) return nullptr;
    walletrequests::Command command; command.kind = walletrequests::Kind::Edit;
    command.id = record->entry.id; command.revision = record->revision;
    command.draft = {record->display.address, label, message, note, {}, amount};
    return submitRequest(std::move(command));
}

void WalletQmlModel::refreshReceiveRequestPayments()
{
    if (m_stopping || !m_request_store) return;
    m_request_store->interest(1, m_receiving_address->address());
}

void WalletQmlModel::publishReceipts()
{
    if (observationsAllowed() && !m_admission_closed && available() && m_receipt_projection) m_receipt_projection->consume();
}

CAmount WalletQmlModel::receivedPaymentRequestAmount(const QString& address) const
{
    return m_receipt_projection ? m_receipt_projection->amount(address) : 0;
}

void WalletQmlModel::updateReceivedPaymentRequestAmounts()
{
    const QPointer<WalletQmlModel> observer{this};
    for (auto* request : {m_current_payment_request, m_detail_payment_request}) {
        if (request && !request->id().isEmpty()) {
            request->setReceivedAmountSatoshi(receivedPaymentRequestAmount(request->address()));
            if (!observer || m_stopping) return;
        }
    }
}

void WalletQmlModel::usePaymentRequestAsTemplate(const QString& request_id)
{
    if (!available() || !m_request_store || !m_current_payment_request || !m_receive_requests) return;
    const auto record = m_receive_requests->preparedEntry(request_id);
    if (!record) return;
    const auto prepared = record->display;
    const QPointer<PaymentRequest> target{m_current_payment_request};
    const auto target_generation = target->generation();
    const auto target_revision = target->revision();
    // Fence the previous password question at admission. Storage cleanup and
    // the fresh-address decision stay ordered with the next reserve command.
    walletrequests::Command command; command.kind = walletrequests::Kind::ForgetAddress;
    command.view = 1; command.view_revision = m_receiving_address->revision();
    command.clear_reservation = true; command.draft.address = m_receiving_address->address();
    const auto admitted = m_request_store->submit(std::move(command));
    if (!admitted) {
        m_request_storage_error = tr("The receiving address could not be reset. Try again.");
        Q_EMIT receiveRequestStorageChanged();
        return;
    }
    const QPointer<WalletQmlModel> observer{this};
    m_receiving_intent.reset();
    m_receiving_address_ready = false; m_receiving_address_pending = false;
    m_receiving_address->clear();
    if (!observer || m_stopping) return;
    Q_EMIT receivingAddressStateChanged();
    if (!observer || m_stopping) return;
    if (!target || target->generation() != target_generation || target->revision() != target_revision) return;
    target->clear();
    if (!observer || !target || m_stopping || target->generation() != target_generation + 1 ||
        !target->label().isEmpty() || !target->message().isEmpty() || !target->noteSelf().isEmpty() || target->amount()->isSet()) return;
    applyRequest(target, prepared, true);
}

ReceiveRequestOperation* WalletQmlModel::setAddressLabel(const QString& address, const QString& label)
{
    walletrequests::Command command; command.kind = walletrequests::Kind::Label;
    command.address = address; command.label = label;
    return submitRequest(std::move(command));
}

TextEditSessionModel* WalletQmlModel::editAddressLabel(const QString& address, QObject* parent)
{
    if (!available() || !m_request_store || address.isEmpty() || address.size() > 128) return nullptr;
    TextEditSessionModel::Ports ports;
    ports.execution = {m_opening->scheduler(), m_fee_read_services.post};
    ports.validate = [](const QString& value) { return value.size() <= 16384; };
    ports.read = [weak = std::weak_ptr<walletopening::Session>{m_opening}, address](auto done) {
        const auto opening = weak.lock();
        if (!opening) return execution::Admission{{}, execution::Rejection::Closed};
        return opening->read([address](interfaces::Wallet& wallet) {
            TextEditSessionModel::Read result;
            const auto destination = DecodeDestination(address.toStdString());
            std::string label; wallet::AddressPurpose purpose;
            if (!IsValidDestination(destination) || !wallet.getAddress(destination, &label, &purpose) || purpose != wallet::AddressPurpose::RECEIVE)
                result.error = {TextEditError::Invalid, tr("This receiving address is no longer available.")};
            // UTF8 bytes and UTF16 editor units have different bounds. A
            // supported16384-unit note must survive its own readback.
            else if (label.size() > 65536) result.error = {TextEditError::Invalid, tr("The saved note is too long to edit here.")};
            else result.value = QString::fromStdString(label);
            return result;
        }, done, [done](std::exception_ptr) { done({{}, {TextEditError::Refused, tr("The address note could not be read.")}}); }, execution::WorkClass::Observation);
    };
    ports.admitWrite = [weak_store = std::weak_ptr<walletrequests::Store>{m_request_store}, weak_opening = std::weak_ptr<walletopening::Session>{m_opening}, address]() -> TextEditSessionModel::Writer {
        const auto store = weak_store.lock(); const auto opening = weak_opening.lock();
        if (!store || !opening) return {};
        const auto action = opening->startAction();
        if (!action) return {};
        return [store, action, address](const auto& attempt, auto done, const auto&) {
        walletrequests::Command command; command.kind = walletrequests::Kind::Label;
        command.address = address; command.label = attempt.values[0]; command.expected_label = attempt.base[0];
        const auto admission = store->submitResult(std::move(command), [saved_value = attempt.values[0], done = std::move(done)](const walletrequests::Result& result) {
            const auto* outcome = &result;
            TextEditSessionModel::Write value;
            value.persisted = outcome->label_saved;
            value.value = saved_value;
            value.outcome = outcome->error == walletrequests::Error::Uncertain ? persistentedit::Outcome::Unknown
                : outcome->error == walletrequests::Error::None ? persistentedit::Outcome::Saved
                : outcome->label_saved ? persistentedit::Outcome::Partial : persistentedit::Outcome::Failed;
            value.error = {outcome->error == walletrequests::Error::None ? TextEditError::None
                : outcome->error == walletrequests::Error::Changed ? TextEditError::Changed
                : outcome->error == walletrequests::Error::Uncertain ? TextEditError::Unknown : TextEditError::Persistence,
                ReceiveRequestOperation::ErrorText(outcome->error)};
            if (!outcome->label_saved && outcome->current_label) value.current = TextEditSessionModel::Read{*outcome->current_label, {}};
            done(std::move(value));
        }, action);
        return execution::Admission{{}, admission.rejection};
        };
    };
    // The read-only port cannot infer an unknown write's durability from a
    // matching value. Until a retained native receipt resolves it, stay unknown.
    auto* editor = new AddressLabelEditorModel(this, address, std::move(ports), parent);
    if (!editor->available()) { delete editor; return nullptr; }
    connect(this, &WalletQmlModel::availableChanged, editor, [this, editor] { if (!available()) editor->retire(); });
    connect(this, &WalletQmlModel::addressLabelsChanged, editor, &TextEditSessionModel::refresh);
    connect(editor, &TextEditSessionModel::changed, this, [this, editor, consumed = QString{}]() mutable {
        const auto result = editor->saveResult();
        if (!result.persisted || result.operationId == consumed || !observationsAllowed()) return;
        consumed = result.operationId;
        const QPointer<WalletQmlModel> observer{this};
        Q_EMIT addressLabelsChanged();
        if (observer && observationsAllowed()) Q_EMIT addressListChanged();
    });
    return editor;
}

void WalletQmlModel::requestSendFacts()
{
    if (!available() || m_stopping || !m_opening || coinSelectionPending()) return;
    {
        std::lock_guard lock{m_send_facts_input->mutex};
        m_send_facts_input->control = m_coin_control;
        m_send_facts_input->control.m_confirm_target = feeTargetBlocks();
        m_send_facts_input->selected = selectedCoins();
        m_send_facts_input->input_mode = m_coins_list_model && m_coins_list_model->manualInputSelection()
            ? coinselection::InputMode::Manual : coinselection::InputMode::Automatic;
        m_send_facts_input->draft = m_send_recipients->draftSnapshot();
        m_send_facts_input->maximum_token = m_send_recipients->recipientToken(m_maximum_recipient);
        ++m_send_facts_input->revision;
    }
    if (!m_send_facts_query) {
        const QPointer<WalletQmlModel> observer{this};
        m_send_facts_query = std::make_shared<execution::SnapshotQuery<SendFacts>>(m_opening->scheduler(),
            [opening = m_opening, input = m_send_facts_input, node = m_node, policy = m_fee_policy] {
                wallet::CCoinControl control;
                quint64 revision;
                std::shared_ptr<const CoinsListModel::Selection> selected;
                coinselection::InputMode input_mode;
                std::shared_ptr<const senddraft::Snapshot> draft;
                quint64 maximum_token;
                {
                    std::lock_guard lock{input->mutex};
                    control = input->control;
                    selected = input->selected;
                    input_mode = input->input_mode;
                    draft = input->draft;
                    maximum_token = input->maximum_token;
                    revision = input->revision;
                }
                auto wallet = opening->borrow();
                if (!wallet) throw execution::NotStarted{execution::NotStarted::Cause::Cancelled};
                if (selected) for (const auto& outpoint : *selected) control.Select(outpoint);
                ApplySelectedInputsPolicy(control, input_mode);
                SendFacts facts;
                wallet::CCoinControl automatic_control;
                ApplySelectedInputsPolicy(automatic_control, coinselection::InputMode::Automatic);
                facts.available = wallet->getAvailableBalance(automatic_control);
                facts.selected = wallet->getAvailableBalance(control);
                if (draft) facts.gross_remainder = senddraft::GrossRemainder(*draft, maximum_token, facts.selected);
                facts.dust = node ? node->getDustRelayFee().GetFeePerK() : DUST_RELAY_TX_FEE;
                facts.minimum_relay = policy.minimum_relay ? std::max<CAmount>(DEFAULT_MIN_RELAY_TX_FEE, policy.minimum_relay()) : DEFAULT_MIN_RELAY_TX_FEE;
                facts.revision = revision;
                for (const auto target : CUSTOM_FEE_TARGETS) {
                    auto target_control = control;
                    target_control.m_feerate.reset();
                    target_control.m_confirm_target = target;
                    facts.target_rates.insert(target, wallet->getMinimumFee(FEE_RATE_BASIS_VBYTES, target_control, nullptr, nullptr));
                }
                control.m_feerate.reset();
                feeestimate::ApplyAutomaticPolicy(*wallet, control);
                facts.selected_target_rate = control.m_feerate ? control.m_feerate->GetFeePerK()
                    : wallet->getMinimumFee(FEE_RATE_BASIS_VBYTES, control, nullptr, nullptr);
                return facts;
            }, [post = m_fee_read_services.post, observer] {
                post([observer] { if (observer) observer->publishSendFacts(); });
            });
    }
    const bool was_known = std::exchange(m_send_facts_known, false);
    m_send_facts_query->request();
    if (was_known) Q_EMIT sendFactsChanged();
}

void WalletQmlModel::publishSendFacts()
{
    if (!observationsAllowed()) return;
    // A delivery acknowledges the query notice even when a newer selection
    // makes this result ineligible. Otherwise later reads cannot post a fresh
    // notice and the new selection's facts remain unpublished indefinitely.
    const auto result = m_send_facts_query->take();
    if (coinSelectionPending() || m_stopping || !available() || !result) return;
    if (!result->snapshot) {
        Q_EMIT customFeeTargetPendingChanged();
        return;
    }
    {
        std::lock_guard lock{m_send_facts_input->mutex};
        if (result->snapshot->revision != m_send_facts_input->revision) return;
    }
    m_available_send_balance = result->snapshot->available;
    m_selected_send_balance = result->snapshot->selected;
    const bool was_custom_valid = customFeeRateValid();
    m_dust_relay_fee = result->snapshot->dust;
    m_minimum_relay_fee = result->snapshot->minimum_relay;
    m_send_facts_known = true;
    const QPointer<WalletQmlModel> observer{this};
    Q_EMIT sendFactsChanged();
    if (!observer || m_stopping) return;
    if (was_custom_valid != customFeeRateValid()) {
        Q_EMIT customFeeRateValidChanged();
        if (!observer || m_stopping) return;
    }
    Q_EMIT sendAmountExhaustsBalanceChanged();
    if (!observer || m_stopping) return;
    {
        std::lock_guard lock{m_send_facts_input->mutex};
        if (result->snapshot->revision != m_send_facts_input->revision) return;
    }
    auto rates = result->snapshot->target_rates;
    rates.insert(feeTargetBlocks(), result->snapshot->selected_target_rate);
    if (m_fees) m_fees->observe(result->snapshot->minimum_relay, std::move(rates));
    if (!observer || m_stopping) return;
    const auto draft = m_send_recipients ? m_send_recipients->draftSnapshot() : nullptr;
    if (draft && m_maximum_recipient && draft->maximum == m_maximum_recipient->identity() &&
        m_maximum_recipient->address()->address().isEmpty() && result->snapshot->gross_remainder) {
        QScopedValueRollback<bool> updating{m_updating_maximum, true};
        m_maximum_recipient->amount()->setSatoshi(*result->snapshot->gross_remainder, BitcoinAmount::Origin::Model);
    }
}

quint8 WalletQmlModel::automaticMaximumCapabilities() const
{
    return quint8((m_metadata_known ? 1 : 0) | (m_private_keys_disabled ? 2 : 0) | (m_external_signer ? 4 : 0) |
                  (isEncrypted() ? 8 : 0) | (isLocked() ? 16 : 0) | (m_can_get_addresses ? 32 : 0) | (m_taproot ? 64 : 0));
}
void WalletQmlModel::invalidateAutomaticMaximum()
{
    if (!m_automatic_maximum) return;
    m_automatic_maximum.reset();
    // A foreign edit may reenter inside the automatic amount signal. The old
    // GUI receipt must not finish that newer intent before its draft is ready.
    if (m_updating_maximum) ++m_fee_estimate_request_id;
}
void WalletQmlModel::publishAutomaticMaximum(CAmount amount)
{
    if (!m_maximum_recipient || !m_send_recipients || m_stopping || !available()) return;
    const auto recipient = m_maximum_recipient;
    const auto draft = m_send_recipients->draftSnapshot();
    if (!draft || draft->maximum != recipient->identity() || recipient->amount()->satoshi() == amount) return;
    m_automatic_maximum = AutomaticMaximum{recipient, recipient->identity(), recipient->validationRevision() + 1,
        m_fee_estimate_request_id, amount, m_send_recipients->count(), automaticMaximumCapabilities()};
    const QPointer<WalletQmlModel> observer{this};
    const bool previous = std::exchange(m_updating_maximum, true);
    recipient->amount()->setSatoshi(amount, BitcoinAmount::Origin::Model);
    if (observer) m_updating_maximum = previous;
}
void WalletQmlModel::scheduleFeesForRecipientValidation()
{
    if (m_automatic_maximum && !m_stopping && available() && m_send_recipients) {
        const auto& original = *m_automatic_maximum;
        if (original.recipient && m_maximum_recipient == original.recipient &&
            original.recipient->identity() == original.token && original.recipient->validationRevision() == original.revision &&
            original.recipient->amount()->satoshi() == original.amount && m_send_recipients->count() == original.count &&
            automaticMaximumCapabilities() == original.capabilities &&
            m_fee_estimate_request_id == original.fee_request) {
            if (m_send_recipients->draftSnapshot() && !original.recipient->validationPending()) m_automatic_maximum.reset();
            return;
        }
    }
    invalidateAutomaticMaximum();
    scheduleFeeEstimates();
}
void WalletQmlModel::scheduleFeeEstimates()
{
    if (!observationsAllowed() || m_updating_maximum || m_fee_read_services.timer == nullptr) return;
    m_automatic_maximum.reset();
    const QPointer<WalletQmlModel> observer{this};
    const auto prior_request = m_fee_estimate_request_id;
    const auto timer = m_fee_read_services.timer;
    const auto prior_owns = [observer, prior_request] {
        return observer && observer->observationsAllowed() && observer->m_fee_estimate_request_id == prior_request;
    };
    requestSendFacts();
    if (!prior_owns()) return;
    timer->stop();
    if (!prior_owns()) return;

    if (!m_wallet || !m_send_recipients || !ReadyForFeePreview(m_send_recipients->draftSnapshot(), m_send_recipients->recipientToken(m_maximum_recipient))) {
        clearFeeEstimates();
        return;
    }
    const auto request = ++m_fee_estimate_request_id;
    invalidateFeeRead(false, true);
    if (!observer || !observationsAllowed() || request != m_fee_estimate_request_id) return;
    if (!m_fees || m_fees->policyPending()) { clearFeeEstimates(); return; }
    m_fees->invalidateEstimate(FeeSelectionModel::Scheduled);
    if (!observer || !observationsAllowed() || request != m_fee_estimate_request_id) return;
    timer->restart([observer] { if (observer) observer->requestFeeEstimatesNow(); });
}

void WalletQmlModel::requestFeeEstimatesNow()
{
    if (!observationsAllowed()) return;
    if (m_stopping || !m_wallet || !m_send_recipients || coinSelectionPending() || coinSelectionBlocked() || !m_fees || m_fees->policyPending()) { clearFeeEstimates(); return; }
    FeeReadInput input;
    input.input_mode = m_coins_list_model && m_coins_list_model->manualInputSelection() ? coinselection::InputMode::Manual : coinselection::InputMode::Automatic;
    input.coin_control = m_coin_control; input.coin_control.m_confirm_target = feeTargetBlocks(); input.selected = selectedCoins();
    input.draft = m_send_recipients->draftSnapshot();
    input.maximum_token = m_send_recipients->recipientToken(m_maximum_recipient);
    if (!ReadyForFeePreview(input.draft, input.maximum_token)) { clearFeeEstimates(); return; }
    input.custom_fee_enabled = customFeeEnabled(); input.custom_fee_rate = customFeeRate(); input.custom_rate_per_kvb = m_fees->ratePerKvB();
    if (input.custom_fee_enabled && !input.custom_rate_per_kvb) { clearFeeEstimates(); return; }
    input.dependencies = {input.draft->revision, input.draft->intent_revision, m_coins_list_model ? m_coins_list_model->selectionRevision() : 0, m_fees->policyRevision(), m_fee_backend_revision, m_fees->observationRevision()};
    const auto request = ++m_fee_estimate_request_id;
    if (m_fees) m_fees->invalidateEstimate(FeeSelectionModel::Scheduled);
    if (m_fee_reader) m_fee_reader->request(request, std::move(input));
}

void WalletQmlModel::publishFeeEstimates()
{
    if (!observationsAllowed() || m_stopping || !m_fee_reader) return;
    const auto publication = m_fee_reader->take(); if (!publication || publication->revision != m_fee_estimate_request_id) return;
    const auto request = m_fee_estimate_request_id;
    const QPointer<WalletQmlModel> observer{this};
    m_fees->publishEstimate(publication->phase, publication->result, publication->fresh, publication->observed_dependencies, !publication->fresh && publication->source_revision != 0);
    if (!observer || !observationsAllowed() || request != m_fee_estimate_request_id) return;
    if (const auto preview = selectedFeePreview(); publication->fresh && m_maximum_recipient && preview && preview->maximum_amount) publishAutomaticMaximum(*preview->maximum_amount);
}

void WalletQmlModel::clearFeeEstimates()
{
    if (!observationsAllowed()) return;
    ++m_fee_estimate_request_id; invalidateFeeRead();
    if (m_fees) m_fees->invalidateEstimate();
}

WalletQmlModel::PreparationState WalletQmlModel::preparationState() const
{
    switch (m_preparation_projection.phase) {
    case transactionattempt::Phase::Idle: return PreparationState::PreparationIdle;
    case transactionattempt::Phase::Preparing: return PreparationState::PreparationPending;
    case transactionattempt::Phase::NeedsUnlock: return PreparationState::PreparationNeedsUnlock;
    case transactionattempt::Phase::Reviewing: return PreparationState::PreparationReady;
    case transactionattempt::Phase::NeedsRevalidation: return PreparationState::PreparationStale;
    case transactionattempt::Phase::Submitting: return PreparationState::PreparationPending;
    case transactionattempt::Phase::Settled: return PreparationState::PreparationReady;
    case transactionattempt::Phase::Failed: return PreparationState::PreparationFailed;
    case transactionattempt::Phase::Closed: return PreparationState::PreparationClosed;
    case transactionattempt::Phase::Uncertain: return PreparationState::PreparationUncertain;
    }
    return PreparationState::PreparationFailed;
}

bool WalletQmlModel::currentTransactionNeedsRevalidation() const
{
    return m_current_transaction && m_current_transaction_source == CurrentTransactionSource::SendDraft &&
        ((m_send_recipients && m_send_recipients->importPending()) || !m_preparation_projection.candidate_current);
}

quint64 WalletQmlModel::preparationId() const
{
    return m_preparation_projection.id;
}

bool WalletQmlModel::preparationBusy() const
{
    return m_preparation_projection.phase == transactionattempt::Phase::Preparing;
}

void WalletQmlModel::publishPreparationProjection(transactionattempt::Snapshot snapshot)
{
    if (m_admission_closed || snapshot.sequence < m_preparation_projection_sequence || snapshot.id < m_preparation_projection.id ||
        snapshot.intent_revision < m_gui_preparation_intent_revision) return;
    const auto& current = m_preparation_projection;
    const bool changed = snapshot.id != current.id || snapshot.intent_revision != current.intent_revision ||
        snapshot.ticket != current.ticket || snapshot.phase != current.phase || snapshot.candidate != current.candidate ||
        snapshot.candidate_review != current.candidate_review || snapshot.current_review != current.current_review ||
        snapshot.candidate_current != current.candidate_current;
    m_preparation_projection_sequence = snapshot.sequence;
    m_preparation_projection = std::move(snapshot);
    if (m_gui_preparation_intent_revision < m_preparation_projection.intent_revision)
        m_gui_preparation_intent_revision = static_cast<quint64>(m_preparation_projection.intent_revision);
    if (changed) Q_EMIT preparationChanged();
}

transactionattempt::CurrentFacts WalletQmlModel::preparationFacts() const
{
    return {
        m_opening ? m_opening->identity() : walletopening::Opening{},
        m_current_review_identity,
        available() && m_opening && m_opening->available(),
    };
}

bool WalletQmlModel::setCurrentTransaction(WalletQmlModelTransaction* transaction, bool from_preparation)
{
    if (m_current_transaction == transaction) return false;
    m_imported_review_owner = 0; m_imported_review_child = 0;
    invalidateImportReview();
    if (m_send_review && m_send_review != transaction && m_send_consent) m_send_consent->withdraw();
    if (m_send_review && m_send_review != transaction && m_send_operation &&
        (m_send_operation->stage() == walletsend::Stage::AwaitingSweepConsent ||
         m_send_operation->stage() == walletsend::Stage::AwaitingCredentials)) {
        cancelSendTransaction(m_send_request_id);
    }
    const auto previous_phase = m_preparation_projection.phase;
    const bool previous_candidate_current = m_preparation_projection.candidate_current;
    const bool preparation_state_exists = bool(m_preparation_projection.candidate) || previous_phase != transactionattempt::Phase::Idle;
    m_current_transaction = transaction;
    if (m_current_review_identity == std::numeric_limits<quint64>::max()) {
        m_preparation_projection.phase = transactionattempt::Phase::Closed;
        if (m_transaction_attempt) m_transaction_attempt->close({});
        return preparation_state_exists || previous_candidate_current;
    }
    ++m_current_review_identity;
    if (!from_preparation) {
        if (m_preparation_projection.candidate_review != m_current_review_identity && previous_candidate_current) {
            m_preparation_projection.candidate_current = false;
            if (m_preparation_projection.phase == transactionattempt::Phase::Reviewing)
                m_preparation_projection.phase = transactionattempt::Phase::NeedsRevalidation;
        }
        if (m_transaction_attempt) {
            const QPointer<WalletQmlModel> observer{this};
            const auto post = m_fee_read_services.post;
            m_transaction_attempt->reviewReplaced(m_current_review_identity, [observer, post](auto result) {
                post([observer, snapshot = std::move(result.snapshot)]() mutable {
                    if (observer) observer->publishPreparationProjection(std::move(snapshot));
                });
            });
        }
    }
    return preparation_state_exists || m_preparation_projection.phase != previous_phase ||
        previous_candidate_current != m_preparation_projection.candidate_current;
}

quint64 WalletQmlModel::requestPrepareTransaction()
{
    return requestPrepareTransactionInternal();
}

quint64 WalletQmlModel::requestPrepareTransactionWithPassphrase(const QString& passphrase)
{
    return requestPrepareTransactionInternal(passphrase);
}

void WalletQmlModel::invalidatePreparation()
{
    if (m_updating_maximum) return;
    const auto phase = m_preparation_projection.phase;
    const bool candidate_was_current = m_preparation_projection.candidate_current;
    if (m_gui_preparation_intent_revision != std::numeric_limits<quint64>::max()) ++m_gui_preparation_intent_revision;
    if (m_preparation_projection.phase == transactionattempt::Phase::Reviewing && m_preparation_projection.candidate) {
        m_preparation_projection.phase = transactionattempt::Phase::NeedsRevalidation;
        m_preparation_projection.candidate_current = false;
    }
    m_preparation_projection.intent_revision = m_gui_preparation_intent_revision;
    if (m_transaction_attempt) {
        const QPointer<WalletQmlModel> observer{this};
        const auto post = m_fee_read_services.post;
        m_transaction_attempt->intentChanged(m_gui_preparation_intent_revision, [observer, post](auto result) {
            post([observer, snapshot = std::move(result.snapshot)]() mutable {
                if (observer) observer->publishPreparationProjection(std::move(snapshot));
            });
        });
    }
    if (phase != m_preparation_projection.phase || candidate_was_current != m_preparation_projection.candidate_current)
        Q_EMIT preparationChanged();
}

quint64 WalletQmlModel::requestPrepareTransactionInternal(std::optional<QString> passphrase)
{
    if (!observationsAllowed() || coinSelectionPending() || psbtActionBusy() || preparationBusy() || customFeeTargetPending() || signerApprovalBusy() || (sendBusy() && m_send_review && m_send_review == m_current_transaction) || securityBusy() || m_sending_commit->load()) return 0;
    if (!m_transaction_attempt || m_next_preparation_id == std::numeric_limits<quint64>::max()) return 0;
    // Terminal outcomes have independent Native value ownership. Retire their
    // operation slots before reserving a successor so a capacity failure can
    // recover without requiring the wallet facade to be destroyed.
    m_preparation.reset(); m_preparation_lease.reset();
    const auto opening = m_opening;
    const quint64 id = ++m_next_preparation_id;
    const auto facts = preparationFacts();
    const auto intent_revision = m_gui_preparation_intent_revision;
    const auto draft_error = m_send_recipients ? m_send_recipients->validationError() : QString{};
    transactionattempt::StartInput input;
    input.id = id;
    input.intent_revision = intent_revision;
    input.facts = facts;
    input.draft = m_send_recipients ? m_send_recipients->draftSnapshot() : nullptr;
    input.draft_current = available() && m_opening && bool(input.draft);
    input.draft_error = draft_error;
    input.selection_needs_reconciliation = m_coins_list_model && m_coins_list_model->selectionReconciliationRequired();
    input.manual_selection = m_coins_list_model && m_coins_list_model->manualInputSelection();
    input.has_selected_coins = selectedCoins() && !selectedCoins()->empty();
    input.custom_fee_enabled = customFeeEnabled();
    const auto custom_rate = input.custom_fee_enabled && m_fees ? m_fees->ratePerKvB() : std::optional<CAmount>{};
    input.custom_fee_rate_valid = !input.custom_fee_enabled || custom_rate.has_value();

    auto request = std::make_shared<walletprepare::Request>();
    if (input.draft_current) {
        request->control = m_coin_control;
        request->control.m_confirm_target = feeTargetBlocks();
        request->selected = selectedCoins();
        request->input_mode = input.manual_selection ? coinselection::InputMode::Manual : coinselection::InputMode::Automatic;
        if (m_send_facts_known) request->available_balance = m_selected_send_balance;
        ApplySelectedInputsPolicy(request->control, request->input_mode);
        request->draft = input.draft;
        request->maximum_token = m_send_recipients->recipientToken(m_maximum_recipient);
        request->target_blocks = feeTargetBlocks();
        request->fee_rate = estimatedFeeRate();
        request->display_unit = m_display_unit;
        if (input.custom_fee_enabled && custom_rate) {
            request->control.m_confirm_target.reset();
            request->control.m_feerate = CFeeRate{*custom_rate};
            request->control.fOverrideFeeRate = true;
        } else {
            request->control.m_feerate.reset();
            if (!request->control.m_confirm_target) request->control.m_confirm_target = DEFAULT_STANDARD_FEE_TARGET;
            request->automatic_fee = true;
        }
    }

    m_preparation_projection = {};
    m_preparation_projection.id = id;
    m_preparation_projection.intent_revision = intent_revision;
    m_preparation_projection.ticket = {id, intent_revision, facts.session, facts.review};
    m_preparation_projection.phase = transactionattempt::Phase::Preparing;
    m_preparation_projection_sequence = 0;

    const QPointer<WalletQmlModel> observer{this};
    const auto post = presentation::ReliablePost(m_fee_read_services.post);
    const auto admission = m_transaction_attempt->begin(std::move(input),
        [observer, post, request = std::move(request), passphrase = std::move(passphrase), opening, draft_error](auto result) mutable {
            post([observer, result = std::move(result), request = std::move(request),
                  passphrase = std::move(passphrase), opening, draft_error]() mutable {
                if (observer) observer->startPreparation(std::move(result), std::move(request),
                    std::move(passphrase), opening, draft_error);
            });
        });
    if (!admission) {
        m_preparation_projection.phase = transactionattempt::Phase::Failed;
        setTransactionStatus(tr("The transaction preparation could not be queued."));
        Q_EMIT preparationChanged();
    } else {
        Q_EMIT preparationChanged();
    }
    return id;
}

void WalletQmlModel::startPreparation(transactionattempt::Result admission,
    std::shared_ptr<walletprepare::Request> request, std::optional<QString> passphrase,
    std::shared_ptr<walletopening::Session> opening, QString draft_error)
{
    if (!observationsAllowed()) return;
    const QPointer<WalletQmlModel> observer{this};
    const auto ticket = admission.snapshot.ticket;
    if (admission.snapshot.id != m_preparation_projection.id || ticket.id != m_preparation_projection.id) {
        m_preparation_projection.phase = admission.admission_error == transactionattempt::AdmissionError::Closed
            ? transactionattempt::Phase::Closed : transactionattempt::Phase::NeedsRevalidation;
        m_preparation_projection.candidate_current = false;
        if (admission.admission_error == transactionattempt::AdmissionError::StaleIntent)
            setTransactionStatus(tr("The transaction changed while it was being prepared. Prepare it again."));
        if (observer && observer->observationsAllowed()) Q_EMIT preparationChanged();
        return;
    }
    const auto post = presentation::ReliablePost(m_fee_read_services.post);
    const auto reject = [this, observer, post, ticket](transactionattempt::Phase phase, const QString& error) {
        if (!observer || !observer->observationsAllowed()) return;
        if (m_preparation_projection.id == ticket.id) {
            m_preparation_projection.phase = phase;
            if (phase != transactionattempt::Phase::Reviewing) {
                m_preparation_projection.candidate.reset();
                m_preparation_projection.candidate_current = false;
            }
        }
        if (!error.isEmpty()) setTransactionStatus(error);
        if (!observer || !observer->observationsAllowed()) return;
        m_transaction_attempt->reject(ticket, phase, [observer, post](auto result) {
            post([observer, snapshot = std::move(result.snapshot)]() mutable {
                if (observer) observer->publishPreparationProjection(std::move(snapshot));
            });
        });
        if (observer && observer->observationsAllowed()) Q_EMIT preparationChanged();
    };
    publishPreparationProjection(admission.snapshot);
    if (!observer || !observer->observationsAllowed()) return;
    if (admission.admission_error != transactionattempt::AdmissionError::None) {
        QString error;
        switch (admission.admission_error) {
        case transactionattempt::AdmissionError::None: break;
        case transactionattempt::AdmissionError::Closed: error = tr("This wallet is closing."); break;
        case transactionattempt::AdmissionError::InvalidDraft:
            error = !draft_error.isEmpty() ? draft_error : tr("Enter at least one valid recipient to continue.");
            break;
        case transactionattempt::AdmissionError::SelectionNeedsReconciliation:
            error = tr("Some manually selected coins are no longer available. Choose Automatic inputs or update the selection.");
            break;
        case transactionattempt::AdmissionError::ManualSelectionEmpty:
            error = tr("Select at least one coin or choose Automatic inputs.");
            break;
        case transactionattempt::AdmissionError::CustomFeeInvalid:
            error = tr("Enter a valid custom fee rate.");
            break;
        case transactionattempt::AdmissionError::StaleIntent:
            error = tr("The transaction changed while it was being prepared. Prepare it again.");
            break;
        }
        if (admission.admission_error == transactionattempt::AdmissionError::StaleIntent) {
            reject(transactionattempt::Phase::NeedsRevalidation, error);
        } else {
            const auto phase = admission.admission_error == transactionattempt::AdmissionError::Closed
                ? transactionattempt::Phase::Closed : transactionattempt::Phase::Failed;
            reject(phase, error);
        }
        return;
    }
    if (admission.transition != transactionattempt::Transition::Accepted) return;
    const auto unchanged = [this, observer, opening, ticket] {
        return observer && observationsAllowed() && m_opening == opening && opening && opening->available() &&
            m_preparation_projection.id == ticket.id && m_preparation_projection.intent_revision == ticket.intent_revision &&
            m_gui_preparation_intent_revision == ticket.intent_revision && m_current_review_identity == ticket.prior_review;
    };
    if (!unchanged()) {
        m_transaction_attempt->assess(ticket, preparationFacts(), [observer, post](auto result) {
            post([observer, snapshot = std::move(result.snapshot)]() mutable {
                if (observer) observer->publishPreparationProjection(std::move(snapshot));
            });
        });
        return;
    }
    if (passphrase) {
        auto captured = secretinput::Capture(opening->scheduler(), *passphrase);
        if (!unchanged()) return;
        if (!captured) {
            reject(transactionattempt::Phase::Failed, tr("The password input could not be queued. No transaction was prepared."));
            return;
        }
        request->captured_passphrase = std::move(captured.input);
    }
    const auto action = opening->startAction();
    if (!unchanged()) return;
    if (!action) {
        reject(transactionattempt::Phase::Failed, tr("The transaction preparation could not be queued."));
        return;
    }
    auto operation = std::make_shared<execution::OwnedCommand<walletprepare::Result>>(opening->scheduler(),
        [post = presentation::ReliablePost(m_fee_read_services.post), observer] {
            post([observer] { if (observer) observer->publishPreparation(); });
        });
    auto native_owner = std::make_shared<std::shared_ptr<execution::OwnedCommand<walletprepare::Result>>>(operation);
    auto hold = opening->holdValues([native_owner] { native_owner->reset(); });
    if (!unchanged()) return;
    auto output = walletopening::RetainValues<walletprepare::Result>(opening, [] { return std::make_unique<walletprepare::Result>(); });
    if (!unchanged()) return;
    if (!hold || !output) {
        reject(transactionattempt::Phase::Failed, tr("Transaction review is unavailable."));
        return;
    }
    m_preparation = operation;
    m_preparation_lease.emplace(std::move(*hold.lease));
    const auto admitted = operation->startWith([opening, action, request, output](auto done, auto failed) {
        return opening->perform(action, [request, output](interfaces::Wallet& wallet) {
            return walletprepare::ExecuteInto(wallet, std::move(*request), output);
        }, std::move(done), std::move(failed));
    });
    if (!admitted) {
        reject(transactionattempt::Phase::Failed, tr("The transaction preparation could not be queued."));
        return;
    }
    if (!unchanged()) return;
    clearTransactionStatus();
    if (unchanged()) Q_EMIT preparationChanged();
}

void WalletQmlModel::publishPreparation()
{
    if (!observationsAllowed()) return;
    if (!m_preparation || !preparationBusy() || !available()) return;
    const auto result = m_preparation->result();
    if (!result) return;
    auto value = result->value.value_or(walletprepare::Result{});
    if (result->error) value.error = walletprepare::Error::Failed;
    const auto ticket = m_preparation_projection.ticket;
    const auto original_facts = preparationFacts();
    auto installed_review = m_current_review_identity;
    if (value.error == walletprepare::Error::None && value.review && installed_review != std::numeric_limits<quint64>::max()) {
        ++installed_review;
    } else if (value.error == walletprepare::Error::None) {
        value.error = walletprepare::Error::Failed;
    }
    const QPointer<WalletQmlModel> observer{this};
    const auto post = presentation::ReliablePost(m_fee_read_services.post);
    const auto opening = m_opening;
    const auto command = m_preparation;
    const auto outcome = value.error;
    const auto candidate = value.review;
    m_transaction_attempt->resolve(ticket, original_facts, outcome, candidate, installed_review,
        [observer, post, value = std::move(value), ticket, original_facts, opening, command](auto resolution) mutable {
            post([observer, resolution = std::move(resolution), value = std::move(value),
                  ticket, original_facts, opening, command]() mutable {
                if (observer) observer->applyPreparationResolution(std::move(resolution), std::move(value),
                    ticket, original_facts, opening, command);
            });
        });
}

void WalletQmlModel::applyPreparationResolution(transactionattempt::Result resolution,
    walletprepare::Result effect, transactionattempt::Ticket ticket,
    transactionattempt::CurrentFacts original_facts, std::shared_ptr<walletopening::Session> opening,
    std::shared_ptr<execution::OwnedCommand<walletprepare::Result>> command)
{
    if (!observationsAllowed() || !opening || !opening->available() || m_opening != opening ||
        m_preparation != command || m_preparation_projection.id != ticket.id || !available()) return;
    if (resolution.transition != transactionattempt::Transition::Accepted) {
        publishPreparationProjection(std::move(resolution.snapshot));
        return;
    }
    if (m_preparation_projection.intent_revision != ticket.intent_revision ||
        m_gui_preparation_intent_revision != ticket.intent_revision ||
        m_current_review_identity != original_facts.review) {
        // The attempt owner may have accepted its result just before a newer
        // GUI intent reached its Domain turn. Publish only if this snapshot
        // already contains that newer revision; otherwise the queued intent
        // outcome will carry the authoritative stale phase.
        publishPreparationProjection(std::move(resolution.snapshot));
        return;
    }
    if (resolution.snapshot.phase != transactionattempt::Phase::Reviewing || !resolution.snapshot.candidate) {
        publishPreparationProjection(std::move(resolution.snapshot));
        QString message = effect.message;
        if (message.isEmpty()) {
            switch (effect.error) {
            case walletprepare::Error::InsufficientBalance: message = tr("The wallet does not have enough balance for this transaction."); break;
            case walletprepare::Error::InsufficientSelectedBalance: message = tr("Selected inputs do not cover the amount plus fee"); break;
            case walletprepare::Error::NeedsUnlock: message = tr("Enter your wallet password to prepare this transaction."); break;
            case walletprepare::Error::IncorrectPassphrase: message = tr("The wallet password is incorrect."); break;
            case walletprepare::Error::RelockFailed: message = tr("The wallet lock could not be restored. Check its security state before retrying."); break;
            case walletprepare::Error::SweepUnknown: message = tr("The available wallet funds could not be verified. Refresh the wallet and try again."); break;
            default: message = tr("The transaction could not be prepared."); break;
            }
        }
        setTransactionStatus(message, effect.error == walletprepare::Error::NeedsUnlock || effect.error == walletprepare::Error::IncorrectPassphrase);
        if (!observationsAllowed()) return;
        opening->refreshMetadata();
        if (!observationsAllowed()) return;
        Q_EMIT preparationChanged();
        return;
    }

    WalletQmlModelTransaction* prepared{nullptr};
    try { prepared = new WalletQmlModelTransaction(resolution.snapshot.candidate, opening, m_fee_read_services.post, this); }
    catch (const std::exception&) {
        m_transaction_attempt->discard(ticket, transactionattempt::Phase::Failed, {});
        m_preparation_projection.phase = transactionattempt::Phase::Failed;
        m_preparation_projection.candidate.reset();
        m_preparation_projection.candidate_current = false;
        setTransactionStatus(tr("Transaction review is unavailable."));
        if (observationsAllowed()) Q_EMIT preparationChanged();
        return;
    }
    if (!observationsAllowed() || m_gui_preparation_intent_revision != ticket.intent_revision ||
        m_current_review_identity != original_facts.review || m_preparation_projection.id != ticket.id) {
        prepared->deleteLater();
        return;
    }
    auto* previous = m_current_transaction;
    setCurrentTransaction(prepared, true);
    if (m_current_review_identity != resolution.snapshot.candidate_review) {
        prepared->deleteLater();
        return;
    }
    publishPreparationProjection(std::move(resolution.snapshot));
    if (previous) previous->deleteLater();
    m_psbt_fee_request.reset();
    m_psbt_fee_pending = false;
    m_psbt_fee_proof = effect.review->proof
        ? std::shared_ptr<const psbtfee::Analysis>{effect.review, effect.review->proof.get()}
        : std::shared_ptr<const psbtfee::Analysis>{};
    m_current_psbt.reset(); m_psbt_document_lease.reset(); m_psbt_requires_recheck = false;
    m_current_transaction_source = CurrentTransactionSource::SendDraft;
    m_current_transaction_sweeps_wallet = effect.review->sweeps;
    m_current_transaction_can_send = effect.review->can_send;
    m_current_transaction_can_broadcast = false;
    m_current_transaction_review_message.clear();
    prepared->setDisplayUnit(m_display_unit);
    if (!observationsAllowed()) return;
    adoptCurrentFlow(effect.review->flow);
    if (!observationsAllowed()) return;
    opening->refreshMetadata();
    if (!observationsAllowed()) return;
    Q_EMIT currentTransactionChanged();
    if (!observationsAllowed()) return;
    Q_EMIT preparationChanged();
}

quint64 WalletQmlModel::approveExternalSignerTransaction()
{
    if (!observationsAllowed() || currentTransactionNeedsRevalidation() || signerApprovalState() == ApprovalState::ApprovalUncertain ||
        signerApprovalState() == ApprovalState::ApprovalUnverified || psbtActionBusy() || signerApprovalBusy() ||
        preparationBusy() || sendBusy() || m_sending_commit->load() || !available() || !m_opening || !m_current_transaction ||
        !m_current_transaction->getWtx() || !m_signer_approval_operation || m_approval_id == std::numeric_limits<quint64>::max()) return 0;
    const QPointer<WalletQmlModel> observer{this};
    const QPointer<WalletQmlModelTransaction> review{m_current_transaction};
    const auto opening = m_opening;
    const auto owner = m_signer_approval_operation;
    const auto reviewed = review->getWtx();
    const auto view = m_psbt_view_epoch;
    const auto review_id = m_current_review_identity;
    const walletsigning::Request request{reviewed, m_current_psbt, m_psbt_fee_proof, review->reviewedFee()};
    const auto ticket = ++m_approval_id;
    const auto owns = [observer, opening, ticket] {
        return observer && observer->observationsAllowed() && observer->available() &&
            observer->m_opening == opening && observer->m_approval_id == ticket;
    };
    const auto owns_view = [owns, observer, review, view, review_id, reviewed] {
        return owns() && review && observer->m_current_transaction == review &&
            observer->m_current_review_identity == review_id && observer->m_psbt_view_epoch == view &&
            review->getWtx() == reviewed;
    };
    const auto gate = m_approval_running;
    quint64 no_approval{0};
    if (!gate->compare_exchange_strong(no_approval, ticket)) return 0;
    auto release = qScopeGuard([gate, ticket] {
        auto original = ticket;
        gate->compare_exchange_strong(original, 0);
    });

    observer->m_approval_attempt_review = review_id;
    observer->m_approval_attempt_view = view;
    observer->m_approval_state = ApprovalState::ApprovalWaiting;
    observer->m_approval_error.clear(); observer->m_approval_signer_not_found = false;
    observer->m_approval_local_pending = true;
    observer->m_approval_command_admitted = false;

    // Reserving retained cleanup can reenter the GUI (including deleting this
    // facade). Keep the opening and owner independently alive and recheck the
    // original review before touching any facade state afterward.
    const auto retained_request = walletopening::RetainValues<walletsigning::Request>(opening,
        [&request] { return std::make_unique<walletsigning::Request>(request); });
    if (!owns()) return ticket;
    if (!owns_view()) {
        observer->m_approval_local_pending = false;
        observer->m_approval_state = ApprovalState::ApprovalInitial;
        observer->m_approval_error.clear(); observer->m_approval_signer_not_found = false;
        Q_EMIT observer->signerApprovalChanged();
        return ticket;
    }
    if (!retained_request) {
        observer->m_approval_local_pending = false;
        observer->m_approval_command_admitted = false;
        observer->m_approval_state = ApprovalState::ApprovalFailed;
        observer->m_approval_error = tr("External signer result is unavailable.");
        Q_EMIT observer->signerApprovalChanged();
        if (!observer || !observer->available()) return ticket;
        Q_EMIT observer->externalSignerApprovalFailed(observer->m_approval_error, false);
        return ticket;
    }

    const auto admitted = owner->request(ticket, review_id, view, retained_request);
    if (admitted) {
        release.dismiss();
    }
    if (!owns()) return ticket;
    if (admitted) observer->m_approval_command_admitted = true;
    if (!admitted) {
        const bool same_view = owns_view();
        observer->m_approval_local_pending = false;
        observer->m_approval_command_admitted = false;
        observer->m_approval_state = same_view ? ApprovalState::ApprovalFailed : ApprovalState::ApprovalInitial;
        observer->m_approval_error = same_view ? tr("External signer approval could not be queued.") : QString{};
        observer->m_approval_signer_not_found = false;
        Q_EMIT observer->signerApprovalChanged();
        return ticket;
    }
    if (owns_view()) Q_EMIT observer->signerApprovalChanged();
    return ticket;
}

void WalletQmlModel::publishApproval(walletsigning::ApprovalSnapshot snapshot)
{
    if (!observationsAllowed() || !available()) return;
    if (m_approval_projection && snapshot.sequence <= m_approval_projection->sequence) return;
    m_approval_projection = std::make_shared<const walletsigning::ApprovalSnapshot>(snapshot);
    if (snapshot.attempt_id != m_approval_id) return;
    const bool same_view = snapshot.attempt_review == m_current_review_identity &&
        snapshot.attempt_view == m_psbt_view_epoch && snapshot.current_review == m_current_review_identity &&
        snapshot.current_view == m_psbt_view_epoch;
    if (!snapshot.in_flight) m_approval_command_admitted = false;
    if (!same_view || !snapshot.publishable) {
        if (snapshot.in_flight) {
            m_approval_local_pending = false;
            m_approval_state = ApprovalState::ApprovalWaiting;
        } else {
            m_approval_local_pending = false;
            m_approval_state = ApprovalState::ApprovalInitial;
            m_approval_error.clear(); m_approval_signer_not_found = false;
        }
        Q_EMIT signerApprovalChanged();
        return;
    }
    m_approval_local_pending = false;
    if (snapshot.in_flight) {
        m_approval_state = ApprovalState::ApprovalWaiting;
        m_approval_error.clear(); m_approval_signer_not_found = false;
        Q_EMIT signerApprovalChanged();
        return;
    }

    const auto value = snapshot.result ? *snapshot.result : walletsigning::Result{.error = snapshot.error};
    m_approval_local_pending = false;
    m_approval_error.clear(); m_approval_signer_not_found = value.error == walletsigning::Error::SignerNotFound;
    switch (value.error) {
    case walletsigning::Error::None: break;
    case walletsigning::Error::FeeUnavailable: m_approval_error = tr("The transaction fee is missing or invalid."); break;
    case walletsigning::Error::InvalidFee: m_approval_error = tr("The transaction fee changed after review."); break;
    case walletsigning::Error::ChangedTransaction: m_approval_error = tr("External signer changed the reviewed transaction."); break;
    case walletsigning::Error::SignerNotFound: m_approval_error = tr("External signer not found. Connect one device and try again."); break;
    case walletsigning::Error::SignerFailed: m_approval_error = tr("External signer failed to sign. Try again."); break;
    case walletsigning::Error::Fill: m_approval_error = value.fill_error ? PsbtQmlModel::PsbtErrorText(*value.fill_error) : tr("The transaction could not be signed."); break;
    case walletsigning::Error::Failed: m_approval_error = tr("External signer approval failed."); break;
    case walletsigning::Error::Uncertain: m_approval_error = tr("The signer outcome could not be confirmed. Reconcile the PSBT before retrying."); break;
    case walletsigning::Error::UnverifiedInputs: m_approval_error = tr("The transaction inputs could not be verified. Refresh the wallet before trying again."); break;
    case walletsigning::Error::InvalidSignature: m_approval_error = tr("The returned signatures could not be verified for the reviewed transaction. Check the signing device and try again."); break;
    case walletsigning::Error::UnverifiedSignatures: m_approval_error = tr("The returned PSBT needs more verification information. The reviewed transaction is unchanged. Export the unverified result to reconcile it."); break;
    }
    // Unknown returned bytes remain owned by the approval result. They cannot
    // replace the authenticated review, even when a failed call yielded bytes.
    if (value.document && !value.quarantined_document) { invalidateImportReview(); m_current_psbt = value.document; m_current_transaction->setAssessedPsbt(value.document); }
    if (value.quarantined_document) {
        m_current_transaction_can_send = false;
        m_current_transaction_can_broadcast = false;
        if (value.error != walletsigning::Error::UnverifiedSignatures)
            m_approval_error += QLatin1Char(' ') + tr("An unverified result is available for export. The reviewed transaction is unchanged.");
    }
    const QPointer<WalletQmlModel> observer{this};
    if (!m_approval_error.isEmpty()) {
        m_approval_state = value.error == walletsigning::Error::Uncertain ? ApprovalState::ApprovalUncertain
            : value.quarantined_document ? ApprovalState::ApprovalUnverified : ApprovalState::ApprovalFailed;
        const auto error = m_approval_error;
        Q_EMIT signerApprovalChanged();
        if (!observer || !available()) return;
        if (value.document || value.quarantined_document) { Q_EMIT currentTransactionChanged(); if (!observer || !available()) return; }
        Q_EMIT externalSignerApprovalFailed(error, value.error == walletsigning::Error::SignerNotFound);
        return;
    }
    m_current_transaction_can_send = !value.partial;
    m_current_transaction_can_broadcast = false;
    m_current_transaction_review_message = value.partial ? tr("Signed on external signer. More signatures are required.") : QString{};
    if (value.signed_transaction) {
        m_current_transaction->setWtx(value.signed_transaction);
        if (!observer || !available()) return;
    }
    m_approval_state = value.partial ? ApprovalState::ApprovalPartiallySigned : ApprovalState::ApprovalSigned;
    Q_EMIT currentTransactionChanged();
    if (!observer || !available()) return;
    Q_EMIT signerApprovalChanged();
    if (!observer || !available()) return;
    if (value.partial) Q_EMIT externalSignerApprovalPartiallySucceeded();
    else Q_EMIT externalSignerApprovalSucceeded();
}

struct WalletQmlModel::SentLabelCapture {
    std::shared_ptr<walletrequests::Store> store;
    std::shared_ptr<const walletopening::Session::Action> action;
    std::shared_ptr<QMap<QString, QString>> labels;
    std::optional<execution::ResourceLease> label_lifetime;
    bool valid{false};
    std::shared_ptr<QObject> bridge;
    QPointer<WalletQmlModel> observer;
    std::shared_ptr<const int> ticket;
};

WalletQmlModel::SentLabelCapture WalletQmlModel::captureSentRecipientLabels(const std::shared_ptr<const walletopening::Session::Action>& action)
{
    if (m_current_transaction_source != CurrentTransactionSource::SendDraft || !m_current_transaction || m_current_transaction->recipientLabels().isEmpty()) return {};
    SentLabelCapture capture;
    capture.store = m_request_store; capture.action = action; capture.bridge = m_notification_bridge;
    capture.observer = this; capture.ticket = std::make_shared<const int>(0);
    capture.labels = std::make_shared<QMap<QString, QString>>();
    if (!m_current_transaction->recipientLabels().isEmpty()) {
        auto held = m_opening->holdValues([labels = capture.labels] { labels->clear(); });
        if (!held) return capture;
        capture.label_lifetime.emplace(std::move(*held.lease));
        *capture.labels = m_current_transaction->recipientLabels();
    }
    capture.valid = true;
    return capture;
}

void WalletQmlModel::publishSentRecipientLabels()
{
    if (!observationsAllowed()) return;
    if (m_admission_closed) return;
    if (m_sent_labels_operation) {
        const auto result = m_sent_labels_operation->result();
        if (result) {
            m_sent_labels_pending = false;
            m_sent_labels_error = result->error == walletrequests::Error::None ? QString{}
                : result->error == walletrequests::Error::Uncertain
                    ? tr("The transaction was committed, but its outgoing note results could not be confirmed.")
                    : tr("The transaction was committed, but some outgoing notes could not be saved.");
            m_sent_labels_operation.reset();
        }
    }
    Q_EMIT recipientLabelsChanged();
}


quint64 WalletQmlModel::requestSendTransaction()
{
    return requestSendTransactionInternal();
}

quint64 WalletQmlModel::requestSignOnlyTransaction()
{
    return requestSendTransactionInternal(std::nullopt, true);
}

quint64 WalletQmlModel::requestSendTransactionWithPassphrase(const QString& passphrase)
{
    return requestSendTransactionInternal(passphrase);
}

quint64 WalletQmlModel::requestSendTransactionInternal(std::optional<QString> passphrase, bool sign_only)
{
    const auto intent = sign_only ? walletsend::Intent::SignOnly : walletsend::Intent::Submit;
    if (passphrase && m_send_operation && m_send_state == SendState::SendNeedsUnlock &&
        m_send_review && m_send_review == m_current_transaction) {
        auto captured = secretinput::Capture(m_opening ? m_opening->scheduler() : nullptr, *passphrase);
        if (!captured) {
            setTransactionStatus(tr("The password input could not be queued. The transaction was not sent."), true);
            Q_EMIT sendOperationChanged();
            return m_send_request_id;
        }
        const auto admission = m_send_operation->providePassphrase(m_send_request_id, m_send_review_id, std::move(captured.input));
        if (!admission) {
            setTransactionStatus(tr("The password continuation could not be queued. The transaction was not sent."), true);
            Q_EMIT sendOperationChanged();
            return m_send_request_id;
        }
        m_send_state = SendState::SendPending;
        clearTransactionStatus();
        Q_EMIT sendOperationChanged();
        return m_send_request_id;
    }
    if (psbtActionBusy() || signerApprovalBusy() || preparationBusy()) return 0;
    if ((m_send_state == SendState::SendCommitted || m_send_state == SendState::SendUncertain || m_send_state == SendState::SendUnverified) && m_send_review && m_send_review == m_current_transaction) return 0;
    if (sendBusy() || m_sending_commit->exchange(true)) return 0;
    if (m_send_request_id == std::numeric_limits<quint64>::max()) {
        m_sending_commit->store(false);
        return 0;
    }
    const auto request_id = ++m_send_request_id;
    const auto gate = m_sending_commit;
    auto rejected = qScopeGuard([gate] { gate->store(false); });
    const QPointer<WalletQmlModel> self{this};
    const QPointer<WalletQmlModelTransaction> approved{m_current_transaction};
    const auto approved_revision = m_preparation_projection.intent_revision;
    clearTransactionStatus();
    if (!self || !observationsAllowed() || approved != m_current_transaction || approved_revision != m_preparation_projection.intent_revision || request_id != m_send_request_id) return request_id;
    if (signerApprovalState() == ApprovalState::ApprovalUncertain || signerApprovalState() == ApprovalState::ApprovalUnverified) {
        m_send_state = SendState::SendUncertain;
        m_send_review = m_current_transaction;
        setTransactionStatus(tr("The signer outcome could not be confirmed. Reconcile the PSBT before retrying."));
        if (self) Q_EMIT sendOperationChanged();
        return request_id;
    }
    if (!available() || !m_current_transaction || !m_current_transaction->getWtx()) {
        m_send_state = available() ? SendState::SendFailed : SendState::SendClosed;
        setTransactionStatus(tr("Review a transaction before sending it."));
        if (self && observationsAllowed() && request_id == m_send_request_id) Q_EMIT sendOperationChanged();
        return request_id;
    }
    if (currentTransactionNeedsRevalidation()) {
        m_send_state = SendState::SendFailed;
        setTransactionStatus(tr("The reviewed transaction no longer matches the current draft. Prepare it again."));
        if (self && observationsAllowed() && request_id == m_send_request_id) Q_EMIT sendOperationChanged();
        return request_id;
    }
    const bool can_start = sign_only ? currentTransactionCanSignOnly() : m_current_transaction_can_send;
    if (!can_start) {
        m_send_state = SendState::SendFailed;
        const QString reason = sign_only && (!m_current_psbt || !m_metadata_known || m_private_keys_disabled)
            ? tr("This wallet cannot sign this PSBT locally.")
            : !m_current_transaction_review_message.isEmpty() ? m_current_transaction_review_message :
            (m_metadata_known && m_private_keys_disabled && !m_external_signer ? tr("This wallet cannot sign transactions.") : tr("This transaction is not ready to send."));
        setTransactionStatus(reason);
        if (self && observationsAllowed() && approved == m_current_transaction && approved_revision == m_preparation_projection.intent_revision && request_id == m_send_request_id) Q_EMIT sendOperationChanged();
        return request_id;
    }
    const auto action = m_opening->startAction();
    if (!action) { m_send_state = SendState::SendFailed; setTransactionStatus(tr("The send could not be queued.")); if (self && observationsAllowed() && request_id == m_send_request_id) Q_EMIT sendOperationChanged(); return request_id; }
    auto notes = intent == walletsend::Intent::Submit ? captureSentRecipientLabels(action) : SentLabelCapture{};
    if (notes.ticket && !notes.valid) { m_send_state = SendState::SendFailed; setTransactionStatus(tr("Outgoing notes could not be queued. Try again.")); if (self && observationsAllowed() && request_id == m_send_request_id) Q_EMIT sendOperationChanged(); return request_id; }
    walletsend::Request request;
    request.intent = intent;
    request.attempt_id = request_id;
    if (m_current_review_identity == std::numeric_limits<quint64>::max()) {
        m_send_state = SendState::SendFailed;
        setTransactionStatus(tr("The transaction review identity is exhausted. Prepare it again before sending."));
        if (self) Q_EMIT sendOperationChanged();
        return request_id;
    }
    request.review_id = m_current_review_identity;
    request.reviewed = m_current_transaction->getWtx(); request.psbt = m_current_psbt;
    request.assessment = m_current_transaction->assessedPsbt();
    request.policy = m_psbt_fee_services.policy;
    request.sweeps_wallet = intent == walletsend::Intent::Submit && currentTransactionSweepsWallet();
    request.assessed_complete = m_current_transaction_can_broadcast;
    request.proof = m_psbt_fee_proof; request.approved_fee = m_current_transaction->reviewedFee();
    if (passphrase) {
        auto captured = secretinput::Capture(m_opening->scheduler(), *passphrase);
        if (!captured) {
            m_send_state = SendState::SendFailed;
            setTransactionStatus(tr("The password input could not be queued. The transaction was not sent."));
            if (self) Q_EMIT sendOperationChanged();
            return request_id;
        }
        request.captured_passphrase = std::move(captured.input);
    }
    if (intent == walletsend::Intent::Submit) {
        request.consent = std::make_shared<walletsend::CommitConsent>();
        m_send_consent = request.consent;
    } else {
        m_send_consent.reset();
    }
    if (notes.labels) request.notes = *notes.labels;
    const QPointer<WalletQmlModel> observer{this};
    const auto ticket = std::make_shared<const int>(0);
    auto notify = [gate, post = m_fee_read_services.post, observer, ticket](walletsend::Notice notice) {
        if (notice == walletsend::Notice::Finished) gate->store(false);
        post([observer, ticket, notice] {
            if (!observer || observer->m_stopping) return;
            if (notice == walletsend::Notice::Finished && observer->m_send_ticket == ticket) observer->publishSendOperation();
            else if ((notice == walletsend::Notice::SweepConsentRequired || notice == walletsend::Notice::CredentialsRequired) && observer->m_send_ticket == ticket) observer->publishSendPrompt();
            else if (notice == walletsend::Notice::Notes && observer->m_sent_labels_ticket == ticket) observer->publishSentRecipientLabels();
        });
    };
    m_send_review = m_current_transaction; m_send_review_id = request.review_id;
    m_send_sign_only = intent == walletsend::Intent::SignOnly;
    m_send_result_txid.clear(); m_send_submission = SendSubmission::SubmissionUnknown;
    m_send_record = SendRecord::SendRecordUnknown; m_send_ticket = ticket;
    m_send_operation = std::make_shared<walletsend::Operation>(m_opening, m_request_store, std::move(notify));
    m_send_state = SendState::SendPending;
    const auto admission = m_send_operation->start(std::move(request), action);
    if (admission) rejected.dismiss();
    else { m_send_state = SendState::SendFailed; setTransactionStatus(tr("The send could not be queued.")); }
    if (self && observationsAllowed() && request_id == m_send_request_id) Q_EMIT sendOperationChanged();
    return request_id;
}

quint64 WalletQmlModel::confirmSendTransaction(quint64 request_id)
{
    if (!request_id || request_id != m_send_request_id || m_send_state != SendState::SendNeedsConfirmation ||
        !m_send_operation || !m_send_review || m_send_review != m_current_transaction ||
        m_send_review_id != m_current_review_identity) return 0;
    const auto admission = m_send_operation->confirmSweep(request_id, m_send_review_id);
    if (!admission) return 0;
    m_send_state = SendState::SendPending;
    clearTransactionStatus();
    Q_EMIT sendOperationChanged();
    return request_id;
}

quint64 WalletQmlModel::cancelSendTransaction(quint64 request_id)
{
    if (!request_id || request_id != m_send_request_id || !m_send_operation) return 0;
    const auto stage = m_send_operation->stage();
    if (stage != walletsend::Stage::AwaitingSweepConsent && stage != walletsend::Stage::AwaitingCredentials) return 0;
    const auto admission = m_send_operation->cancelPrompt(request_id, m_send_review_id);
    if (!admission) return 0;
    m_send_state = SendState::SendPending;
    Q_EMIT sendOperationChanged();
    return request_id;
}

void WalletQmlModel::publishSendPrompt()
{
    if (!observationsAllowed() || m_admission_closed || !m_send_operation) return;
    if (!m_send_review || m_send_review != m_current_transaction || m_send_review_id != m_current_review_identity) {
        cancelSendTransaction(m_send_request_id);
        return;
    }
    switch (m_send_operation->stage()) {
    case walletsend::Stage::AwaitingSweepConsent:
        m_send_state = SendState::SendNeedsConfirmation;
        clearTransactionStatus();
        break;
    case walletsend::Stage::AwaitingCredentials:
        m_send_state = SendState::SendNeedsUnlock;
        setTransactionStatus(m_send_operation->promptError() == walletsend::Error::IncorrectPassphrase
            ? tr("The wallet password is incorrect.") : tr("Enter your wallet password to send this transaction."), true);
        break;
    case walletsend::Stage::Submitting:
    case walletsend::Stage::SigningOnly:
        m_send_state = SendState::SendPending;
        break;
    case walletsend::Stage::Idle:
    case walletsend::Stage::Finished:
        return;
    }
    Q_EMIT sendOperationChanged();
}

void WalletQmlModel::publishSendOperation()
{
    if (!observationsAllowed()) return;
    if (m_admission_closed) return;
    if (!m_send_operation) return;
    const QPointer<WalletQmlModel> self{this};
    const auto result = m_send_operation->result(); if (!result) return;
    if (m_send_state != SendState::SendPending) {
        if (result->notes && result->notes == m_sent_labels_operation) publishSentRecipientLabels();
        return;
    }
    if (m_send_sign_only && result->document && !result->quarantined_document && m_send_review && m_send_review == m_current_transaction) {
        // A sign-only result may contain new valid partial signatures even
        // when the wallet reported a terminal signing error. Preserve that
        // captured document, while allowing broadcast only after a clean,
        // verified complete result.
        invalidateImportReview();
        m_current_psbt = result->document;
        m_current_transaction->setAssessedPsbt(result->document);
        if (result->error == walletsend::Error::None && !result->relock_failed) {
            if (result->signed_transaction) { m_current_transaction->setWtx(result->signed_transaction); if (!self) return; }
            m_current_transaction_can_broadcast = m_node && !result->partial;
            m_send_state = result->partial ? SendState::SendPartiallySigned : SendState::SendSignedOnly;
            setTransactionStatus(result->partial
                ? tr("Some PSBT inputs were signed. The transaction was not sent.")
                : tr("The transaction was signed. It was not sent."));
            Q_EMIT currentTransactionChanged();
            if (!self) return;
            Q_EMIT sendOperationChanged();
            return;
        }
    }
    if (result->committed) {
        switch (result->record) {
        case walletsend::Record::Unknown: m_send_record = SendRecord::SendRecordUnknown; break;
        case walletsend::Record::AbsentAtObservation: m_send_record = SendRecord::SendRecordAbsentAtObservation; break;
        case walletsend::Record::Recorded: m_send_record = SendRecord::SendRecordRecorded; break;
        }
    }
    if (result->committed && result->notes_requested) {
        m_sent_labels_ticket = m_send_ticket;
        m_sent_labels_operation = result->notes;
        m_sent_labels_txid = QString::fromStdString(result->committed->GetHash().GetHex());
        m_sent_labels_pending = bool(result->notes);
        m_sent_labels_error = result->notes ? QString{}
            : m_send_record != SendRecord::SendRecordRecorded
                ? tr("The wallet transaction record could not be confirmed, so its outgoing notes were not saved.")
            : result->notes_rejection == execution::Rejection::Transport
                ? tr("The transaction was committed, but its outgoing note results could not be confirmed.")
                : tr("The transaction was committed, but its outgoing notes could not be queued.");
        publishSentRecipientLabels();
        if (!self) return;
    }
    m_opening->refreshMetadata();
    // A consumed effect belongs to its original review. A successor gets no
    // old success, password prompt, mutation or transaction error.
    if (!available() || !m_send_review || m_send_review != m_current_transaction) {
        m_send_state = SendState::SendClosed; Q_EMIT sendOperationChanged(); return;
    }
    if (result->committed) {
        switch (result->submission) {
        case walletsend::Submission::Unknown: m_send_submission = SendSubmission::SubmissionUnknown; break;
        case walletsend::Submission::Mempool: m_send_submission = SendSubmission::SubmissionMempool; break;
        case walletsend::Submission::Confirmed: m_send_submission = SendSubmission::SubmissionConfirmed; break;
        case walletsend::Submission::NotInMempool: m_send_submission = SendSubmission::SubmissionNotInMempool; break;
        case walletsend::Submission::Conflicted: m_send_submission = SendSubmission::SubmissionConflicted; break;
        case walletsend::Submission::Abandoned: m_send_submission = SendSubmission::SubmissionAbandoned; break;
        }
        m_send_state = SendState::SendCommitted;
        m_send_result_txid = QString::fromStdString(result->committed->GetHash().GetHex());
        m_current_transaction_can_send = false; m_current_transaction_can_broadcast = false;
        m_current_transaction->setWtx(result->committed);
        if (!self || !m_send_review || m_send_review != m_current_transaction) return;
        m_current_psbt.reset(); m_psbt_document_lease.reset(); m_psbt_requires_recheck = false; m_current_transaction_source = CurrentTransactionSource::None;
        m_current_transaction_can_send = false; m_current_transaction_can_broadcast = false;
        m_current_transaction_review_message.clear(); clearTransactionStatus();
        if (!self || !m_send_review || m_send_review != m_current_transaction) return;
        clearSelectedCoins();
        if (!self) return;
    } else {
        using Error = walletsend::Error;
        m_send_state = result->error == Error::NeedsUnlock || result->error == Error::IncorrectPassphrase ? SendState::SendNeedsUnlock
            : result->error == Error::Uncertain ? SendState::SendUncertain : SendState::SendFailed;
        QString message;
        if (m_send_sign_only && result->error == Error::Uncertain) {
            message = result->relock_failed
                ? tr("The signed PSBT was not sent, but the wallet could not be relocked. Check wallet status before continuing.")
                : tr("The signing outcome could not be confirmed. The transaction was not sent; check the PSBT before retrying.");
        } else if (m_send_sign_only && result->error == Error::Signer) {
            message = tr("Wallet signing did not complete. Any verified signatures were retained. The transaction was not sent.");
        } else switch (result->error) {
        case Error::InvalidStructure: message = tr("This PSBT has duplicate inputs and cannot be signed or sent."); break;
        case Error::NeedsUnlock: message = m_send_sign_only ? tr("Enter your wallet password to sign this PSBT. The transaction will not be sent.") : tr("Enter your wallet password to send this transaction."); break;
        case Error::IncorrectPassphrase: message = tr("The wallet password is incorrect."); break;
        case Error::CannotSign: message = tr("This wallet cannot sign transactions."); break;
        case Error::InvalidFee: message = tr("The transaction fee is missing or changed after review."); break;
        case Error::ChangedTransaction: message = tr("External signer changed the reviewed transaction."); break;
        case Error::Incomplete: message = tr("Only PSBTs this wallet can fully sign are supported right now."); break;
        case Error::NeedsSignerApproval: message = tr("Approve this transaction on the signing device before sending it."); break;
        case Error::InvalidSignature: message = tr("The returned signatures could not be verified for the reviewed transaction. Check the signing device and try again."); break;
        case Error::UnverifiedSignatures: message = tr("The returned PSBT needs more verification information. The reviewed transaction is unchanged. Export the unverified result to reconcile it."); break;
        case Error::Signer: message = result->signer_error ? PsbtQmlModel::PsbtErrorText(*result->signer_error) : tr("Signing failed."); break;
        case Error::Uncertain: message = tr("The transaction result could not be confirmed. Check wallet activity before retrying."); break;
        case Error::Cancelled: message = tr("The transaction confirmation was cancelled."); break;
        case Error::Rejected: message = tr("The send could not be queued."); break;
        case Error::NodePolicy: message = result->policy_error; break;
        case Error::None: message = tr("The transaction was not committed."); break;
        }
        if (result->relock_failed) { m_send_state = SendState::SendUncertain; message = tr("The wallet could not be relocked. Check wallet status before retrying."); }
        if (result->quarantined_document) {
            if (m_send_state != SendState::SendUncertain) m_send_state = SendState::SendUnverified;
            m_current_transaction_can_send = false;
            m_current_transaction_can_broadcast = false;
        }
        if (m_send_state == SendState::SendUncertain) m_current_transaction_can_send = false;
        setTransactionStatus(message, m_send_state == SendState::SendNeedsUnlock);
        if (!self) return;
        if (m_send_sign_only && (result->document || result->quarantined_document)) {
            Q_EMIT currentTransactionChanged();
            if (!self) return;
            Q_EMIT signerApprovalChanged();
            if (!self) return;
        }
    }
    if (m_send_state == SendState::SendCommitted || m_send_state == SendState::SendUncertain) {
        Q_EMIT currentTransactionChanged();
        if (!self) return;
    }
    Q_EMIT sendOperationChanged();
}

void WalletQmlModel::invalidateImportReview()
{
    m_import_review_current->store(false);
    m_import_review_current = std::make_shared<std::atomic_bool>(true);
}

std::shared_ptr<const paymentimport::TargetPort> WalletQmlModel::importTarget(SendRecipient* recipient)
{
    if (!observationsAllowed() || !m_opening || !m_send_recipients) return {};
    const auto payment = m_send_recipients->importTarget(recipient);
    if (!payment) return {};
    const QPointer<WalletQmlModel> observer{this};
    const QPointer<WalletQmlModelTransaction> original_review{m_current_transaction};
    const auto opening = m_opening;
    const auto current = m_import_review_current;
    const auto view = m_psbt_view_epoch, review_identity = m_current_review_identity;
    const auto post = presentation::ReliablePost(m_fee_read_services.post);
    const int unit = m_display_unit;
    psbtwork::Context context{m_psbt_fee_services.lookup, psbtwork::NodeBroadcast(m_node)};
    context.policy = m_psbt_fee_services.policy;
    paymentimport::ReviewServices services;
    services.scheduler = opening->scheduler(); services.view = view;
    services.current = [opening, current] { return current->load() && opening->available(); };
    services.prepare = [opening, context, unit](auto source, auto done, auto failed) {
        // Complete the retained result on Native before the Domain acknowledgment
        // can be lost. Only the existing public Wallet review API is used.
        return opening->read([source, context, unit, done](interfaces::Wallet& wallet) {
            done(psbtwork::PrepareReview(wallet, source->psbt->document, context, unit));
            return true;
        }, [](bool) {}, std::move(failed), execution::WorkClass::Observation);
    };
    services.present = [post, observer, opening, current, view, review_identity, original_review](auto key, auto child, auto imported, auto presentation_allowed) {
        post([observer, opening, current, view, review_identity, original_review, key, child, imported, presentation_allowed] {
            if (!observer || !observer->observationsAllowed() || observer->m_opening != opening || !current->load() ||
                observer->m_psbt_view_epoch != view || observer->m_current_review_identity != review_identity ||
                observer->m_current_transaction != original_review || !presentation_allowed->load() || !imported->review) return;
            // These aliases keep every Core value's final release on its Native
            // lease, including after the original import owner acknowledges.
            const auto review = std::shared_ptr<const walletreview::Snapshot>{imported, imported->review.get()};
            WalletQmlModelTransaction* transaction{nullptr};
            try { transaction = new WalletQmlModelTransaction(review, opening, observer->m_fee_read_services.post, observer); }
            catch (const std::exception&) {
                if (observer && current->load()) {
                    observer->m_psbt_action_state = PsbtActionState::PsbtFailed;
                    observer->m_psbt_action_error = tr("Transaction review is unavailable.");
                    Q_EMIT observer->psbtActionChanged();
                }
                return;
            }
            if (!observer || !current->load() || !presentation_allowed->load() || !observer->observationsAllowed()) { transaction->deleteLater(); return; }
            auto* previous = observer->m_current_transaction;
            observer->setCurrentTransaction(transaction);
            if (!observer || observer->m_current_transaction != transaction) { if (observer) transaction->deleteLater(); return; }
            if (previous) previous->deleteLater();
            observer->m_current_psbt = std::shared_ptr<const PartiallySignedTransaction>{imported, imported->document.get()};
            observer->m_psbt_document_lease.reset(); observer->m_psbt_requires_recheck = false;
            observer->m_psbt_fee_proof = review->proof ? std::shared_ptr<const psbtfee::Analysis>{review, review->proof.get()} : nullptr;
            observer->m_psbt_fee_request.reset(); observer->m_psbt_fee_pending = false;
            observer->m_psbt_key_can_send = imported->key_can_send;
            observer->m_psbt_can_commit_complete = imported->can_commit_complete;
            observer->m_current_transaction_source = CurrentTransactionSource::ImportedPsbt;
            observer->m_current_transaction_can_send = review->can_send;
            observer->m_current_transaction_can_broadcast = imported->key_can_broadcast && review->proof && review->proof->amount.has_value();
            observer->m_current_transaction_review_message = imported->message;
            observer->m_psbt_import_result = review->node_policy_blocked ? PsbtImportResult::ReviewPending
                : review->can_send ? PsbtImportResult::WalletCanSign : PsbtImportResult::WalletCannotSign;
            observer->m_psbt_action_state = PsbtActionState::PsbtImported;
            observer->m_imported_review_owner = key.owner; observer->m_imported_review_child = child.id;
            observer->adoptCurrentFlow(review->flow);
            if (!observer || observer->m_current_transaction != transaction) return;
            Q_EMIT observer->currentTransactionChanged();
            if (observer && observer->m_current_transaction == transaction) Q_EMIT observer->psbtActionChanged();
        });
    };
    return paymentimport::WithReviewFactory(payment, std::move(services));
}

quint64 WalletQmlModel::requestImportPsbtFromFile(const QString& path) { return startPsbtAction(PsbtActionKind::Import, path); }
quint64 WalletQmlModel::requestSaveCurrentTransactionAsPsbt(const QString& path) { return startPsbtAction(PsbtActionKind::Save, path); }
quint64 WalletQmlModel::requestSaveUnverifiedSignerResult(const QString& token, const QString& path)
{
    if (token.isEmpty() || token != signerUnverifiedResultToken()) return 0;
    const auto document = token.startsWith(QStringLiteral("local:"))
        ? m_send_operation->result()->quarantined_document : m_approval_projection->result->quarantined_document;
    return startPsbtAction(PsbtActionKind::Save, path, document);
}
quint64 WalletQmlModel::requestBroadcastCurrentTransaction() { return startPsbtAction(PsbtActionKind::Broadcast); }
quint64 WalletQmlModel::requestRecheckCurrentTransaction()
{
    return currentTransactionIsImportedPsbt() ? startPsbtAction(PsbtActionKind::Recheck) : 0;
}
QString WalletQmlModel::psbtSavedPath() const
{
    if (!m_psbt_receipt) return {};
    std::lock_guard lock{m_psbt_receipt->mutex}; return m_psbt_receipt->saved ? m_psbt_receipt->path : QString{};
}
QString WalletQmlModel::psbtSubmittedTxid() const
{
    if (!m_psbt_receipt) return {};
    std::lock_guard lock{m_psbt_receipt->mutex}; return m_psbt_receipt->submitted ? m_psbt_receipt->txid : QString{};
}
std::shared_ptr<FileExportRequest> WalletQmlModel::preparePsbtFileExport(
    quint64 action_id, const QString& path, const std::shared_ptr<PsbtReceipt>& receipt)
{
    if (!observationsAllowed() || action_id != m_psbt_action_id || m_psbt_action_kind != PsbtActionKind::Save) return {};
    const QPointer<WalletQmlModel> observer{this};
    const auto opening = m_opening;
    auto services = m_backup_services;
    services.scheduler = opening ? opening->scheduler() : m_psbt_fee_services.scheduler;
    services.post = presentation::ReliablePost(m_fee_read_services.post);
    services.available = [has_opening = bool(opening), weak = std::weak_ptr<walletopening::Session>{opening}] {
        if (!has_opening) return true;
        const auto session = weak.lock(); return session && session->observationOpen();
    };
    const auto action_slot = std::make_shared<PsbtExportActionSlot>();
    if (opening) {
        services.submit = [weak = std::weak_ptr<walletopening::Session>{opening}, action_slot](std::function<void()> work) {
            const auto session = weak.lock();
            const auto action = action_slot->get();
            if (!session || !action) return execution::Admission{{}, execution::Rejection::Closed};
            return session->submitActionWork(action, std::move(work));
        };
        services.reserve_prompt_retirement = [weak = std::weak_ptr<walletopening::Session>{opening}, action_slot](std::function<void()> work) {
            const auto session = weak.lock();
            const auto action = action_slot->get();
            if (!session || !action) return execution::Admission{{}, execution::Rejection::Closed};
            return session->reserveActionCleanup(action, std::move(work));
        };
        services.activate_prompt_retirement = [weak = std::weak_ptr<walletopening::Session>{opening}, action_slot](execution::TaskId id) {
            const auto session = weak.lock();
            const auto action = action_slot->get();
            return session && action && session->activateActionCleanup(action, id);
        };
    } else if (services.scheduler) {
        // A detached PSBT export still needs a private serialization key for
        // its close receipt; unkeyed Core and Cleanup turns could race while
        // inspecting an existing destination.
        const auto scheduler = services.scheduler;
        try {
            const auto action_key = scheduler->allocateKey();
            services.submit = [scheduler, action_key](std::function<void()> work) {
                return execution::Submit(*scheduler, std::move(work), [] {}, {},
                    {execution::Lane::Core, action_key, execution::WorkClass::Action});
            };
            services.reserve_prompt_retirement = [scheduler, action_key](std::function<void()> work) {
                return execution::Submit(*scheduler, std::move(work), [] {}, {},
                    {execution::Lane::Cleanup, action_key, execution::WorkClass::Action}, true);
            };
            services.activate_prompt_retirement = [scheduler](execution::TaskId id) {
                return scheduler->activateCleanup(id);
            };
        } catch (...) {
            services.submit = [](std::function<void()>) {
                return execution::Admission{{}, execution::Rejection::Transport};
            };
            services.reserve_prompt_retirement = [](std::function<void()>) {
                return execution::Admission{{}, execution::Rejection::Transport};
            };
            services.activate_prompt_retirement = [](execution::TaskId) { return false; };
        }
    }
    const auto previous_outcome = std::move(services.outcome);
    services.outcome = [receipt, action_slot, previous_outcome](backup::Snapshot snapshot) {
        {
            std::lock_guard lock{receipt->mutex};
            receipt->path = QString::fromUtf8(snapshot.target.path.data(), static_cast<qsizetype>(snapshot.target.path.size()));
            receipt->saved = snapshot.phase == backup::Phase::Succeeded;
        }
        action_slot->clear();
        if (previous_outcome) previous_outcome(std::move(snapshot));
    };
    const auto previous_abandon = std::move(services.abandon);
    services.abandon = [action_slot, previous_abandon] {
        action_slot->clear();
        if (previous_abandon) previous_abandon();
    };
    if (!services.coordinator) {
        services.coordinator = std::make_shared<backup::InstallCoordinator>();
        m_backup_services.coordinator = services.coordinator;
    }
    // Construct without a QObject parent. Cleanup reservation admission can
    // run a reentrant close callback, so the model must not be deleted from
    // inside its own constructor.
    auto* export_operation = new FileExportOperationModel(path, std::move(services));
    if (!observer) { delete export_operation; return {}; }
    if (opening) {
        // Reserve the original wallet action before admitting PSBT creation.
        // The retained action holder releases its wallet borrower on Cleanup.
        action_slot->retained = walletopening::RetainValues<std::shared_ptr<const walletopening::Session::Action>>(
            opening, [] { return std::make_unique<std::shared_ptr<const walletopening::Session::Action>>(); });
        if (!action_slot->retained || !observer || !opening->available()) { delete export_operation; return {}; }
        *action_slot->retained = opening->startAction();
        if (!*action_slot->retained) { action_slot->clear(); delete export_operation; return {}; }
    }
    if (m_file_export_operation) {
        m_file_export_operation->retire();
        m_file_export_operation->deleteLater();
    }
    export_operation->setParent(this);
    m_file_export_operation = export_operation;
    m_psbt_file_export_action_id = action_id;
    const auto request = export_operation->requestForAcceptedContinuation();
    connect(export_operation, &FileExportOperationModel::changed, this,
        [observer = QPointer<WalletQmlModel>{this}, export_operation, receipt, action_id] {
            if (!observer || observer->m_file_export_operation != export_operation ||
                observer->m_psbt_action_id != action_id || observer->m_psbt_action_kind != PsbtActionKind::Save ||
                !observer->observationsAllowed() || observer->m_admission_closed) return;
            if (export_operation->pending()) {
                Q_EMIT observer->psbtActionChanged();
                return;
            }
            observer->m_psbt_action_state = export_operation->saved()
                ? PsbtActionState::PsbtSaved : PsbtActionState::PsbtFailed;
            observer->m_psbt_action_error = export_operation->errorText();
            {
                std::lock_guard lock{receipt->mutex};
                receipt->path = export_operation->finalPath();
                receipt->saved = export_operation->saved();
            }
            Q_EMIT observer->psbtActionChanged();
        });
    return request;
}

void WalletQmlModel::startPsbtFileExport(quint64 action_id, const QString& path,
    std::shared_ptr<const PartiallySignedTransaction> document, const std::shared_ptr<PsbtReceipt>& receipt)
{
    if (!observationsAllowed() || action_id != m_psbt_action_id || m_psbt_action_kind != PsbtActionKind::Save || !document) return;
    const QPointer<WalletQmlModel> observer{this};
    const auto opening = m_opening;
    const auto review = m_psbt_action_review;
    const auto view = m_psbt_action_view_epoch;
    const bool had_review = m_psbt_action_had_review;
    const bool had_document = m_psbt_action_had_document;
    const auto request = preparePsbtFileExport(action_id, path, receipt);
    if (!observer) {
        if (request) request->abandon();
        return;
    }
    if (!request) {
        if (observationsAllowed() && action_id == m_psbt_action_id && m_psbt_action_state == PsbtActionState::PsbtPending) {
            m_psbt_action_state = PsbtActionState::PsbtFailed;
            m_psbt_action_error = tr("The PSBT export could not be queued.");
        }
        if (!observer) return;
        Q_EMIT fileExportOperationChanged();
        if (observer && action_id == m_psbt_action_id) Q_EMIT psbtActionChanged();
        return;
    }
    const bool same_document = had_document && m_current_psbt &&
        !m_psbt_action_document.owner_before(m_current_psbt) && !m_current_psbt.owner_before(m_psbt_action_document);
    const bool same_review = had_review ? review && m_current_transaction == review : !m_current_transaction;
    const bool still_pending = observer && observationsAllowed() && !m_admission_closed &&
        m_opening == opening && action_id == m_psbt_action_id &&
        m_psbt_action_kind == PsbtActionKind::Save && m_psbt_action_state == PsbtActionState::PsbtPending &&
        m_psbt_view_epoch == view && m_psbt_action_view_epoch == view && same_review &&
        (had_document ? same_document : !m_current_psbt);
    if (!still_pending) {
        const bool same_action = observer && action_id == m_psbt_action_id &&
            m_psbt_action_kind == PsbtActionKind::Save && m_psbt_action_state == PsbtActionState::PsbtPending;
        QPointer<FileExportOperationModel> stale_export;
        if (same_action) {
            m_psbt_action_state = PsbtActionState::PsbtStale;
            m_psbt_action_error.clear();
            if (m_psbt_file_export_action_id == action_id) {
                stale_export = m_file_export_operation;
                m_file_export_operation = nullptr;
                m_psbt_file_export_action_id = 0;
            }
        }
        request->abandon();
        if (!observer) return;
        if (stale_export) stale_export->deleteLater();
        if (same_action) {
            Q_EMIT fileExportOperationChanged();
            if (observer && action_id == m_psbt_action_id) Q_EMIT psbtActionChanged();
        }
        return;
    }
    if (!request->start(std::move(document))) {
        if (observer && action_id == m_psbt_action_id && m_psbt_action_state == PsbtActionState::PsbtPending) {
            m_psbt_action_state = PsbtActionState::PsbtFailed;
            m_psbt_action_error = tr("The PSBT export could not be queued.");
            request->abandon();
        }
    }
    if (!observer) return;
    Q_EMIT fileExportOperationChanged();
    if (observer && action_id == m_psbt_action_id) Q_EMIT psbtActionChanged();
}
quint64 WalletQmlModel::startPsbtAction(PsbtActionKind kind, const QString& requested_path,
                                      std::shared_ptr<const PartiallySignedTransaction> export_document)
{
    const bool replacing_import = kind == PsbtActionKind::Import && m_psbt_action_kind == PsbtActionKind::Import;
    if ((psbtActionBusy() && !replacing_import) || preparationBusy() || sendBusy() || securityBusy() || m_sending_commit->load() || signerApprovalBusy() || m_admission_closed || (m_wallet && !available())) return 0;
    const QPointer<WalletQmlModel> observer{this};
    const QPointer<WalletQmlModelTransaction> review{m_current_transaction};
    const bool had_review = bool(review);
    const auto opening = m_opening;
    const auto scheduler = opening ? opening->scheduler() : m_psbt_fee_services.scheduler;
    const auto post = presentation::ReliablePost(m_fee_read_services.post);
    const auto path = QString{requested_path};
    const auto review_document = m_current_psbt;
    const auto document = export_document ? export_document : review_document;
    const auto proof = m_psbt_fee_proof;
    const auto assessment = review ? review->assessedPsbt() : nullptr;
    const auto reviewed = review ? review->getWtx() : CTransactionRef{};
    const auto fee = review ? review->reviewedFee() : std::optional<CAmount>{};
    const bool fee_known = review && review->feeKnown();
    const bool fill = !document || (m_current_transaction_source != CurrentTransactionSource::ImportedPsbt && m_current_transaction_can_send);
    const int unit = m_display_unit;
    psbtwork::Context context{m_psbt_fee_services.lookup, psbtwork::NodeBroadcast(m_node)};
    context.policy = m_psbt_fee_services.policy;
    const bool missing_review = kind != PsbtActionKind::Import && !reviewed;
    const bool unavailable_broadcast = kind == PsbtActionKind::Broadcast && (!m_node || !document || !m_current_transaction_can_broadcast);
    // Keep previous owned packets alive locally until after all new handoffs;
    // releasing an old reservation can itself call an injected resource port.
    auto previous_action = std::move(m_psbt_action);
    auto previous_lease = std::move(m_psbt_action_lease);
    m_psbt_action_lease.reset(); // Destroy only the now-empty moved-from handle.
    (void)previous_action; (void)previous_lease;
    const auto id = ++m_psbt_action_id;
    m_psbt_action_kind = kind; m_psbt_action_state = PsbtActionState::PsbtPending;
    m_psbt_file_export_action_id = 0;
    m_psbt_action_error.clear(); m_psbt_import_result = PsbtImportResult::PsbtUnsupported;
    m_psbt_action_path = path;
    if (kind == PsbtActionKind::Import || kind == PsbtActionKind::Recheck) { invalidateImportReview(); ++m_psbt_view_epoch; }
    const auto view = m_psbt_view_epoch;
    m_psbt_action_view_epoch = view;
    m_psbt_action_review = review; m_psbt_action_had_review = had_review;
    m_psbt_action_document = review_document; m_psbt_action_had_document = bool(review_document);
    const auto owns_action = [observer, opening, id, kind] {
        return observer && observer->observationsAllowed() && observer->m_opening == opening &&
            observer->m_psbt_action_id == id && observer->m_psbt_action_kind == kind;
    };
    const auto owns_pending = [owns_action, observer, view, document = review_document, review, had_review] {
        if (!owns_action() || observer->m_psbt_action_state != PsbtActionState::PsbtPending || observer->m_psbt_view_epoch != view) return false;
        if (document) return observer->m_current_transaction && observer->m_current_psbt &&
            !document.owner_before(observer->m_current_psbt) && !observer->m_current_psbt.owner_before(document);
        return had_review ? review && observer->m_current_transaction == review : !observer->m_current_transaction;
    };
    const auto rejected = [owns_action, owns_pending, observer](const QString& error) {
        if (!owns_action() || observer->m_psbt_action_state != PsbtActionState::PsbtPending) return;
        const bool same = owns_pending();
        observer->m_psbt_action_state = same ? PsbtActionState::PsbtFailed : PsbtActionState::PsbtStale;
        observer->m_psbt_action_error = same ? error : QString{};
        Q_EMIT observer->psbtActionChanged();
    };
    auto receipt = std::make_shared<PsbtReceipt>(); m_psbt_receipt = receipt;
    if (missing_review || unavailable_broadcast) {
        const auto error = missing_review ? tr("No transaction is available for this action.")
            : !fee_known ? tr("The transaction fee is missing or invalid.") : tr("This transaction is not available for broadcast.");
        m_psbt_action_state = PsbtActionState::PsbtFailed; m_psbt_action_error = error;
        setTransactionStatus(error);
        if (owns_action() && m_psbt_view_epoch == view && m_psbt_action_state == PsbtActionState::PsbtFailed &&
            m_current_transaction == review) Q_EMIT observer->psbtActionChanged();
        return id;
    }
    std::shared_ptr<FileExportRequest> file_export_request;
    if (kind == PsbtActionKind::Save && !document) {
        file_export_request = preparePsbtFileExport(id, path, m_psbt_receipt);
        if (!file_export_request) {
            rejected(tr("The PSBT export could not be reserved."));
            return id;
        }
    }
    auto abandon_unaccepted_export = qScopeGuard([&] {
        if (file_export_request) file_export_request->abandon();
    });
    if (kind == PsbtActionKind::Save && document) {
        if (!opening || !opening->available() || !reviewed || !owns_pending()) {
            rejected(tr("The captured PSBT review is no longer available."));
            return id;
        }
        startPsbtFileExport(id, path, document, m_psbt_receipt);
        return id;
    }
    auto weak = std::make_shared<std::weak_ptr<PsbtAction>>();
    auto operation = std::make_shared<PsbtAction>(scheduler, [weak, receipt, post, observer, id, kind, file_export_request] {
        if (const auto owner = weak->lock()) {
            if (const auto result = owner->result(); result && result->value) {
                if (kind == PsbtActionKind::Save && file_export_request) {
                    if (!result->value->error.isEmpty() || !result->value->export_document) file_export_request->abandon();
                    else file_export_request->start(result->value->export_document);
                }
                std::lock_guard lock{receipt->mutex};
                receipt->path = result->value->effect.path; receipt->txid = result->value->effect.txid;
                receipt->saved = result->value->effect.saved; receipt->submitted = result->value->effect.submitted;
            } else if (kind == PsbtActionKind::Save && file_export_request) {
                file_export_request->abandon();
            }
        } else if (kind == PsbtActionKind::Save && file_export_request) {
            file_export_request->abandon();
        }
        post([observer, id] { if (observer && observer->m_psbt_action_id == id) observer->publishPsbtAction(); });
    });
    if (!owns_pending()) { rejected({}); return id; }
    *weak = operation;
    auto owner = std::make_shared<std::shared_ptr<PsbtAction>>(operation);
    auto hold = [&]() -> execution::BorrowAdmission {
        if (opening) return opening->holdValues([owner] { owner->reset(); });
        if (!scheduler) return {{}, execution::Rejection::Closed};
        const auto key = scheduler->allocateKey();
        const auto cleanup = execution::Submit(*scheduler, [owner] { owner->reset(); }, [] {}, {}, {execution::Lane::Cleanup, key}, true);
        if (!cleanup) return {{}, cleanup.rejection};
        auto held = scheduler->borrow(key); scheduler->activateCleanup(cleanup.id); return held;
    }();
    if (!owns_pending()) { rejected({}); return id; }
    if (!scheduler || !hold) { rejected(tr("PSBT result is unavailable.")); return id; }
    std::shared_ptr<CTransactionRef> transaction_owner;
    if (kind == PsbtActionKind::Broadcast) {
        const auto factory = [] { return std::make_unique<CTransactionRef>(); };
        transaction_owner = opening ? walletopening::RetainValues<CTransactionRef>(opening, factory)
            : execution::RetainValues<CTransactionRef>(scheduler, factory);
        if (!owns_pending()) { rejected({}); return id; }
        if (!transaction_owner) { rejected(tr("PSBT transaction outcome is unavailable.")); return id; }
    }
    const auto output_factory = [] { return std::make_unique<PsbtActionValue>(); };
    auto output = opening ? walletopening::RetainValues<PsbtActionValue>(opening, output_factory)
        : execution::RetainValues<PsbtActionValue>(scheduler, output_factory);
    if (!owns_pending()) { rejected({}); return id; }
    if (!output) { rejected(tr("PSBT outcome is unavailable.")); return id; }
    m_psbt_action = operation; m_psbt_action_lease.emplace(std::move(*hold.lease));
    auto effect = [kind, path, document, assessment, proof, reviewed, fee, fill, unit, context, transaction_owner](interfaces::Wallet* wallet) {
        PsbtActionValue value;
        if (kind == PsbtActionKind::Recheck) {
            if (wallet) value.imported = psbtwork::PrepareReview(*wallet, document, context, unit);
            else value.error = QStringLiteral("No wallet is loaded.");
        } else if (kind == PsbtActionKind::Import) {
            CMutableTransaction empty; PartiallySignedTransaction imported{empty, 2};
            value.error = psbtwork::LoadFile(path, imported);
            if (value.error.isEmpty()) {
                if (wallet) value.imported = psbtwork::PrepareReview(*wallet, std::make_shared<const PartiallySignedTransaction>(std::move(imported)), context, unit);
                else value.error = QStringLiteral("No wallet is loaded.");
            }
        } else if (kind == PsbtActionKind::Save) {
            if (!wallet) { value.error = QStringLiteral("No wallet is loaded."); return value; }
            const auto exported = psbtwork::ExportReview(*wallet, document, reviewed, fill, proof, fee);
            value.error = exported.error;
            if (value.error.isEmpty()) value.export_document = exported.document;
        } else {
            psbtwork::Request request; request.kind = psbtwork::Kind::Broadcast;
            request.document = document; request.assessment = assessment; request.proof = proof; request.approved_fee = fee;
            value.effect = psbtwork::Execute(wallet, std::move(request), context);
            if (value.effect.transaction) {
                *transaction_owner = value.effect.transaction;
                value.effect.transaction = CTransactionRef{transaction_owner, transaction_owner->get()};
            }
        }
        return value;
    };
    auto work = [effect, output](interfaces::Wallet* wallet) {
        *output = effect(wallet);
        auto value = *output;
        value.lifetime = output;
        // The production review constructor consumes an already-retained
        // snapshot. Keep its last owner on the original Native output lease,
        // rather than exposing PrepareReview's ordinary shared_ptr deleter.
        if (value.imported.review) value.imported.review = std::shared_ptr<const walletreview::Snapshot>{output, value.imported.review.get()};
        if (value.imported.document) value.imported.document = std::shared_ptr<const PartiallySignedTransaction>{output, value.imported.document.get()};
        if (value.export_document) value.export_document = std::shared_ptr<const PartiallySignedTransaction>{output, value.export_document.get()};
        return value;
    };
    execution::Admission admission;
    if (opening) {
        const bool observation = kind == PsbtActionKind::Import || kind == PsbtActionKind::Recheck;
        const auto action = observation ? nullptr : opening->startAction();
        if (!owns_pending()) { rejected({}); return id; }
        if (observation) admission = operation->startWith([opening, work](auto done, auto failed) {
            return opening->read([work](interfaces::Wallet& wallet) { return work(&wallet); }, std::move(done), std::move(failed), execution::WorkClass::Observation);
        });
        else if (action) admission = operation->startWith([opening, action, work](auto done, auto failed) {
            return opening->perform(action, [work](interfaces::Wallet& wallet) { return work(&wallet); }, std::move(done), std::move(failed));
        });
    } else admission = operation->start([work] { return work(nullptr); }, {execution::Lane::Core});
    if (admission) abandon_unaccepted_export.dismiss();
    // Effect admission can close/delete/replace the observer. The original
    // command owns its actual outcome independently; never adopt a successor.
    if (!owns_action() || observer->m_psbt_action != operation || observer->m_psbt_action_state != PsbtActionState::PsbtPending) return id;
    if (!admission) rejected(tr("The PSBT action could not be queued."));
    else if (owns_pending()) {
        if (file_export_request) Q_EMIT observer->fileExportOperationChanged();
        if (owns_pending()) Q_EMIT observer->psbtActionChanged();
    }
    return id;
}

void WalletQmlModel::publishPsbtAction()
{
    if (!observationsAllowed()) return;
    if (!m_psbt_action || !psbtActionBusy()) return;
    const auto result = m_psbt_action->result(); if (!result) return;
    if (m_admission_closed) return;
    const QPointer<WalletQmlModel> observer{this};
    const auto original_action = m_psbt_action;
    const auto original_id = m_psbt_action_id;
    const auto original_kind = m_psbt_action_kind;
    const auto original_view = m_psbt_action_view_epoch;
    const auto original_preparation = m_preparation_projection.id;
    const auto original_send = m_send_request_id;
    const auto original_preparation_phase = m_preparation_projection.phase;
    bool preparation_projection_changed{false};
    const auto owns_action = [&] {
        return observer && observationsAllowed() && m_psbt_action == original_action && m_psbt_action_id == original_id;
    };
    QPointer<WalletQmlModelTransaction> projected_review{m_current_transaction};
    bool had_projected_review{bool(projected_review)};
    bool document_view{original_kind != PsbtActionKind::Import && m_psbt_action_had_document};
    bool original_review_matches{true};
    const auto same_document = [&] {
        return m_current_psbt && !m_psbt_action_document.owner_before(m_current_psbt) &&
            !m_current_psbt.owner_before(m_psbt_action_document);
    };
    const auto owns_view = [&] {
        return owns_action() && original_review_matches && m_psbt_view_epoch == original_view &&
            (document_view ? m_current_transaction && same_document()
                : had_projected_review ? projected_review && m_current_transaction == projected_review.data() : !m_current_transaction);
    };
    const bool same_review = m_psbt_action_had_document
        ? m_current_transaction && same_document()
        : m_psbt_action_had_review ? m_psbt_action_review && m_psbt_action_review == m_current_transaction : !m_current_transaction;
    original_review_matches = original_kind == PsbtActionKind::Import || same_review;
    // These commands own a frozen reviewed document, not the mutable send
    // draft. Import can replace its prior view after internal fee refinement;
    // Save/Broadcast can only project onto their original review and view.
    const bool same = original_view == m_psbt_view_epoch &&
        (original_kind == PsbtActionKind::Import || same_review);
    if (result->error || !result->value) {
        const bool not_started{execution::NotStartedCause(result->error)};
        m_psbt_action_state = not_started ? PsbtActionState::PsbtFailed : PsbtActionState::PsbtUncertain;
        m_psbt_action_error = not_started ? tr("The PSBT action did not start.") : tr("The PSBT action ended without a confirmed outcome. Reconcile before retrying.");
        if (!not_started && same && m_psbt_action_kind == PsbtActionKind::Broadcast) {
            m_current_transaction_can_send = false; m_current_transaction_can_broadcast = false;
            m_psbt_requires_recheck = true;
        }
    } else {
        const auto& value = *result->value;
        if (same && m_psbt_action_kind == PsbtActionKind::Broadcast) {
            m_current_transaction_can_send = false; m_current_transaction_can_broadcast = false;
            m_psbt_requires_recheck = true;
        }
        m_psbt_action_error = !value.error.isEmpty() ? value.error : value.effect.error;
        if (original_kind == PsbtActionKind::Save && m_psbt_action_error.isEmpty() && value.export_document) {
            if (m_psbt_file_export_action_id == original_id) return;
            if (!same) {
                m_psbt_action_state = PsbtActionState::PsbtStale;
            } else {
                const auto receipt = m_psbt_receipt;
                if (!receipt) {
                    m_psbt_action_state = PsbtActionState::PsbtFailed;
                    m_psbt_action_error = tr("The PSBT export could not be started.");
                } else {
                    startPsbtFileExport(original_id, m_psbt_action_path, value.export_document, receipt);
                    return;
                }
            }
        } else if (value.effect.submitted) {
            m_psbt_action_state = PsbtActionState::PsbtSubmitted;
            if (same) {
                m_current_transaction_can_send = false; m_current_transaction_can_broadcast = false;
                if (value.effect.transaction) { m_current_transaction->setWtx(value.effect.transaction); if (!owns_view()) return; }
                m_current_psbt.reset(); m_psbt_document_lease.reset(); m_psbt_requires_recheck = false;
                document_view = false; projected_review = m_current_transaction;
                had_projected_review = bool(projected_review);
                m_current_transaction_source = CurrentTransactionSource::None;
                m_current_transaction_review_message.clear();
                clearSelectedCoins(); if (!owns_view()) return;
            }
        } else if (value.effect.saved) m_psbt_action_state = PsbtActionState::PsbtSaved;
        else if (value.effect.code == psbtwork::Error::Uncertain) {
            m_psbt_action_state = PsbtActionState::PsbtUncertain;
            if (same && m_psbt_action_kind == PsbtActionKind::Broadcast) m_current_transaction_can_broadcast = false;
        } else if (!same) m_psbt_action_state = PsbtActionState::PsbtStale;
        else if (!m_psbt_action_error.isEmpty()) m_psbt_action_state = PsbtActionState::PsbtFailed;
        else if (m_psbt_action_kind == PsbtActionKind::Import || m_psbt_action_kind == PsbtActionKind::Recheck) {
            const auto& imported = value.imported;
            if (imported.disposition == psbtwork::ImportResult::Disposition::Known) {
                m_psbt_import_result = PsbtImportResult::TransactionAlreadyKnown;
                m_psbt_action_state = PsbtActionState::PsbtKnown;
                if (m_psbt_action_kind == PsbtActionKind::Recheck) {
                    m_current_transaction_can_send = false; m_current_transaction_can_broadcast = false;
                    m_current_transaction_source = CurrentTransactionSource::None;
                }
                m_imported_psbt_model->setMatchedTxid(imported.txid);
                if (!observer) return;
            } else if (imported.review) {
                WalletQmlModelTransaction* transaction{nullptr};
                try { transaction = new WalletQmlModelTransaction(imported.review, m_opening, m_fee_read_services.post, this); }
                catch (const std::exception&) {
                    if (!owns_view()) return;
                    m_psbt_action_state = PsbtActionState::PsbtFailed;
                    m_psbt_action_error = tr("Transaction review is unavailable.");
                    Q_EMIT psbtActionChanged(); return;
                }
                if (!owns_view()) { if (observer) transaction->deleteLater(); return; }
                auto* previous = m_current_transaction;
                preparation_projection_changed = setCurrentTransaction(transaction);
                projected_review = transaction; had_projected_review = true;
                if (previous) previous->deleteLater();
                m_current_psbt.reset(); m_psbt_document_lease.reset(); m_psbt_requires_recheck = false;
                m_current_psbt = imported.document;
                if (m_psbt_action_lease) { m_psbt_document_lease.emplace(std::move(*m_psbt_action_lease)); m_psbt_action_lease.reset(); }
                m_psbt_fee_proof = imported.review->proof
                    ? std::shared_ptr<const psbtfee::Analysis>{imported.review, imported.review->proof.get()}
                    : std::shared_ptr<const psbtfee::Analysis>{};
                m_psbt_fee_request.reset(); m_psbt_fee_pending = false;
                m_psbt_requires_recheck = false;
                m_psbt_key_can_send = imported.key_can_send; m_psbt_can_commit_complete = imported.can_commit_complete;
                m_current_transaction_source = CurrentTransactionSource::ImportedPsbt;
                m_current_transaction_can_send = imported.review->can_send;
                m_current_transaction_can_broadcast = imported.key_can_broadcast && imported.review->proof->amount.has_value();
                m_current_transaction_review_message = imported.message;
                m_psbt_import_result = imported.review->node_policy_blocked ? PsbtImportResult::ReviewPending
                    : imported.review->can_send ? PsbtImportResult::WalletCanSign : PsbtImportResult::WalletCannotSign;
                m_psbt_action_state = PsbtActionState::PsbtImported;
                const QPointer<WalletQmlModelTransaction> imported_review{transaction};
                const auto owns_import = [&] {
                    return observer && observationsAllowed() && m_psbt_action == original_action &&
                        m_psbt_action_id == original_id && m_psbt_view_epoch == original_view &&
                        m_psbt_action_view_epoch == original_view &&
                        imported_review && m_current_transaction == imported_review;
                };
                adoptCurrentFlow(imported.review->flow);
                if (!owns_import()) return;
                if (m_preparation_projection.id == original_preparation && m_send_request_id == original_send) {
                    clearTransactionStatus();
                    if (!owns_import()) return;
                }
            } else { m_psbt_action_state = PsbtActionState::PsbtFailed; m_psbt_action_error = imported.message; }
        } else m_psbt_action_state = PsbtActionState::PsbtFailed;
    }
    if (!owns_action()) return;
    const auto owns_status = [&] {
        return owns_action() && m_psbt_view_epoch == original_view && m_preparation_projection.id == original_preparation && m_send_request_id == original_send;
    };
    if (!m_psbt_action_error.isEmpty() && same && owns_status()) {
        setTransactionStatus(m_psbt_action_error); if (!owns_view()) return;
        if (owns_status() && original_kind == PsbtActionKind::Import && m_imported_psbt_model) { m_imported_psbt_model->setError(m_psbt_action_error); if (!owns_view()) return; }
    }
    if (!owns_view()) return;
    const auto notified_review = QPointer<WalletQmlModelTransaction>{m_current_transaction};
    const bool had_notified_review = bool(notified_review);
    Q_EMIT currentTransactionChanged();
    if (!owns_action() || m_psbt_view_epoch != original_view ||
        (had_notified_review ? !notified_review || m_current_transaction != notified_review : bool(m_current_transaction))) return;
                if (preparation_projection_changed || m_preparation_projection.phase != original_preparation_phase) Q_EMIT preparationChanged();
    Q_EMIT psbtActionChanged();
}

QVariantMap WalletQmlModel::currentTransactionFlow() const { return m_current_flow; }

QVariantMap WalletQmlModel::currentTransactionFlowPreview() const
{
    return {{QStringLiteral("presentation"), m_current_flow.value(QStringLiteral("presentation"))}};
}

void WalletQmlModel::publishFlowDetails()
{
    if (!observationsAllowed()) return;
    if (!m_flow_details) return;
    if (!available() || !m_current_transaction || m_current_flow.isEmpty()) { m_flow_details->clearSource(); return; }
    auto anchor = std::make_shared<std::pair<std::shared_ptr<const void>, std::shared_ptr<const void>>>(
        m_flow_lifetime, m_current_transaction->retainedValues());
    m_flow_details->publishSource(m_current_flow, m_current_transaction->reviewedRecipients(),
        m_current_transaction, m_flow_revision, std::move(anchor), m_flow_unit);
}

void WalletQmlModel::adoptCurrentFlow(QVariantMap flow)
{
    m_flow_review = m_current_transaction;
    m_flow_transaction = m_current_transaction ? m_current_transaction->getWtx() : CTransactionRef{};
    m_flow_document = m_current_psbt;
    m_flow_proof = m_psbt_fee_proof;
    m_flow_unit = m_display_unit;
    m_current_flow = std::move(flow);
    ++m_flow_revision;
    m_flow_lifetime = m_current_transaction ? m_current_transaction->retainedValues() : nullptr;
    { std::lock_guard lock{m_flow_input->mutex}; ++m_flow_input->revision; }
    const QPointer<WalletQmlModel> observer{this};
    publishFlowDetails();
    if (!observer || !available()) return;
    Q_EMIT currentTransactionFlowChanged();
}

void WalletQmlModel::requestCurrentFlow()
{
    if (!available() || !m_opening) return;
    if (!m_current_transaction || !m_current_transaction->getWtx()) { adoptCurrentFlow({}); return; }
    const auto transaction = m_current_transaction->getWtx();
    if (m_flow_review == m_current_transaction && m_flow_transaction == transaction &&
        m_flow_document == m_current_psbt && m_flow_proof == m_psbt_fee_proof && m_flow_unit == m_display_unit) return;
    m_flow_review = m_current_transaction;
    m_flow_transaction = transaction;
    m_flow_document = m_current_psbt;
    m_flow_proof = m_psbt_fee_proof;
    m_flow_unit = m_display_unit;
    {
        std::lock_guard lock{m_flow_input->mutex};
        m_flow_input->transaction = transaction;
        m_flow_input->document = m_current_psbt;
        m_flow_input->proof = m_psbt_fee_proof;
        m_flow_input->recipients = m_current_transaction->reviewedAddresses();
        m_flow_input->notes = m_current_transaction->recipientLabels();
        m_flow_input->unit = m_display_unit;
        ++m_flow_input->revision;
    }
    if (!m_flow_query) {
        const QPointer<WalletQmlModel> observer{this};
        m_flow_query = std::make_shared<execution::SnapshotQuery<ReviewFlowResult>>(m_opening->scheduler(),
            [opening = m_opening, input = m_flow_input] {
                CTransactionRef transaction;
                std::shared_ptr<const PartiallySignedTransaction> document;
                std::shared_ptr<const psbtfee::Analysis> proof;
                QSet<QString> recipients;
                QMap<QString, QString> notes;
                int unit; quint64 revision;
                {
                    std::lock_guard lock{input->mutex};
                    transaction = input->transaction; document = input->document; proof = input->proof;
                    recipients = input->recipients; notes = input->notes; unit = input->unit; revision = input->revision;
                }
                ReviewFlowResult result; result.revision = revision;
                try {
                    auto wallet = opening->borrow();
                    if (!wallet) throw execution::NotStarted{execution::NotStarted::Cause::Cancelled};
                    result.flow = psbtwork::CollectReviewFlow(*wallet, transaction, document, proof, recipients, unit, notes);
                    auto retained = walletopening::RetainValues<QVariantMap>(opening, [&] {
                        return std::make_unique<QVariantMap>(result.flow);
                    });
                    if (!retained) throw std::runtime_error{"Transaction flow cleanup is unavailable"};
                    result.lifetime = std::move(retained);
                } catch (...) {
                    result.flow.clear(); // Rejected ownership retires the full Native value here.
                    result.lifetime.reset();
                    result.error = tr("The transaction flow could not be refreshed.");
                }
                return result;
            }, [post = m_fee_read_services.post, observer] {
                post([observer] { if (observer) observer->publishCurrentFlow(); });
            });
    }
    const QPointer<WalletQmlModel> observer{this};
    if (m_flow_details) m_flow_details->clearSource();
    if (!observer || !available()) return;
    m_flow_query->request();
    Q_EMIT currentTransactionFlowChanged();
}

void WalletQmlModel::publishCurrentFlow()
{
    if (!observationsAllowed()) return;
    const auto result = m_flow_query->take();
    if (!available() || !result) return;
    if (result->snapshot) {
        std::lock_guard lock{m_flow_input->mutex};
        if (result->snapshot->revision != m_flow_input->revision) return;
    }
    m_current_flow = result->snapshot ? result->snapshot->flow : QVariantMap{};
    ++m_flow_revision;
    m_flow_lifetime = result->snapshot ? result->snapshot->lifetime : nullptr;
    const QPointer<WalletQmlModel> observer{this};
    publishFlowDetails();
    if (!observer || !available()) return;
    Q_EMIT currentTransactionFlowChanged();
}

void WalletQmlModel::discardCurrentTransaction()
{
    if (!observationsAllowed()) return;
    const QPointer<WalletQmlModel> observer{this};
    invalidateImportReview();
    const auto view_epoch = ++m_psbt_view_epoch;
    if (m_current_transaction && m_send_review == m_current_transaction && m_send_consent) m_send_consent->withdraw();
    invalidatePreparation();
    const auto revision = m_preparation_projection.intent_revision;
    const auto preparation_id = m_preparation_projection.id;
    const auto send_id = m_send_request_id;
    m_psbt_fee_request.reset();
    m_psbt_fee_proof.reset();
    m_psbt_fee_pending = false;
    const bool had_transaction_state{
        m_current_transaction ||
        m_current_psbt ||
        m_current_transaction_can_send ||
        m_current_transaction_can_broadcast ||
        !m_current_transaction_review_message.isEmpty()};

    auto* previous = m_current_transaction;
    const bool attempt_changed = setCurrentTransaction(nullptr);
    m_current_psbt.reset(); m_psbt_document_lease.reset(); m_psbt_requires_recheck = false;
    m_current_transaction_source = CurrentTransactionSource::None;
    m_current_transaction_can_send = false;
    m_current_transaction_can_broadcast = false;
    m_current_transaction_review_message.clear();
    // Publish a coherent empty review before destruction can invoke a direct
    // QObject handler. A reentrant successor owns its own form and status.
    const auto current = [&] {
        return observer && observationsAllowed() && !m_current_transaction && revision == m_preparation_projection.intent_revision
            && preparation_id == m_preparation_projection.id && send_id == m_send_request_id && view_epoch == m_psbt_view_epoch;
    };
    delete previous;
    if (!observer || !observationsAllowed()) return;
    if (!current()) {
        if (had_transaction_state) Q_EMIT currentTransactionChanged();
        if (attempt_changed && observer && observer->observationsAllowed()) Q_EMIT preparationChanged();
        return;
    }
    clearTransactionStatus();
    if (!observer || !observationsAllowed()) return;
    if (!current()) {
        if (had_transaction_state) Q_EMIT currentTransactionChanged();
        if (attempt_changed && observer && observer->observationsAllowed()) Q_EMIT preparationChanged();
        return;
    }
    m_send_recipients->clear();
    if (!observer || !observationsAllowed()) return;

    if (had_transaction_state) {
        Q_EMIT currentTransactionChanged();
    }
    if (attempt_changed && current()) Q_EMIT preparationChanged();
}

void WalletQmlModel::selectCoin(const COutPoint& output)
{
    if (!m_admission_closed && m_coins_list_model) m_coins_list_model->requestSelectionDelta(output, true);
}

void WalletQmlModel::unselectCoin(const COutPoint& output)
{
    if (!m_admission_closed && m_coins_list_model) m_coins_list_model->requestSelectionDelta(output, false);
}

bool WalletQmlModel::isSelectedCoin(const COutPoint& output)
{
    const auto selection = selectedCoins();
    return selection && selection->contains(output);
}

std::shared_ptr<const CoinsListModel::Selection> WalletQmlModel::selectedCoins() const
{
    return m_coins_list_model ? m_coins_list_model->selectionSnapshot() : nullptr;
}

bool WalletQmlModel::coinSelectionPending() const
{
    return m_coins_list_model && m_coins_list_model->selectionPending();
}

bool WalletQmlModel::coinSelectionBlocked() const
{
    if (!m_coins_list_model) return false;
    return m_coins_list_model->selectionReconciliationRequired() ||
        (m_coins_list_model->manualInputSelection() &&
         (!selectedCoins() || selectedCoins()->empty()));
}

void WalletQmlModel::clearSelectedCoins()
{
    if (m_admission_closed) return;
    const QPointer<WalletQmlModel> observer{this};
    const bool had_maximum = m_maximum_recipient != nullptr;
    setMaximumRecipient(nullptr);
    if (observer && !m_admission_closed && m_coins_list_model) {
        const auto selection = selectedCoins();
        // Pending commands can change an apparently empty projection. An
        // explicit clear must still follow them; a stable empty fixed draft
        // needs neither a new selection command nor another facts read. The
        // explicit clear also returns the input policy to Automatic, including
        // when the acknowledged selection was already empty.
        if (!had_maximum && !coinSelectionPending() && selection && selection->empty() &&
            !m_coins_list_model->manualInputSelection()) return;
        m_coins_list_model->setManualInputSelection(false);
    }
}

unsigned int WalletQmlModel::feeTargetBlocks() const { return m_fees ? static_cast<unsigned int>(m_fees->targetBlocks()) : DEFAULT_STANDARD_FEE_TARGET; }

void WalletQmlModel::setFeeTargetBlocks(unsigned int target) { if (observationsAllowed() && m_fees) m_fees->captureTarget(target); }

void WalletQmlModel::setCustomFeeEnabled(bool enabled) { if (observationsAllowed() && m_fees) m_fees->captureMode(enabled); }

void WalletQmlModel::setCustomFeeRate(const QString& rate) { if (observationsAllowed() && m_fees) m_fees->editRate(rate); }

void WalletQmlModel::setDisplayUnit(int unit)
{
    if (unit != m_display_unit) {
        m_display_unit = unit;
        if (m_address_list_model) {
            m_address_list_model->setDisplayUnit(unit);
        }
        if (m_current_transaction) {
            m_current_transaction->setDisplayUnit(unit);
            Q_EMIT currentTransactionChanged();
        }
        Q_EMIT balanceChanged();
        Q_EMIT displayUnitChanged(unit);
    }
}



void WalletQmlModel::refreshSecurityState()
{
    if (m_opening) m_opening->refreshMetadata();
}


void WalletQmlModel::clearTransactionStatus()
{
    setTransactionStatus(QString());
}

void WalletQmlModel::setTransactionStatus(const QString& error, bool needs_unlock)
{
    if (!observationsAllowed()) return;
    const bool error_changed = m_transaction_error != error;
    const bool unlock_changed = m_transaction_needs_unlock != needs_unlock;
    if (!error_changed && !unlock_changed) return;
    const auto revision = ++m_transaction_status_revision;
    m_transaction_error = error; m_transaction_needs_unlock = needs_unlock;
    const QPointer<WalletQmlModel> observer{this};
    if (error_changed) Q_EMIT transactionErrorChanged();
    if (!observer || !observationsAllowed() || revision != m_transaction_status_revision) return;
    if (unlock_changed) Q_EMIT transactionNeedsUnlockChanged();
}

void WalletQmlModel::setSettingsError(const QString& error)
{
    if (m_settings_error != error) {
        m_settings_error = error;
        Q_EMIT settingsErrorChanged();
    }
}
