// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/backuppolicyoperation.h>
#include <qml/executor.h>

#include <QPointer>

#include <atomic>
#include <exception>
#include <utility>

namespace backup {
struct OperationState {
    QString raw;
    Services services;
    Snapshot current;
    enum class Admission { Open, Writing, Retired };
    std::atomic<Admission> admission{Admission::Open};
    std::atomic_bool started{false};
    std::atomic_bool admitted{false};
    std::atomic_bool retire_if_confirmation_requested{false};
    std::atomic_bool prompt_retirement_submitted{false};
    std::function<void(Snapshot)> publish;

    Decision advance(Event event, const Proposal& proposal = {}, WriteResult result = {})
    {
        auto decision = Advance(current, event, proposal, result);
        if (!decision.accepted) return decision;
        current = decision.snapshot;
        // GUI transport cannot own the outcome or interrupt an admitted write.
        try { publish(current); } catch (...) { }
        if (!Pending(current.phase) && services.outcome) services.outcome(current);
        return decision;
    }
    void consume(Event event, const Services::WriteFile& write)
    {
        try {
            if (current.phase != Phase::Writing && services.available && !services.available()) {
                auto expected = Admission::Open;
                admission.compare_exchange_strong(expected, Admission::Retired);
            }
            if (admission.load() == Admission::Retired && Pending(current.phase)) {
                advance(Event::Retire);
                return;
            }
            auto decision = advance(event);
            if (!decision.accepted) return;
            if (decision.effect == Effect::Inspect) {
                auto proposal = services.inspect(RawUtf8(), services.wallet_name());
                if (admission.load() == Admission::Retired || (services.available && !services.available())) {
                    admission.store(Admission::Retired);
                    advance(Event::Retire);
                    return;
                }
                advance(Event::Inspected, proposal);
                if (current.phase == Phase::AwaitingConfirmation && retire_if_confirmation_requested.load()) {
                    advance(Event::Retire);
                    return;
                }
            }
            if (current.phase == Phase::Ready) {
                if (services.available && !services.available()) {
                    admission.store(Admission::Retired);
                    advance(Event::Retire);
                    return;
                }
                auto expected = Admission::Open;
                if (!admission.compare_exchange_strong(expected, Admission::Writing)) { advance(Event::Retire); return; }
                decision = advance(Event::AdmitWrite);
                if (decision.effect == Effect::Write) {
                    const auto& writer = write ? write : services.write;
                    const auto result = writer(current.target, current.destination_approved);
                    advance(Event::Written, {}, result);
                }
            }
        } catch (...) {
            advance(Event::Fault, {}, {Error::Unknown});
        }
    }
    std::string RawUtf8() const
    {
        // Reject before allocating an unbounded encoded command.
        if (raw.size() > 32'768) return {};
        const auto bytes = raw.toUtf8();
        return {bytes.constData(), static_cast<size_t>(bytes.size())};
    }
};

namespace {
void RequestPromptRetirement(const std::shared_ptr<OperationState>& state)
{
    state->retire_if_confirmation_requested.store(true);
    // A wallet export posts retirement on the same serial queue as its file
    // work. Its open action keeps the queue available until it has an outcome.
    if (!state->admitted.load() || !state->services.submit ||
        state->prompt_retirement_submitted.exchange(true)) return;
    const auto retire_prompt = [state] {
        if (state->current.phase == Phase::AwaitingConfirmation) state->advance(Event::Retire);
    };
    try {
        const auto submitted = state->services.submit([retire_prompt](const Services::WriteFile&) { retire_prompt(); });
        if (submitted) return;
    } catch (...) { }
    state->prompt_retirement_submitted.store(false);
}
} // namespace
} // namespace backup

backup::Operation::Operation(QString raw, Services services, Publish publish)
    : m_state{std::make_shared<OperationState>()}
{
    if (!services.submit && services.utility && services.write) {
        services.submit = [utility = services.utility](Services::Work work) {
            const bool accepted = utility->post([work = std::move(work)] { work({}); });
            return execution::SubmitResult{{}, accepted ? execution::Rejection::None : execution::Rejection::Transport};
        };
    }
    m_state->raw = std::move(raw);
    m_state->services = std::move(services);
    m_state->publish = std::move(publish);
}

bool backup::Operation::start()
{
    auto state = m_state;
    if (!state) return false;
    bool expected{false};
    if (!state->started.compare_exchange_strong(expected, true)) return false;
    const auto& services = state->services;
    if (!services.post || !services.wallet_name || !services.inspect || !services.submit) {
        failBeforeStart(Error::Transport);
        return false;
    }
    return submit(Event::Start);
}

bool backup::Operation::submit(Event event)
{
    const auto state = m_state;
    if (!state || !state->services.submit) return false;
    const auto work = [state, event](const Services::WriteFile& write) { state->consume(event, write); };
    const auto admission = state->services.submit(work);
    if (event == Event::Start) {
        state->admitted.store(bool(admission));
        if (!admission) failBeforeStart(Error::Transport);
        else if (state->retire_if_confirmation_requested.load()) RequestPromptRetirement(state);
    }
    return bool(admission);
}

bool backup::Operation::approve()
{
    const auto state = m_state;
    return state && state->started.load() && !state->retire_if_confirmation_requested.load()
        && state->admission.load() != OperationState::Admission::Retired
        && submit(Event::Approve);
}

bool backup::Operation::refuse()
{
    const auto state = m_state;
    return state && state->started.load() && !state->retire_if_confirmation_requested.load()
        && state->admission.load() != OperationState::Admission::Retired
        && submit(Event::Refuse);
}

void backup::Operation::retire()
{
    const auto state = m_state;
    if (!state || !state->admitted.load()) return;
    auto expected = OperationState::Admission::Open;
    if (!state->admission.compare_exchange_strong(expected, OperationState::Admission::Retired)) return;
    submit(Event::Retire);
}

bool backup::Operation::retiredBeforeWrite() const
{
    const auto state = m_state;
    return state && state->admission.load() == OperationState::Admission::Retired;
}

void backup::Operation::retireIfAwaitingConfirmation()
{
    const auto state = m_state;
    if (!state || !state->services.submit) return;
    RequestPromptRetirement(state);
}

void backup::Operation::failBeforeStart(Error error)
{
    const auto state = m_state;
    if (!state) return;
    auto decision = Advance(state->current, Event::Fault, {}, {error});
    if (!decision.accepted) return;
    state->current = decision.snapshot;
    // A rejected start has no admitted domain operation and therefore no
    // terminal worker receipt. Publish the local failure and release the
    // request through its original owner.
    try { state->publish(state->current); } catch (...) { }
    state->admission.store(OperationState::Admission::Retired);
    if (state->services.abandon) {
        try { state->services.abandon(); } catch (...) { }
    }
}

void backup::Operation::abandonBeforeStart()
{
    const auto state = m_state;
    if (!state) return;
    bool expected{false};
    if (!state->started.compare_exchange_strong(expected, true)) return;
    state->admission.store(OperationState::Admission::Retired);
    if (state->services.abandon) {
        try { state->services.abandon(); } catch (...) { }
    }
}

BackupFileOperation::BackupFileOperation(QString raw, backup::Services services, QObject* parent)
    : FileSaveOperation(parent)
{
    static_assert(static_cast<int>(Phase::Retired) == static_cast<int>(backup::Phase::Retired));
    const QPointer<BackupFileOperation> observer{this};
    const auto post = services.post;
    m_backend = std::make_shared<backup::Operation>(std::move(raw), std::move(services), [post, observer](backup::Snapshot projection) {
        if (post) post([observer, projection = std::move(projection)]() mutable {
            if (observer) observer->apply(std::move(projection));
        });
    });
}

BackupFileOperation::~BackupFileOperation()
{
    if (m_retire_on_destroy) retire();
}

bool BackupFileOperation::start()
{
    if (m_started) return false;
    m_started = true;
    const QPointer<BackupFileOperation> observer{this};
    const auto backend = m_backend;
    if (!backend->start()) {
        if (!observer) return false;
        m_projection.phase = backup::Phase::Failed;
        m_projection.error = backup::Error::Transport;
        ++m_projection.sequence;
        Q_EMIT changed();
        return false;
    }
    return true;
}

bool BackupFileOperation::confirm()
{
    const auto backend = m_backend;
    return backend && backend->approve();
}

bool BackupFileOperation::cancel()
{
    const auto backend = m_backend;
    return backend && backend->refuse();
}

void BackupFileOperation::retire()
{
    if (!backup::Pending(m_projection.phase)) return;
    // Logical retirement is visible to an already queued worker task. The
    // handle remains valid if this QObject presentation is destroyed.
    if (m_backend) m_backend->retire();
}

bool BackupFileOperation::retireBeforeWrite()
{
    const auto backend = m_backend;
    if (!backend || !m_started) return false;
    backend->retire();
    return backend->retiredBeforeWrite();
}

void BackupFileOperation::apply(backup::Snapshot projection)
{
    if (projection.sequence <= m_projection.sequence) return;
    m_projection = std::move(projection);
    Q_EMIT changed();
}

QString BackupFileOperation::finalPath() const
{
    const auto& path = m_projection.target.path;
    return QString::fromUtf8(path.data(), static_cast<qsizetype>(path.size()));
}

QString BackupFileOperation::error() const
{
    switch (m_projection.error) {
    case backup::Error::None: return {};
    case backup::Error::InvalidPath: return tr("Choose a valid local backup destination.");
    case backup::Error::Inspection: return tr("The backup destination could not be inspected.");
    case backup::Error::UnsupportedTarget: return tr("Choose a regular file as the backup destination.");
    case backup::Error::WalletFile: return tr("Choose a file that is not a wallet.");
    case backup::Error::Staging: return tr("A backup could not be created in that directory.");
    case backup::Error::CoreBackup: return tr("The wallet could not be backed up.");
    case backup::Error::Changed: return tr("The destination changed. Choose the backup location again.");
    case backup::Error::Install: return tr("The backup could not be installed. Check the destination before retrying.");
    case backup::Error::Unknown: return tr("The backup may not have been saved. Check the backup file before trying again.");
    case backup::Error::Transport: return tr("The backup could not start. Try again.");
    }
    return tr("The backup may not have been saved. Check the backup file before trying again.");
}
