// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_QML_MODELS_ACTIVITYEXPORTOPERATION_H
#define BITCOIN_QML_MODELS_ACTIVITYEXPORTOPERATION_H

#include <qml/models/activityexportdata.h>
#include <qml/models/backuppolicyoperation.h>
#include <qml/snapshotquery.h>

//! What one export ended with, kept after the operation is replaced.
struct ActivityExportResult {
    QString operationId, snapshotRevision, finalPath, errorText;
    int displayUnit{0};
    bool present{false}, saved{false}, cancelled{false}, outcomeKnown{true};
};

/** One original immutable Activity frame/unit and one versioned destination
 * consent. No file I/O, scan or CSV serialization occurs on GUI. */
class ActivityExportOperation final : public FileSaveOperation
{
    Q_OBJECT
public:
    enum Error { NoError, InvalidPath, Inspection, UnsupportedTarget, WalletFile, Staging, Write, Changed, Install, OutcomeUnknown, Rejected };
    Q_ENUM(Error)
    ActivityExportOperation(activityexport::Request request, activityexport::Files files,
        execution::SnapshotServices execution, std::function<activityexport::Result(const activityexport::Request&)> writer,
        QObject* parent = nullptr);
    ~ActivityExportOperation() override;
    QString operationId() const { return m_id; }
    QString snapshotRevision() const { return m_revision; }
    int displayUnit() const { return m_unit; }
    BackupFileOperation::Phase phase() const;
    bool busy() const override;
    bool confirmationRequired() const override;
    bool replacing() const override;
    bool resultPresent() const;
    bool cancelled() const;
    Error errorCode() const;
    bool saved() const override;
    bool outcomeKnown() const;
    QString finalPath() const override;
    QString error() const override;
    ActivityExportResult result() const;
    bool start();
    bool confirm() override;
    bool cancel() override;
    void retire();
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
