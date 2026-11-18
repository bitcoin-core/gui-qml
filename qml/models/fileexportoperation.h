// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_FILEEXPORTOPERATION_H
#define BITCOIN_QML_MODELS_FILEEXPORTOPERATION_H

#include <qml/models/backuppolicyoperation.h>
#include <psbt.h>

#include <QString>

#include <functional>
#include <memory>

class FileExportRequest;

/** Saves one captured PSBT through the staged write policy. */
class FileExportOperationModel final : public FileSaveOperation
{
    Q_OBJECT
public:
    FileExportOperationModel(QString rawPath, backup::Services services, QObject* parent = nullptr);
    FileExportOperationModel(QString rawPath, const std::shared_ptr<const PartiallySignedTransaction>& document,
                             backup::Services services, QObject* parent = nullptr);
    ~FileExportOperationModel() override;

    bool busy() const override;
    bool confirmationRequired() const override;
    bool replacing() const override;
    bool saved() const override;
    QString finalPath() const override;
    QString error() const override;

    bool start();
    /** Backend continuation for an accepted Save whose PSBT is still being prepared. */
    std::shared_ptr<FileExportRequest> requestForAcceptedContinuation();
    bool confirm() override;
    bool cancel() override;
    void retire();

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
