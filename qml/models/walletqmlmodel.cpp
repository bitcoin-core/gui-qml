
// Copyright (c) 2024-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/walletqmlmodel.h>

#include <qml/bitcoinamount.h>
#include <qml/models/activitylistmodel.h>
#include <qml/models/paymentrequest.h>
#include <qml/models/receiverequestentry.h>
#include <qml/models/sendrecipient.h>
#include <qml/models/sendrecipientslistmodel.h>
#include <qml/models/walletqmlmodeltransaction.h>

#include <chainparams.h>
#include <common/messages.h>
#include <consensus/amount.h>
#include <interfaces/wallet.h>
#include <key_io.h>
#include <addresstype.h>
#include <outputtype.h>
#include <policy/feerate.h>
#include <psbt.h>
#include <qml/bitcoinunits.h>
#include <serialize.h>
#include <streams.h>
#include <util/threadnames.h>
#include <wallet/coincontrol.h>
#include <wallet/wallet.h>

#include <QDateTime>
#include <QMetaObject>
#include <QRegularExpression>

#include <array>
#include <optional>

namespace {
constexpr unsigned int DEFAULT_STANDARD_FEE_TARGET{2};
constexpr int FEE_ESTIMATE_DEBOUNCE_MS{250};
constexpr unsigned int FEE_RATE_BASIS_VBYTES{1000};
constexpr std::array<unsigned int, 3> STANDARD_FEE_TARGETS{1, DEFAULT_STANDARD_FEE_TARGET, 6};
const QRegularExpression CUSTOM_FEE_RATE_PATTERN{QStringLiteral(R"(^[0-9]+(?:\.[0-9]{0,3})?$)")};

int FallbackFeeMultiplier(const unsigned int target)
{
    for (size_t i = 0; i < STANDARD_FEE_TARGETS.size(); ++i) {
        if (STANDARD_FEE_TARGETS[i] == target) {
            return static_cast<int>(STANDARD_FEE_TARGETS.size() - i);
        }
    }

    return 1;
}

QString FormatFeeEstimate(CAmount amount)
{
    BitcoinAmount bitcoin_amount;
    bitcoin_amount.setSatoshi(amount);
    return bitcoin_amount.toDisplay() + QStringLiteral(" ") + bitcoin_amount.unitLabel();
}

std::optional<CAmount> ParseCustomFeeRatePerKvB(const QString& custom_fee_rate)
{
    const QString trimmed = custom_fee_rate.trimmed();
    if (trimmed.isEmpty() || !CUSTOM_FEE_RATE_PATTERN.match(trimmed).hasMatch()) {
        return std::nullopt;
    }

    const QStringList parts = trimmed.split('.');
    bool whole_ok{false};
    const CAmount whole_part{parts.at(0).toLongLong(&whole_ok)};
    if (!whole_ok) {
        return std::nullopt;
    }

    QString fractional_part = parts.size() == 2 ? parts.at(1) : QString{};
    while (fractional_part.size() < 3) {
        fractional_part += QLatin1Char{'0'};
    }

    bool fractional_ok{false};
    const CAmount fractional_value{
        fractional_part.isEmpty() ? 0 : fractional_part.toLongLong(&fractional_ok)};
    if (!fractional_part.isEmpty() && !fractional_ok) {
        return std::nullopt;
    }

    const CAmount fee_rate_per_kvb = (whole_part * FEE_RATE_BASIS_VBYTES) + fractional_value;
    if (fee_rate_per_kvb <= 0) {
        return std::nullopt;
    }

    return fee_rate_per_kvb;
}

void ApplyPreviewChangeDestination(wallet::CCoinControl& coin_control, const QString& preview_change_address)
{
    if (preview_change_address.isEmpty()) {
        return;
    }

    const CTxDestination change_destination = DecodeDestination(preview_change_address.toStdString());
    if (IsValidDestination(change_destination)) {
        coin_control.destChange = change_destination;
    }
}

void ApplyRegtestStaticFeeOverride(wallet::CCoinControl& coin_control)
{
    if (Params().GetChainType() != ChainType::REGTEST) {
        return;
    }

    // Regtest commonly runs without fee estimation, so use a fixed static fee
    // rate instead of target-based estimation.
    coin_control.m_confirm_target.reset();
    coin_control.m_feerate = CFeeRate{wallet::DEFAULT_TRANSACTION_MINFEE};
}

std::optional<CAmount> TryPreviewFee(interfaces::Wallet& wallet,
                                     const std::vector<wallet::CRecipient>& recipients,
                                     const wallet::CCoinControl& coin_control)
{
    int change_position{-1};
    CAmount fee{0};
    const auto result = wallet.createTransaction(recipients, coin_control, /*sign=*/false, change_position, fee);
    if (!result) {
        return std::nullopt;
    }

    return fee;
}

std::optional<QString> EstimatePreviewFee(interfaces::Wallet& wallet,
                                          const std::vector<wallet::CRecipient>& recipients,
                                          const wallet::CCoinControl& base_coin_control,
                                          const QString& preview_change_address,
                                          const unsigned int target)
{
    wallet::CCoinControl coin_control{base_coin_control};
    coin_control.m_feerate.reset();
    coin_control.m_confirm_target = target;
    ApplyPreviewChangeDestination(coin_control, preview_change_address);
    ApplyRegtestStaticFeeOverride(coin_control);

    if (const auto fee = TryPreviewFee(wallet, recipients, coin_control)) {
        return FormatFeeEstimate(*fee);
    }

    if (Params().GetChainType() == ChainType::REGTEST) {
        return std::nullopt;
    }

    const CAmount required_fee_per_k = wallet.getRequiredFee(FEE_RATE_BASIS_VBYTES);
    if (required_fee_per_k <= 0) {
        return std::nullopt;
    }

    wallet::CCoinControl fallback_coin_control{coin_control};
    fallback_coin_control.m_confirm_target.reset();
    // Keep fallback previews distinct across presets even when the backend can
    // only provide a minimum required feerate.
    fallback_coin_control.m_feerate = CFeeRate{required_fee_per_k * FallbackFeeMultiplier(target)};

    if (const auto fee = TryPreviewFee(wallet, recipients, fallback_coin_control)) {
        return FormatFeeEstimate(*fee);
    }

    return std::nullopt;
}

std::optional<QString> EstimateCustomPreviewFee(interfaces::Wallet& wallet,
                                                const std::vector<wallet::CRecipient>& recipients,
                                                const wallet::CCoinControl& base_coin_control,
                                                const QString& preview_change_address,
                                                const CAmount fee_rate_per_kvb)
{
    wallet::CCoinControl coin_control{base_coin_control};
    coin_control.m_confirm_target.reset();
    coin_control.m_feerate = CFeeRate{fee_rate_per_kvb};
    ApplyPreviewChangeDestination(coin_control, preview_change_address);

    if (const auto fee = TryPreviewFee(wallet, recipients, coin_control)) {
        return FormatFeeEstimate(*fee);
    }

    return std::nullopt;
}

std::optional<std::vector<wallet::CRecipient>> BuildRecipients(const SendRecipientsListModel& recipients)
{
    std::vector<wallet::CRecipient> vec_send;
    vec_send.reserve(recipients.recipients().size());

    for (auto* recipient : recipients.recipients()) {
        if (recipient == nullptr || !recipient->isValid()) {
            return std::nullopt;
        }

        const CTxDestination destination = DecodeDestination(recipient->address()->address().toStdString());
        if (!IsValidDestination(destination)) {
            return std::nullopt;
        }

        vec_send.push_back({destination, recipient->cAmount(), recipient->subtractFeeFromAmount()});
    }

    if (vec_send.empty()) {
        return std::nullopt;
    }

    return vec_send;
}
} // namespace
WalletQmlModel::WalletQmlModel(std::unique_ptr<interfaces::Wallet> wallet, QObject *parent)
    : QObject(parent)
{
    m_wallet = std::move(wallet);
    m_activity_list_model = new ActivityListModel(this);
    m_bump_transaction_model = new BumpTransactionModel(m_wallet.get(), this);
    m_coins_list_model = new CoinsListModel(this);
    m_send_recipients = new SendRecipientsListModel(this);
    m_current_payment_request = new PaymentRequest(this);
    initializeFeeEstimator();
    m_handler_status_changed = handleStatusChanged([this] {
        QMetaObject::invokeMethod(this, [this] {
            Q_EMIT balanceChanged();
        });
    });
    m_handler_transaction_changed = handleTransactionChanged([this](const uint256&, ChangeType) {
        QMetaObject::invokeMethod(this, [this] {
            Q_EMIT balanceChanged();
        });
    });
}

WalletQmlModel::WalletQmlModel(QObject* parent)
    : QObject(parent)
{
    m_activity_list_model = new ActivityListModel(this);
    m_bump_transaction_model = new BumpTransactionModel(nullptr, this);
    m_coins_list_model = new CoinsListModel(this);
    m_send_recipients = new SendRecipientsListModel(this);
    m_current_payment_request = new PaymentRequest(this);
    initializeFeeEstimator();
}

WalletQmlModel::~WalletQmlModel()
{
    if (m_fee_estimation_timer) {
        m_fee_estimation_timer->stop();
    }
    if (m_fee_estimation_thread) {
        m_fee_estimation_thread->quit();
        m_fee_estimation_thread->wait();
    }
    delete m_fee_estimation_worker;
    delete m_activity_list_model;
    delete m_coins_list_model;
    delete m_send_recipients;
    delete m_current_payment_request;
    if (m_current_transaction) {
        delete m_current_transaction;
    }
}

void WalletQmlModel::initializeFeeEstimator()
{
    m_fee_estimation_worker = new QObject;
    m_fee_estimation_thread = new QThread(this);
    m_fee_estimation_worker->moveToThread(m_fee_estimation_thread);
    m_fee_estimation_thread->start();
    QTimer::singleShot(0, m_fee_estimation_worker, []() {
        util::ThreadRename("qml-fee-est");
    });

    m_fee_estimation_timer = new QTimer(this);
    m_fee_estimation_timer->setSingleShot(true);
    m_fee_estimation_timer->setInterval(FEE_ESTIMATE_DEBOUNCE_MS);
    connect(m_fee_estimation_timer, &QTimer::timeout, this, &WalletQmlModel::requestFeeEstimatesNow);
}

QString WalletQmlModel::balance() const
{
    if (!m_wallet) {
        return "0";
    }
    return QmlBitcoinUnits::format(QmlBitcoinUnits::Unit::BTC, m_wallet->getBalance());
}

CAmount WalletQmlModel::balanceSatoshi() const
{
    if (!m_wallet) {
        return 0;
    }
    return m_wallet->getBalance();
}

QString WalletQmlModel::estimatedFee() const
{
    if (m_custom_fee_enabled) {
        return customFeeRateValid() ? m_custom_fee_estimate : QString{};
    }
    return estimatedFeeForTarget(feeTargetBlocks());
}

bool WalletQmlModel::customFeeRateValid() const
{
    return ParseCustomFeeRatePerKvB(m_custom_fee_rate).has_value();
}

QString WalletQmlModel::estimatedFeeForTarget(const unsigned int target_blocks) const
{
    const QString estimate = m_fee_estimates.value(target_blocks);
    if (!estimate.isEmpty()) {
        return estimate;
    }

    return {};
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
    if (!m_wallet) {
        return QString();
    }
    return QString::fromStdString(m_wallet->getWalletName());
}

QString WalletQmlModel::newAddress(QString label)
{
    if (!m_wallet) {
        return QString();
    }
    OutputType output_type = m_wallet->getDefaultAddressType();
    util::Result<CTxDestination> dest{m_wallet->getNewDestination(output_type, label.toStdString())};
    return QString::fromStdString(EncodeDestination(dest.value()));
}

void WalletQmlModel::commitPaymentRequest()
{
    if (!m_wallet || !m_current_payment_request) {
        return;
    }

    if (m_current_payment_request->id().isEmpty()) {
        m_current_payment_request->setId(nextPaymentRequestId());
    }

    if (m_current_payment_request->address().isEmpty()) {
        const OutputType output_type = m_wallet->getDefaultAddressType();
        const auto destination{m_wallet->getNewDestination(output_type, m_current_payment_request->label().toStdString())};
        if (!destination) {
            return;
        }
        m_current_payment_request->setDestination(destination.value());
    }

    bool parse_ok{false};
    const int64_t request_id{m_current_payment_request->id().toLongLong(&parse_ok)};
    if (!parse_ok || request_id <= 0) {
        return;
    }

    QmlRecentRequestEntry request_entry;
    request_entry.id = request_id;
    request_entry.date = QDateTime::currentDateTime();
    request_entry.recipient.address = m_current_payment_request->address().toStdString();
    request_entry.recipient.label = m_current_payment_request->label().toStdString();
    request_entry.recipient.amount = m_current_payment_request->amount()->satoshi();
    request_entry.recipient.message = m_current_payment_request->message().toStdString();

    DataStream ss{};
    ss << request_entry;

    m_wallet->setAddressReceiveRequest(
        m_current_payment_request->destination(),
        m_current_payment_request->id().toStdString(),
        ss.str());
}

unsigned int WalletQmlModel::nextPaymentRequestId() const
{
    if (!m_wallet) {
        return 1;
    }

    int64_t max_id{0};
    for (const std::string& request : m_wallet->getAddressReceiveRequests()) {
        std::vector<uint8_t> data(request.begin(), request.end());
        DataStream ss{data};
        QmlRecentRequestEntry entry;
        try {
            ss >> entry;
        } catch (const std::ios_base::failure&) {
            continue;
        }
        if (entry.id > max_id) {
            max_id = entry.id;
        }
    }

    if (max_id <= 0 || max_id >= std::numeric_limits<unsigned int>::max() - 1) {
        return 1;
    }

    return static_cast<unsigned int>(max_id + 1);
}

std::set<interfaces::WalletTx> WalletQmlModel::getWalletTxs() const
{
    if (!m_wallet) {
        return {};
    }
    return m_wallet->getWalletTxs();
}

interfaces::WalletTx WalletQmlModel::getWalletTx(const uint256& hash) const
{
    if (!m_wallet) {
        return {};
    }
    return m_wallet->getWalletTx(Txid::FromUint256(hash));
}

bool WalletQmlModel::tryGetTxStatus(const uint256& txid,
                                    interfaces::WalletTxStatus& tx_status,
                                    int& num_blocks,
                                    int64_t& block_time) const
{
    if (!m_wallet) {
        return false;
    }
    return m_wallet->tryGetTxStatus(Txid::FromUint256(txid), tx_status, num_blocks, block_time);
}

QString WalletQmlModel::getAddressLabel(const QString& address) const
{
    if (!m_wallet || address.isEmpty()) {
        return {};
    }

    const CTxDestination destination = DecodeDestination(address.toStdString());
    if (!IsValidDestination(destination)) {
        return {};
    }

    std::string label;
    if (!m_wallet->getAddress(destination, &label, nullptr, nullptr)) {
        return {};
    }

    return QString::fromStdString(label);
}

std::unique_ptr<interfaces::Handler> WalletQmlModel::handleTransactionChanged(TransactionChangedFn fn)
{
    if (!m_wallet) {
        return nullptr;
    }
    return m_wallet->handleTransactionChanged(fn);
}

void WalletQmlModel::scheduleFeeEstimates()
{
    if (m_fee_estimation_timer == nullptr) {
        return;
    }

    if (!m_wallet || !m_send_recipients) {
        clearFeeEstimates();
        return;
    }

    m_fee_estimation_timer->start();
}

QString WalletQmlModel::ensurePreviewChangeAddress()
{
    if (!m_wallet || !m_preview_change_address.isEmpty()) {
        return m_preview_change_address;
    }

    const auto destination = m_wallet->getNewDestination(m_wallet->getDefaultAddressType(), "qml-fee-preview");
    if (!destination) {
        return {};
    }

    m_preview_change_address = QString::fromStdString(EncodeDestination(destination.value()));
    return m_preview_change_address;
}

void WalletQmlModel::requestFeeEstimatesNow()
{
    if (!m_wallet || !m_send_recipients) {
        clearFeeEstimates();
        return;
    }

    const auto recipients = BuildRecipients(*m_send_recipients);
    if (!recipients.has_value()) {
        clearFeeEstimates();
        return;
    }

    const quint64 request_id = ++m_fee_estimate_request_id;
    const QString preview_change_address = ensurePreviewChangeAddress();
    const wallet::CCoinControl base_coin_control{m_coin_control};
    const bool custom_fee_enabled{m_custom_fee_enabled};
    const std::optional<CAmount> custom_fee_rate_per_kvb{
        ParseCustomFeeRatePerKvB(m_custom_fee_rate)};
    interfaces::Wallet* const wallet = m_wallet.get();

    if (!m_fee_estimate_pending) {
        m_fee_estimate_pending = true;
        Q_EMIT feeEstimatePendingChanged();
        ++m_fee_estimate_revision;
        Q_EMIT feeEstimateRevisionChanged();
    }

    QTimer::singleShot(0, m_fee_estimation_worker, [this, request_id, recipients = *recipients, base_coin_control, preview_change_address, custom_fee_enabled, custom_fee_rate_per_kvb, wallet]() {
        QHash<unsigned int, QString> estimates;
        QString custom_estimate;

        for (const unsigned int target : STANDARD_FEE_TARGETS) {
            if (const auto estimate = EstimatePreviewFee(*wallet,
                                                         recipients,
                                                         base_coin_control,
                                                         preview_change_address,
                                                         target)) {
                estimates.insert(target, *estimate);
            }
        }

        if (custom_fee_enabled && custom_fee_rate_per_kvb.has_value()) {
            if (const auto estimate = EstimateCustomPreviewFee(*wallet,
                                                               recipients,
                                                               base_coin_control,
                                                               preview_change_address,
                                                               *custom_fee_rate_per_kvb)) {
                custom_estimate = *estimate;
            }
        }

        QMetaObject::invokeMethod(this, [this, estimates, custom_estimate, request_id]() {
            applyFeeEstimates(estimates, custom_estimate, request_id);
        }, Qt::QueuedConnection);
    });
}

void WalletQmlModel::applyFeeEstimates(const QHash<unsigned int, QString>& estimates,
                                       const QString& custom_estimate,
                                       const quint64 request_id)
{
    if (request_id != m_fee_estimate_request_id) {
        return;
    }

    bool estimates_changed{m_fee_estimates != estimates};
    bool custom_estimate_changed{m_custom_fee_estimate != custom_estimate};
    if (estimates_changed) {
        m_fee_estimates = estimates;
    }
    if (custom_estimate_changed) {
        m_custom_fee_estimate = custom_estimate;
    }
    if (estimates_changed || custom_estimate_changed) {
        Q_EMIT estimatedFeeChanged();
    }

    bool pending_changed{m_fee_estimate_pending};
    if (pending_changed) {
        m_fee_estimate_pending = false;
        Q_EMIT feeEstimatePendingChanged();
    }

    if (estimates_changed || custom_estimate_changed || pending_changed) {
        ++m_fee_estimate_revision;
        Q_EMIT feeEstimateRevisionChanged();
    }
}

void WalletQmlModel::clearFeeEstimates()
{
    ++m_fee_estimate_request_id;

    bool estimates_changed{!m_fee_estimates.isEmpty()};
    bool custom_estimate_changed{!m_custom_fee_estimate.isEmpty()};
    if (estimates_changed) {
        m_fee_estimates.clear();
    }
    if (custom_estimate_changed) {
        m_custom_fee_estimate.clear();
    }
    if (estimates_changed || custom_estimate_changed) {
        Q_EMIT estimatedFeeChanged();
    }

    bool pending_changed{m_fee_estimate_pending};
    if (pending_changed) {
        m_fee_estimate_pending = false;
        Q_EMIT feeEstimatePendingChanged();
    }

    if (estimates_changed || custom_estimate_changed || pending_changed) {
        ++m_fee_estimate_revision;
        Q_EMIT feeEstimateRevisionChanged();
    }
}

std::unique_ptr<interfaces::Handler> WalletQmlModel::handleStatusChanged(StatusChangedFn fn)
{
    if (!m_wallet) {
        return nullptr;
    }
    return m_wallet->handleStatusChanged(fn);
}

bool WalletQmlModel::prepareTransaction()
{
    if (!m_wallet || !m_send_recipients || m_send_recipients->recipients().empty()) {
        return false;
    }

    const auto vec_send = BuildRecipients(*m_send_recipients);
    if (!vec_send.has_value()) {
        return false;
    }

    CAmount total = 0;
    bool subtract_fee_from_amount = false;
    for (const auto& recipient : *vec_send) {
        total += recipient.nAmount;
        if (recipient.fSubtractFeeFromAmount) {
            subtract_fee_from_amount = true;
        }
    }

    wallet::CCoinControl coin_control{m_coin_control};
    if (m_custom_fee_enabled) {
        const auto custom_fee_rate_per_kvb = ParseCustomFeeRatePerKvB(m_custom_fee_rate);
        if (!custom_fee_rate_per_kvb.has_value()) {
            return false;
        }
        coin_control.m_confirm_target.reset();
        coin_control.m_feerate = CFeeRate{*custom_fee_rate_per_kvb};
    } else {
        coin_control.m_feerate.reset();
        if (!coin_control.m_confirm_target.has_value()) {
            coin_control.m_confirm_target = DEFAULT_STANDARD_FEE_TARGET;
        }
        ApplyRegtestStaticFeeOverride(coin_control);
    }

    CAmount balance = m_wallet->getBalance();
    if (balance < total) {
        return false;
    }

    int nChangePosRet = -1;
    CAmount nFeeRequired = 0;
    const bool sign = !m_wallet->privateKeysDisabled();
    const auto& result = m_wallet->createTransaction(*vec_send, coin_control, sign, nChangePosRet, nFeeRequired);
    if (result) {
        if (m_current_transaction) {
            delete m_current_transaction;
        }
        const CTransactionRef& newTx = *result;
        m_current_transaction = new WalletQmlModelTransaction(m_send_recipients, this);
        m_current_transaction->setWtx(newTx);
        m_current_transaction->setTransactionFee(nFeeRequired);
        if (subtract_fee_from_amount) {
            m_current_transaction->reassignAmounts(nChangePosRet);
        }
        Q_EMIT currentTransactionChanged();
        return true;
    } else {
        return false;
    }
}

void WalletQmlModel::approveExternalSignerTransaction()
{
    if (!m_wallet || !m_current_transaction || !m_wallet->hasExternalSigner()) {
        Q_EMIT externalSignerApprovalFailed(tr("External signer not available."), true);
        return;
    }

    CTransactionRef& current_tx = m_current_transaction->getWtx();
    if (!current_tx) {
        Q_EMIT externalSignerApprovalFailed(tr("Couldn't prepare transaction for external signing."), false);
        return;
    }

    try {
        CMutableTransaction mtx{*current_tx};
        PartiallySignedTransaction psbtx(mtx);
        bool complete = false;

        const auto draft_err = m_wallet->fillPSBT(std::nullopt, /*sign=*/false, /*bip32derivs=*/true,
            /*n_signed=*/nullptr, psbtx, complete);
        if (draft_err || complete) {
            const QString message = draft_err
                ? QString::fromStdString(common::PSBTErrorString(*draft_err).translated)
                : tr("Couldn't prepare transaction for external signing.");
            Q_EMIT externalSignerApprovalFailed(message, draft_err && *draft_err == common::PSBTError::EXTERNAL_SIGNER_NOT_FOUND);
            return;
        }

        const auto sign_err = m_wallet->fillPSBT(std::nullopt, /*sign=*/true, /*bip32derivs=*/true,
            /*n_signed=*/nullptr, psbtx, complete);
        if (sign_err) {
            const bool signer_not_found = *sign_err == common::PSBTError::EXTERNAL_SIGNER_NOT_FOUND;
            QString message;
            switch (*sign_err) {
            case common::PSBTError::EXTERNAL_SIGNER_NOT_FOUND:
                message = tr("External signer not found. Connect one device and try again.");
                break;
            case common::PSBTError::EXTERNAL_SIGNER_FAILED:
                message = tr("External signer failed to sign. Try again.");
                break;
            default:
                message = QString::fromStdString(common::PSBTErrorString(*sign_err).translated);
                break;
            }
            Q_EMIT externalSignerApprovalFailed(message, signer_not_found);
            return;
        }

        complete = FinalizeAndExtractPSBT(psbtx, mtx);
        if (!complete) {
            Q_EMIT externalSignerApprovalFailed(
                QString::fromStdString(common::PSBTErrorString(common::PSBTError::INCOMPLETE).translated),
                false);
            return;
        }

        m_current_transaction->setWtx(MakeTransactionRef(mtx));
        Q_EMIT externalSignerApprovalSucceeded();
    } catch (const std::runtime_error& err) {
        Q_EMIT externalSignerApprovalFailed(QString::fromStdString(err.what()), false);
    }
}

void WalletQmlModel::sendTransaction()
{
    if (!m_wallet || !m_current_transaction) {
        return;
    }

    CTransactionRef newTx = m_current_transaction->getWtx();
    if (!newTx) {
        return;
    }

    interfaces::WalletValueMap value_map;
    interfaces::WalletOrderForm order_form;
    m_wallet->commitTransaction(newTx, value_map, order_form);
}

bool WalletQmlModel::canBumpTransaction(const uint256& txid) const
{
    if (!m_wallet) {
        return false;
    }
    return m_wallet->transactionCanBeBumped(Txid::FromUint256(txid));
}

interfaces::Wallet::CoinsList WalletQmlModel::listCoins() const
{
    if (!m_wallet) {
        return {};
    }
    return m_wallet->listCoins();
}

bool WalletQmlModel::lockCoin(const COutPoint& output)
{
    if (!m_wallet) {
        return false;
    }
    return m_wallet->lockCoin(output, true);
}

bool WalletQmlModel::unlockCoin(const COutPoint& output)
{
    if (!m_wallet) {
        return false;
    }
    return m_wallet->unlockCoin(output);
}

bool WalletQmlModel::isLockedCoin(const COutPoint& output)
{
    if (!m_wallet) {
        return false;
    }
    return m_wallet->isLockedCoin(output);
}

void WalletQmlModel::listLockedCoins(std::vector<COutPoint>& outputs)
{
    if (!m_wallet) {
        return;
    }
    m_wallet->listLockedCoins(outputs);
}

void WalletQmlModel::selectCoin(const COutPoint& output)
{
    m_coin_control.Select(output);
    scheduleFeeEstimates();
}

void WalletQmlModel::unselectCoin(const COutPoint& output)
{
    m_coin_control.UnSelect(output);
    scheduleFeeEstimates();
}

bool WalletQmlModel::isSelectedCoin(const COutPoint& output)
{
    return m_coin_control.IsSelected(output);
}

std::vector<COutPoint> WalletQmlModel::listSelectedCoins() const
{
    return m_coin_control.ListSelected();
}

unsigned int WalletQmlModel::feeTargetBlocks() const
{
    return m_coin_control.m_confirm_target.value_or(DEFAULT_STANDARD_FEE_TARGET);
}

void WalletQmlModel::setFeeTargetBlocks(unsigned int target_blocks)
{
    if (m_coin_control.m_confirm_target != target_blocks) {
        m_coin_control.m_confirm_target = target_blocks;
        Q_EMIT feeTargetBlocksChanged();
        Q_EMIT estimatedFeeChanged();
        scheduleFeeEstimates();
    }
}

void WalletQmlModel::setCustomFeeEnabled(const bool enabled)
{
    if (m_custom_fee_enabled != enabled) {
        m_custom_fee_enabled = enabled;
        Q_EMIT customFeeEnabledChanged();
        Q_EMIT estimatedFeeChanged();
        scheduleFeeEstimates();
    }
}

void WalletQmlModel::setCustomFeeRate(const QString& fee_rate)
{
    const QString trimmed_fee_rate = fee_rate.trimmed();
    const bool was_valid = customFeeRateValid();

    if (m_custom_fee_rate == trimmed_fee_rate) {
        return;
    }

    m_custom_fee_rate = trimmed_fee_rate;
    m_custom_fee_estimate.clear();

    Q_EMIT customFeeRateChanged();
    if (was_valid != customFeeRateValid()) {
        Q_EMIT customFeeRateValidChanged();
    }
    Q_EMIT estimatedFeeChanged();
    scheduleFeeEstimates();
}
