// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/fileexportoperation.h>

#include <qml/psbtserialization.h>
#include <qml/retainedvalue.h>

#include <QFile>
#include <QPointer>

#include <atomic>
#include <mutex>
#include <utility>

namespace {
constexpr qsizetype MAX_PSBT_EXPORT_BYTES{100 * 1024 * 1024};
} // namespace

struct FileExportRequest::State {
    struct Source {
        mutable std::mutex mutex;
        std::shared_ptr<std::shared_ptr<const PartiallySignedTransaction>> document;

        bool set(std::shared_ptr<const PartiallySignedTransaction> value)
        {
            std::lock_guard lock{mutex};
            if (!document || !value || *document) return false;
            *document = std::move(value);
            return true;
        }
        bool hasDocument() const
        {
            std::lock_guard lock{mutex};
            return document && bool(*document);
        }
        std::shared_ptr<const PartiallySignedTransaction> take()
        {
            std::lock_guard lock{mutex};
            return document ? std::exchange(*document, {}) : std::shared_ptr<const PartiallySignedTransaction>{};
        }
        void clear()
        {
            // Releasing this alias only activates its reserved Cleanup turn.
            // The retained PSBT itself is destroyed by the scheduler.
            std::shared_ptr<std::shared_ptr<const PartiallySignedTransaction>> retired;
            {
                std::lock_guard lock{mutex};
                retired = std::move(document);
            }
            retired.reset();
        }
    };

    std::shared_ptr<Source> source{std::make_shared<Source>()};
    std::shared_ptr<backup::Operation> operation;
    std::atomic_bool started{false};
    std::shared_ptr<std::atomic_bool> continuation{std::make_shared<std::atomic_bool>(false)};
};

FileExportRequest::FileExportRequest(std::shared_ptr<State> state) : m_state{std::move(state)} {}

FileExportRequest::~FileExportRequest()
{
    const auto state = m_state;
    // Once an accepted continuation has started, its operation owns the
    // retained source and cleanup path even if its last GUI-facing handle is
    // released with the facade.
    if (state && state->started.load() && state->continuation->load()) return;
    abandon();
}

bool FileExportRequest::prepare(std::shared_ptr<const PartiallySignedTransaction> document)
{
    const auto state = m_state;
    return state && state->source->set(std::move(document));
}

bool FileExportRequest::startPrepared()
{
    const auto state = m_state;
    if (!state || !state->operation) return false;
    bool expected{false};
    if (!state->started.compare_exchange_strong(expected, true)) return false;
    if (!state->source->hasDocument()) {
        state->operation->failBeforeStart(backup::Error::CoreBackup);
        return false;
    }
    return state->operation->start();
}

bool FileExportRequest::start(std::shared_ptr<const PartiallySignedTransaction> document)
{
    const auto state = m_state;
    if (!state || state->started.load()) return false;
    const bool prepared = prepare(std::move(document));
    if (!prepared && !state->source->hasDocument()) {
        bool expected{false};
        if (state->started.compare_exchange_strong(expected, true)) state->operation->failBeforeStart(backup::Error::Transport);
        return false;
    }
    return startPrepared();
}

void FileExportRequest::retainAcceptedContinuation()
{
    if (m_state) m_state->continuation->store(true);
}

void FileExportRequest::abandon()
{
    const auto state = m_state;
    if (!state) return;
    state->continuation->store(false);
    if (state->started.load()) state->operation->retire();
    else if (state->operation) state->operation->abandonBeforeStart();
    state->source->clear();
}

void FileExportRequest::retireFromProjection()
{
    const auto state = m_state;
    if (!state) return;
    // A pre-start continuation is still waiting for the accepted PSBT
    // preparation result and must survive loss of its GUI projection. Once
    // file export has started, accepted continuations only retire if the
    // inspected destination requires confirmation. A fresh-path save remains
    // the accepted effect; an already admitted write is also preserved.
    if (!state->started.load() && state->continuation->load()) return;
    if (state->started.load() && state->operation) {
        if (state->continuation->load()) state->operation->retireIfAwaitingConfirmation();
        else state->operation->retire();
    } else {
        abandon();
    }
}

FileExportOperationModel::FileExportOperationModel(QString rawPath, backup::Services services, QObject* parent)
    : FileSaveOperation(parent)
{
    auto state = std::make_shared<FileExportRequest::State>();
    const auto scheduler = services.scheduler;
    if (!scheduler) {
        m_start_error = tr("PSBT export cleanup is unavailable.");
    } else {
        auto retained_document = execution::RetainValues<std::shared_ptr<const PartiallySignedTransaction>>(
            scheduler, [] { return std::make_unique<std::shared_ptr<const PartiallySignedTransaction>>(); });
        if (!retained_document) m_start_error = tr("PSBT export cleanup is unavailable.");
        else state->source->document = std::move(retained_document);
    }
    const auto previous_available = std::move(services.available);
    const auto continuation = state->continuation;
    services.available = [continuation, previous_available] {
        if (continuation->load()) return true;
        return !previous_available || previous_available();
    };
    if (!services.coordinator) services.coordinator = std::make_shared<backup::InstallCoordinator>();
    const auto coordinator = services.coordinator;
    const auto source = state->source;
    services.wallet_name = [] { return std::string{"transaction"}; };
    services.inspect = [coordinator](const std::string& raw, const std::string&) {
        return backup::Screen(backup::Inspect(raw, "transaction", ".psbt"), *coordinator);
    };
    services.write = [source, coordinator](const backup::Proposal& target, bool replace) {
        return backup::Write(target, replace, [source](const std::string& stagedPath) {
            const auto document = source->take();
            if (!document) return false;
            const auto bytes = psbtwork::SerializeRaw(*document);
            if (bytes.isEmpty() || bytes.size() > MAX_PSBT_EXPORT_BYTES) return false;
            const QString path = QString::fromUtf8(stagedPath.data(), static_cast<qsizetype>(stagedPath.size()));
            QFile file{path};
            if (!file.open(QIODevice::WriteOnly)) return false;
            return file.write(bytes) == bytes.size() && file.flush();
        }, *coordinator);
    };
    const auto previous_outcome = std::move(services.outcome);
    services.outcome = [source, continuation, previous_outcome](backup::Snapshot snapshot) {
        if (previous_outcome) previous_outcome(snapshot);
        if (!backup::Pending(snapshot.phase) && continuation->load()) source->clear();
    };
    const auto previous_abandon = std::move(services.abandon);
    services.abandon = [source, previous_abandon] {
        source->clear();
        if (previous_abandon) previous_abandon();
    };
    m_operation = new BackupFileOperation(std::move(rawPath), std::move(services), this);
    state->operation = m_operation->backend();
    m_request = std::shared_ptr<FileExportRequest>{new FileExportRequest{std::move(state)}};
    connect(m_operation, &FileSaveOperation::changed, this, [this] {
        if (m_operation && !m_operation->busy() && m_request) m_request->m_state->source->clear();
        Q_EMIT changed();
    });
}

FileExportOperationModel::FileExportOperationModel(QString rawPath,
    const std::shared_ptr<const PartiallySignedTransaction>& document, backup::Services services, QObject* parent)
    : FileExportOperationModel(std::move(rawPath), std::move(services), parent)
{
    if (!document) {
        m_start_error = tr("No captured PSBT is available to export.");
        return;
    }
    const auto state = m_request->m_state;
    if (!state || !state->source->set(document)) m_start_error = tr("PSBT export cleanup is unavailable.");
}

FileExportOperationModel::~FileExportOperationModel()
{
    retire();
    if (m_operation) m_operation->detachRetirement();
}

bool FileExportOperationModel::busy() const
{
    return m_operation && m_operation->busy();
}

bool FileExportOperationModel::confirmationRequired() const
{
    return m_operation && m_operation->confirmationRequired();
}

bool FileExportOperationModel::replacing() const
{
    return m_operation && m_operation->replacing();
}

bool FileExportOperationModel::saved() const
{
    return m_operation && m_operation->saved();
}

QString FileExportOperationModel::finalPath() const
{
    return m_operation ? m_operation->finalPath() : QString{};
}

QString FileExportOperationModel::error() const
{
    if (!m_start_error.isEmpty()) return m_start_error;
    if (!m_operation || m_operation->busy() || m_operation->saved()) return {};
    if (m_operation->phase() == BackupFileOperation::Phase::Declined) return tr("PSBT export was cancelled.");
    if (m_operation->phase() == BackupFileOperation::Phase::Retired) return tr("PSBT export ended before the file was written.");
    switch (m_operation->errorCode()) {
    case backup::Error::None: return tr("The PSBT could not be saved.");
    case backup::Error::InvalidPath: return tr("Choose a valid local PSBT destination.");
    case backup::Error::Inspection: return tr("The PSBT destination could not be inspected.");
    case backup::Error::UnsupportedTarget: return tr("Choose a regular file as the PSBT destination.");
    case backup::Error::WalletFile: return tr("Choose a file that is not a wallet.");
    case backup::Error::Staging: return tr("A temporary PSBT file could not be created in that directory.");
    case backup::Error::CoreBackup: return tr("The captured PSBT could not be written.");
    case backup::Error::Changed: return tr("The PSBT destination changed. Choose the file again.");
    case backup::Error::Install: return tr("The PSBT file could not be installed. Check the destination before retrying.");
    case backup::Error::Unknown: return tr("The PSBT may not have been saved. Check the file before trying again.");
    case backup::Error::Transport: return tr("The PSBT could not be saved. Try again.");
    }
    return tr("The PSBT may not have been saved.");
}

bool FileExportOperationModel::start()
{
    if (std::exchange(m_started, true)) return false;
    const QPointer<FileExportOperationModel> observer{this};
    if (!m_start_error.isEmpty() || !m_request) {
        Q_EMIT changed();
        return false;
    }
    const auto state = m_request->m_state;
    if (!state || !state->source->hasDocument()) {
        if (state && state->operation) state->operation->failBeforeStart(backup::Error::CoreBackup);
        if (observer) Q_EMIT changed();
        return false;
    }
    bool expected{false};
    if (!state->started.compare_exchange_strong(expected, true)) return false;
    const bool admitted = m_operation->start();
    if (!observer) return admitted;
    if (!admitted && !busy()) {
        state->source->clear();
        Q_EMIT changed();
    }
    return admitted;
}

std::shared_ptr<FileExportRequest> FileExportOperationModel::requestForAcceptedContinuation()
{
    if (m_request) m_request->retainAcceptedContinuation();
    return m_request;
}

bool FileExportOperationModel::confirm()
{
    return m_operation && m_operation->confirm();
}

bool FileExportOperationModel::cancel()
{
    return m_operation && m_operation->cancel();
}

void FileExportOperationModel::retire()
{
    if (m_request) m_request->retireFromProjection();
}
