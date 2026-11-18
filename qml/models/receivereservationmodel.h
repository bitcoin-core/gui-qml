// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_QML_MODELS_RECEIVERESERVATIONMODEL_H
#define BITCOIN_QML_MODELS_RECEIVERESERVATIONMODEL_H

#include <qml/walletrequests.h>
#include <qml/models/texteditsessionmodel.h>
#include <qml/secretinput.h>

#include <QPointer>

class ReceiveReservationModel;
class ReceiveAuthorizationPrompt final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool active READ active NOTIFY changed)
    Q_PROPERTY(QString errorText READ errorText NOTIFY changed)
public:
    ReceiveAuthorizationPrompt(ReceiveReservationModel* owner, QString token, QObject* parent);
    QString token() const { return m_token; }
    bool active() const;
    QString errorText() const;
    Q_INVOKABLE void answer(const QString& passphrase);
    Q_INVOKABLE void cancel();
    void invalidate();
Q_SIGNALS:
    void changed();
private:
    const QPointer<ReceiveReservationModel> m_owner;
    const QString m_token;
    bool m_active{true};
};

/** GUI projection of the sole Store reservation/generation writer. A cancelled
 * question owns no Wallet or secret; already admitted effects still drain. */
class ReceiveReservationModel final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(Phase phase READ phase NOTIFY changed)
    Q_PROPERTY(QString reservationId READ reservationId NOTIFY changed)
    Q_PROPERTY(QString address READ address NOTIFY changed)
    Q_PROPERTY(QString addressType READ addressType NOTIFY changed)
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(bool historicallyUsed READ historicallyUsed NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(TextEditError error READ error NOTIFY changed)
    Q_PROPERTY(ReceiveAuthorizationPrompt* prompt READ prompt NOTIFY changed)
    Q_PROPERTY(ReceiveError::Code generationError READ generationError NOTIFY changed)
    Q_PROPERTY(QString lastGeneratedAddressType READ lastGeneratedAddressType NOTIFY changed)
    Q_PROPERTY(bool generationPersisted READ generationPersisted NOTIFY changed)
    Q_PROPERTY(QString selectedRequestId READ selectedRequestId NOTIFY changed)
    Q_PROPERTY(bool preferenceRequested READ preferenceRequested NOTIFY changed)
    Q_PROPERTY(bool preferenceSaved READ preferenceSaved NOTIFY changed)
public:
    enum Phase { Idle, Generating, Authorizing, Ready, Failed, Cancelling, Cancelled, Closed };
    Q_ENUM(Phase)
    struct Admission {
        std::shared_ptr<const walletrequests::AddressIntent> intent;
        // The adapter reserves background retirement before retaining an
        // operation whose original result can contain large legacy records.
        std::shared_ptr<const void> owner;
        execution::Rejection rejection{execution::Rejection::None};
        explicit operator bool() const { return intent && rejection == execution::Rejection::None; }
    };
    struct Ports {
        execution::SnapshotServices execution;
        std::function<Admission(walletrequests::Command, std::function<void(const walletrequests::Result&)>)> submit;
        // Bounded native QString handoff. The production adapter reserves and
        // materializes its secure input on the injected executor.
        std::function<Admission(walletrequests::Command, const QString&, std::function<void(const walletrequests::Result&)>)> authorize;
    };
    ReceiveReservationModel(uint64_t view, Ports ports, QObject* parent = nullptr);
    Phase phase() const { return m_phase; }
    QString reservationId() const { return m_intent ? QString::number(m_intent->token) : QString{}; }
    QString address() const { return m_address; }
    QString addressType() const { return m_type; }
    bool available() const { return m_phase == Ready && !m_used && m_source_current; }
    bool historicallyUsed() const { return m_used; }
    bool busy() const { return m_phase == Generating || m_phase == Cancelling; }
    TextEditError error() const { return m_error; }
    ReceiveAuthorizationPrompt* prompt() const { return m_prompt; }
    walletrequests::Error generationError() const { return m_last.error; }
    QString lastGeneratedAddressType() const { return m_last.address ? m_last.address->address_type : QString{}; }
    bool generationPersisted() const { return m_last.address && m_last.address->persisted; }
    QString selectedRequestId() const { return m_last.request_loaded && m_last.id > 0 ? QString::number(m_last.id) : QString{}; }
    bool preferenceRequested() const { return m_last.preference_requested; }
    bool preferenceSaved() const { return m_last.preference_saved; }
    const std::shared_ptr<const walletrequests::AddressIntent>& intent() const { return m_intent; }
    uint64_t view() const { return m_view; }
    uint64_t revision() const { return m_revision; }
    bool ensure(bool next, const QString& type, bool explicit_type = false, bool remember_type = false);
    bool select(const QString& address);
    void observe(bool used, bool source_current);
    void retire();
    Q_INVOKABLE void retry();
    Q_INVOKABLE void cancel();
    void authorize(const QString& token, const QString& passphrase);
    bool ownsPrompt(const QString& token) const;
Q_SIGNALS:
    void changed();
    void requestSelected(const QString& request_id);
    void commandRejected(const QString& command, const TextEditError& error);
private:
    bool start(walletrequests::Command command, bool cancellation = false, std::optional<QString> secret = {});
    void complete(uint64_t sequence, const std::shared_ptr<const walletrequests::AddressIntent>& intent,
                  walletrequests::Result result, bool cancellation);
    bool refuse(const QString& command, TextEditError error);
    void revokePrompt();
    const uint64_t m_view;
    Ports m_ports;
    uint64_t m_revision{0}, m_sequence{0};
    Phase m_phase{Idle};
    QString m_address, m_type;
    TextEditError m_error;
    walletrequests::Result m_last;
    std::shared_ptr<const walletrequests::AddressIntent> m_intent;
    std::shared_ptr<const void> m_operation_owner;
    ReceiveAuthorizationPrompt* m_prompt{nullptr};
    bool m_used{false}, m_source_current{true}, m_next{false}, m_explicit_type{false}, m_remember_type{false};
    QString m_requested_type;
};

#endif // BITCOIN_QML_MODELS_RECEIVERESERVATIONMODEL_H
