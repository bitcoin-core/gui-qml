// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_QML_MODELS_ACTIVITYEXPORTOPERATION_H
#define BITCOIN_QML_MODELS_ACTIVITYEXPORTOPERATION_H

#include <qml/models/activityexportdata.h>
#include <qml/models/backuppolicyoperation.h>
#include <qml/snapshotquery.h>

class ActivityExportResult {
    Q_GADGET
    Q_PROPERTY(QString operationId MEMBER operationId)
    Q_PROPERTY(QString snapshotRevision MEMBER snapshotRevision)
    Q_PROPERTY(QString finalPath MEMBER finalPath)
    Q_PROPERTY(QString errorText MEMBER errorText)
    Q_PROPERTY(int displayUnit MEMBER displayUnit)
    Q_PROPERTY(bool present MEMBER present)
    Q_PROPERTY(bool saved MEMBER saved)
    Q_PROPERTY(bool cancelled MEMBER cancelled)
    Q_PROPERTY(bool outcomeKnown MEMBER outcomeKnown)
public:
    QString operationId, snapshotRevision, finalPath, errorText;
    int displayUnit{0};
    bool present{false}, saved{false}, cancelled{false}, outcomeKnown{true};
};
Q_DECLARE_METATYPE(ActivityExportResult)

/** One original immutable Activity frame/unit and one versioned destination
 * consent. No file I/O, scan or CSV serialization occurs on GUI. */
class ActivityExportOperation final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString operationId READ operationId CONSTANT)
    Q_PROPERTY(QString snapshotRevision READ snapshotRevision CONSTANT)
    Q_PROPERTY(int displayUnit READ displayUnit CONSTANT)
    Q_PROPERTY(BackupFileOperation::Phase phase READ phase NOTIFY changed)
    Q_PROPERTY(bool pending READ pending NOTIFY changed)
    Q_PROPERTY(bool confirmationRequired READ confirmationRequired NOTIFY changed)
    Q_PROPERTY(bool replacing READ replacing NOTIFY changed)
    Q_PROPERTY(bool cancelled READ cancelled NOTIFY changed)
    Q_PROPERTY(Error errorCode READ errorCode NOTIFY changed)
    Q_PROPERTY(ActivityExportResult result READ result NOTIFY changed)
    Q_PROPERTY(bool resultPresent READ resultPresent NOTIFY changed)
    Q_PROPERTY(bool saved READ saved NOTIFY changed)
    Q_PROPERTY(bool outcomeKnown READ outcomeKnown NOTIFY changed)
    Q_PROPERTY(QString finalPath READ finalPath NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
public:
    enum Error { NoError, InvalidPath, Inspection, UnsupportedTarget, Staging, Write, Changed, Install, OutcomeUnknown, Rejected };
    Q_ENUM(Error)
    ActivityExportOperation(activityexport::Request request, activityexport::Files files,
        execution::SnapshotServices execution, std::function<activityexport::Result(const activityexport::Request&)> writer,
        QObject* parent = nullptr);
    ~ActivityExportOperation() override;
    QString operationId() const { return m_id; }
    QString snapshotRevision() const { return m_revision; }
    int displayUnit() const { return m_unit; }
    BackupFileOperation::Phase phase() const;
    bool pending() const;
    bool confirmationRequired() const;
    bool replacing() const;
    bool resultPresent() const;
    bool cancelled() const;
    Error errorCode() const;
    bool saved() const;
    bool outcomeKnown() const;
    QString finalPath() const;
    QString error() const;
    ActivityExportResult result() const;
    bool start();
    Q_INVOKABLE bool approve();
    Q_INVOKABLE bool refuse();
    Q_INVOKABLE void retire();
Q_SIGNALS:
    void changed();
private:
    bool retiredWithoutWrite() const;
    void retireImpl(bool notify);
    const QString m_id, m_revision;
    const int m_unit;
    BackupFileOperation* m_policy{nullptr};
    QString m_error;
    std::function<void()> m_abandon;
    bool m_started{false}, m_retired{false};
};
#endif // BITCOIN_QML_MODELS_ACTIVITYEXPORTOPERATION_H
