// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/debuglogmodel.h>
#include <qml/guipresentation.h>
#include <qml/retainedvalue.h>

#include <logging.h>
#include <util/threadnames.h>

#include <algorithm>
#include <map>
#include <stdexcept>
#include <utility>
#include <vector>

#ifdef WIN32
#include <io.h>
#include <qt_windows.h>
#else
#include <sys/stat.h>
#endif

#include <QDateTime>
#include <QDesktopServices>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QObject>
#include <QRegularExpression>
#include <QSet>
#include <QScopeGuard>
#include <QStringList>
#include <QTimer>
#include <QUrl>

static const QRegularExpression TIMESTAMP_RX(
    QStringLiteral(R"(^(?:\[\*\]\s*)?(\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2})(\.\d+)?Z\s*(.*)$)"));
static const QRegularExpression BRACKET_PREFIX_RX(
    QStringLiteral(R"(^\[([^\]]+)\]\s*)"));
static const QRegularExpression LEGACY_LEVEL_RX(
    QStringLiteral(R"(^(ERROR|WARNING):\s*(.*)$)"),
    QRegularExpression::CaseInsensitiveOption);

namespace {
constexpr qint64 TAIL_READ_BLOCK_SIZE{64 * 1024};
constexpr qint64 FILE_ANCHOR_SIZE{256};
constexpr qint64 MAX_DELTA_BYTES{8 * 1024 * 1024};

// Identity is read from the opened file, so a concurrent path replacement
// cannot combine one file's offsets with another file's identity.
std::optional<std::array<uint64_t, 4>> FileIdentity(QFile& file)
{
#ifdef WIN32
    const auto handle = reinterpret_cast<HANDLE>(_get_osfhandle(file.handle()));
    BY_HANDLE_FILE_INFORMATION info{};
    if (handle == INVALID_HANDLE_VALUE || !GetFileInformationByHandle(handle, &info)) return {};
    return std::array<uint64_t, 4>{info.dwVolumeSerialNumber,
        (uint64_t{info.nFileIndexHigh} << 32) | info.nFileIndexLow,
        info.ftLastWriteTime.dwHighDateTime, info.ftLastWriteTime.dwLowDateTime};
#else
    struct stat info{};
    if (fstat(file.handle(), &info) != 0) return {};
#ifdef __APPLE__
    const auto modified = info.st_mtimespec;
#else
    const auto modified = info.st_mtim;
#endif
    return std::array<uint64_t, 4>{static_cast<uint64_t>(info.st_dev), static_cast<uint64_t>(info.st_ino),
        static_cast<uint64_t>(modified.tv_sec), static_cast<uint64_t>(modified.tv_nsec)};
#endif
}

QByteArray ReadAnchor(QFile& file, qint64 file_size)
{
    const qint64 anchor_size = std::min(file_size, FILE_ANCHOR_SIZE);
    if (anchor_size <= 0 || !file.seek(file_size - anchor_size)) return {};
    return file.read(anchor_size);
}

bool IsLogLevel(const QString& value)
{
    static const QSet<QString> levels{
        QStringLiteral("trace"),
        QStringLiteral("debug"),
        QStringLiteral("info"),
        QStringLiteral("warning"),
        QStringLiteral("error"),
    };
    return levels.contains(value);
}

bool IsLogCategory(const QString& value)
{
    static const QSet<QString> categories = [] {
        QSet<QString> result{QStringLiteral("all")};
        for (const LogCategory& category : LogInstance().LogCategoriesList()) {
            result.insert(QString::fromStdString(category.category));
        }
        return result;
    }();
    return categories.contains(value);
}

} // namespace

struct DebugLogModel::State {
    std::optional<std::array<uint64_t, 4>> file_identity;
    QList<LogLine> all, display;
    qint64 file_size{-1};
    QByteArray partial, anchor;
    bool oversized{false}, has_more{false}, memory_limited{false};
    int loaded_limit{0};
};
struct DebugLogModel::Update {
    std::shared_ptr<const QList<LogLine>> display;
    bool file_opened{false}, did_read{false}, has_more{false}, force_reset{false};
    QString error, watch_file, watch_parent;
    int newly_completed{0}, loaded_limit{0};
    int old_start{0}, new_start{0}, common{0};
    quint64 generation{0}, activation_generation{0};
};
struct DebugLogModel::DrainState {
    std::atomic_bool retired{false};
    std::function<void()> notify;
};
struct DebugLogModel::Mailbox {
    explicit Mailbox(std::shared_ptr<DrainState> value) : drain(std::move(value)) {}
    ~Mailbox() {
        // The original scheduler's pre-acquired resource owns all final Native
        // payload destruction, even when ordinary/eager cleanup is rejected.
        releaseRetired(); update.reset();
        drain->retired = true;
        try { drain->notify(); } catch (...) {}
    }
    std::shared_ptr<DrainState> drain;
    std::mutex mutex;
    std::shared_ptr<Update> update;
    std::vector<std::shared_ptr<const void>> retired;
    bool retirement_posted{false}, retirement_failed{false};
    void releaseRetired() {
        std::vector<std::shared_ptr<const void>> values;
        { std::lock_guard lock{mutex}; values.swap(retired); }
        values.clear();
        { std::lock_guard lock{mutex}; retirement_failed = false; }
    }
    bool closed{false};
    std::atomic_bool cancelled{false};
    std::atomic_size_t pending{0};
    std::function<void()> notify;
    struct Pending {
        std::shared_ptr<Mailbox> owner;
        explicit Pending(std::shared_ptr<Mailbox> value) : owner(std::move(value)) { ++owner->pending; }
        ~Pending() { --owner->pending; try { owner->notify(); } catch (...) {} }
    };
    struct Retirement {
        std::shared_ptr<Mailbox> owner;
        explicit Retirement(std::shared_ptr<Mailbox> value) : owner(std::move(value)) {}
        ~Retirement() {
            // Packet destruction follows TaskQueue's actual ledger erasure.
            // Native value release alone does not free its Cleanup task slot.
            { std::lock_guard lock{owner->mutex}; owner->retirement_posted = false; }
            try { owner->notify(); } catch (...) {}
        }
    };
};

DebugLogModel::DebugLogModel(const fs::path& path, bool log_to_file, QObject* parent)
    : DebugLogModel(path, log_to_file, execution::SnapshotServices{}, parent) {}

struct DebugLogModel::Construction {
    fs::path path;
    QPointer<QObject> parent;
    bool had_parent{false};
    std::unique_ptr<BackendExecutor> executor;
    execution::SnapshotServices services;
    std::shared_ptr<DrainState> drain;
    std::shared_ptr<Mailbox> mailbox;
};

DebugLogModel::Construction DebugLogModel::prepareConstruction(const fs::path& path, execution::SnapshotServices services, QObject* parent)
{
    // An injected reservation may destroy the original parent. No partially
    // constructed QObject may be attached during that admission boundary.
    Construction prepared;
    prepared.path = path;
    prepared.parent = parent; prepared.had_parent = parent != nullptr;
    prepared.services = std::move(services);
    if (!prepared.services.scheduler) {
        prepared.executor = std::make_unique<BackendExecutor>(execution::Options{});
        prepared.services.scheduler = prepared.executor->scheduler();
    }
    prepared.services.post = presentation::ReliablePost(std::move(prepared.services.post));
    prepared.drain = std::make_shared<DrainState>();
    prepared.mailbox = execution::RetainValues<Mailbox>(prepared.services.scheduler,
        [drain = prepared.drain] { return std::make_unique<Mailbox>(drain); });
    if (prepared.had_parent && !prepared.parent)
        throw std::runtime_error{"The original debug-log owner is no longer available"};
    return prepared;
}

DebugLogModel::DebugLogModel(const fs::path& path, bool log_to_file, execution::SnapshotServices services, QObject* parent)
    : DebugLogModel(log_to_file, prepareConstruction(path, std::move(services), parent)) {}

DebugLogModel::DebugLogModel(bool log_to_file, Construction construction)
    : QAbstractListModel(construction.parent.data()), m_log_path(std::move(construction.path)), m_log_to_file(log_to_file),
      m_state(construction.mailbox ? std::make_shared<State>() : nullptr),
      m_open_local_file_fn([](const QString& value) { return QDesktopServices::openUrl(QUrl::fromLocalFile(value)); }),
      m_executor(std::move(construction.executor)), m_services(std::move(construction.services)),
      m_mailbox(std::move(construction.mailbox)), m_drain_state(std::move(construction.drain))
{
    if (m_executor) m_executor->setParent(this);
    const QPointer<DebugLogModel> observer{this};
    m_drain_state->notify = [observer, post = m_services.post] {
        post([observer] { if (observer) observer->receiveUpdate(); });
    };
    if (m_mailbox) m_mailbox->notify = m_drain_state->notify;
    else {
        m_stopping = true; m_drain_state->retired = true;
        m_open_error = tr("Debug log cleanup is unavailable.");
    }
    m_debounce.setSingleShot(true); m_debounce.setInterval(500);
    connect(&m_debounce, &QTimer::timeout, this, [this] { refresh(); });
    m_apply_timer.setSingleShot(true);
    connect(&m_apply_timer, &QTimer::timeout, this, &DebugLogModel::applyUpdate);
    if (m_executor) connect(m_executor.get(), &BackendExecutor::drained, this, &DebugLogModel::finishDrain);
    connectFileWatcher();
    if (!m_log_to_file) {
        m_open_error = tr("Writing the debug log to a file is turned off.");
        m_log_available = false;
    }
}

DebugLogModel::~DebugLogModel()
{
    // Owned packets and the pre-reserved cleanup anchor survive observer loss.
    m_destroying = true;
    stop();
    m_changing_rows = false;
    finishStop();
}

int DebugLogModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid()) return 0;
    return int(m_display_lines.size());
}

QVariant DebugLogModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || size_t(index.row()) >= m_display_lines.size())
        return {};

    const LogLine& line = m_display_lines.at(size_t(index.row()));
    switch (role) {
    case MessageRole:   return line.message;
    case TimestampRole: return line.timestamp;
    case IsErrorRole:   return line.is_error;
    case IsWarningRole: return line.is_warning;
    }
    return {};
}

QHash<int, QByteArray> DebugLogModel::roleNames() const
{
    return {
        {MessageRole,   "message"},
        {TimestampRole, "timestamp"},
        {IsErrorRole,   "isError"},
        {IsWarningRole, "isWarning"},
    };
}

void DebugLogModel::setLoadLimit(int limit)
{
    if (m_stopping) return;
    limit = std::clamp(limit, 1, kMaxLoadLimit);
    if (m_load_limit == limit) return;
    const QPointer<DebugLogModel> observer{this};
    const auto interest = ++m_interest_revision;
    const int previous_limit = m_load_limit;
    m_load_limit = limit;
    Q_EMIT loadLimitChanged();
    if (!observer || m_stopping || m_interest_revision != interest) return;

    if (m_load_limit < previous_limit) buildDisplayLines(false);
    else if (m_active) refresh(true);
}

void DebugLogModel::setActive(bool active)
{
    if (m_stopping) return;
    if (m_active == active) return;

    const QPointer<DebugLogModel> observer{this};
    const auto interest = ++m_interest_revision;
    m_active = active;
    ++m_activation_generation;
    Q_EMIT activeChanged();
    if (!observer || m_stopping || m_interest_revision != interest) return;

    if (m_active) {
        watchLogPath();
        // Retained rows can paint immediately on reactivation. Catch up from
        // the saved file offset unless the retained tail is too narrow.
        refresh(/*full_load=*/m_loaded_limit < m_load_limit);
        return;
    }

    m_debounce.stop();
    const auto watched_files = m_watcher.files();
    if (!watched_files.isEmpty()) {
        m_watcher.removePaths(watched_files);
    }
    const auto watched_directories = m_watcher.directories();
    if (!watched_directories.isEmpty()) {
        m_watcher.removePaths(watched_directories);
    }
    m_refresh_pending = false;
    m_pending_full_load = false;
}

void DebugLogModel::setFilter(const QString& filter)
{
    if (m_stopping) return;
    const auto bounded = filter.left(256);
    if (m_filter == bounded) return;
    const QPointer<DebugLogModel> observer{this};
    const auto interest = ++m_interest_revision;
    m_filter = bounded;
    Q_EMIT filterChanged();
    if (!observer || m_stopping || m_interest_revision != interest) return;
    // Cached rows remain observable while inactive, so keep the display
    // projection in sync even when the page is currently unloaded.
    buildDisplayLines(/*force_reset=*/true);
}

void DebugLogModel::setWarningsAndErrorsOnly(bool warnings_and_errors_only)
{
    if (m_stopping) return;
    if (m_warnings_and_errors_only == warnings_and_errors_only) return;
    const QPointer<DebugLogModel> observer{this};
    const auto interest = ++m_interest_revision;
    m_warnings_and_errors_only = warnings_and_errors_only;
    Q_EMIT warningsAndErrorsOnlyChanged();
    if (!observer || m_stopping || m_interest_revision != interest) return;
    buildDisplayLines(/*force_reset=*/true);
}

void DebugLogModel::refresh(bool full_load)
{
    if (!m_active || m_stopping || !m_log_to_file) return;
    const QPointer<DebugLogModel> observer{this};
    const auto mailbox = m_mailbox;
    const auto services = m_services;
    const auto generation = m_work_generation;
    retireValue(mailbox, services, {}, true); // Explicit interest retries a retained rejected cleanup.
    if (!observer || m_stopping || m_mailbox != mailbox || m_work_generation != generation) return;
    requestUpdate(true, full_load, false);
}

void DebugLogModel::requestUpdate(bool read_file, bool full_load, bool force_reset)
{
    if (m_stopping) return;
    if (m_read_in_flight) {
        m_refresh_pending |= read_file;
        m_pending_full_load |= full_load;
        m_projection_pending = true;
        m_force_reset_pending |= force_reset;
        return;
    }
    force_reset |= std::exchange(m_force_reset_pending, false);
    bool backpressure{false}, cleanup_failed{false}, cleanup_pending{false};
    {
        std::lock_guard lock{m_mailbox->mutex};
        backpressure = m_mailbox->retired.size() >= 8;
        cleanup_failed = m_mailbox->retirement_failed;
        cleanup_pending = m_mailbox->retirement_posted;
    }
    if (backpressure) {
        m_waiting_retirement = !cleanup_failed || cleanup_pending;
        m_refresh_pending |= read_file; m_pending_full_load |= full_load;
        m_projection_pending = true; m_force_reset_pending |= force_reset;
        const QPointer<DebugLogModel> observer{this};
        const auto generation = m_work_generation;
        const bool changed = cleanup_failed && m_retirement_error.isEmpty();
        if (changed) m_retirement_error = tr("Previous debug log output could not be released. Refresh to try again.");
        Q_EMIT busyChanged();
        if (!observer || m_stopping || m_work_generation != generation) return;
        if (changed) Q_EMIT openErrorChanged();
        return;
    }
    const auto mailbox = m_mailbox;
    const auto services = m_services;
    auto state = m_state;
    const auto retire_state = qScopeGuard([&] { retireValue(mailbox, services, std::move(state)); });
    const auto path = m_log_path;
    const auto limit = m_load_limit;
    const auto filter = m_filter.left(256);
    const auto warnings = m_warnings_and_errors_only;
    const auto generation = ++m_work_generation;
    const auto activation = m_activation_generation;
    const QPointer<DebugLogModel> observer{this};
    const auto owns = [observer, mailbox, generation] {
        return observer && !observer->m_stopping && observer->m_mailbox == mailbox && observer->m_work_generation == generation;
    };
    m_read_in_flight = true;
    Q_EMIT busyChanged();
    if (!owns()) return;
    const auto pending = std::make_shared<Mailbox::Pending>(mailbox);
    const auto admission = execution::Submit(*services.scheduler,
        [state, mailbox, pending, path, limit, filter, warnings, generation, activation, read_file, full_load, force_reset] {
            ReadResult read;
            if (read_file) read = ReadAndFilter(path, limit, full_load, state->file_size,
                state->partial, state->oversized, state->anchor, state->file_identity, mailbox->cancelled);
            if (read_file && !read.file_opened && read.error_message.isEmpty() && !mailbox->cancelled.load())
                read.error_message = QObject::tr("Could not finish reading the debug log. Refresh to try again.");
            auto update = Reduce(*state, std::move(read), read_file, limit, filter, warnings,
                                 force_reset, QString::fromStdString(path.utf8string()), generation);
            update->activation_generation = activation;
            return update;
        }, [mailbox, pending](std::shared_ptr<Update> update) {
            { std::lock_guard lock{mailbox->mutex}; if (!mailbox->closed) mailbox->update = std::move(update); }
            mailbox->notify();
        }, [mailbox, state, pending, generation, activation, read_file](std::exception_ptr) {
            auto failed = std::make_shared<Update>();
            failed->display = std::make_shared<const QList<LogLine>>(state->display);
            failed->generation = generation; failed->activation_generation = activation;
            failed->did_read = read_file; failed->error = QObject::tr("Could not read debug log.");
            { std::lock_guard lock{mailbox->mutex}; if (!mailbox->closed) mailbox->update = std::move(failed); }
            mailbox->notify();
        }, {execution::Lane::Core, {}, execution::WorkClass::Observation});
    if (!owns()) return;
    if (!admission) {
        m_read_in_flight = false;
        Q_EMIT busyChanged();
        if (!owns()) return;
        m_open_error = tr("Debug log work could not be scheduled.");
        Q_EMIT openErrorChanged();
    }
}

void DebugLogModel::loadMore()
{
    if (m_stopping) return;
    const QPointer<DebugLogModel> observer{this};
    const auto interest = ++m_interest_revision;
    if (m_load_limit >= kMaxLoadLimit) {
        if (m_has_more_lines) {
            m_has_more_lines = false;
            Q_EMIT hasMoreLinesChanged();
        }
        return;
    }
    m_load_limit = std::min(m_load_limit + 1000, kMaxLoadLimit);
    Q_EMIT loadLimitChanged();
    if (!observer || m_stopping || m_interest_revision != interest) return;
    refresh(true);
}

bool DebugLogModel::openLogFile()
{
    const QString path_str = QString::fromStdString(m_log_path.utf8string());
    // A missing log is a state of the log, not the outcome of this click, so
    // it is left to the background reader to report and keep reporting. The
    // UI disables this action while the log is unavailable; the check remains
    // because the method is invokable from QML at any time. With file logging
    // off, a file at the path is stale and must not be handed out either.
    if (m_stopping || !m_log_to_file || !m_log_available) return false;
    const QPointer<DebugLogModel> observer{this};
    const auto action = ++m_native_action_revision;
    const auto handoff = m_open_local_file_fn;
    const bool ok = handoff(path_str);
    // Native handoff success/failure remains the actual return outcome. Only
    // its original live presentation may adopt an error or clear an old one.
    if (!observer || m_stopping || m_native_action_revision != action) return ok;
    if (!ok) {
        m_external_open_error = tr("Could not open debug log file. "
                                   "No application is associated with this file type.");
        Q_EMIT openErrorChanged();
        return false;
    }
    if (!m_external_open_error.isEmpty()) {
        m_external_open_error.clear();
        Q_EMIT openErrorChanged();
    }
    return true;
}

void DebugLogModel::clearOpenError()
{
    if (m_stopping || m_external_open_error.isEmpty()) return;
    ++m_native_action_revision;
    m_external_open_error.clear();
    Q_EMIT openErrorChanged();
}

void DebugLogModel::retireValue(const std::shared_ptr<Mailbox>& anchor,
                                    const execution::SnapshotServices& services, std::shared_ptr<const void> value, bool retry)
{
    const auto scheduler = services.scheduler;
    const auto post = services.post;
    {
        std::lock_guard lock{anchor->mutex};
        const auto found = std::find_if(anchor->retired.begin(), anchor->retired.end(),
            [&](const auto& held) {
                return held.get() == value.get() && !held.owner_before(value) && !value.owner_before(held);
            });
        if (value) {
            if (found == anchor->retired.end()) anchor->retired.push_back(std::move(value));
            else *found = std::move(value); // Empty the GUI owner while the identical Native owner is protected.
        }
        if (anchor->retired.empty() || anchor->retirement_posted || (anchor->retirement_failed && !retry)) return;
        anchor->retirement_posted = true;
    }
    // Submit outside the caller's model transaction, retaining only original
    // values/ports. No facade continuation crosses injected admission reentry.
    post([anchor, scheduler] {
        const auto retirement = std::make_shared<Mailbox::Retirement>(anchor);
        const auto failed = [anchor, retirement](std::exception_ptr) {
            (void)retirement; // Keep the attempt latched through the actual packet lifetime.
            std::lock_guard lock{anchor->mutex};
            // An empty inventory needs no retry, even if admission failed.
            anchor->retirement_failed = !anchor->retired.empty();
        };
        try {
            const auto admission = execution::Submit(*scheduler,
                [anchor, retirement] { (void)retirement; anchor->releaseRetired(); },
                [retirement] { (void)retirement; }, failed,
                {execution::Lane::Cleanup, {}, execution::WorkClass::Action});
            if (!admission) failed({});
        } catch (...) { failed(std::current_exception()); }
    });
}

void DebugLogModel::stop()
{
    if (std::exchange(m_shutdown_started, true)) return;
    m_stopping = true; ++m_work_generation; ++m_interest_revision;
    m_debounce.stop(); m_apply_timer.stop();
    if (m_mailbox) {
        m_mailbox->cancelled = true;
        std::lock_guard lock{m_mailbox->mutex};
        m_mailbox->closed = true;
    }
    const auto files = m_watcher.files(); if (!files.isEmpty()) m_watcher.removePaths(files);
    const auto dirs = m_watcher.directories(); if (!dirs.isEmpty()) m_watcher.removePaths(dirs);
    // Preserve the declared original row count through the matching end.
    if (!m_changing_rows) finishStop();
}

void DebugLogModel::finishStop()
{
    if (!m_shutdown_started || m_changing_rows) return;
    const auto mailbox = m_mailbox;
    if (mailbox) {
        auto display = std::make_shared<const std::deque<LogLine>>(std::exchange(m_display_lines, {}));
        {
            std::lock_guard lock{mailbox->mutex};
            if (mailbox->update) mailbox->retired.push_back(std::exchange(mailbox->update, {}));
            if (m_update) mailbox->retired.push_back(std::exchange(m_update, {}));
            mailbox->retired.push_back(std::move(display));
            if (m_state) mailbox->retired.push_back(std::exchange(m_state, {}));
        }
        m_mailbox.reset();
    }
    const QPointer<DebugLogModel> observer{this};
    if (m_executor) m_executor->shutdown();
    if (observer) observer->finishDrain();
}

void DebugLogModel::finishDrain()
{
    // Final Native destruction is part of the same injected/owned ledger as
    // the original read. A detached facade cannot hide a private worker.
    if (m_destroying || !m_shutdown_started || m_drained || !m_drain_state->retired ||
        (m_executor && !m_executor->isDrained())) return;
    m_drained = true; Q_EMIT drained();
}

// ── Private ──────────────────────────────────────────────────────────────────

DebugLogModel::ReadResult DebugLogModel::ReadAndFilter(const fs::path& log_path,
                                                      int load_limit,
                                                      bool full_load,
                                                      qint64 previous_file_size,
                                                      const QByteArray& previous_partial,
                                                      bool previous_discarding_oversized_line,
                                                      const QByteArray& previous_anchor,
                                                      const std::optional<std::array<uint64_t, 4>>& previous_identity,
                                                      const std::atomic_bool& cancelled)
{
    if (cancelled.load(std::memory_order_relaxed)) return {};
    if (full_load || previous_file_size < 0) {
        return ReadTail(log_path, load_limit, cancelled);
    }

    const QString path_str = QString::fromStdString(log_path.utf8string());
    QFile file(path_str);
    ReadResult result;
    if (!file.open(QIODevice::ReadOnly)) {
        result.error_message = fs::exists(log_path)
            ? QObject::tr("Could not open debug log file: %1").arg(path_str)
            : QObject::tr("Debug log file not found: %1").arg(path_str);
        return result;
    }
    result.file_opened = true;
    result.file_identity = FileIdentity(file);

    // Validate bytes at the old end-of-file before trusting the saved offset.
    // Identity detects replacement even when the suffix is unchanged. The
    // anchor additionally detects truncation/rewrite of the same open file.
    const qint64 snapshot_size = file.size();
    bool continuity_valid = result.file_identity && previous_identity
        && (*result.file_identity)[0] == (*previous_identity)[0]
        && (*result.file_identity)[1] == (*previous_identity)[1]
        && (snapshot_size != previous_file_size || result.file_identity == previous_identity)
        && snapshot_size >= previous_file_size
        && previous_partial.size() <= previous_file_size;
    if (continuity_valid && previous_file_size > 0) {
        continuity_valid = !previous_anchor.isEmpty()
            && file.seek(previous_file_size - previous_anchor.size())
            && file.read(previous_anchor.size()) == previous_anchor;
    }
    if (!continuity_valid) {
        file.close();
        result = ReadTail(log_path, load_limit, cancelled);
        result.continuity_lost = result.file_opened;
        return result;
    }

    const qint64 appended_size = snapshot_size - previous_file_size;
    if (appended_size > MAX_DELTA_BYTES
        || previous_partial.size() > MAX_DELTA_BYTES - appended_size) {
        file.close();
        return ReadTail(log_path, load_limit, cancelled);
    }

    if (cancelled.load(std::memory_order_relaxed)
        || !file.seek(previous_file_size)) {
        return {};
    }
    const QByteArray appended = file.read(appended_size);
    if (appended.size() != appended_size) {
        file.close();
        result = ReadTail(log_path, load_limit, cancelled);
        result.continuity_lost = result.file_opened;
        return result;
    }

    QByteArray combined;
    qint64 combined_offset{previous_file_size - previous_partial.size()};
    if (previous_discarding_oversized_line) {
        const qsizetype newline = appended.indexOf('\n');
        if (newline < 0) {
            result.full_snapshot = false;
            result.file_size = snapshot_size;
            result.file_anchor = ReadAnchor(file, snapshot_size);
            result.discarding_oversized_line = true;
            return result;
        }
        combined = appended.mid(newline + 1);
        combined_offset = previous_file_size + newline + 1;
    } else {
        combined = previous_partial + appended;
    }

    const qsizetype last_newline = combined.lastIndexOf('\n');
    result.full_snapshot = false;
    result.file_size = snapshot_size;
    result.file_anchor = ReadAnchor(file, snapshot_size);
    if (last_newline < 0) {
        if (combined.size() <= kMaxLogLineBytes) {
            result.trailing_partial = combined;
        } else {
            result.discarding_oversized_line = true;
        }
        return result;
    }

    const qsizetype complete_size = last_newline + 1;
    result.trailing_partial = combined.mid(complete_size);
    if (result.trailing_partial.size() > kMaxLogLineBytes) {
        result.trailing_partial.clear();
        result.discarding_oversized_line = true;
    }
    result.lines = ParseCompleteLines(
        combined.first(complete_size),
        combined_offset,
        std::min(load_limit, kMaxLoadLimit) + 1,
        cancelled);
    result.has_more_lines = result.lines.size() > load_limit;
    return result;
}

DebugLogModel::ReadResult DebugLogModel::ReadTail(const fs::path& log_path,
                                                 int load_limit,
                                                 const std::atomic_bool& cancelled)
{
    ReadResult result;
    if (cancelled.load(std::memory_order_relaxed)) return result;

    const QString path_str = QString::fromStdString(log_path.utf8string());
    QFile file(path_str);
    if (!file.open(QIODevice::ReadOnly)) {
        result.error_message = fs::exists(log_path)
            ? QObject::tr("Could not open debug log file: %1").arg(path_str)
            : QObject::tr("Debug log file not found: %1").arg(path_str);
        return result;
    }

    result.file_opened = true;
    result.file_identity = FileIdentity(file);
    result.full_snapshot = true;
    result.snapshot_limit = std::min(load_limit, kMaxLoadLimit);
    result.file_size = file.size();
    result.file_anchor = ReadAnchor(file, result.file_size);

    // Find the final newline without accumulating the unfinished suffix on
    // every backward step. Bitcoin Core normally writes newline-terminated
    // entries, but the viewer can observe an in-progress or interrupted write.
    qint64 complete_end{-1};
    qint64 cursor = result.file_size;
    qint64 scanned{0};
    while (cursor > 0) {
        if (cancelled.load(std::memory_order_relaxed)) return {};
        if (scanned >= MAX_DELTA_BYTES) {
            result.memory_limited = result.has_more_lines = true;
            result.discarding_oversized_line = true;
            return result;
        }
        const qint64 start = std::max(qint64{0}, cursor - TAIL_READ_BLOCK_SIZE);
        if (!file.seek(start)) return {};
        const QByteArray chunk = file.read(cursor - start);
        scanned += cursor - start;
        if (chunk.size() != cursor - start) return {};
        const qsizetype last_newline = chunk.lastIndexOf('\n');
        if (last_newline >= 0) {
            complete_end = start + last_newline + 1;
            break;
        }
        cursor = start;
    }

    const qint64 partial_start = std::max(qint64{0}, complete_end);
    const qint64 partial_size = result.file_size - partial_start;
    if (partial_size > kMaxLogLineBytes) {
        result.discarding_oversized_line = true;
    } else if (partial_size > 0) {
        if (!file.seek(partial_start)) return {};
        result.trailing_partial = file.read(partial_size);
        if (result.trailing_partial.size() != partial_size) return {};
    }
    if (complete_end < 0) {
        return result;
    }

    const int target = std::min(load_limit, kMaxLoadLimit) + 1;
    QList<LogLine> newest_first;
    qsizetype retained_bytes{0};
    bool byte_limited{false};
    newest_first.reserve(target);
    QByteArray carry;
    bool discarding_oversized_line{false};
    cursor = complete_end;

    // Process complete lines from newest to oldest. At most one boundary-
    // spanning line is carried. If it exceeds the viewer limit, discard its
    // remaining prefix until the preceding newline restores framing.
    while (cursor > 0 && newest_first.size() < target) {
        if (cancelled.load(std::memory_order_relaxed)) return {};
        if (scanned >= MAX_DELTA_BYTES) { byte_limited = true; break; }
        const qint64 start = std::max(qint64{0}, cursor - TAIL_READ_BLOCK_SIZE);
        if (!file.seek(start)) return {};
        const QByteArray chunk = file.read(cursor - start);
        scanned += cursor - start;
        if (chunk.size() != cursor - start) return {};

        QByteArray data;
        if (discarding_oversized_line) {
            const qsizetype preceding_newline = chunk.lastIndexOf('\n');
            if (preceding_newline < 0) {
                cursor = start;
                continue;
            }
            data = chunk.first(preceding_newline + 1);
            discarding_oversized_line = false;
        } else {
            data = chunk + carry;
        }

        QByteArray complete_segment;
        qint64 segment_offset{start};
        if (start == 0) {
            complete_segment = data;
            carry.clear();
        } else {
            const qsizetype first_newline = data.indexOf('\n');
            if (first_newline < 0) {
                if (data.size() > kMaxLogLineBytes) {
                    carry.clear();
                    discarding_oversized_line = true;
                } else {
                    carry = data;
                }
                cursor = start;
                continue;
            }
            complete_segment = data.mid(first_newline + 1);
            segment_offset = start + first_newline + 1;
            carry = data.first(first_newline + 1);
            if (carry.size() > kMaxLogLineBytes) {
                carry.clear();
                discarding_oversized_line = true;
            }
        }

        QList<LogLine> parsed = ParseCompleteLines(
            complete_segment,
            segment_offset,
            target - newest_first.size(),
            cancelled);
        for (auto& line : parsed) {
            const qsizetype bytes = sizeof(LogLine) + 2 * (line.message.size() + line.timestamp.size());
            if (retained_bytes + bytes > MAX_DELTA_BYTES) { byte_limited = true; break; }
            retained_bytes += bytes;
            newest_first.append(std::move(line));
        }
        if (byte_limited) break;
        cursor = start;
    }

    result.has_more_lines = byte_limited || newest_first.size() > load_limit;
    if (newest_first.size() > load_limit) newest_first.removeLast();
    result.memory_limited = byte_limited;
    result.lines = std::move(newest_first);
    return result;
}

QList<DebugLogModel::LogLine> DebugLogModel::ParseCompleteLines(
    const QByteArray& bytes,
    qint64 base_offset,
    int max_filtered_lines,
    const std::atomic_bool& cancelled)
{
    QList<LogLine> result;
    result.reserve(std::min<int>(max_filtered_lines, 1024));
    qsizetype scan_end = bytes.size();
    while (scan_end > 0 && result.size() < max_filtered_lines) {
        if (cancelled.load(std::memory_order_relaxed)) return {};
        const qsizetype terminator = bytes.at(scan_end - 1) == '\n'
            ? scan_end - 1
            : scan_end;
        const qsizetype previous_newline = terminator > 0
            ? bytes.lastIndexOf('\n', terminator - 1)
            : -1;
        const qsizetype start = previous_newline + 1;
        const qsizetype line_size = terminator - start;
        if (line_size <= kMaxLogLineBytes) {
            QByteArray raw = bytes.mid(start, line_size);
            if (raw.endsWith('\r')) raw.chop(1);

            LogLine entry;
            entry.source_offset = base_offset + start;
            QString raw_message;
            const QString line = QString::fromUtf8(raw);
            const QRegularExpressionMatch match = TIMESTAMP_RX.match(line);
            if (match.hasMatch()) {
                entry.timestamp = FormatTime(match.captured(1));
                raw_message = match.captured(3);
                if (line.startsWith(QLatin1String("[*]"))) {
                    raw_message.prepend(QStringLiteral("[*] "));
                }
            } else {
                raw_message = line;
            }
            PopulateParsedFields(entry, raw_message);
            if (!entry.message.isEmpty() || !entry.timestamp.isEmpty()) {
                result.append(std::move(entry));
            }
        }

        if (previous_newline < 0) break;
        scan_end = previous_newline + 1;
    }
    return result;
}

std::shared_ptr<DebugLogModel::Update> DebugLogModel::Reduce(State& state, ReadResult read,
    bool did_read, int limit, const QString& filter, bool warnings_only, bool force_reset,
    const QString& path, quint64 generation)
{
    auto result = std::make_shared<Update>();
    result->generation = generation; result->did_read = did_read;
    result->file_opened = read.file_opened; result->error = read.error_message;
    // Filesystem facts are obtained here; QFileSystemWatcher registration is
    // the only necessary GUI/native handoff left in watchLogPath().
    const QFileInfo info(path);
    result->watch_file = info.isFile() ? path : QString{};
    result->watch_parent = info.absolutePath();
    if (did_read && read.file_opened) {
        state.file_identity = read.file_identity;
        state.file_size = read.file_size; state.partial = std::move(read.trailing_partial);
        state.oversized = read.discarding_oversized_line; state.anchor = std::move(read.file_anchor);
        if (read.full_snapshot) {
            if (read.lines.size() > limit) read.lines.resize(limit);
            std::reverse(read.lines.begin(), read.lines.end());
            state.all = std::move(read.lines);
            state.loaded_limit = std::min(read.snapshot_limit, limit);
            state.has_more = read.has_more_lines;
            state.memory_limited = read.memory_limited;
        } else {
            result->newly_completed = read.lines.size();
            std::reverse(read.lines.begin(), read.lines.end());
            state.all.append(read.lines);
            state.has_more |= read.has_more_lines;
        }
        force_reset |= read.continuity_lost;
    }
    qsizetype bytes{0};
    int keep{0};
    bool byte_limited{false};
    for (int i = state.all.size() - 1; i >= 0 && keep < limit; --i) {
        const auto& line = state.all[i];
        const qsizetype size = sizeof(LogLine) + 2 * (line.message.size() + line.timestamp.size());
        if (bytes + size > MAX_DELTA_BYTES) { byte_limited = true; break; }
        bytes += size; ++keep;
    }
    if (keep < state.all.size()) { state.all = state.all.last(keep); state.has_more = true; }
    state.memory_limited |= byte_limited;
    state.loaded_limit = std::min(state.loaded_limit, limit);
    result->has_more = state.has_more && limit < kMaxLoadLimit;
    result->loaded_limit = state.loaded_limit;
    auto display = std::make_shared<QList<LogLine>>();
    qsizetype display_bytes{0};
    bool display_limited{state.memory_limited};
    // Retain the newest records under both budgets. Chunks are temporarily
    // accumulated newest first, then reversed on this background lane.
    for (auto entry = state.all.crbegin(); entry != state.all.crend(); ++entry) {
        const auto& line = *entry;
        if (warnings_only && !line.is_error && !line.is_warning) continue;
        if (!filter.isEmpty()) {
            const QString searchable = line.timestamp + QLatin1Char(' ') +
                (line.is_error ? QStringLiteral("error ") : line.is_warning ? QStringLiteral("warning ") : QStringLiteral("regular ")) + line.message;
            if (!searchable.contains(filter, Qt::CaseInsensitive)) continue;
        }
        QList<LogLine> chunks;
        for (qsizetype offset = 0, chunk = 0; offset < std::max(qsizetype{1}, line.message.size()); ++chunk) {
            auto row = line;
            qsizetype count = std::min(qsizetype{1024}, line.message.size() - offset);
            if (count > 0 && offset + count < line.message.size() && line.message[offset + count - 1].isHighSurrogate()) --count;
            row.message = line.message.mid(offset, count); row.chunk = chunk;
            if (chunk) row.timestamp.clear();
            chunks.append(std::move(row)); offset += std::max(qsizetype{1}, count);
        }
        bool full{false};
        for (auto row = chunks.crbegin(); row != chunks.crend(); ++row) {
            const qsizetype bytes = sizeof(LogLine) + 2 * (row->message.size() + row->timestamp.size());
            if (display->size() >= kMaxLoadLimit - 1 || display_bytes + bytes > MAX_DELTA_BYTES - 1024) { full = display_limited = true; break; }
            display_bytes += bytes; display->append(*row);
        }
        if (full) break;
    }
    std::reverse(display->begin(), display->end());
    if (display_limited) {
        LogLine notice;
        notice.message = QObject::tr("[Older output omitted by the viewer limit. Open debug.log for the full log.]");
        display->prepend(std::move(notice));
    }
    // Calculate the stable contiguous overlap off GUI. Physical byte offset
    // plus chunk ordinal remains stable across incremental append/filtering.
    if (!force_reset) {
        // Qt 6.4's pair-key hash combiner shifts its unsigned seed with
        // intentional wraparound, which fails the pinned integer checks.
        // This Domain-local index needs exact key lookup, not hashing.
        std::map<std::pair<qint64, int>, int> positions;
        for (int i = 0; i < display->size(); ++i) positions.insert_or_assign({display->at(i).source_offset, display->at(i).chunk}, i);
        for (int i = 0; i < state.display.size(); ++i) {
            const auto& row = state.display.at(i);
            const auto found = positions.find({row.source_offset, row.chunk});
            if (found == positions.cend() || !(row == display->at(found->second))) continue;
            result->old_start = i; result->new_start = found->second;
            while (i + result->common < state.display.size() && found->second + result->common < display->size()
                && state.display.at(i + result->common) == display->at(found->second + result->common)) ++result->common;
            break;
        }
    }
    result->force_reset = force_reset || (did_read && read.full_snapshot && state.display.isEmpty());
    state.display = *display;
    result->display = std::move(display);
    return result;
}

void DebugLogModel::receiveUpdate()
{
    if (m_stopping) { m_read_in_flight = false; finishDrain(); return; }
    // Retain the original resource before any signal. Every exit returns its
    // local frame to that Native retirement owner, even after facade deletion.
    const auto mailbox = m_mailbox;
    const auto services = m_services;
    const QPointer<DebugLogModel> observer{this};
    const auto generation = m_work_generation;
    const auto owns = [observer, mailbox, generation] {
        return observer && !observer->m_stopping && observer->m_mailbox == mailbox && observer->m_work_generation == generation;
    };
    std::shared_ptr<Update> update;
    bool released{false}, failed{false}, cleanup_pending{false}, retry_cleanup{false};
    {
        std::lock_guard lock{mailbox->mutex};
        update = std::exchange(mailbox->update, {});
        released = mailbox->retired.size() < 8;
        failed = mailbox->retirement_failed;
        cleanup_pending = mailbox->retirement_posted;
        retry_cleanup = !mailbox->retired.empty() && !cleanup_pending && !failed;
    }
    const auto retire_frame = qScopeGuard([&] { if (update) retireValue(mailbox, services, std::move(update)); });
    if (failed) {
        const bool changed = m_retirement_error.isEmpty();
        if (changed) m_retirement_error = tr("Previous debug log output could not be released. Refresh to try again.");
        if (m_waiting_retirement && !cleanup_pending) {
            m_waiting_retirement = false;
            Q_EMIT busyChanged();
            if (!owns()) return;
        }
        if (changed) { Q_EMIT openErrorChanged(); if (!owns()) return; }
    } else if (released && !m_retirement_error.isEmpty()) {
        m_retirement_error.clear(); Q_EMIT openErrorChanged();
        if (!owns()) return;
    }
    if (retry_cleanup) {
        // Actual retirement freed the original task slot. Values staged after
        // its Native swap now have one coalesced, release-driven successor.
        retireValue(mailbox, services, {});
        if (!owns()) return;
    }
    if (!update) {
        if (m_waiting_retirement && released && !failed) {
            m_waiting_retirement = false;
            const bool read = std::exchange(m_refresh_pending, false);
            const bool full = std::exchange(m_pending_full_load, false);
            m_projection_pending = false;
            requestUpdate(read && m_active, full, std::exchange(m_force_reset_pending, false));
        }
        return;
    }
    if (update->did_read && update->activation_generation != m_activation_generation) {
        // Accepted reads still finish and retire. A different activation owns
        // its own visible read; unrelated commands are never cancelled.
        m_read_in_flight = false;
        const bool read = m_active;
        const bool full = std::exchange(m_pending_full_load, false) || m_loaded_limit < m_load_limit;
        const bool projection = std::exchange(m_projection_pending, false);
        // Native reduction has advanced, but the GUI never adopted its old
        // overlap. The successor must publish a complete coherent projection.
        m_force_reset_pending = true;
        m_refresh_pending = false;
        Q_EMIT busyChanged();
        if (!owns()) return;
        if (read || projection) requestUpdate(read, full, true);
        return;
    }
    m_update = update; m_apply_stage = m_apply_position = 0;
    if (update->did_read) {
        if (m_log_available != update->file_opened) {
            m_log_available = update->file_opened; Q_EMIT logAvailableChanged();
            if (!owns()) return;
        }
        if (m_open_error != update->error) {
            m_open_error = update->error; Q_EMIT openErrorChanged();
            if (!owns()) return;
        }
    }
    m_watch_file = update->watch_file; m_watch_parent = update->watch_parent; watchLogPath();
    m_loaded_limit = update->loaded_limit;
    if (m_has_more_lines != update->has_more) {
        m_has_more_lines = update->has_more; Q_EMIT hasMoreLinesChanged();
        if (!owns()) return;
    }
    if (update->force_reset) {
        m_changing_rows = true;
        beginResetModel();
        if (!observer) return;
        retireValue(mailbox, services, std::make_shared<const std::deque<LogLine>>(std::exchange(m_display_lines, {})));
        if (!observer) return;
        endResetModel();
        if (!observer) return;
        m_changing_rows = false;
        if (m_stopping) { finishStop(); return; }
        if (!owns()) return;
        update->old_start = update->new_start = update->common = 0;
    }
    applyUpdate();
}

void DebugLogModel::applyUpdate()
{
    if (m_stopping || m_changing_rows || !m_update) return;
    const auto mailbox = m_mailbox;
    const auto services = m_services;
    auto frame = m_update;
    const auto retire_frame = qScopeGuard([&] { retireValue(mailbox, services, std::move(frame)); });
    const QPointer<DebugLogModel> observer{this};
    const auto generation = m_work_generation;
    const auto owns = [&] {
        return observer && !observer->m_stopping && observer->m_mailbox == mailbox &&
            observer->m_work_generation == generation && observer->m_update == frame;
    };
    const auto& update = *frame;
    const auto& lines = *update.display;
    // Four bounded stages retain the original frame through each begin/end.
    int budget{16};
    while (budget && m_apply_stage < 4) {
        int count{0};
        if (m_apply_stage == 0) {
            const int target = update.common ? update.old_start + update.common : 0;
            count = std::min(budget, int(m_display_lines.size()) - target);
            if (count) {
                const int first = m_display_lines.size() - count;
                m_changing_rows = true; beginRemoveRows({}, first, m_display_lines.size() - 1);
                if (!observer) return;
                for (int i = 0; i < count; ++i) m_display_lines.pop_back();
                endRemoveRows();
            } else ++m_apply_stage;
        } else if (m_apply_stage == 1) {
            count = std::min(budget, int(m_display_lines.size()) - update.common);
            if (count) {
                m_changing_rows = true; beginRemoveRows({}, 0, count - 1);
                if (!observer) return;
                for (int i = 0; i < count; ++i) m_display_lines.pop_front();
                endRemoveRows();
            } else { ++m_apply_stage; m_apply_position = update.new_start; }
        } else if (m_apply_stage == 2) {
            count = std::min(budget, m_apply_position);
            if (count) {
                m_changing_rows = true; beginInsertRows({}, 0, count - 1);
                if (!observer) return;
                for (int i = 0; i < count; ++i) m_display_lines.push_front(lines[--m_apply_position]);
                endInsertRows();
            } else { ++m_apply_stage; m_apply_position = update.new_start + update.common; }
        } else {
            count = std::min(budget, int(lines.size()) - m_apply_position);
            if (count) {
                const int first = m_display_lines.size();
                m_changing_rows = true; beginInsertRows({}, first, first + count - 1);
                if (!observer) return;
                for (int i = 0; i < count; ++i) m_display_lines.push_back(lines[m_apply_position++]);
                endInsertRows();
            } else ++m_apply_stage;
        }
        if (!observer) return;
        m_changing_rows = false;
        if (m_stopping) { finishStop(); return; }
        if (!owns()) return;
        budget -= count;
    }
    if (m_apply_stage < 4) { m_apply_timer.start(0); return; }
    const int newly_completed = update.newly_completed;
    retireValue(mailbox, services, std::exchange(m_update, {}));
    if (!observer || m_stopping || m_work_generation != generation) return;
    m_read_in_flight = false;
    Q_EMIT busyChanged();
    if (!observer || m_stopping || m_work_generation != generation) return;
    if (newly_completed) {
        Q_EMIT newLinesAdded(std::min(newly_completed, m_load_limit));
        if (!observer || m_stopping || m_work_generation != generation) return;
    }
    const bool read = std::exchange(m_refresh_pending, false);
    const bool full = std::exchange(m_pending_full_load, false);
    const bool projection = std::exchange(m_projection_pending, false);
    const bool reset = std::exchange(m_force_reset_pending, false);
    if ((read && m_active) || projection) requestUpdate(read && m_active, full, reset);
}

void DebugLogModel::connectFileWatcher()
{
    connect(&m_watcher, &QFileSystemWatcher::fileChanged,
            this, [this](const QString&) {
                if (!m_active || m_stopping) return;
                watchLogPath();
                m_debounce.start();
            });
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged,
            this, [this](const QString&) {
                if (!m_active || m_stopping) return;
                watchLogPath();
                m_debounce.start();
            });
}

void DebugLogModel::watchLogPath()
{
    if (!m_active || m_stopping || !m_log_to_file) return;
    const auto files = m_watcher.files();
    if (!m_watch_file.isEmpty() && !files.contains(m_watch_file)) m_watcher.addPath(m_watch_file);
    if (!m_watch_file.isEmpty() && m_watcher.files().contains(m_watch_file)) {
        const auto dirs = m_watcher.directories(); if (!dirs.isEmpty()) m_watcher.removePaths(dirs);
        return;
    }
    if (!files.isEmpty()) m_watcher.removePaths(files);
    if (!m_watch_parent.isEmpty() && !m_watcher.directories().contains(m_watch_parent)) m_watcher.addPath(m_watch_parent);
}

void DebugLogModel::buildDisplayLines(bool force_reset)
{
    requestUpdate(false, false, force_reset);
}

void DebugLogModel::PopulateParsedFields(LogLine& entry, const QString& raw_message)
{
    QString remaining = raw_message.trimmed();
    QStringList preserved_prefixes;
    entry.is_error = false;
    entry.is_warning = false;

    while (true) {
        const QRegularExpressionMatch prefix_match = BRACKET_PREFIX_RX.match(remaining);
        if (!prefix_match.hasMatch()) break;

        const QString original = QStringLiteral("[%1]").arg(prefix_match.captured(1));
        const QString value = prefix_match.captured(1).trimmed().toLower();
        bool recognised{false};

        if (IsLogLevel(value)) {
            entry.is_error = value == QLatin1String("error");
            entry.is_warning = value == QLatin1String("warning");
            recognised = true;
        } else {
            const qsizetype separator = value.indexOf(QLatin1Char(':'));
            if (separator > 0 && value.indexOf(QLatin1Char(':'), separator + 1) < 0) {
                const QString category = value.first(separator);
                const QString level = value.sliced(separator + 1);
                if (IsLogCategory(category) && IsLogLevel(level)) {
                    entry.is_error = level == QLatin1String("error");
                    entry.is_warning = level == QLatin1String("warning");
                    recognised = true;
                }
            } else if (IsLogCategory(value)) {
                recognised = true;
            }
        }

        if (!recognised) preserved_prefixes.append(original);
        remaining.remove(0, prefix_match.capturedLength());
        remaining = remaining.trimmed();
    }

    const QRegularExpressionMatch legacy_match = LEGACY_LEVEL_RX.match(remaining);
    if (legacy_match.hasMatch()) {
        entry.is_error = legacy_match.captured(1).compare(QLatin1String("ERROR"), Qt::CaseInsensitive) == 0;
        entry.is_warning = !entry.is_error;
        remaining = legacy_match.captured(2).trimmed();
    }

    if (!preserved_prefixes.isEmpty()) {
        remaining.prepend(preserved_prefixes.join(QLatin1Char(' ')) + QLatin1Char(' '));
    }
    entry.message = remaining.trimmed();
}

QString DebugLogModel::FormatTime(const QString& utc_seconds)
{
    const QDateTime utc_time = QDateTime::fromString(
        utc_seconds + QLatin1Char('Z'), Qt::ISODate);
    return utc_time.isValid()
        ? utc_time.toLocalTime().toString(QStringLiteral("HH:mm:ss"))
        : utc_seconds.right(8);
}
