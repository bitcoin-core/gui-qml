// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_FILEEXPORTOPERATION_H
#define BITCOIN_QML_MODELS_FILEEXPORTOPERATION_H

#include <qml/models/backuppolicyoperation.h>
#include <psbt.h>

#include <QObject>
#include <QString>

#include <functional>
#include <memory>

class FileExportRequest;

/** One captured PSBT value exported through the shared M24 destination policy. */
class FileExportOperationModel final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool pending READ pending NOTIFY changed)
    Q_PROPERTY(bool confirmationRequired READ confirmationRequired NOTIFY changed)
    Q_PROPERTY(bool replacing READ replacing NOTIFY changed)
    Q_PROPERTY(bool saved READ saved NOTIFY changed)
    Q_PROPERTY(QString finalPath READ finalPath NOTIFY changed)
    Q_PROPERTY(QString errorText READ errorText NOTIFY changed)
public:
    FileExportOperationModel(QString rawPath, backup::Services services, QObject* parent = nullptr);
    FileExportOperationModel(QString rawPath, const std::shared_ptr<const PartiallySignedTransaction>& document,
                             backup::Services services, QObject* parent = nullptr);
    ~FileExportOperationModel() override;

    bool pending() const;
    bool confirmationRequired() const;
    bool replacing() const;
    bool saved() const;
    QString finalPath() const;
    QString errorText() const;

    bool start();
    /** Backend continuation for an accepted Save whose PSBT is still being prepared. */
    std::shared_ptr<FileExportRequest> requestForAcceptedContinuation();
    Q_INVOKABLE bool approve();
    Q_INVOKABLE bool refuse();
    Q_INVOKABLE void retire();

Q_SIGNALS:
    void changed();

private:
    struct Source;
    std::shared_ptr<FileExportRequest> m_request;
    BackupFileOperation* m_operation{nullptr};
    QString m_start_error;
    bool m_started{false};
};

/** Non-QObject continuation retained independently of the QML projection. */
class FileExportRequest final
{
public:
    ~FileExportRequest();
    bool start(std::shared_ptr<const PartiallySignedTransaction> document);
    void abandon();

private:
    friend class FileExportOperationModel;
    struct State;
    explicit FileExportRequest(std::shared_ptr<State> state);
    bool prepare(std::shared_ptr<const PartiallySignedTransaction> document);
    bool startPrepared();
    void retainAcceptedContinuation();
    void retireFromProjection();
    std::shared_ptr<State> m_state;
};

#endif // BITCOIN_QML_MODELS_FILEEXPORTOPERATION_H
