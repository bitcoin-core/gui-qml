// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_QML_MODELS_REQUESTSHARINGMODEL_H
#define BITCOIN_QML_MODELS_REQUESTSHARINGMODEL_H

#include <qml/requestsharing.h>
#include <qml/models/requestimageexportoperation.h>
#include <QJSValue>

#include <QImage>
#include <QObject>
#include <QPointer>
#include <QUrl>

#include <atomic>
#include <mutex>

class QQmlEngine;

class ShareResult {
    Q_GADGET
    Q_PROPERTY(QString operationId MEMBER operationId)
    Q_PROPERTY(QString requestId MEMBER requestId)
    Q_PROPERTY(QString payloadRevision MEMBER payloadRevision)
    Q_PROPERTY(bool present MEMBER present)
    Q_PROPERTY(bool admitted MEMBER admitted)
    Q_PROPERTY(bool performed MEMBER performed)
    Q_PROPERTY(bool outcomeKnown MEMBER outcomeKnown)
public:
    QString operationId, requestId, payloadRevision;
    bool present{false}, admitted{false}, performed{false}, outcomeKnown{true};
};
Q_DECLARE_METATYPE(ShareResult)

/** Each returned projection has its own outcome. Native workers own all
 * preparation; a queued GUI handoff retains no Wallet or executor lease. */
class ShareOperationModel final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString operationId READ operationId CONSTANT)
    Q_PROPERTY(Phase phase READ phase NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool outcomeKnown READ outcomeKnown NOTIFY changed)
    Q_PROPERTY(bool canCancel READ canCancel NOTIFY changed)
    Q_PROPERTY(SharingError::Code error READ error NOTIFY changed)
    Q_PROPERTY(QString errorText READ errorText NOTIFY changed)
    Q_PROPERTY(ShareResult result READ result NOTIFY changed)
public:
    enum Phase { Checking, Preparing, Authorizing, Delivering, Cancelling, Completed, Failed, OutcomeUnknown, Cancelled };
    Q_ENUM(Phase)
    ShareOperationModel(requestshare::Kind kind, int64_t request_id, requestshare::Services services,
                        std::shared_ptr<requestshare::View> view, std::optional<requestshare::Source> expected = {}, QObject* parent = nullptr);
    ~ShareOperationModel() override;
    QString operationId() const { return m_id; }
    Phase phase() const;
    bool busy() const;
    bool outcomeKnown() const;
    bool canCancel() const;
    SharingError::Code error() const;
    QString errorText() const;
    ShareResult result() const;
    Q_INVOKABLE void cancel();
Q_SIGNALS:
    void changed();
private:
    struct Cell;
    struct Work;
    void publish();
    const QString m_id;
    const std::shared_ptr<Cell> m_cell;
};

class RequestSharingModel final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString requestId READ requestId CONSTANT)
    Q_PROPERTY(QString payloadRevision READ payloadRevision NOTIFY changed)
    Q_PROPERTY(SharingInterest phase READ phase NOTIFY changed)
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY changed)
    Q_PROPERTY(bool canCopyUri READ canCopyUri NOTIFY changed)
    Q_PROPERTY(bool canCopyQr READ canCopyQr NOTIFY changed)
    Q_PROPERTY(bool canExportQr READ canExportQr NOTIFY changed)
    Q_PROPERTY(QString disabledReason READ disabledReason NOTIFY changed)
    Q_PROPERTY(SharingError::Code error READ error NOTIFY changed)
    Q_PROPERTY(QString paymentUri READ paymentUri NOTIFY changed)
    Q_PROPERTY(QImage image READ image NOTIFY changed)
    Q_PROPERTY(ShareOperationModel* lastOperation READ lastOperation NOTIFY changed)
    //! The latest QR image export; this model owns it.
    Q_PROPERTY(RequestImageExportOperation* qrExport READ qrExport NOTIFY qrExportChanged)
public:
    enum SharingInterest { Inactive, Active, Closed };
    Q_ENUM(SharingInterest)
    RequestSharingModel(int64_t id, requestshare::Services services, std::shared_ptr<std::atomic_bool> input = {}, QObject* parent = nullptr,
        std::shared_ptr<ChangeCounter> input_epoch = {});
    ~RequestSharingModel() override;
    QString requestId() const { return QString::number(m_request_id); }
    QString payloadRevision() const;
    SharingInterest phase() const;
    bool active() const;
    bool canCopyUri() const;
    bool canCopyQr() const;
    bool canExportQr() const { return canCopyQr(); }
    QString disabledReason() const;
    SharingError::Code error() const;
    QString paymentUri() const;
    QImage image() const;
    ShareOperationModel* lastOperation() const { return m_last; }
    RequestImageExportOperation* qrExport() const { return m_qr_export; }
    Q_INVOKABLE void setActive(bool active);
    Q_INVOKABLE ShareOperationModel* copyUri();
    Q_INVOKABLE ShareOperationModel* copyQr();
    //! Starts a QR image export, replacing a finished one. Going inactive
    //! cancels it while it can still be cancelled.
    Q_INVOKABLE RequestImageExportOperation* beginQrExport();
    void refresh();
    Q_INVOKABLE void retire();
Q_SIGNALS:
    void changed();
    void qrExportChanged();
    void commandRejected(const QString& command, const QString& error);
private:
    struct Cache;
    ShareOperationModel* copy(requestshare::Kind kind);
    void publish(std::shared_ptr<const Cache> cache, uint64_t token);
    const int64_t m_request_id;
    requestshare::Services m_services;
    const std::shared_ptr<requestshare::View> m_view;
    std::shared_ptr<const Cache> m_cache;
    QPointer<ShareOperationModel> m_last;
    QPointer<RequestImageExportOperation> m_qr_export;
    QPointer<QQmlEngine> m_gui_engine;
    QJSValue m_last_script;
    uint64_t m_refresh{0};
    bool m_refresh_pending{false}, m_refresh_again{false};
};

#endif // BITCOIN_QML_MODELS_REQUESTSHARINGMODEL_H
