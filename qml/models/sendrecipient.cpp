// Copyright (c) 2025-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/sendrecipient.h>
#include <qml/guipresentation.h>
#include <qml/models/walletqmlmodel.h>
#include <qml/retainedvalue.h>
#include <key_io.h>
#include <policy/feerate.h>
#include <policy/policy.h>
#include <QPointer>
#include <atomic>
#include <mutex>
#include <stdexcept>

namespace {
std::atomic<quint64> NEXT_RECIPIENT{0};
bool SameText(const QString& a, const QString& b) { return a.size() == b.size() && (a.isEmpty() || a.constData() == b.constData()); }
}
struct SendRecipient::Input {
    std::mutex mutex;
    Snapshot draft;
    bool initialized{false}, amount_set{false}, wallet{false}, balance_known{false};
    CAmount balance{0};
    CFeeRate dust{DUST_RELAY_TX_FEE};
};
struct SendRecipient::Retired {
    std::shared_ptr<Input> input;
    std::shared_ptr<execution::SnapshotQuery<std::shared_ptr<const Snapshot>>> query;
    std::shared_ptr<const Snapshot> validated;
    QString address_error, amount_error, label, message, request_label, request_address;
};
SendRecipient::SendRecipient(WalletQmlModel* wallet, QObject* parent)
    : QObject(parent), m_wallet(wallet), m_identity(++NEXT_RECIPIENT), m_input(std::make_shared<Input>()),
      m_address(new BitcoinAddress(this)), m_amount(new BitcoinAmount(this))
{
    connect(m_address, &BitcoinAddress::addressChanged, this, [this] {
        if (closed()) return;
        const QPointer<SendRecipient> guard{this};
        Q_EMIT addressChanged(); if (guard && !closed()) requestValidation();
    });
    connect(m_address, &BitcoinAddress::normalizationChanged, this, [this] {
        // A closed recipient ignores its address editor.
        if (closed()) return;
        const QPointer<SendRecipient> guard{this};
        // The address field also requests canonical formatting on blur. An
        // unchanged address is presentation, not a new user intent. Changes
        // during a payment import belong to the import.
        const auto address = m_address->address();
        const bool input_changed = address != m_address_intent_value;
        m_address_intent_value = address;
        if (input_changed && !m_batching && !inputLocked()) Q_EMIT inputIntentSubmitted();
        if (guard && !closed()) requestValidation();
    });
    connect(m_amount, &BitcoinAmount::valueEdited, this, &SendRecipient::captureAmountIntent);
    connect(m_amount, &BitcoinAmount::amountChanged, this, &SendRecipient::requestValidation);
    connect(m_amount, &BitcoinAmount::inputChanged, this, &SendRecipient::requestValidation);
    if (m_wallet) {
        connect(m_wallet, &WalletQmlModel::balanceChanged, this, &SendRecipient::requestValidation);
        connect(m_wallet, &WalletQmlModel::balanceObservationChanged, this, &SendRecipient::requestValidation);
        connect(m_wallet, &WalletQmlModel::sendFactsChanged, this, &SendRecipient::requestValidation);
    }
}
SendRecipient::~SendRecipient() { close(); }
bool SendRecipient::inputLocked() const { return m_closed || (m_importing && m_importing->load()); }
void SendRecipient::setOpening(std::shared_ptr<walletopening::Session> opening, execution::SnapshotServices services)
{
    if (closed() || (m_opening && m_opening != opening) || m_query) return;
    m_opening = std::move(opening); m_services = std::move(services);
    const auto setup = ++m_setup_revision;
    if (!m_services.scheduler && m_opening) m_services.scheduler = m_opening->scheduler();
    const QPointer<SendRecipient> observer{this};
    if (!m_services.post) m_services.post = presentation::QueuedPost();
    const auto original_opening = m_opening;
    const auto ports = m_services;
    const auto input = m_input;
    const auto current = [observer, setup, original_opening, scheduler = ports.scheduler] {
        return observer && !observer->closed() && observer->m_setup_revision == setup &&
            observer->m_opening == original_opening && observer->m_services.scheduler == scheduler;
    };
    if (!current() || !ports.scheduler) return;
    auto retired = execution::RetainValues<Retired>(ports.scheduler, [] { return std::make_unique<Retired>(); });
    if (!current()) return;
    if (!retired) { m_addressError = tr("Recipient cleanup is unavailable."); return; }
    m_retired = std::move(retired);
    m_query = std::make_shared<execution::SnapshotQuery<std::shared_ptr<const Snapshot>>>(ports.scheduler,
        [input, scheduler = ports.scheduler] {
            Snapshot draft; bool amount_set, wallet, balance_known; CAmount balance; CFeeRate dust;
            { std::lock_guard lock{input->mutex}; draft = input->draft; amount_set = input->amount_set; wallet = input->wallet;
              balance_known = input->balance_known; balance = input->balance; dust = input->dust; }
            auto value = execution::RetainValues<Snapshot>(scheduler, [&] {
                auto result = std::make_unique<Snapshot>(std::move(draft));
                if (result->payment_request && result->address != result->request_address) {
                    result->payment_request = false; result->request_address.clear();
                    result->request_label.clear(); result->message.clear();
                }
                result->destination = DecodeDestination(result->address.toStdString());
                result->validation_pending = false; // This is the validated result.
                const bool decoded_valid = IsValidDestination(result->destination);
                result->address_valid = decoded_valid;
                if (!result->address.isEmpty() && !decoded_valid) {
                    if (IsValidDestinationString(result->address.toStdString(), *CChainParams::Main())) result->address_error = tr("Address is valid for mainnet, not the current network");
                    else if (IsValidDestinationString(result->address.toStdString(), *CChainParams::TestNet())) result->address_error = tr("Address is valid for testnet, not the current network");
                    else result->address_error = tr("Invalid address format");
                }
                if (amount_set) {
                    if (result->amount <= 0) result->amount_error = tr("Amount must be greater than zero");
                    else if (result->amount > MAX_MONEY) result->amount_error = tr("Amount exceeds maximum limit of 21,000,000 BTC");
                    else if (result->address_valid && IsDust(CTxOut{result->amount, GetScriptForDestination(result->destination)}, dust)) result->amount_error = tr("Amount is too small to send.");
                    else if (wallet && balance_known && result->amount > balance) result->amount_error = tr("Amount exceeds available balance");
                }
                result->valid = (!wallet || balance_known) && result->input_acceptable && result->address_valid && result->amount_error.isEmpty() && result->amount > 0;
                return result;
            });
            if (!value) throw std::runtime_error{"Recipient validation cleanup unavailable"};
            return std::shared_ptr<const Snapshot>{std::move(value)};
        }, [post = ports.post, observer, setup] { post([observer, setup] { if (observer && observer->m_setup_revision == setup) observer->publishValidation(); }); }, execution::Lane::Domain);
    { std::lock_guard lock{input->mutex}; input->initialized = false; }
    requestValidation();
}
void SendRecipient::close()
{
    if (m_closed) return;
    m_closed = true; if (m_query) m_query->close();
    if (auto retired = std::move(m_retired)) {
        retired->input = std::move(m_input); retired->query = std::move(m_query); retired->validated = std::move(m_validated);
        retired->address_error = std::move(m_addressError); retired->amount_error = std::move(m_amountError);
        retired->label = std::move(m_label); retired->message = std::move(m_message);
        retired->request_label = std::move(m_request_label); retired->request_address = std::move(m_request_address);
    }
}
void SendRecipient::retryPublications()
{
    if (closed()) return;
    const QPointer<SendRecipient> guard{this};
    publishValidation(); if (!guard || closed()) return;
    if (m_query && !m_pending && (!m_validated || m_validated->revision != m_revision)) {
        const QPointer<SendRecipient> retry_guard{this};
        const auto query = m_query;
        const auto revision = m_revision, setup = m_setup_revision;
        m_pending = true; query->request();
        if (!retry_guard || closed() || m_revision != revision || m_setup_revision != setup) return;
        Q_EMIT draftChanged(); if (retry_guard && !closed()) Q_EMIT isValidChanged();
    }
}
SendRecipient::Snapshot SendRecipient::draft() const
{
    if (!validationPending() && m_validated && m_validated->revision == m_revision) return *m_validated;
    return editorDraft();
}
SendRecipient::Snapshot SendRecipient::editorDraft() const
{
    Snapshot value; value.token = m_identity; value.revision = m_revision; value.amount_intent_revision = m_amount_intent_revision; value.address = m_address->address();
    value.label = m_label; value.message = m_message; value.request_label = m_request_label; value.request_address = m_request_address; value.payment_request = hasPaymentRequest();
    value.mode = m_amount_mode == AmountMode::Maximum ? senddraft::AmountMode::Maximum : senddraft::AmountMode::Fixed;
    value.amount_set = m_amount->isSet();
    value.validation_pending = validationPending();
    value.amount = m_amount->satoshi(); value.input_acceptable = m_amount->inputAcceptable() && m_budget_error.isEmpty(); value.input_error = m_budget_error.isEmpty() ? m_amount->inputError() : m_budget_error;
    return value;
}
void SendRecipient::requestValidation()
{
    if (closed() || m_batching || (m_opening && !m_opening->available())) return;
    // Capture this editor intent before comparing it with the last request.
    // The published draft still belongs to the previous validated revision.
    auto value = editorDraft();
    value.valid = value.address_valid = false; value.destination = CNoDestination{};
    value.address_error.clear(); value.amount_error.clear();
    const bool amount_set = m_amount->isSet(), wallet = bool(m_wallet);
    const bool known = wallet && m_wallet->balanceKnown();
    const auto balance = wallet ? m_wallet->displayedBalanceSatoshi() : 0;
    const auto dust = wallet ? m_wallet->dustRelayFee() : CFeeRate{DUST_RELAY_TX_FEE};
    {
        std::lock_guard lock{m_input->mutex};
        const auto& old = m_input->draft;
        if (m_input->initialized && old.address == value.address && SameText(old.label, value.label) && SameText(old.message, value.message) &&
            SameText(old.request_label, value.request_label) && old.request_address == value.request_address && old.payment_request == value.payment_request && old.amount == value.amount && old.amount_intent_revision == value.amount_intent_revision &&
            old.input_acceptable == value.input_acceptable && old.input_error == value.input_error &&
            m_input->amount_set == amount_set && m_input->wallet == wallet && m_input->balance_known == known &&
            m_input->balance == balance && m_input->dust.GetFeePerK() == dust.GetFeePerK()) return;
        value.revision = ++m_revision; m_pending = true;
        m_input->draft = std::move(value); m_input->initialized = true;
        m_input->amount_set = amount_set; m_input->wallet = wallet; m_input->balance_known = known;
        m_input->balance = balance; m_input->dust = dust;
    }
    const QPointer<SendRecipient> guard{this};
    const auto query = m_query;
    const auto revision = m_revision, setup = m_setup_revision;
    if (query) query->request();
    if (!guard || closed() || m_revision != revision || m_setup_revision != setup) return;
    Q_EMIT draftChanged(); if (!guard || closed()) return;
    Q_EMIT amountErrorChanged(); if (guard && !closed()) Q_EMIT isValidChanged();
}
void SendRecipient::publishValidation()
{
    if (closed() || !m_query) return;
    const auto result = m_query->take(); if (!result) return;
    const auto value = result->snapshot ? *result->snapshot : std::shared_ptr<const Snapshot>{};
    if (value && value->revision != m_revision) return;
    if (!m_pending && value && m_validated && value->revision == m_validated->revision) return;
    m_pending = false; m_validated = value;
    const bool provenance_changed = value && (m_request_address != value->request_address || m_request_label != value->request_label || m_message != value->message);
    if (value) { m_request_address = value->request_address; m_request_label = value->request_label; m_message = value->message; }
    m_addressError = value ? value->address_error : tr("Recipient validation could not be completed.");
    m_amountError = value ? value->amount_error : QString{};
    const QPointer<SendRecipient> guard{this};
    if (provenance_changed) { Q_EMIT paymentRequestChanged(); if (!guard || closed()) return; Q_EMIT messageChanged(); if (!guard || closed()) return; }
    Q_EMIT addressErrorChanged(); if (!guard || closed()) return;
    Q_EMIT amountErrorChanged(); if (!guard || closed()) return;
    Q_EMIT draftChanged(); if (guard && !closed()) Q_EMIT isValidChanged();
}
BitcoinAddress* SendRecipient::address() const { return m_address; }
void SendRecipient::setAddress(const QString& address) { if (!inputLocked()) m_address->setAddress(address, 0); }
QString SendRecipient::addressError() const { return m_addressError; }
QString SendRecipient::label() const { return m_label; }
bool SendRecipient::editPrivateNote(const QString& value)
{
    if (inputLocked()) return false;
    const bool accepted = value.size() <= senddraft::Limits::note;
    const QPointer<SendRecipient> guard{this}; setLabel(value);
    return guard && !inputLocked() && accepted;
}
void SendRecipient::setLabel(const QString& label)
{
    if (inputLocked()) return;
    if (label.size() > senddraft::Limits::note) { m_budget_error = tr("Private note exceeds the 256-character limit."); const QPointer<SendRecipient> guard{this}; Q_EMIT inputIntentSubmitted(); if (guard && !inputLocked()) requestValidation(); return; }
    const bool changed = !SameText(m_label, label), had_error = !m_budget_error.isEmpty();
    m_budget_error.clear(); if (!changed && !had_error) return;
    m_label = label; const QPointer<SendRecipient> guard{this};
    if (!m_batching) Q_EMIT inputIntentSubmitted();
    if (!guard || inputLocked()) return;
    if (changed) Q_EMIT labelChanged();
    if (guard && !inputLocked()) requestValidation();
}
BitcoinAmount* SendRecipient::amount() const { return m_amount; }
void SendRecipient::setAmount(const QString& amount) { if (!inputLocked()) m_amount->fromDisplay(amount); }
QString SendRecipient::amountError() const { return m_amount->inputAcceptable() ? m_amountError : m_amount->inputError(); }
void SendRecipient::setAmountError(const QString& error) { if (!closed() && m_amountError != error) { m_amountError = error; Q_EMIT amountErrorChanged(); } }
QString SendRecipient::message() const { return m_message; }
void SendRecipient::setMessage(const QString& message)
{
    if (inputLocked()) return;
    if (message.size() > senddraft::Limits::message) { m_budget_error = tr("Payment message exceeds the 2048-character limit."); requestValidation(); return; }
    m_budget_error.clear(); if (SameText(m_message, message)) { requestValidation(); return; }
    m_message = message; const QPointer<SendRecipient> guard{this};
    if (!m_batching) Q_EMIT inputIntentSubmitted();
    if (!guard || inputLocked()) return;
    Q_EMIT messageChanged();
    if (guard && !inputLocked()) requestValidation();
}
CAmount SendRecipient::cAmount() const { return m_amount->satoshi(); }
void SendRecipient::captureAmountIntent()
{
    if (inputLocked()) return;
    ++m_amount_intent_revision;
    if (!m_batching) Q_EMIT inputIntentSubmitted();
}
void SendRecipient::chooseFixedAmount()
{
    const QPointer<SendRecipient> guard{this}; captureAmountIntent();
    if (guard && !closed()) requestValidation();
}
void SendRecipient::chooseMaximum()
{
    if (!inputLocked()) Q_EMIT amountModeRequested(AmountMode::Maximum);
}
void SendRecipient::projectAmountMode(senddraft::AmountMode mode)
{
    const auto value = mode == senddraft::AmountMode::Maximum ? AmountMode::Maximum : AmountMode::Fixed;
    if (closed() || m_amount_mode == value) return;
    m_amount_mode = value; Q_EMIT amountModeChanged();
}
void SendRecipient::clearImportedMetadata()
{
    if (inputLocked()) return;
    m_request_address.clear(); m_request_label.clear(); m_message.clear();
    const QPointer<SendRecipient> guard{this}; Q_EMIT inputIntentSubmitted();
    if (!guard || inputLocked()) return;
    Q_EMIT paymentRequestChanged();
    if (!guard || inputLocked()) return;
    Q_EMIT messageChanged(); if (guard && !inputLocked()) requestValidation();
}
void SendRecipient::applyPaymentRequest(const QString& address, const QString& pay_to, const QString& message)
{
    applyImportedPayment({address, pay_to, message, {}, {}, false});
}
void SendRecipient::projectImportedPayment(const Snapshot& value, const QString& formatted_address,
                                          bool amount_changed, bool private_note_changed)
{
    if (closed() || value.token != m_identity) return;
    const QPointer<SendRecipient> guard{this};
    ++m_import_sequence;
    m_batching = true;
    if (private_note_changed) m_budget_error.clear();
    m_request_address = value.request_address; m_request_label = value.request_label;
    m_message = value.message; m_label = value.label;
    m_amount_intent_revision = value.amount_intent_revision;
    m_address->projectAddress(value.address, formatted_address);
    if (!guard || closed()) return;
    // An omitted URI amount preserves the raw editor, including incomplete
    // text and Maximum intent. Only an explicit imported amount replaces it.
    if (amount_changed) m_amount->setSatoshi(value.amount, BitcoinAmount::Origin::Model);
    if (!guard || closed()) return;
    projectAmountMode(value.mode);
    if (!guard || closed()) return;
    m_batching = false;
    Q_EMIT paymentRequestChanged(); if (!guard || closed()) return;
    Q_EMIT messageChanged(); if (!guard || closed()) return;
    Q_EMIT labelChanged(); if (guard && !closed()) requestValidation();
}
void SendRecipient::applyImportedPayment(const senddraft::PaymentRequest& request)
{
    if (inputLocked() || (m_opening && !m_opening->available())) return;
    if (request.label.size() > senddraft::Limits::note || request.message.size() > senddraft::Limits::message ||
        request.private_note.size() > senddraft::Limits::note || request.address.size() > 1024 ||
        (request.amount && !MoneyRange(*request.amount))) {
        m_budget_error = tr("The payment request exceeds the Send draft input limits."); requestValidation(); return;
    }
    const QPointer<SendRecipient> guard{this};
    const auto sequence = ++m_import_sequence;
    m_batching = true; m_budget_error.clear();
    if (request.amount) chooseFixedAmount();
    if (!guard || inputLocked() || m_import_sequence != sequence) return;
    const auto amount_intent = m_amount_intent_revision;
    // The canonical address editor owns normalization. All imported public
    // fields belong to this one request; private text changes only with approval.
    m_request_address = request.address; m_request_label = request.label; m_message = request.message;
    const bool note_changed = !request.private_note.isEmpty() && (m_label.isEmpty() || request.replace_private_note);
    if (note_changed) m_label = request.private_note;
    setAddress(request.address); if (!guard || inputLocked() || m_import_sequence != sequence) return;
    // The request's address is the editor's normalized form of it.
    m_request_address = m_address->address();
    if (request.amount && m_amount_intent_revision == amount_intent) m_amount->setSatoshi(*request.amount, BitcoinAmount::Origin::Model);
    if (!guard || inputLocked() || m_import_sequence != sequence) return;
    m_batching = false;
    Q_EMIT inputIntentSubmitted(); if (!guard || inputLocked()) return;
    Q_EMIT paymentRequestChanged(); if (!guard || inputLocked()) return;
    Q_EMIT messageChanged(); if (!guard || inputLocked()) return;
    if (note_changed) Q_EMIT labelChanged();
    if (guard && !inputLocked()) requestValidation();
}
void SendRecipient::applyParsedPaymentRequest(const QVariantMap& parsed, bool replace_private_note)
{
    if (!parsed.value("success").toBool()) return;
    senddraft::PaymentRequest request;
    request.address = parsed.value("address").toString();
    if (parsed.value("hasLabel").toBool()) request.label = parsed.value("label").toString();
    if (parsed.value("hasMessage").toBool()) request.message = parsed.value("uriMessage").toString();
    if (parsed.value("hasAmount").toBool()) request.amount = parsed.value("amountSats").toLongLong();
    request.private_note = parsed.value("privateNote").toString(); request.replace_private_note = replace_private_note;
    applyImportedPayment(request);
}
void SendRecipient::clear()
{
    if (inputLocked()) return;
    m_request_address.clear(); m_request_label.clear(); m_label.clear(); m_message.clear(); m_budget_error.clear();
    ++m_import_sequence; m_batching = true; const QPointer<SendRecipient> guard{this};
    m_address->setAddress("", 0); if (!guard || inputLocked()) return;
    m_amount->clear(); if (!guard || inputLocked()) return;
    m_batching = false; Q_EMIT inputIntentSubmitted(); if (!guard || inputLocked()) return;
    Q_EMIT paymentRequestChanged(); if (!guard || inputLocked()) return;
    Q_EMIT labelChanged(); if (!guard || inputLocked()) return;
    Q_EMIT messageChanged(); if (guard && !inputLocked()) requestValidation();
}
void SendRecipient::validateAddress() { requestValidation(); }
bool SendRecipient::isValid() const { return !closed() && (!m_wallet || m_wallet->available()) && !validationPending() && m_validated && m_validated->revision == m_revision && m_validated->valid; }
