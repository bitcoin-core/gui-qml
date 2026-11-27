// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_QML_MODELS_WALLETRECEIVEMODEL_H
#define BITCOIN_QML_MODELS_WALLETRECEIVEMODEL_H

#include <qml/models/paymentrequesteditormodel.h>
#include <qml/models/addressreservationmodel.h>
#include <qml/models/requestsharingmodel.h>

/** Opening one saved request in its editor. Pending until the editor shows
 * the request's source, then Opened with that editor, or Failed with why.
 * The caller owns it; cancelling or deleting it stops the open. Once
 * finished it no longer changes. */
class PaymentRequestOpenOperation final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(State state READ state NOTIFY changed)
    Q_PROPERTY(QString requestId READ requestId CONSTANT)
    Q_PROPERTY(PaymentRequestEditorModel* editor READ editor NOTIFY changed)
    Q_PROPERTY(TextEditError error READ error NOTIFY changed)
public:
    enum class State { Pending, Opened, Failed, Cancelled };
    Q_ENUM(State)
    PaymentRequestOpenOperation(QString request_id, QObject* parent) : QObject(parent), m_request_id{std::move(request_id)} {}
    State state() const { return m_state; }
    bool pending() const { return m_state == State::Pending; }
    QString requestId() const { return m_request_id; }
    PaymentRequestEditorModel* editor() const { return m_editor; }
    TextEditError error() const { return m_error; }
    Q_INVOKABLE void cancel() { finish(State::Cancelled); }
Q_SIGNALS:
    void changed();
private:
    friend class WalletReceiveModel;
    void finish(State state, PaymentRequestEditorModel* editor = nullptr, TextEditError error = {})
    {
        if (!pending()) return;
        m_state = state; m_editor = editor; m_error = std::move(error);
        Q_EMIT changed();
    }
    const QString m_request_id;
    State m_state{State::Pending};
    QPointer<PaymentRequestEditorModel> m_editor;
    TextEditError m_error;
};

/** Opening-scoped receive composition. Store remains the single request and
 * reservation writer; this facade captures narrow view/user intent only. */
class WalletReceiveModel final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(PaymentRequestEditorModel* editor READ editor NOTIFY changed)
    Q_PROPERTY(AddressReservationModel* reservation READ reservation CONSTANT)
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(bool replacementPending READ replacementPending NOTIFY changed)
    Q_PROPERTY(TextEditError error READ error NOTIFY changed)
public:
    struct Ports {
        PaymentRequestEditorModel::Ports editor;
        AddressReservationModel::Ports reservation;
        requestshare::Services sharing;
        QPointer<ReceiptReconciliationModel> receipts{};
    };
    WalletReceiveModel(uint64_t view, Ports ports, QObject* parent = nullptr);
    ~WalletReceiveModel() override;
    PaymentRequestEditorModel* editor() const { return m_editor; }
    AddressReservationModel* reservation() { return &m_reservation; }
    bool available() const { return !m_closed; }
    bool replacementPending() const { return m_pending.has_value() || m_repeat.has_value(); }
    TextEditError error() const { return m_error; }
    Q_INVOKABLE bool ensureAddress();
    Q_INVOKABLE bool nextAddress();
    Q_INVOKABLE bool chooseAddressType(const QString& type);
    Q_INVOKABLE bool newRequest();
    Q_INVOKABLE bool useTemplate(const QString& id);
    Q_INVOKABLE bool requestAgain(PaymentRequestEditorModel* original);
    Q_INVOKABLE bool useAddress(const QString& address);
    //! Starts opening saved request \p id, owned by \p owner. Null when it
    //! cannot start (another open is pending); error() says why.
    PaymentRequestOpenOperation* openRequest(const QString& id, QObject* owner);
    //! A delete question for saved request \p id; the caller owns it.
    PaymentRequestEditorModel* deleteRequest(const QString& id);
    Q_INVOKABLE RequestSharingModel* sharing(PaymentRequestEditorModel* editor, QObject* view = nullptr);
    Q_INVOKABLE RequestSharingModel* sharingRequest(const QString& id, QObject* view);
    void refresh();
    void retire();
Q_SIGNALS:
    void changed();
    void existingRequestRequested(const QString& id);
    void requestPersisted(const RequestSaveResult& result);
    void commandRejected(const QString& command, const TextEditError& error);
private:
    enum class Target { Fresh, Template, Address };
    struct Replacement {
        Target kind{Target::Fresh};
        QString original_editor, address, type;
        std::optional<requestedit::Editor::Values> values;
        bool reading{false}, waiting{false}, refresh_pending{false}, closing{false}, cancelling{false};
        int64_t source{0};
        uint64_t token{0};
    };
    PaymentRequestEditorModel* makeEditor(int64_t id, bool erase = false, std::optional<requestedit::Editor::Values> initial = {});
    bool begin(Replacement target);
    void askToReplace();
    void readTemplate();
    void applyReplacement();
    void observeReservation();
    void clearRepeat();
    void readOpenRequest();
    void completeOpenRequest(PaymentRequestEditorModel* editor);
    //! Stops following the pending open, if any.
    void endOpen();
    void failOpen(const TextEditError& error);
    bool refuse(const QString& command, TextEditError error);
    bool owns(PaymentRequestEditorModel* editor) const;
    const uint64_t m_view;
    Ports m_ports;
    AddressReservationModel m_reservation;
    QPointer<PaymentRequestEditorModel> m_editor;
    QList<QPointer<PaymentRequestEditorModel>> m_editors;
    QList<QPointer<RequestSharingModel>> m_sharing;
    struct SharingView { QPointer<QObject> view; QPointer<PaymentRequestEditorModel> editor; QPointer<RequestSharingModel> context; QString id; };
    QList<SharingView> m_sharing_views;
    std::optional<Replacement> m_pending;
    struct Repeat { QString editor, request; uint64_t token; };
    std::optional<Repeat> m_repeat;
    QMetaObject::Connection m_repeat_closed, m_repeat_kept;
    uint64_t m_token{0};
    bool m_closed{false};
    //! The pending open and its request.
    QPointer<PaymentRequestOpenOperation> m_open;
    bool m_open_waiting{false};
    int64_t m_open_id{0};
    QPointer<PaymentRequestEditorModel> m_open_editor;
    QMetaObject::Connection m_open_ready;
    bool m_open_refresh_pending{false};
    bool m_observing{false}, m_observe_again{false};
    TextEditError m_error;
};
#endif // BITCOIN_QML_MODELS_WALLETRECEIVEMODEL_H
