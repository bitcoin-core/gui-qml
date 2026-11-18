// Copyright (c) 2025-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_SENDRECIPIENT_H
#define BITCOIN_QML_MODELS_SENDRECIPIENT_H

#include <qml/bitcoinamount.h>
#include <qml/models/bitcoinaddress.h>
#include <qml/snapshotquery.h>
#include <qml/walletopening.h>
#include <qml/senddraft.h>

#include <QObject>
#include <QString>
#include <atomic>
#include <memory>

class WalletQmlModel;

class SendRecipient : public QObject
{
    Q_OBJECT
    Q_PROPERTY(BitcoinAddress* address READ address CONSTANT)
    Q_PROPERTY(QString label READ label NOTIFY labelChanged)
    Q_PROPERTY(QString recipientId READ recipientId CONSTANT)
    Q_PROPERTY(AmountMode amountMode READ amountMode NOTIFY amountModeChanged)
    Q_PROPERTY(QString inputBudgetError READ inputBudgetError NOTIFY isValidChanged)
    Q_PROPERTY(int maximumNoteLength READ maximumNoteLength CONSTANT)
    Q_PROPERTY(QString message READ message WRITE setMessage NOTIFY messageChanged)
    Q_PROPERTY(QString paymentRequestLabel READ paymentRequestLabel NOTIFY paymentRequestChanged)
    Q_PROPERTY(bool hasPaymentRequest READ hasPaymentRequest NOTIFY paymentRequestChanged)
    Q_PROPERTY(BitcoinAmount* amount READ amount CONSTANT)

    Q_PROPERTY(QString addressError READ addressError NOTIFY addressErrorChanged)
    Q_PROPERTY(QString amountError READ amountError NOTIFY amountErrorChanged)
    Q_PROPERTY(bool isValid READ isValid NOTIFY isValidChanged)
    Q_PROPERTY(bool validationPending READ validationPending NOTIFY isValidChanged)
    Q_PROPERTY(quint64 validationRevision READ validationRevision NOTIFY isValidChanged)

public:
    enum class AmountMode { Fixed, Maximum };
    Q_ENUM(AmountMode)
    AmountMode amountMode() const { return m_amount_mode; }
    QString recipientId() const { return QString::number(m_identity); }
    QString inputBudgetError() const { return m_budget_error; }
    int maximumNoteLength() const { return int(senddraft::Limits::note); }
    Q_INVOKABLE void chooseFixedAmount();
    Q_INVOKABLE void chooseMaximum();
    Q_INVOKABLE bool editPrivateNote(const QString& value);
    Q_INVOKABLE void clearImportedMetadata();
    void projectAmountMode(senddraft::AmountMode mode);
    void applyImportedPayment(const senddraft::PaymentRequest& request);
    using Snapshot = senddraft::Recipient;
    explicit SendRecipient(WalletQmlModel* wallet, QObject* parent = nullptr);
    ~SendRecipient() override;
    void setOpening(std::shared_ptr<walletopening::Session> opening, execution::SnapshotServices services);
    void closeAdmission();
    /** The owning list sets this flag while it imports a payment request, from
     * its background import work. User input is ignored while it is set. */
    void setImportLock(std::shared_ptr<const std::atomic_bool> importing) { m_importing = std::move(importing); }
    void projectImportedPayment(const senddraft::Recipient& value, const QString& formatted_address,
                                bool amount_changed, bool private_note_changed);
    void retryPublications();
    quint64 identity() const { return m_identity; }
    bool validationPending() const { return !closed() && m_pending; }
    quint64 validationRevision() const { return m_revision; }
    quint64 amountIntentRevision() const { return m_amount_intent_revision; }
    std::shared_ptr<const Snapshot> validationSnapshot() const { return !closed() && !validationPending() && m_validated && m_validated->revision == m_revision ? m_validated : nullptr; }
    Snapshot draft() const;
    Q_INVOKABLE void applyParsedPaymentRequest(const QVariantMap& parsed, bool replace_private_note = false);

    BitcoinAddress* address() const;
    void setAddress(const QString& address);
    QString addressError() const;

    QString label() const;
    void setLabel(const QString& label);

    BitcoinAmount* amount() const;
    void setAmount(const QString& amount);
    QString amountError() const;
    void setAmountError(const QString& error);

    QString message() const;
    void setMessage(const QString& message);

    CAmount cAmount() const;

    bool isValid() const;

    QString paymentRequestLabel() const { return m_request_label; }
    bool hasPaymentRequest() const { return !m_request_address.isEmpty(); }
    Q_INVOKABLE void applyPaymentRequest(const QString& address, const QString& pay_to, const QString& message);
    Q_INVOKABLE void clear();

Q_SIGNALS:
    void inputIntentSubmitted();
    void amountModeRequested(AmountMode mode);
    void amountModeChanged();
    void paymentRequestChanged();
    void addressChanged();
    void addressErrorChanged();
    void amountErrorChanged();
    void labelChanged();
    void messageChanged();
    void isValidChanged();
    void draftChanged();

private:
    bool closed() const { return m_closed; }
    /** User input is ignored once closed or while the list imports a payment request. */
    bool inputLocked() const;
    std::shared_ptr<const std::atomic_bool> m_importing;
    struct Retired;
    std::shared_ptr<Retired> m_retired;
    struct Input;
    Snapshot editorDraft() const;
    void requestValidation();
    void captureAmountIntent();
    void publishValidation();
    void validateAddress();

    const WalletQmlModel* m_wallet;
    const quint64 m_identity;
    quint64 m_revision{0}, m_setup_revision{0}, m_amount_intent_revision{0}, m_import_sequence{0};
    QString m_address_intent_value;
    bool m_pending{true}, m_closed{false}, m_batching{false};
    AmountMode m_amount_mode{AmountMode::Fixed};
    QString m_budget_error;
    std::shared_ptr<walletopening::Session> m_opening;
    execution::SnapshotServices m_services;
    std::shared_ptr<Input> m_input;
    std::shared_ptr<execution::SnapshotQuery<std::shared_ptr<const Snapshot>>> m_query;
    std::shared_ptr<const Snapshot> m_validated;
    BitcoinAddress* m_address;
    QString m_addressError{""};
    QString m_label{""};
    QString m_message{""};
    QString m_request_label;
    QString m_request_address;
    BitcoinAmount* m_amount;
    QString m_amountError{""};
};

#endif // BITCOIN_QML_MODELS_SENDRECIPIENT_H
