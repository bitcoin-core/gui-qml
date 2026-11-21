// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_QML_MODELS_WALLETRECEIVEMODEL_H
#define BITCOIN_QML_MODELS_WALLETRECEIVEMODEL_H

#include <qml/models/paymentrequesteditormodel.h>
#include <qml/models/receivereservationmodel.h>
#include <qml/models/requestsharingmodel.h>

/** Opening-scoped receive composition. Store remains the single request and
 * reservation writer; this facade captures narrow view/user intent only. */
class WalletReceiveModel final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(PaymentRequestEditorModel* editor READ editor NOTIFY changed)
    Q_PROPERTY(ReceiveReservationModel* reservation READ reservation CONSTANT)
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(bool replacementPending READ replacementPending NOTIFY changed)
    Q_PROPERTY(TextEditError error READ error NOTIFY changed)
public:
    struct Ports {
        PaymentRequestEditorModel::Ports editor;
        ReceiveReservationModel::Ports reservation;
        requestshare::Services sharing;
        QPointer<ReceiptReconciliationModel> receipts{};
    };
    WalletReceiveModel(uint64_t view, Ports ports, QObject* parent = nullptr);
    ~WalletReceiveModel() override;
    PaymentRequestEditorModel* editor() const { return m_editor; }
    ReceiveReservationModel* reservation() { return &m_reservation; }
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
    Q_INVOKABLE bool openRequest(const QString& id);
    Q_INVOKABLE QString beginOpenRequest(const QString& id);
    Q_INVOKABLE void cancelOpenRequest(const QString& token);
    Q_INVOKABLE PaymentRequestEditorModel* deleteRequest(const QString& id);
    Q_INVOKABLE RequestSharingModel* sharing(PaymentRequestEditorModel* editor, QObject* view = nullptr);
    Q_INVOKABLE RequestSharingModel* sharingRequest(const QString& id, QObject* view);
    void refresh();
    void retire();
Q_SIGNALS:
    void changed();
    void requestOpened(PaymentRequestEditorModel* editor);
    void requestViewReady(const QString& token, PaymentRequestEditorModel* editor);
    void requestViewFailed(const QString& token, const TextEditError& error);
    void existingRequestRequested(const QString& id);
    void requestOpenFailed(const TextEditError& error);
    void requestPersisted(const RequestSaveResult& result);
    void commandRejected(const QString& command, const TextEditError& error);
private:
    enum class Target { Fresh, Template, Address };
    struct Replacement {
        Target kind{Target::Fresh};
        QString original_editor, address, type;
        std::optional<requestedit::Editor::Values> values;
        bool reading{false}, closing{false}, cancelling{false};
        uint64_t token{0};
    };
    PaymentRequestEditorModel* makeEditor(int64_t id, bool erase = false, std::optional<requestedit::Editor::Values> initial = {});
    bool begin(Replacement target);
    void askToReplace();
    void applyReplacement();
    void observeReservation();
    void clearRepeat();
    void readOpenRequest();
    void completeOpenRequest(uint64_t token, PaymentRequestEditorModel* editor);
    bool refuse(const QString& command, TextEditError error);
    bool owns(PaymentRequestEditorModel* editor) const;
    const uint64_t m_view;
    Ports m_ports;
    ReceiveReservationModel m_reservation;
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
    bool m_closed{false}, m_open_pending{false};
    bool m_open_waiting{false};
    int64_t m_open_id{0};
    uint64_t m_open_token{0};
    QPointer<PaymentRequestEditorModel> m_open_editor;
    QMetaObject::Connection m_open_ready;
    bool m_open_refresh_pending{false};
    bool m_observing{false}, m_observe_again{false};
    TextEditError m_error;
};
#endif // BITCOIN_QML_MODELS_WALLETRECEIVEMODEL_H
