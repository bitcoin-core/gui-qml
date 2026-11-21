// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/profileinspectionprocess.h>

#include <QCoreApplication>
#include <QDir>
#include <QProcess>
#include <QThread>

#include <algorithm>
#include <exception>
#include <utility>

namespace profileinspection {
namespace {
constexpr qsizetype MAX_PIPE_BYTES{4096};
constexpr auto WAIT_SLICE = std::chrono::milliseconds{10};

Result Failure(Outcome outcome, QString error)
{
    Result result;
    result.outcome = outcome; result.error = std::move(error);
    return result;
}

void Facts(Result& result, const process::Status& status, bool previously_started)
{
    result.started = previously_started || status.phase == process::Phase::Running || status.phase == process::Phase::Exited;
    result.exited = status.phase == process::Phase::Exited;
    result.exit_status = result.exited && status.normal_exit ? status.exit_status : std::nullopt;
}

class NativeProcess final : public process::Process {
    QProcess m_child;
    QByteArray m_output, m_error;
    const Limits m_limits;
    bool m_started{false}, m_kill_requested{false}, m_kill_sent{false};

    void sendKill() noexcept
    {
        if (m_kill_requested && !m_kill_sent && m_child.state() == QProcess::Running) {
            m_kill_sent = true; m_child.kill();
        }
    }
    void collect(process::Channel channel)
    {
        if (!m_child.isOpen()) return;
        auto& bytes = channel == process::Channel::Output ? m_output : m_error;
        const auto limit = channel == process::Channel::Output ? m_limits.stdout_bytes : m_limits.stderr_bytes;
        m_child.setReadChannel(channel == process::Channel::Output ? QProcess::StandardOutput : QProcess::StandardError);
        const auto remaining = limit + 1 - bytes.size();
        if (remaining > 0) bytes += m_child.read(remaining);
        if (bytes.size() > limit || m_child.bytesAvailable() > 0) {
            // Retain only one sentinel byte beyond the budget. React directly
            // on this Native object's pipe signal, before another wait turn.
            m_kill_requested = true; sendKill();
            (void)m_child.read(64 * 1024); // Bounded discard during physical reap.
        }
    }
public:
    explicit NativeProcess(Limits limits) : m_limits(limits)
    {
        m_child.setProcessChannelMode(QProcess::SeparateChannels);
        m_child.setStandardInputFile(QProcess::nullDevice());
        QObject::connect(&m_child, &QProcess::started, &m_child, [this] { m_started = true; sendKill(); });
        QObject::connect(&m_child, &QProcess::readyReadStandardOutput, &m_child, [this] { collect(process::Channel::Output); });
        QObject::connect(&m_child, &QProcess::readyReadStandardError, &m_child, [this] { collect(process::Channel::Error); });
    }
    void start(const QString& executable, const QStringList& arguments) override
    {
        m_child.start(executable, arguments, QIODevice::ReadOnly);
    }
    process::Status status() const override
    {
        if (m_child.state() == QProcess::Starting) return {};
        if (m_child.state() == QProcess::Running) return {process::Phase::Running, false, {}, {}};
        if (!m_started) return {process::Phase::FailedToStart, false, {}, m_child.errorString().left(1024)};
        const bool normal = m_child.exitStatus() == QProcess::NormalExit;
        return {process::Phase::Exited, normal, normal ? std::optional<int>{m_child.exitCode()} : std::nullopt,
            normal ? QString{} : m_child.errorString().left(1024)};
    }
    QByteArray read(process::Channel channel, qsizetype max_bytes) override
    {
        collect(channel);
        auto& bytes = channel == process::Channel::Output ? m_output : m_error;
        const auto count = std::min(max_bytes, bytes.size());
        auto result = bytes.first(count); bytes.remove(0, count);
        return result;
    }
    void wait(std::chrono::milliseconds duration) override
    {
        const auto milliseconds = static_cast<int>(duration.count()); // Driver caps this at10ms.
        if (m_child.state() == QProcess::Starting) (void)m_child.waitForStarted(milliseconds);
        else if (m_child.state() == QProcess::Running) (void)m_child.waitForReadyRead(milliseconds);
    }
    void kill() noexcept override { m_kill_requested = true; sendKill(); }
    process::Status reap() noexcept override
    {
        // Stay on Native and retain the same QProcess until Qt has physically
        // reaped it. OS scheduling/kill failure has no universal time bound.
        while (m_child.state() != QProcess::NotRunning) {
            if (m_child.state() == QProcess::Starting) (void)m_child.waitForStarted(-1);
            else { sendKill(); (void)m_child.waitForFinished(-1); }
        }
        collect(process::Channel::Output); collect(process::Channel::Error);
        return status();
    }
};
} // namespace

Result process::Run(const Request& request, const Cancellation& cancelled, const QString& executable,
                    Limits limits, Process& child, const Clock& clock)
{
    const auto is_cancelled = [&] { return cancelled && cancelled->load(); };
    if (is_cancelled()) return Failure(Outcome::Cancelled, QStringLiteral("Wallet directory inspection was cancelled."));
    if (const auto error = Validate(request); !error.isEmpty()) return Failure(Outcome::Rejected, error);
    if (!clock || executable.isEmpty() || executable.contains(QChar{u'\0'}) || limits.stdout_bytes < 0 || limits.stderr_bytes < 0
        || limits.stdout_bytes > MAX_PIPE_BYTES || limits.stderr_bytes > MAX_PIPE_BYTES)
        return Failure(Outcome::Rejected, QStringLiteral("Wallet directory inspection has invalid process limits or executable."));
    if (limits.deadline <= std::chrono::milliseconds::zero())
        return Failure(Outcome::TimedOut, QStringLiteral("Wallet directory inspection timed out before launch."));

    QByteArray output, errors;
    bool attempted{false}, killed{false}, started{false};
    std::optional<Status> reaped;
    const auto kill = [&] { if (!std::exchange(killed, true)) child.kill(); };
    const auto finish = [&](Result result, bool terminate) {
        if (!reaped) {
            if (terminate) kill();
            reaped = child.reap();
        }
        Facts(result, *reaped, started);
        return result;
    };
    try {
        const auto start_time = clock();
        const auto arguments = Arguments(request);
        if (is_cancelled()) return Failure(Outcome::Cancelled, QStringLiteral("Wallet directory inspection was cancelled."));
        if (clock() - start_time >= limits.deadline)
            return Failure(Outcome::TimedOut, QStringLiteral("Wallet directory inspection timed out before launch."));
        if (is_cancelled()) return Failure(Outcome::Cancelled, QStringLiteral("Wallet directory inspection was cancelled."));
        attempted = true; child.start(executable, arguments);
        for (;;) {
            const auto state = child.status();
            started |= state.phase == Phase::Running || state.phase == Phase::Exited;
            output += child.read(Channel::Output, limits.stdout_bytes + 1 - output.size());
            errors += child.read(Channel::Error, limits.stderr_bytes + 1 - errors.size());
            const auto now = clock();
            const bool live = state.phase == Phase::Starting || state.phase == Phase::Running;
            if (is_cancelled()) return finish(Failure(Outcome::Cancelled, QStringLiteral("Wallet directory inspection was cancelled.")), live);
            if (now < start_time) return finish(Failure(Outcome::Failed, QStringLiteral("Wallet directory inspection lost its monotonic clock.")), live);
            if (now - start_time >= limits.deadline)
                return finish(Failure(Outcome::TimedOut, QStringLiteral("Wallet directory inspection timed out.")), live);
            if (output.size() > limits.stdout_bytes || errors.size() > limits.stderr_bytes)
                return finish(Failure(Outcome::Failed, QStringLiteral("The wallet directory helper exceeded its output budget.")), live);
            if (state.phase == Phase::FailedToStart)
                return finish(Failure(Outcome::Failed, QStringLiteral("The wallet directory helper could not start: ") + state.error.left(1024)), false);
            if (state.phase == Phase::Exited) {
                reaped = child.reap();
                const auto& terminal = *reaped;
                // Reap can make final pipe bytes visible; consume them under
                // the same original aggregate budgets before decoding.
                output += child.read(Channel::Output, limits.stdout_bytes + 1 - output.size());
                errors += child.read(Channel::Error, limits.stderr_bytes + 1 - errors.size());
                Result result;
                const auto final_time = clock();
                if (is_cancelled()) result = Failure(Outcome::Cancelled, QStringLiteral("Wallet directory inspection was cancelled."));
                else if (final_time < start_time) result = Failure(Outcome::Failed, QStringLiteral("Wallet directory inspection lost its monotonic clock."));
                else if (final_time - start_time >= limits.deadline) result = Failure(Outcome::TimedOut, QStringLiteral("Wallet directory inspection timed out."));
                else if (output.size() > limits.stdout_bytes || errors.size() > limits.stderr_bytes)
                    result = Failure(Outcome::Failed, QStringLiteral("The wallet directory helper exceeded its output budget."));
                else if (!errors.isEmpty()) result = Failure(Outcome::Failed, QStringLiteral("The wallet directory helper wrote an error: ") + QString::fromUtf8(errors).left(1024));
                else if (terminal.phase != Phase::Exited || !terminal.normal_exit)
                    result = Failure(Outcome::Failed, QStringLiteral("The wallet directory helper terminated abnormally."));
                else {
                    result = Decode(request, output);
                    if (!terminal.exit_status || *terminal.exit_status != 0) {
                        if (result.outcome != Outcome::Failed || result.error.isEmpty())
                            result = Failure(Outcome::Failed, QStringLiteral("The wallet directory helper exited unsuccessfully."));
                    }
                }
                Facts(result, terminal, started); return result;
            }
            if (!errors.isEmpty()) return finish(Failure(Outcome::Failed, QStringLiteral("The wallet directory helper wrote an error: ") + QString::fromUtf8(errors).left(1024)), true);
            child.wait(std::min(WAIT_SLICE, limits.deadline - (now - start_time)));
        }
    } catch (const std::exception&) {
        if (attempted) return finish(Failure(Outcome::Failed, QStringLiteral("The wallet directory helper transport failed.")), true);
        return Failure(Outcome::Failed, QStringLiteral("Wallet directory inspection could not prepare its transport."));
    } catch (...) {
        if (attempted) return finish(Failure(Outcome::Failed, QStringLiteral("The wallet directory helper transport failed.")), true);
        return Failure(Outcome::Failed, QStringLiteral("Wallet directory inspection could not prepare its transport."));
    }
}

Result Run(const Request& request, const Cancellation& cancelled, const QString& executable, Limits limits)
{
    if (cancelled && cancelled->load()) return Failure(Outcome::Cancelled, QStringLiteral("Wallet directory inspection was cancelled."));
    if (QCoreApplication::instance() && QThread::currentThread() == QCoreApplication::instance()->thread())
        return Failure(Outcome::Rejected, QStringLiteral("Wallet directory inspection must run on a Native worker."));
    auto program = executable;
    if (program.isEmpty()) {
        if (!QCoreApplication::instance()) return Failure(Outcome::Rejected, QStringLiteral("The wallet directory helper location is unavailable."));
        program = QDir{QCoreApplication::applicationDirPath()}.filePath(QStringLiteral("bitcoin-core-profile-inspector")
#ifdef Q_OS_WIN
            + QStringLiteral(".exe")
#endif
        );
    }
    if (limits.stdout_bytes < 0 || limits.stderr_bytes < 0 || limits.stdout_bytes > MAX_PIPE_BYTES || limits.stderr_bytes > MAX_PIPE_BYTES)
        return Failure(Outcome::Rejected, QStringLiteral("Wallet directory inspection has invalid process limits."));
    NativeProcess child{limits};
    return process::Run(request, cancelled, program, limits, child, [] {
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch());
    });
}
} // namespace profileinspection
