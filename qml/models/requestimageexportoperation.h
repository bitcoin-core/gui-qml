// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_QML_MODELS_REQUESTIMAGEEXPORTOPERATION_H
#define BITCOIN_QML_MODELS_REQUESTIMAGEEXPORTOPERATION_H

#include <qml/models/backuppolicy.h>
#include <qml/requestsharing.h>

#include <QObject>
#include <QUrl>

class RequestImageExportError {
    Q_GADGET
public:
    enum Code { None, Source, Rejected, InvalidPath, Inspection, UnsupportedTarget, Encoding, Changed, Install, Unknown, Cancelled, Chooser };
    Q_ENUM(Code)
};
namespace requestimageexport {
struct Services {
    std::function<backup::Proposal(const QUrl&)> inspect;
    // Native only. The adapter must call Grant::admit immediately before the
    // final installation, after encoding and target-version validation.
    std::function<backup::WriteResult(const backup::Proposal&, bool, const QImage&, const std::shared_ptr<requestshare::Grant>&)> write;
};
}
class RequestImageExportResult {
    Q_GADGET
    Q_PROPERTY(QString operationId MEMBER operationId)
    Q_PROPERTY(QString requestId MEMBER requestId)
    Q_PROPERTY(QString payloadRevision MEMBER payloadRevision)
    Q_PROPERTY(QString finalPath MEMBER finalPath)
    Q_PROPERTY(bool present MEMBER present)
    Q_PROPERTY(bool admitted MEMBER admitted)
    Q_PROPERTY(bool written MEMBER written)
    Q_PROPERTY(bool outcomeKnown MEMBER outcomeKnown)
public:
    QString operationId, requestId, payloadRevision, finalPath;
    bool present{false}, admitted{false}, written{false}, outcomeKnown{true};
};
Q_DECLARE_METATYPE(RequestImageExportResult)

/** Original request, payload and target consent. A chooser or confirmation
 * holds no action lease. Native preparation/install owns its actual result. */
class RequestImageExportOperation final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString operationId READ operationId CONSTANT)
    Q_PROPERTY(Phase phase READ phase NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool canCancel READ canCancel NOTIFY changed)
    Q_PROPERTY(bool choosing READ choosing NOTIFY changed)
    Q_PROPERTY(bool confirmationRequired READ confirmationRequired NOTIFY changed)
    Q_PROPERTY(bool replacing READ replacing NOTIFY changed)
    Q_PROPERTY(QString finalPath READ finalPath NOTIFY changed)
    Q_PROPERTY(RequestImageExportError::Code error READ error NOTIFY changed)
    Q_PROPERTY(QString errorText READ errorText NOTIFY changed)
    Q_PROPERTY(RequestImageExportResult result READ result NOTIFY changed)
public:
    enum Phase { Checking, Preparing, Choosing, Inspecting, Confirming, Writing, Cancelling, Completed, Failed, OutcomeUnknown, Cancelled };
    Q_ENUM(Phase)
    RequestImageExportOperation(int64_t id, requestshare::Services services, std::shared_ptr<requestshare::View> view,
        std::optional<requestshare::Source> expected = {}, QObject* parent = nullptr);
    ~RequestImageExportOperation() override;
    QString operationId() const { return m_id; }
    Phase phase() const;
    bool busy() const;
    bool canCancel() const;
    bool choosing() const { return phase() == Choosing; }
    bool confirmationRequired() const { return phase() == Confirming; }
    bool replacing() const;
    QString finalPath() const;
    RequestImageExportError::Code error() const;
    QString errorText() const;
    RequestImageExportResult result() const;
    Q_INVOKABLE bool choose(const QUrl& destination);
    Q_INVOKABLE bool approve();
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void chooserFailed();
Q_SIGNALS:
    void changed();
    void commandRejected(const QString& command);
private:
    struct Cell;
    struct Work;
    const QString m_id;
    const std::shared_ptr<Cell> m_cell;
    const std::shared_ptr<Work> m_work;
};
#endif // BITCOIN_QML_MODELS_REQUESTIMAGEEXPORTOPERATION_H
