
// Copyright (c) 2024-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/walletqmlmodel.h>

#include <common/messages.h>
#include <qml/bitcoinamount.h>
#include <qml/models/activitylistmodel.h>
#include <qml/models/addresslistmodel.h>
#include <qml/models/paymentrequest.h>
#include <qml/models/receiverequestentry.h>
#include <qml/models/receiverequesthistorymodel.h>
#include <qml/models/sendrecipient.h>
#include <qml/models/sendrecipientslistmodel.h>
#include <qml/models/signverifymessagemodel.h>
#include <qml/models/walletunlock.h>
#include <qml/models/walletqmlmodeltransaction.h>
#include <qml/util.h>

#include <chainparams.h>
#include <common/types.h>
#include <consensus/amount.h>
#include <interfaces/node.h>
#include <interfaces/wallet.h>
#include <key_io.h>
#include <node/psbt.h>
#include <node/transaction.h>
#include <node/types.h>
#include <addresstype.h>
#include <outputtype.h>
#include <policy/feerate.h>
#include <primitives/transaction.h>
#include <psbt.h>
#include <qml/bitcoinunits.h>
#include <script/solver.h>
#include <serialize.h>
#include <streams.h>
#include <support/allocators/secure.h>
#include <util/result.h>
#include <util/strencodings.h>
#include <util/threadnames.h>
#include <util/translation.h>
#include <wallet/coincontrol.h>
#include <wallet/wallet.h>

#include <QClipboard>
#include <QDateTime>
#include <QFile>
#include <QGuiApplication>
#include <QMetaObject>
#include <QRegularExpression>
#include <QSettings>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include <array>
#include <cstddef>
#include <functional>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

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
    return bitcoin_amount.displayWithUnit();
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
QString LocalizedString(const bilingual_str& value)
{
    return QString::fromStdString(value.translated.empty() ? value.original : value.translated);
}

QString OutputTypeId(OutputType type)
{
    return QString::fromStdString(FormatOutputType(type));
}

QString OutputTypeIdFromDestination(const CTxDestination& destination)
{
    if (std::get_if<PKHash>(&destination)) return OutputTypeId(OutputType::LEGACY);
    if (std::get_if<ScriptHash>(&destination)) return OutputTypeId(OutputType::P2SH_SEGWIT);
    if (std::get_if<WitnessV0KeyHash>(&destination) || std::get_if<WitnessV0ScriptHash>(&destination)) return OutputTypeId(OutputType::BECH32);
    if (std::get_if<WitnessV1Taproot>(&destination)) return OutputTypeId(OutputType::BECH32M);
    return {};
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

QString LocalFilePath(const QString& path)
{
    const QUrl url(path);
    if (url.isLocalFile()) {
        return url.toLocalFile();
    }
    return path;
}

QString FormatBtc(CAmount amount)
{
    return QmlBitcoinUnits::format(QmlBitcoinUnits::Unit::BTC, amount) + QStringLiteral(" BTC");
}

QString SerializePsbtBase64(const PartiallySignedTransaction& psbt)
{
    DataStream stream{};
    stream << psbt;
    return QString::fromStdString(EncodeBase64(stream.str()));
}

QByteArray SerializePsbtRaw(const PartiallySignedTransaction& psbt)
{
    DataStream stream{};
    stream << psbt;
    const std::string raw{stream.str()};
    return QByteArray(raw.data(), static_cast<qsizetype>(raw.size()));
}

bool IsMultisigScript(const CScript& script)
{
    if (script.empty()) {
        return false;
    }

    std::vector<std::vector<unsigned char>> solutions;
    return Solver(script, solutions) == TxoutType::MULTISIG || MatchMultiA(script).has_value();
}

bool IsMultisigPsbtInput(const PartiallySignedTransaction& psbt, size_t index)
{
    const PSBTInput& input{psbt.inputs[index]};
    if (IsMultisigScript(input.redeem_script) || IsMultisigScript(input.witness_script)) {
        return true;
    }

    CTxOut utxo;
    return psbt.GetInputUTXO(utxo, index) && IsMultisigScript(utxo.scriptPubKey);
}

bool DecodePsbtFromBytes(const QByteArray& bytes, PartiallySignedTransaction& psbt, std::string& error)
{
    std::vector<std::byte> raw;
    raw.reserve(bytes.size());
    for (const char ch : bytes) {
        raw.push_back(static_cast<std::byte>(static_cast<unsigned char>(ch)));
    }
    return DecodeRawPSBT(psbt, std::span<const std::byte>{raw.data(), raw.size()}, error);
}

QString PsbtErrorText(common::PSBTError error)
{
    return QString::fromStdString(common::PSBTErrorString(error).translated);
}

QString TransactionErrorText(node::TransactionError error)
{
    return QString::fromStdString(common::TransactionErrorString(error).translated);
}
} // namespace

WalletQmlModel::WalletQmlModel(std::unique_ptr<interfaces::Wallet> wallet, QObject *parent)
    : WalletQmlModel(std::move(wallet), nullptr, parent)
{
}

WalletQmlModel::WalletQmlModel(std::unique_ptr<interfaces::Wallet> wallet, interfaces::Node* node, QObject *parent)
    : QObject(parent)
    , m_wallet(std::move(wallet))
    , m_node(node)
{
    m_receive_requests = new ReceiveRequestHistoryModel(this);
    reloadReceiveRequests();
    m_activity_list_model = new ActivityListModel(this);
    m_address_list_model = new AddressListModel(this);
    m_bump_transaction_model = new BumpTransactionModel(m_wallet.get(), this);
    m_coins_list_model = new CoinsListModel(this);
    m_send_recipients = new SendRecipientsListModel(this);
    m_sign_verify_message_model = new SignVerifyMessageModel(m_wallet.get(), this);
    m_sign_verify_message_model->setSecurityStateChangedFn([this]() { refreshSecurityState(); });
    m_current_payment_request = new PaymentRequest(this);
    m_detail_payment_request = new PaymentRequest(this);
    initializeFeeEstimator();
    refreshSecurityState();
    subscribeToWalletSignals();
}

WalletQmlModel::WalletQmlModel(QObject* parent)
    : QObject(parent)
{
    m_activity_list_model = new ActivityListModel(this);
    m_address_list_model = new AddressListModel(this);
    m_bump_transaction_model = new BumpTransactionModel(nullptr, this);
    m_coins_list_model = new CoinsListModel(this);
    m_send_recipients = new SendRecipientsListModel(this);
    m_sign_verify_message_model = new SignVerifyMessageModel(nullptr, this);
    m_sign_verify_message_model->setSecurityStateChangedFn([this]() { refreshSecurityState(); });
    m_current_payment_request = new PaymentRequest(this);
    m_detail_payment_request = new PaymentRequest(this);
    m_receive_requests = new ReceiveRequestHistoryModel(this);
    initializeFeeEstimator();
}

WalletQmlModel::~WalletQmlModel()
{
    unsubscribeFromWalletSignals();
    if (m_fee_estimation_timer) {
        m_fee_estimation_timer->stop();
    }
    if (m_fee_estimation_thread) {
        m_fee_estimation_thread->quit();
        m_fee_estimation_thread->wait();
    }
    delete m_fee_estimation_worker;
    delete m_activity_list_model;
    delete m_address_list_model;
    delete m_coins_list_model;
    delete m_send_recipients;
    delete m_sign_verify_message_model;
    delete m_current_payment_request;
    delete m_detail_payment_request;
    delete m_receive_requests;
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
    QmlBitcoinUnits::Unit unit = (m_display_unit == 1)
        ? QmlBitcoinUnits::Unit::SAT
        : QmlBitcoinUnits::Unit::BTC;
    return QmlBitcoinUnits::format(unit, m_wallet->getBalance());
}

qint64 WalletQmlModel::balanceSatoshi() const
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

QString WalletQmlModel::displayName() const
{
    if (!m_display_name.isEmpty()) {
        return m_display_name;
    }
    return name();
}

void WalletQmlModel::setDisplayName(const QString& display_name)
{
    if (m_display_name != display_name) {
        m_display_name = display_name;
        Q_EMIT displayNameChanged();
    }
}

QString WalletQmlModel::keyScheme() const
{
    if (!m_wallet) {
        return {};
    }
    if (m_wallet->privateKeysDisabled()) {
        return tr("Watch-only");
    }
    return tr("Single-key");
}

QString WalletQmlModel::privateKeysStatus() const
{
    if (!m_wallet) {
        return {};
    }
    return m_wallet->privateKeysDisabled() ? tr("Disabled") : tr("Enabled");
}

QString WalletQmlModel::externalSignerStatus() const
{
    if (!m_wallet) {
        return {};
    }
    return m_wallet->hasExternalSigner() ? tr("Enabled") : tr("None");
}

bool WalletQmlModel::canManagePassphrase() const
{
    return m_wallet && !m_wallet->privateKeysDisabled();
}

bool WalletQmlModel::encryptWallet(const QString& passphrase)
{
    clearSettingsError();
    if (!m_wallet) {
        setSettingsError(tr("No wallet is selected."));
        return false;
    }
    if (passphrase.isEmpty()) {
        setSettingsError(tr("Enter a new wallet password."));
        return false;
    }

    SecureString secure_passphrase{QmlUtil::SecureStringFromQString(passphrase)};
    const bool encrypted{m_wallet->encryptWallet(secure_passphrase)};
    QmlUtil::ClearSecureString(secure_passphrase);
    if (!encrypted) {
        setSettingsError(tr("The wallet password could not be set."));
        return false;
    }

    refreshSecurityState();
    return true;
}

bool WalletQmlModel::changeWalletPassphrase(const QString& old_passphrase, const QString& new_passphrase)
{
    clearSettingsError();
    if (!m_wallet) {
        setSettingsError(tr("No wallet is selected."));
        return false;
    }
    if (old_passphrase.isEmpty()) {
        setSettingsError(tr("Enter the current wallet password."));
        return false;
    }
    if (new_passphrase.isEmpty()) {
        setSettingsError(tr("Enter a new wallet password."));
        return false;
    }

    SecureString secure_old_passphrase{QmlUtil::SecureStringFromQString(old_passphrase)};
    SecureString secure_new_passphrase{QmlUtil::SecureStringFromQString(new_passphrase)};
    const bool changed{m_wallet->changeWalletPassphrase(secure_old_passphrase, secure_new_passphrase)};
    QmlUtil::ClearSecureString(secure_old_passphrase);
    QmlUtil::ClearSecureString(secure_new_passphrase);
    if (!changed) {
        setSettingsError(tr("The current wallet password was incorrect."));
        return false;
    }

    refreshSecurityState();
    return true;
}

bool WalletQmlModel::backupWallet(const QString& path)
{
    clearSettingsError();
    if (!m_wallet) {
        setSettingsError(tr("No wallet is selected."));
        return false;
    }
    if (path.trimmed().isEmpty()) {
        setSettingsError(tr("Choose a location for the wallet backup."));
        return false;
    }

    if (!m_wallet->backupWallet(path.toStdString())) {
        setSettingsError(tr("The wallet could not be backed up."));
        return false;
    }

    return true;
}

void WalletQmlModel::clearSettingsError()
{
    setSettingsError(QString());
}

QVariantList WalletQmlModel::availableReceiveAddressTypes() const
{
    if (!m_wallet || !m_wallet->canGetAddresses()) {
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
        if (type == OutputType::BECH32M && (!m_wallet || !m_wallet->taprootEnabled())) {
            continue;
        }
        QVariantMap item;
        item.insert(QStringLiteral("id"), OutputTypeId(type));
        item.insert(QStringLiteral("label"), OutputTypeLabel(type));
        item.insert(QStringLiteral("description"), OutputTypeDescription(type));
        types.append(item);
    }
    return types;
}

QString WalletQmlModel::defaultReceiveAddressType() const
{
    const QVariantList available_types = availableReceiveAddressTypes();
    QSettings settings;
    const QString saved_type{settings.value(persistedReceiveAddressTypeKey()).toString()};
    for (const QVariant& item : available_types) {
        const QVariantMap type{item.toMap()};
        if (type.value(QStringLiteral("id")).toString() == saved_type) {
            return saved_type;
        }
    }

    const QString core_default{m_wallet ? OutputTypeId(m_wallet->getDefaultAddressType()) : QString()};
    for (const QVariant& item : available_types) {
        const QVariantMap type{item.toMap()};
        if (type.value(QStringLiteral("id")).toString() == core_default) {
            return core_default;
        }
    }

    return available_types.empty()
        ? QString{}
        : available_types.front().toMap().value(QStringLiteral("id")).toString();
}

void WalletQmlModel::setDefaultReceiveAddressType(const QString& address_type)
{
    for (const QVariant& item : availableReceiveAddressTypes()) {
        const QVariantMap type{item.toMap()};
        if (type.value(QStringLiteral("id")).toString() == address_type) {
            QSettings settings;
            settings.setValue(persistedReceiveAddressTypeKey(), address_type);
            settings.sync();
            return;
        }
    }
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
    if (!m_wallet) {
        return;
    }
    m_wallet->remove();
}

bool WalletQmlModel::setCurrentPaymentRequestAddress(QString address)
{
    if (!m_wallet || !m_current_payment_request || address.isEmpty()) {
        return false;
    }

    const CTxDestination destination{DecodeDestination(address.toStdString())};
    if (!IsValidDestination(destination)) {
        return false;
    }

    m_current_payment_request->clear();
    m_current_payment_request->setDestination(destination);
    m_current_payment_request->setLabel(getAddressLabel(address));
    m_current_payment_request->setIsEditing(false);
    m_current_payment_request->setIsEditing(true);
    return true;
}

bool WalletQmlModel::ensurePaymentRequestDestination()
{
    if (!m_wallet || !m_current_payment_request) {
        return false;
    }
    if (!m_current_payment_request->address().isEmpty()) {
        return true;
    }
    const QString address_type_id = m_current_payment_request->addressType().isEmpty()
        ? defaultReceiveAddressType()
        : m_current_payment_request->addressType();
    OutputType output_type = m_wallet->getDefaultAddressType();
    if (!address_type_id.isEmpty()) {
        const auto parsed_type{ParseOutputType(address_type_id.toStdString())};
        if (!parsed_type) {
            return false;
        }
        output_type = *parsed_type;
    }
    const auto destination{m_wallet->getNewDestination(output_type, m_current_payment_request->label().toStdString())};
    if (!destination || !IsValidDestination(destination.value())) {
        return false;
    }
    m_current_payment_request->setAddressType(OutputTypeId(output_type));
    m_current_payment_request->setDestination(destination.value());
    return true;
}

bool WalletQmlModel::saveCurrentPaymentRequest()
{
    if (!m_wallet || !m_current_payment_request) {
        return false;
    }

    const bool is_update = !m_current_payment_request->id().isEmpty();
    const QString request_id_text = is_update
        ? m_current_payment_request->id()
        : QString::number(nextPaymentRequestId());

    bool parse_ok{false};
    const int64_t request_id{request_id_text.toLongLong(&parse_ok)};
    if (!parse_ok || request_id <= 0) {
        return false;
    }

    QmlRecentRequestEntry request_entry;
    request_entry.id = request_id;
    request_entry.date = is_update ? m_current_payment_request->created() : QDateTime::currentDateTime();
    if (!is_update) {
        m_current_payment_request->setCreated(request_entry.date);
    }
    request_entry.recipient.address = m_current_payment_request->address().toStdString();
    request_entry.recipient.label = m_current_payment_request->label().toStdString();
    request_entry.recipient.amount = m_current_payment_request->amount()->satoshi();
    request_entry.recipient.message = m_current_payment_request->message().toStdString();
    request_entry.recipient.noteSelf = m_current_payment_request->noteSelf().toStdString();

    const bool saved = m_wallet->setAddressReceiveRequest(
        m_current_payment_request->destination(),
        request_id_text.toStdString(),
        ReceiveRequestHistoryModel::SerializeEntry(request_entry));
    if (!saved) {
        return false;
    }

    if (!is_update) {
        m_current_payment_request->setId(static_cast<unsigned int>(request_id));
    }

    if (m_receive_requests) {
        m_receive_requests->prependOrReplace(request_entry);
    }

    if (m_activity_list_model) {
        if (is_update) {
            m_activity_list_model->updateReceiveRequest(
                m_current_payment_request->id(),
                m_current_payment_request->label(),
                m_current_payment_request->amount()->satoshi());
        } else {
            m_activity_list_model->addReceiveRequest(
                m_current_payment_request->address(),
                m_current_payment_request->label(),
                m_current_payment_request->amount()->satoshi(),
                request_entry.date.toSecsSinceEpoch(),
                m_current_payment_request->id());
        }
    }

    m_current_payment_request->setIsEditing(false);

    if (m_detail_payment_request && m_detail_payment_request->id() == m_current_payment_request->id()) {
        loadPaymentRequestDetail(m_current_payment_request->id());
    }

    return true;
}

bool WalletQmlModel::commitPaymentRequest()
{
    if (!m_wallet || !m_current_payment_request) {
        return false;
    }

    if (!ensurePaymentRequestDestination()) {
        if (m_wallet->isCrypted() && m_wallet->isLocked()) {
            m_current_payment_request->setNeedsUnlock(true);
        }
        return false;
    }
    return saveCurrentPaymentRequest();
}

bool WalletQmlModel::commitPaymentRequestWithPassphrase(const QString& passphrase)
{
    if (!m_wallet || !m_current_payment_request) {
        return false;
    }

    SecureString secure_passphrase{QmlUtil::SecureStringFromQString(passphrase)};
    const auto result{TryUnlockWithPassphrase(*m_wallet, secure_passphrase)};
    switch (result) {
    case WalletUnlockResult::IncorrectPassphrase:
        m_current_payment_request->setUnlockError(tr("The wallet password you entered was incorrect."));
        return false;
    case WalletUnlockResult::AlreadyUnlocked:
    case WalletUnlockResult::UnlockedNowRelockRequired:
        break;
    }

    const bool need_relock = result == WalletUnlockResult::UnlockedNowRelockRequired;
    if (need_relock) {
        refreshSecurityState();
    }
    WalletRelockGuard relock_guard{*m_wallet, [this] { refreshSecurityState(); }, need_relock};

    if (!ensurePaymentRequestDestination()) {
        return false;
    }
    if (!saveCurrentPaymentRequest()) {
        return false;
    }

    m_current_payment_request->setNeedsUnlock(false);
    m_current_payment_request->setUnlockError(QString());
    return true;
}

void WalletQmlModel::reloadReceiveRequests()
{
    if (!m_receive_requests) return;
    if (!m_wallet) {
        m_receive_requests->setEntries({});
        return;
    }
    m_receive_requests->setEntries(
        ReceiveRequestHistoryModel::DeserializeEntries(m_wallet->getAddressReceiveRequests()));
}

bool WalletQmlModel::removeReceiveRequest(const QString& request_id)
{
    if (!m_wallet || !m_receive_requests) return false;
    const auto entry = m_receive_requests->entryById(request_id);
    if (!entry) return false;
    const CTxDestination destination = DecodeDestination(entry->recipient.address);
    if (!IsValidDestination(destination)) return false;
    if (!m_wallet->setAddressReceiveRequest(destination, request_id.toStdString(), std::string{})) {
        return false;
    }
    m_receive_requests->removeByRequestId(request_id);
    if (m_activity_list_model) {
        m_activity_list_model->removePendingReceiveRequest(request_id);
    }
    return true;
}

bool WalletQmlModel::loadPaymentRequest(const QString& request_id)
{
    if (!m_current_payment_request || !m_receive_requests) return false;
    const auto entry = m_receive_requests->entryById(request_id);
    if (!entry) return false;
    if (entry->id < 0 || entry->id > std::numeric_limits<unsigned int>::max()) return false;

    const CTxDestination destination = DecodeDestination(entry->recipient.address);
    if (!IsValidDestination(destination)) return false;

    m_current_payment_request->clear();
    m_current_payment_request->setDestination(destination);
    m_current_payment_request->setLabel(QString::fromStdString(entry->recipient.label));
    m_current_payment_request->setMessage(QString::fromStdString(entry->recipient.message));
    m_current_payment_request->setNoteSelf(QString::fromStdString(entry->recipient.noteSelf));
    m_current_payment_request->amount()->setSatoshi(entry->recipient.amount);
    m_current_payment_request->setId(static_cast<unsigned int>(entry->id));
    m_current_payment_request->setCreated(entry->date);
    m_current_payment_request->setIsEditing(false);
    return true;
}

bool WalletQmlModel::loadPaymentRequestDetail(const QString& request_id)
{
    if (!m_detail_payment_request || !m_receive_requests) return false;
    const auto entry = m_receive_requests->entryById(request_id);
    if (!entry) return false;
    if (entry->id < 0 || entry->id > std::numeric_limits<unsigned int>::max()) return false;

    const CTxDestination destination = DecodeDestination(entry->recipient.address);
    if (!IsValidDestination(destination)) return false;

    m_detail_payment_request->clear();
    m_detail_payment_request->setDestination(destination);
    m_detail_payment_request->setLabel(QString::fromStdString(entry->recipient.label));
    m_detail_payment_request->setMessage(QString::fromStdString(entry->recipient.message));
    m_detail_payment_request->setNoteSelf(QString::fromStdString(entry->recipient.noteSelf));
    m_detail_payment_request->amount()->setSatoshi(entry->recipient.amount);
    m_detail_payment_request->setId(static_cast<unsigned int>(entry->id));
    m_detail_payment_request->setCreated(entry->date);
    m_detail_payment_request->setIsEditing(false);
    return true;
}

void WalletQmlModel::usePaymentRequestAsTemplate(const QString& request_id)
{
    if (!m_current_payment_request || !m_receive_requests) return;
    const auto entry = m_receive_requests->entryById(request_id);
    if (!entry) return;
    const CTxDestination destination = DecodeDestination(entry->recipient.address);

    m_current_payment_request->clear();
    m_current_payment_request->setLabel(QString::fromStdString(entry->recipient.label));
    m_current_payment_request->setMessage(QString::fromStdString(entry->recipient.message));
    m_current_payment_request->setNoteSelf(QString::fromStdString(entry->recipient.noteSelf));
    m_current_payment_request->amount()->setSatoshi(entry->recipient.amount);
    m_current_payment_request->setAddressType(OutputTypeIdFromDestination(destination));

    // Toggle isEditing to re-trigger QML input sync with populated values
    m_current_payment_request->setIsEditing(false);
    m_current_payment_request->setIsEditing(true);
}

unsigned int WalletQmlModel::nextPaymentRequestId() const
{
    if (!m_receive_requests) return 1;
    const int64_t max_id = m_receive_requests->maxId();
    if (max_id <= 0 || max_id >= std::numeric_limits<unsigned int>::max() - 1) return 1;
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
    if (m_wallet->getAddress(destination, &label, nullptr, nullptr)) {
        if (!label.empty()) {
            return QString::fromStdString(label);
        }
    }

    for (const interfaces::WalletAddress& wallet_address : getAddresses()) {
        if (wallet_address.dest == destination) {
            return QString::fromStdString(wallet_address.name);
        }
    }

    return {};
}

bool WalletQmlModel::setAddressLabel(const QString& address, const QString& label)
{
    if (!m_wallet || address.isEmpty()) {
        return false;
    }

    const CTxDestination destination{DecodeDestination(address.toStdString())};
    if (!IsValidDestination(destination)) {
        return false;
    }

    wallet::AddressPurpose purpose{wallet::AddressPurpose::RECEIVE};
    if (!m_wallet->getAddress(destination, nullptr, nullptr, &purpose)) {
        return false;
    }

    return m_wallet->setAddressBook(destination, label.toStdString(), purpose);
}

std::vector<interfaces::WalletAddress> WalletQmlModel::getAddresses() const
{
    if (!m_wallet) {
        return {};
    }
    return m_wallet->getAddresses();
}

std::map<QString, CAmount> WalletQmlModel::addressBalances() const
{
    std::map<QString, CAmount> balances;
    if (!m_wallet) {
        return balances;
    }

    for (const auto& coins_entry : m_wallet->listCoins()) {
        for (const auto& [outpoint, tx_out] : coins_entry.second) {
            CTxDestination destination;
            if (!ExtractDestination(tx_out.txout.scriptPubKey, destination))
                continue;

            const QString address{QString::fromStdString(EncodeDestination(destination))};
            if (address.isEmpty())
                continue;

            balances[address] += tx_out.txout.nValue;
        }
    }

    return balances;
}

std::set<QString> WalletQmlModel::usedAddresses() const
{
    std::set<QString> addresses;

    if (!m_wallet)
        return addresses;

    std::set<QString> receive_addresses;
    for (const interfaces::WalletAddress& wallet_address : getAddresses()) {
        if (wallet_address.purpose != wallet::AddressPurpose::RECEIVE || wallet_address.is_mine == wallet::ISMINE_NO) {
            continue;
        }

        const QString address{QString::fromStdString(EncodeDestination(wallet_address.dest))};
        if (!address.isEmpty()) {
            receive_addresses.insert(address);
        }
    }

    for (const interfaces::WalletTx& wallet_tx : m_wallet->getWalletTxs()) {
        for (size_t i{0}; i < wallet_tx.txout_address.size(); ++i) {
            if (i >= wallet_tx.txout_address_is_mine.size() || !wallet_tx.txout_address_is_mine[i])
                continue;

            if (i < wallet_tx.txout_is_change.size() && wallet_tx.txout_is_change[i])
                continue;

            const QString address{QString::fromStdString(EncodeDestination(wallet_tx.txout_address[i]))};
            if (!address.isEmpty() && receive_addresses.count(address) > 0)
                addresses.insert(address);
        }
    }

    return addresses;
}

std::set<QString> WalletQmlModel::changeAddresses() const
{
    std::set<QString> addresses;
    if (!m_wallet) {
        return addresses;
    }

    std::set<COutPoint> change_outpoints;
    for (const interfaces::WalletTx& wallet_tx : m_wallet->getWalletTxs()) {
        if (!wallet_tx.tx)
            continue;

        const Txid txid{wallet_tx.tx->GetHash()};
        for (size_t i{0}; i < wallet_tx.txout_is_change.size(); ++i) {
            if (!wallet_tx.txout_is_change[i])
                continue;

            change_outpoints.insert(COutPoint{txid, static_cast<uint32_t>(i)});
        }
    }

    for (const auto& coins_entry : m_wallet->listCoins()) {
        for (const auto& [outpoint, tx_out] : coins_entry.second) {
            if (tx_out.txout.nValue <= 0)
                continue;

            if (change_outpoints.count(outpoint) > 0) {
                CTxDestination destination;
                if (!ExtractDestination(tx_out.txout.scriptPubKey, destination))
                    continue;

                const QString address{QString::fromStdString(EncodeDestination(destination))};
                if (address.isEmpty())
                    continue;

                addresses.insert(address);
            }
        }
    }

    return addresses;
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

std::unique_ptr<interfaces::Handler> WalletQmlModel::handleUnload(UnloadFn fn)
{
    if (!m_wallet) {
        return nullptr;
    }
    return m_wallet->handleUnload(fn);
}

bool WalletQmlModel::prepareTransaction()
{
    return prepareTransactionInternal(std::nullopt);
}

bool WalletQmlModel::prepareTransactionWithPassphrase(const QString& passphrase)
{
    return prepareTransactionInternal(std::optional<SecureString>{QmlUtil::SecureStringFromQString(passphrase)});
}

bool WalletQmlModel::prepareTransactionInternal(std::optional<SecureString> passphrase)
{
    clearTransactionStatus();
    if (!m_wallet || !m_send_recipients || m_send_recipients->recipients().empty()) {
        if (passphrase.has_value()) {
            QmlUtil::ClearSecureString(*passphrase);
            passphrase.reset();
        }
        setTransactionStatus(tr("Enter at least one valid recipient to continue."));
        return false;
    }
    if (!m_wallet->privateKeysDisabled() && m_wallet->isCrypted() && m_wallet->isLocked() && !passphrase.has_value()) {
        refreshSecurityState();
        setTransactionStatus(tr("Enter your wallet password to prepare this transaction."), true);
        return false;
    }

    bool relock{false};
    if (!unlockForAction(passphrase, relock)) {
        return false;
    }
    WalletRelockGuard relock_guard{*m_wallet, [this] { refreshSecurityState(); }, relock};

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
        relock_guard.relock();
        setTransactionStatus(tr("The wallet does not have enough balance for this transaction."));
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
        m_current_transaction->setDisplayUnit(m_display_unit);
        relock_guard.relock();
        Q_EMIT currentTransactionChanged();
        return true;
    }

    relock_guard.relock();
    setTransactionStatus(LocalizedString(util::ErrorString(result)));
    return false;
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

bool WalletQmlModel::sendTransaction()
{
    return sendTransactionInternal();
}

bool WalletQmlModel::sendTransactionInternal()
{
    clearTransactionStatus();
    if (!m_wallet || !m_current_transaction) {
        setTransactionStatus(tr("Review a transaction before sending it."));
        return false;
    }

    CTransactionRef signed_tx = m_current_transaction->getWtx();
    if (!signed_tx) {
        setTransactionStatus(tr("Review a transaction before sending it."));
        return false;
    }

    if (m_wallet->privateKeysDisabled() && !m_wallet->hasExternalSigner()) {
        setTransactionStatus(tr("This wallet cannot sign transactions."));
        return false;
    }

    interfaces::WalletValueMap value_map;
    interfaces::WalletOrderForm order_form;
    m_wallet->commitTransaction(signed_tx, value_map, order_form);

    clearTransactionStatus();
    return true;
}

void WalletQmlModel::setImportedPsbtError(const QString& error)
{
    m_imported_psbt.reset();
    m_imported_psbt_mode.clear();
    m_imported_psbt_status.clear();
    m_imported_psbt_error = error;
    m_imported_psbt_summary.clear();
    m_imported_psbt_can_sign = false;
    m_imported_psbt_can_broadcast = false;
    m_imported_psbt_complete = false;
    m_imported_psbt_unsigned_inputs = 0;
    m_imported_psbt_could_sign_inputs = 0;
    Q_EMIT importedPsbtChanged();
}

void WalletQmlModel::clearImportedPsbt()
{
    m_imported_psbt.reset();
    m_imported_psbt_mode.clear();
    m_imported_psbt_status.clear();
    m_imported_psbt_error.clear();
    m_imported_psbt_summary.clear();
    m_imported_psbt_can_sign = false;
    m_imported_psbt_can_broadcast = false;
    m_imported_psbt_complete = false;
    m_imported_psbt_unsigned_inputs = 0;
    m_imported_psbt_could_sign_inputs = 0;
    Q_EMIT importedPsbtChanged();
}

QString WalletQmlModel::importPsbtFromFile(const QString& path)
{
    const QString file_path{LocalFilePath(path)};
    QFile file(file_path);
    if (!file.open(QIODevice::ReadOnly)) {
        setImportedPsbtError(tr("Could not open PSBT file: %1").arg(file.errorString()));
        return QStringLiteral("unsupported");
    }

    const QByteArray bytes{file.readAll()};
    PartiallySignedTransaction psbt;
    std::string error;
    if (!DecodePsbtFromBytes(bytes, psbt, error)) {
        psbt = PartiallySignedTransaction{};
        std::string base64_error;
        if (!DecodeBase64PSBT(psbt, QString::fromUtf8(bytes).trimmed().toStdString(), base64_error)) {
            setImportedPsbtError(tr("Could not decode PSBT: %1").arg(QString::fromStdString(base64_error.empty() ? error : base64_error)));
            return QStringLiteral("unsupported");
        }
    }

    return importPsbt(std::move(psbt));
}

QString WalletQmlModel::importPsbt(PartiallySignedTransaction psbt)
{
    QString mode;
    QString reason;
    if (tryImportPsbtToReview(psbt, mode, reason)) {
        clearImportedPsbt();
        return mode;
    }

    setImportedPsbtError(reason.isEmpty() ? tr("This PSBT is not supported yet.") : reason);
    return QStringLiteral("unsupported");
}

bool WalletQmlModel::tryImportPsbtToReview(const PartiallySignedTransaction& psbt, QString& mode, QString& reason)
{
    if (!m_wallet) {
        reason = tr("No wallet is loaded.");
        return false;
    }
    if (!psbt.tx) {
        reason = tr("The PSBT does not contain an unsigned transaction.");
        return false;
    }
    if (psbt.tx->vin.empty() || psbt.tx->vout.empty()) {
        reason = tr("The PSBT has no inputs or outputs.");
        return false;
    }
    if (psbt.inputs.size() != psbt.tx->vin.size() || psbt.outputs.size() != psbt.tx->vout.size()) {
        reason = tr("The PSBT is malformed.");
        return false;
    }

    for (size_t i{0}; i < psbt.inputs.size(); ++i) {
        if (!m_wallet->txinIsMine(psbt.tx->vin[i])) {
            reason = tr("Only PSBTs that spend this wallet's own inputs are supported right now.");
            return false;
        }
    }

    PartiallySignedTransaction signed_psbt{psbt};
    bool complete{false};
    size_t signed_inputs{0};
    const std::optional<common::PSBTError> fill_error{
        m_wallet->fillPSBT(std::nullopt, /*sign=*/true, /*bip32derivs=*/true, &signed_inputs, signed_psbt, complete)};

    for (size_t i{0}; i < signed_psbt.inputs.size(); ++i) {
        if (IsMultisigPsbtInput(signed_psbt, i)) {
            reason = tr("Multisignature PSBTs are not supported yet.");
            return false;
        }
    }

    if (fill_error || !complete) {
        reason = tr("Only PSBTs this wallet can fully sign are supported right now.");
        return false;
    }

    struct DraftRecipient {
        QString address;
        QString label;
        CAmount amount;
    };
    std::vector<DraftRecipient> draft_recipients;
    CAmount recipient_total{0};
    for (const CTxOut& output : signed_psbt.tx->vout) {
        CTxDestination destination;
        if (!ExtractDestination(output.scriptPubKey, destination)) {
            reason = tr("Only PSBTs with standard address outputs are supported right now.");
            return false;
        }
        if (m_wallet->txoutIsMine(output)) {
            continue;
        }
        const QString address{QString::fromStdString(EncodeDestination(destination))};
        draft_recipients.push_back({address, getAddressLabel(address), output.nValue});
        recipient_total += output.nValue;
    }

    if (draft_recipients.empty()) {
        reason = tr("The PSBT does not have any recipient outputs to review.");
        return false;
    }
    if (draft_recipients.size() > 25) {
        reason = tr("The PSBT has more recipients than this send flow supports.");
        return false;
    }
    if (recipient_total <= 0) {
        reason = tr("The PSBT does not send a positive amount.");
        return false;
    }

    const node::PSBTAnalysis analysis{node::AnalyzePSBT(signed_psbt)};
    CMutableTransaction mutable_tx;
    if (!FinalizeAndExtractPSBT(signed_psbt, mutable_tx)) {
        reason = tr("Only PSBTs this wallet can fully sign are supported right now.");
        return false;
    }

    m_send_recipients->clear();
    for (size_t i{0}; i < draft_recipients.size(); ++i) {
        if (i > 0) {
            m_send_recipients->add();
        }
        SendRecipient* recipient{m_send_recipients->currentRecipient()};
        recipient->setAddress(draft_recipients[i].address);
        recipient->setLabel(draft_recipients[i].label);
        recipient->amount()->setSatoshi(draft_recipients[i].amount);
        recipient->setMessage(QString());
    }
    m_send_recipients->setCurrentIndex(0);

    if (m_current_transaction) {
        delete m_current_transaction;
    }
    m_current_transaction = new WalletQmlModelTransaction(m_send_recipients, this);
    m_current_transaction->setWtx(MakeTransactionRef(std::move(mutable_tx)));
    if (analysis.fee) {
        m_current_transaction->setTransactionFee(*analysis.fee);
    }
    Q_EMIT currentTransactionChanged();

    mode = draft_recipients.size() > 1 ? QStringLiteral("multiple-review") : QStringLiteral("single-review");
    return true;
}

QStringList WalletQmlModel::buildPsbtSummary(const PartiallySignedTransaction& psbt) const
{
    QStringList lines;
    if (!psbt.tx) {
        lines << tr("PSBT does not contain an unsigned transaction.");
        return lines;
    }

    CAmount total{0};
    for (const CTxOut& output : psbt.tx->vout) {
        total += output.nValue;
        CTxDestination destination;
        const QString address{ExtractDestination(output.scriptPubKey, destination) ? QString::fromStdString(EncodeDestination(destination)) : tr("unknown destination")};
        const bool own_address{m_wallet && m_wallet->txoutIsMine(output)};
        lines << tr("Sends %1 to %2%3").arg(FormatBtc(output.nValue), address, own_address ? tr(" (own address)") : QString());
    }

    const node::PSBTAnalysis analysis{node::AnalyzePSBT(psbt)};
    if (!analysis.error.empty()) {
        lines << tr("Analysis: %1").arg(QString::fromStdString(analysis.error));
    }
    if (analysis.fee) {
        lines << tr("Pays transaction fee: %1").arg(FormatBtc(*analysis.fee));
    } else {
        lines << tr("Transaction fee is not available.");
    }
    lines << tr("Total output amount: %1").arg(FormatBtc(total));

    const int unsigned_inputs{static_cast<int>(CountPSBTUnsignedInputs(psbt))};
    if (unsigned_inputs > 0) {
        lines << tr("Unsigned inputs: %1").arg(unsigned_inputs);
    }
    return lines;
}

void WalletQmlModel::refreshImportedPsbtState(const QString& status_override)
{
    if (!m_imported_psbt) {
        return;
    }

    bool complete{FinalizePSBT(*m_imported_psbt)};
    size_t could_sign{0};
    std::optional<common::PSBTError> fill_error;
    if (m_wallet) {
        fill_error = m_wallet->fillPSBT(std::nullopt, /*sign=*/false, /*bip32derivs=*/true, &could_sign, *m_imported_psbt, complete);
    }

    m_imported_psbt_error = fill_error ? PsbtErrorText(*fill_error) : QString();
    m_imported_psbt_complete = complete;
    m_imported_psbt_can_broadcast = complete;
    m_imported_psbt_could_sign_inputs = static_cast<int>(could_sign);
    m_imported_psbt_unsigned_inputs = static_cast<int>(CountPSBTUnsignedInputs(*m_imported_psbt));
    m_imported_psbt_can_sign = !complete && m_wallet && !m_wallet->privateKeysDisabled() && could_sign > 0;
    m_imported_psbt_summary = buildPsbtSummary(*m_imported_psbt);

    if (!status_override.isEmpty()) {
        m_imported_psbt_status = status_override;
    } else if (!m_imported_psbt_error.isEmpty()) {
        m_imported_psbt_status = m_imported_psbt_error;
    } else if (complete) {
        m_imported_psbt_status = tr("Transaction is fully signed and ready for broadcast.");
    } else if (!m_wallet) {
        m_imported_psbt_status = tr("No wallet is loaded. You can inspect, copy, or save this PSBT.");
    } else if (m_wallet->privateKeysDisabled()) {
        m_imported_psbt_status = tr("This wallet cannot sign transactions because private keys are disabled.");
    } else if (could_sign > 0) {
        m_imported_psbt_status = tr("This wallet can sign %1 input(s).").arg(static_cast<int>(could_sign));
    } else {
        m_imported_psbt_status = tr("This wallet does not have the right keys to sign this PSBT.");
    }

    Q_EMIT importedPsbtChanged();
}

void WalletQmlModel::signImportedPsbt()
{
    if (!m_imported_psbt || !m_wallet) {
        setImportedPsbtError(tr("No signable PSBT is loaded."));
        return;
    }
    if (m_wallet->privateKeysDisabled()) {
        refreshImportedPsbtState(tr("This wallet cannot sign transactions because private keys are disabled."));
        return;
    }

    bool complete{false};
    size_t signed_inputs{0};
    const std::optional<common::PSBTError> error{m_wallet->fillPSBT(std::nullopt, /*sign=*/true, /*bip32derivs=*/true, &signed_inputs, *m_imported_psbt, complete)};
    if (error) {
        refreshImportedPsbtState(tr("Could not sign PSBT: %1").arg(PsbtErrorText(*error)));
        return;
    }

    if (complete) {
        refreshImportedPsbtState(tr("PSBT signed. Transaction is ready for broadcast."));
    } else if (signed_inputs > 0) {
        refreshImportedPsbtState(tr("Signed %1 input(s). More signatures are still required.").arg(static_cast<int>(signed_inputs)));
    } else {
        refreshImportedPsbtState(tr("This wallet could not add any signatures to the PSBT."));
    }
}

void WalletQmlModel::broadcastImportedPsbt()
{
    if (!m_imported_psbt) {
        setImportedPsbtError(tr("No PSBT is loaded."));
        return;
    }
    if (!m_node) {
        refreshImportedPsbtState(tr("Cannot broadcast from this context because the node interface is unavailable."));
        return;
    }

    CMutableTransaction mutable_tx;
    if (!FinalizeAndExtractPSBT(*m_imported_psbt, mutable_tx)) {
        refreshImportedPsbtState(tr("PSBT is not complete and cannot be broadcast."));
        return;
    }

    const CTransactionRef tx{MakeTransactionRef(std::move(mutable_tx))};
    std::string error_string;
    const node::TransactionError error{m_node->broadcastTransaction(tx, node::DEFAULT_MAX_RAW_TX_FEE_RATE.GetFeePerK(), error_string)};
    if (error == node::TransactionError::OK) {
        refreshImportedPsbtState(tr("Transaction broadcast successfully. Transaction ID: %1").arg(QString::fromStdString(tx->GetHash().ToString())));
    } else {
        QString message{TransactionErrorText(error)};
        if (!error_string.empty()) {
            message += QStringLiteral(": ") + QString::fromStdString(error_string);
        }
        refreshImportedPsbtState(tr("Transaction broadcast failed: %1").arg(message));
    }
}

void WalletQmlModel::copyImportedPsbtToClipboard()
{
    if (!m_imported_psbt) {
        return;
    }
    QGuiApplication::clipboard()->setText(SerializePsbtBase64(*m_imported_psbt));
    refreshImportedPsbtState(tr("PSBT copied to clipboard."));
}

void WalletQmlModel::saveImportedPsbtToFile(const QString& path)
{
    if (!m_imported_psbt) {
        return;
    }
    QFile file(LocalFilePath(path));
    if (!file.open(QIODevice::WriteOnly)) {
        refreshImportedPsbtState(tr("Could not save PSBT: %1").arg(file.errorString()));
        return;
    }
    const QByteArray raw{SerializePsbtRaw(*m_imported_psbt)};
    if (file.write(raw) != raw.size()) {
        refreshImportedPsbtState(tr("Could not write the complete PSBT file."));
        return;
    }
    refreshImportedPsbtState(tr("PSBT saved."));
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

void WalletQmlModel::setDisplayUnit(int unit)
{
    if (unit != m_display_unit) {
        m_display_unit = unit;
        if (m_activity_list_model) {
            m_activity_list_model->setDisplayUnit(unit);
        }
        if (m_current_transaction) {
            m_current_transaction->setDisplayUnit(unit);
        }
        Q_EMIT balanceChanged();
        Q_EMIT displayUnitChanged(unit);
    }
}

void WalletQmlModel::subscribeToWalletSignals()
{
    if (!m_wallet) {
        return;
    }
    m_handler_status_changed = handleStatusChanged([this]() {
        QMetaObject::invokeMethod(this, [this]() {
            refreshSecurityState();
            Q_EMIT balanceChanged();
        }, Qt::QueuedConnection);
    });
    m_handler_address_list_changed = m_wallet->handleAddressBookChanged([this](const CTxDestination&, const std::string&, bool, wallet::AddressPurpose, ChangeType) {
        QMetaObject::invokeMethod(this, [this] {
            Q_EMIT addressListChanged();
        }, Qt::QueuedConnection);
    });
    m_handler_transaction_changed = handleTransactionChanged([this](const uint256&, ChangeType) {
        QMetaObject::invokeMethod(this, [this] {
            Q_EMIT balanceChanged();
        }, Qt::QueuedConnection);
    });
    m_handler_unload = handleUnload([this]() {
        QMetaObject::invokeMethod(this, [this] {
            Q_EMIT walletUnloaded();
        }, Qt::QueuedConnection);
    });
}

void WalletQmlModel::unsubscribeFromWalletSignals()
{
    if (m_handler_status_changed) {
        m_handler_status_changed->disconnect();
    }
    if (m_handler_address_list_changed) {
        m_handler_address_list_changed->disconnect();
    }
    if (m_handler_transaction_changed) {
        m_handler_transaction_changed->disconnect();
    }
    if (m_handler_unload) {
        m_handler_unload->disconnect();
    }
}

void WalletQmlModel::refreshSecurityState()
{
    const bool encrypted = m_wallet ? m_wallet->isCrypted() : false;
    const bool locked = m_wallet ? m_wallet->isLocked() : false;
    if (m_is_encrypted != encrypted || m_is_locked != locked) {
        m_is_encrypted = encrypted;
        m_is_locked = locked;
        Q_EMIT securityStateChanged();
    }
}

bool WalletQmlModel::unlockForAction(std::optional<SecureString>& passphrase, bool& relock)
{
    relock = false;
    if (!m_wallet) {
        if (passphrase.has_value()) {
            QmlUtil::ClearSecureString(*passphrase);
            passphrase.reset();
        }
        return true;
    }
    if (!passphrase.has_value()) {
        // Either the wallet is unlocked already (action proceeds), or it isn't
        // and the caller asked for an unlock-less attempt — let the action fail
        // downstream rather than blocking here.
        return true;
    }

    const auto result{TryUnlockWithPassphrase(*m_wallet, *passphrase)};
    passphrase.reset();
    switch (result) {
    case WalletUnlockResult::IncorrectPassphrase:
        setTransactionStatus(tr("The wallet password you entered was incorrect."));
        return false;
    case WalletUnlockResult::AlreadyUnlocked:
        return true;
    case WalletUnlockResult::UnlockedNowRelockRequired:
        relock = true;
        refreshSecurityState();
        return true;
    }
    return false;
}

void WalletQmlModel::clearTransactionStatus()
{
    setTransactionStatus(QString());
}

void WalletQmlModel::setTransactionStatus(const QString& error, bool needs_unlock)
{
    if (m_transaction_error != error) {
        m_transaction_error = error;
        Q_EMIT transactionErrorChanged();
    }
    if (m_transaction_needs_unlock != needs_unlock) {
        m_transaction_needs_unlock = needs_unlock;
        Q_EMIT transactionNeedsUnlockChanged();
    }
}

void WalletQmlModel::setSettingsError(const QString& error)
{
    if (m_settings_error != error) {
        m_settings_error = error;
        Q_EMIT settingsErrorChanged();
    }
}

QString WalletQmlModel::persistedReceiveAddressTypeKey() const
{
    return QStringLiteral("receiveAddressTypes/%1").arg(name());
}
