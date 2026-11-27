// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_BACKUPPOLICYOPERATION_H
#define BITCOIN_QML_MODELS_BACKUPPOLICYOPERATION_H

#include <qml/retainedvalue.h>
#include <qml/models/backuppolicy.h>
#include <qml/models/backuppolicyio.h>
#include <qml/models/filesaveoperation.h>

#include <QString>

#include <functional>
#include <memory>

namespace execution { class SerialQueue; }

namespace backup {
struct Services {
    using WriteFile = std::function<WriteResult(const Proposal&, bool)>;
    using Work = std::function<void(const WriteFile&)>;
    execution::ValueCleanup value_cleanup;
    std::function<void(std::function<void()>)> post;
    std::function<std::string()> wallet_name;
    std::function<Proposal(const std::string&, const std::string&)> inspect;
    WriteFile write;
    std::shared_ptr<InstallCoordinator> coordinator;
    // Consumed on the backend lane, even if the GUI observer has disappeared.
    std::function<void(Snapshot)> outcome;
    // The opening supplies its writer for the duration of an admitted action.
    // File-only callers use the writer already configured above.
    std::function<execution::SubmitResult(Work)> submit;
    std::function<bool()> available;
    // Releases the request when abandoned before any file effect is admitted.
    std::function<void()> abandon;
    std::shared_ptr<execution::SerialQueue> utility{};
};
struct OperationState;

/** Thread-safe command handle for the shared backup/export state machine.
 * The QObject wrapper below owns presentation only; continuations may retain
 * this handle and admit the original request without a GUI callback.
 */
class Operation final
{
public:
    using Publish = std::function<void(Snapshot)>;
    Operation(QString raw, Services services, Publish publish);
    bool start();
    bool approve();
    bool refuse();
    void retire();
    /** Retire an accepted continuation only if destination inspection requires confirmation. */
    void retireIfAwaitingConfirmation();
    bool retiredBeforeWrite() const; // Native admission proof; no later file write can start.
    void failBeforeStart(Error error);
    void abandonBeforeStart();

private:
    bool submit(Event event);
    std::shared_ptr<OperationState> m_state;
};
} // namespace backup

/** Saves through the staged write policy: inspect, confirm, write, install.
 * Projections never own wallet or file handles. */
class BackupFileOperation : public FileSaveOperation
{
    Q_OBJECT
public:
    enum class Phase { Queued, Inspecting, AwaitingConfirmation, Ready, Writing, Succeeded, Failed, Declined, Retired };
    Q_ENUM(Phase)
    BackupFileOperation(QString raw, backup::Services services, QObject* parent = nullptr);
    ~BackupFileOperation() override;
    bool start();
    Phase phase() const { return static_cast<Phase>(m_projection.phase); }
    bool busy() const override { return backup::Pending(m_projection.phase); }
    bool confirmationRequired() const override { return m_projection.phase == backup::Phase::AwaitingConfirmation; }
    bool replacing() const override { return m_projection.target.exists; }
    bool saved() const override { return m_projection.phase == backup::Phase::Succeeded; }
    backup::Error errorCode() const { return m_projection.error; }
    QString finalPath() const override;
    QString error() const override;
    bool confirm() override;
    bool cancel() override;
    void retire();
    //! Decided by the backend's atomic Open -> Writing transition, not by
    //! this projection, which can still show inspection after the worker
    //! admitted the write.
    bool retireBeforeWrite() override;
    void detachRetirement() { m_retire_on_destroy = false; }
    std::shared_ptr<backup::Operation> backend() const { return m_backend; }
private:
    void apply(backup::Snapshot projection);
    std::shared_ptr<backup::Operation> m_backend;
    backup::Snapshot m_projection;
    bool m_started{false};
    bool m_retire_on_destroy{true};
};

#endif // BITCOIN_QML_MODELS_BACKUPPOLICYOPERATION_H
