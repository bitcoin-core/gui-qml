// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_SIGNVERIFYMESSAGEMODEL_H
#define BITCOIN_QML_MODELS_SIGNVERIFYMESSAGEMODEL_H

#include <qml/walletmessage.h>
#include <qml/models/messagesigningprompt.h>
#include <qml/models/texteditsessionmodel.h>
#include <qml/walletopening.h>
#include <qml/snapshotquery.h>
#include <qml/guipresentation.h>
#include <QObject>
#include <QString>

class SignVerifyMessageModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString signingError READ signingError NOTIFY signingErrorChanged)
    Q_PROPERTY(bool signingNeedsUnlock READ signingNeedsUnlock NOTIFY signingNeedsUnlockChanged)
    Q_PROPERTY(MessageSigningPrompt* signingPrompt READ signingPrompt NOTIFY signingPromptChanged)
    Q_PROPERTY(TextEditInput* address READ address CONSTANT)
    Q_PROPERTY(TextEditInput* message READ message CONSTANT)
    Q_PROPERTY(TextEditInput* verificationAddressInput READ verificationAddressInput CONSTANT)
    Q_PROPERTY(TextEditInput* verificationMessageInput READ verificationMessageInput CONSTANT)
    Q_PROPERTY(TextEditInput* candidateSignature READ candidateSignature CONSTANT)
    Q_PROPERTY(SigningState phase READ signingState NOTIFY signingStateChanged)
    Q_PROPERTY(bool canSign READ canSign NOTIFY capabilitiesChanged)
    Q_PROPERTY(bool canVerify READ canVerify NOTIFY capabilitiesChanged)
    Q_PROPERTY(MessageSigningError error READ error NOTIFY signingStateChanged)
    Q_PROPERTY(MessageVerificationSnapshot verification READ verification NOTIFY verificationChanged)
    Q_PROPERTY(QString signature READ signature NOTIFY signatureChanged)
    Q_PROPERTY(SigningState signingState READ signingState NOTIFY signingStateChanged)
    Q_PROPERTY(bool signingBusy READ signingBusy NOTIFY signingStateChanged)
    Q_PROPERTY(bool signingSucceeded READ signingSucceeded NOTIFY signingStateChanged)
    Q_PROPERTY(quint64 signingId READ signingId NOTIFY signingStateChanged)
    Q_PROPERTY(VerificationState verificationState READ verificationState NOTIFY verificationChanged)
    Q_PROPERTY(bool verificationBusy READ verificationBusy NOTIFY verificationChanged)
    Q_PROPERTY(bool verificationValid READ verificationValid NOTIFY verificationChanged)
    Q_PROPERTY(quint64 verificationId READ verificationId NOTIFY verificationChanged)
    Q_PROPERTY(QString verificationText READ verificationText NOTIFY verificationChanged)
    Q_PROPERTY(QString signingAddress READ signingAddress WRITE setSigningAddress NOTIFY inputsChanged)
    Q_PROPERTY(QString signingMessage READ signingMessage WRITE setSigningMessage NOTIFY inputsChanged)
    Q_PROPERTY(QString verificationAddress READ verificationAddress WRITE setVerificationAddress NOTIFY inputsChanged)
    Q_PROPERTY(QString verificationMessage READ verificationMessage WRITE setVerificationMessage NOTIFY inputsChanged)
    Q_PROPERTY(QString verificationSignature READ verificationSignature WRITE setVerificationSignature NOTIFY inputsChanged)
    Q_PROPERTY(bool signingAddressValid READ signingAddressValid NOTIFY addressValidationChanged)
    Q_PROPERTY(bool verificationAddressValid READ verificationAddressValid NOTIFY addressValidationChanged)
    Q_PROPERTY(bool addressValidationPending READ addressValidationPending NOTIFY addressValidationChanged)

public:
    enum SigningState { SigningIdle, SigningPending, SigningDiscarding, SigningNeedsUnlock, SigningSucceeded, SigningFailed, SigningUncertain, SigningClosed };
    Q_ENUM(SigningState)
    enum VerificationState { VerificationIdle, VerificationPending, VerificationDiscarding, VerificationValid, VerificationInvalid, VerificationFailed, VerificationClosed };
    Q_ENUM(VerificationState)
    using SecurityStateChangedFn = std::function<void()>;
    explicit SignVerifyMessageModel(QObject* parent = nullptr);
    void setOpening(std::shared_ptr<walletopening::Session> opening, presentation::Post post,
                    std::shared_ptr<execution::SerialQueue> outcomes = {});
    void close();
    void retryPublications();
    void setSecurityStateChangedFn(SecurityStateChangedFn fn);

    QString signingError() const { return m_signing_error; }
    bool signingNeedsUnlock() const { return m_signing_needs_unlock; }
    MessageSigningPrompt* signingPrompt() const { return m_prompt; }
    TextEditInput* address() { return &m_address_input_text; }
    TextEditInput* message() { return &m_message_input_text; }
    TextEditInput* verificationAddressInput() { return &m_verify_address_input_text; }
    TextEditInput* verificationMessageInput() { return &m_verify_message_input_text; }
    TextEditInput* candidateSignature() { return &m_verify_signature_input_text; }
    bool canSign() const { return !m_closed && !m_sign_active && !m_address_pending && m_sign_address_valid; }
    bool canVerify() const { return !m_closed && !m_verify_active && !m_address_pending && m_verify_address_valid; }
    MessageSigningError error() const;
    MessageVerificationSnapshot verification() const;
    Q_INVOKABLE QString sign();
    Q_INVOKABLE QString verify(const QString& signature);
    Q_INVOKABLE void cancel();
    QString signature() const { return m_signature; }
    SigningState signingState() const { return m_signing_state; }
    bool signingBusy() const { return m_sign_active; }
    bool signingSucceeded() const { return m_signing_state == SigningSucceeded; }
    quint64 signingId() const { return m_sign_id; }
    VerificationState verificationState() const { return m_verification_state; }
    bool verificationBusy() const { return m_verify_active; }
    bool verificationValid() const { return m_verification_state == VerificationValid; }
    quint64 verificationId() const { return m_verify_id; }
    QString verificationText() const;
    QString signingAddress() const { return m_sign_address; }
    QString signingMessage() const { return m_sign_message; }
    QString verificationAddress() const { return m_verify_address; }
    QString verificationMessage() const { return m_verify_message; }
    QString verificationSignature() const { return m_verify_signature; }
    void setSigningAddress(const QString& value);
    void setSigningMessage(const QString& value);
    void setVerificationAddress(const QString& value);
    void setVerificationMessage(const QString& value);
    void setVerificationSignature(const QString& value);
    bool signingAddressValid() const { return m_sign_address_valid; }
    bool verificationAddressValid() const { return m_verify_address_valid; }
    bool addressValidationPending() const { return m_address_pending; }
    std::optional<walletmessage::SignResult> signingResult() const;
    std::shared_ptr<const execution::OwnedCommand<walletmessage::SignResult>> signingOperation() const { return m_sign_operation; }
    std::shared_ptr<const execution::OwnedCommand<walletmessage::VerifyResult>> verificationOperation() const { return m_verify_operation; }

    Q_INVOKABLE quint64 requestSignMessage(const QString& address, const QString& message);
    Q_INVOKABLE quint64 requestSignMessageWithPassphrase(const QString& address, const QString& message, const QString& passphrase);
    Q_INVOKABLE quint64 requestVerifyMessage(const QString& address, const QString& message, const QString& signature);
    bool isLegacyP2PKHAddress(const QString& address) const;
    Q_INVOKABLE void clear();
    Q_INVOKABLE void clearSigningStatus();
    Q_INVOKABLE void clearVerification();
    Q_INVOKABLE void cancelSigningPrompt();

Q_SIGNALS:
    void signingErrorChanged();
    void signingNeedsUnlockChanged();
    void signingPromptChanged();
    void signatureChanged();
    void signingStateChanged();
    void verificationChanged();
    void inputsChanged();
    void addressValidationChanged();
    void capabilitiesChanged();
    void commandRejected(const QString& command_id, const MessageSigningError& error);

private:
    struct AddressInput;
    struct AddressChecks;
    quint64 sign(std::optional<SecureString> passphrase, secretinput::Captured captured = {});
    void publishSigning(quint64 id, quint64 revision);
    void publishVerification(quint64 id, quint64 revision);
    void publishAddressValidation();
    void requestAddressValidation();
    void invalidateSigning();
    void invalidateVerification();
    bool notifySigning();
    bool notifyVerification();
    void updateSigningPrompt();
    void retireSigningPrompt();

    std::shared_ptr<walletopening::Session> m_opening;
    presentation::Post m_post;
    std::shared_ptr<AddressInput> m_address_input;
    execution::SnapshotQuery<AddressChecks>::Post m_post_observation;
    std::shared_ptr<execution::SnapshotQuery<AddressChecks>> m_address_query;
    std::shared_ptr<execution::OwnedCommand<walletmessage::SignResult>> m_sign_operation;
    std::shared_ptr<execution::OwnedCommand<walletmessage::VerifyResult>> m_verify_operation;
    MessageSigningPrompt* m_prompt{nullptr};
    TextEditInput m_address_input_text{this}, m_message_input_text{this}, m_verify_address_input_text{this},
        m_verify_message_input_text{this}, m_verify_signature_input_text{this};
    SecurityStateChangedFn m_security_state_changed;
    QString m_signing_error, m_signature;
    QString m_sign_address, m_sign_message, m_verify_address, m_verify_message, m_verify_signature;
    bool m_signing_needs_unlock{false}, m_closed{false}, m_sign_active{false}, m_verify_active{false};
    bool m_sign_address_valid{false}, m_verify_address_valid{false}, m_address_pending{false};
    quint64 m_prompt_revision{0}, m_sign_id{0}, m_verify_id{0}, m_sign_revision{0}, m_verify_revision{0}, m_address_revision{0};
    quint64 m_sign_operation_revision{0}, m_verify_operation_revision{0};
    MessageSigningError m_error;
    SigningState m_signing_state{SigningIdle};
    VerificationState m_verification_state{VerificationIdle};
};

#endif // BITCOIN_QML_MODELS_SIGNVERIFYMESSAGEMODEL_H
