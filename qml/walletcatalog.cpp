// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/walletcatalog.h>

#include <qml/bitcoinunits.h>
#include <qml/walletidentity.h>
#include <QDir>
#include <qml/retainedsnapshot.h>

#include <QCoreApplication>
#include <QSet>
#include <QSettings>

#include <algorithm>
#include <deque>
#include <limits>
#include <map>
#include <mutex>
#include <stdexcept>

namespace walletcatalog {
namespace {
QString CatalogError(const char* text) { return QCoreApplication::translate("WalletListModel", text); }

// This value owner has no QObject, scheduler, node or GUI dependency. The
// reader supplies directory facts; ordered observations are never coalesced.
struct Catalog {
    struct Information { qint64 balance{0}; int scheme{-1}; bool known{false}; };
    Reader::Directory directory;
    std::map<QString, Information> information;
    std::map<QString, QString> operations;
    std::map<QString, quint64> facades;
    QSet<QString> open, observed_external;
    std::optional<QString> loading, failed;
    QString row_error, read_error;
    Availability availability{Availability::Available};
    bool loaded{false};
    int unit{0};
    quint64 revision{0}, directory_epoch{0}, directory_revision{0};

    void apply(const Input& input)
    {
        revision = std::max(revision, input.revision);
        switch (input.kind) {
        case Input::Kind::Directory: case Input::Kind::Names: break;
        case Input::Kind::Unit: unit = input.unit; break;
        case Input::Kind::Info: information[input.name] = {input.balance, input.scheme, input.known}; break;
        case Input::Kind::Availability: availability = input.availability; break;
        case Input::Kind::OpenWallet: facades[input.name] = input.revision; break;
        case Input::Kind::Operation: operations[input.name] = input.operation; break;
        case Input::Kind::Load:
            if (input.load == LoadState::Loading) { loading = input.name; failed.reset(); row_error.clear(); }
            else if (input.load == LoadState::LoadError) { loading.reset(); failed = input.name; row_error = input.error; }
            else {
                if (input.load == LoadState::Open) { open.insert(input.name); if (QDir::isAbsolutePath(input.name)) observed_external.insert(input.name); } else open.remove(input.name);
                if (loading == input.name) loading.reset();
                if (failed == input.name) { failed.reset(); row_error.clear(); }
            }
            break;
        }
    }

    std::unique_ptr<Snapshot> project(const std::shared_ptr<const Snapshot>& previous, const std::map<QString, QString>& aliases) const
    {
        auto result = std::make_unique<Snapshot>();
        result->revision = revision; result->directory_epoch = directory_epoch;
        result->directory_revision = directory_revision; result->loaded = loaded;
        result->error = read_error; result->base = previous;
        if (loaded) {
            QSet<QString> listed;
            for (const auto& [path, format] : directory) {
                Row item; item.name = walletidentity::Canonical(QString::fromStdString(path)); item.format = QString::fromStdString(format); item.from_wallet_dir = true;
                listed.insert(item.name); result->rows.push_back(std::move(item));
            }
            for (const auto& name : open | observed_external) if (!listed.contains(name)) { Row item; item.name = name; result->rows.push_back(std::move(item)); }
        }
        if (result->rows.size() > size_t(std::numeric_limits<int>::max())) throw std::length_error("Wallet catalog exceeds Qt row capacity");
        for (auto& item : result->rows) {
            // Aliases are presentation data. Never trim the canonical identity.
            if (const auto alias = aliases.find(item.name); alias != aliases.end()) item.display_name = alias->second;
            if (item.display_name.isEmpty()) item.display_name = walletidentity::Display(item.name);
            if (const auto found = information.find(item.name); found != information.end()) {
                item.balance = found->second.balance; item.balance_known = found->second.known; item.key_scheme = found->second.scheme;
            }
            if (const auto found = operations.find(item.name); found != operations.end()) item.operation = found->second;
            if (const auto found = facades.find(item.name); found != facades.end()) item.facade_revision = found->second;
            if (item.balance_known) item.balance_text = QmlBitcoinUnits::formatForDisplay(QmlBitcoinUnits::fromDisplayUnit(unit), item.balance);
            item.load_state = failed == item.name ? LoadState::LoadError : loading == item.name ? LoadState::Loading : open.contains(item.name) ? LoadState::Open : LoadState::Closed;
            item.section = open.contains(item.name) ? QStringLiteral("open") : QStringLiteral("closed");
            if (failed == item.name) item.error = row_error;
        }
        std::stable_sort(result->rows.begin(), result->rows.end(), [&](const auto& a, const auto& b) {
            const bool a_open = open.contains(a.name), b_open = open.contains(b.name);
            if (a_open != b_open) return a_open;
            for (auto sensitivity : {Qt::CaseInsensitive, Qt::CaseSensitive}) {
                const int order = QString::compare(a.name, b.name, sensitivity); if (order) return order < 0;
            }
            const int order = QString::compare(a.format, b.format, Qt::CaseInsensitive);
            return order ? order < 0 : QString::compare(a.format, b.format, Qt::CaseSensitive) < 0;
        });
        result->structure_changed = !previous || previous->rows.size() != result->rows.size();
        for (size_t row = 0; !result->structure_changed && row < result->rows.size(); ++row) {
            const auto& a = previous->rows[row]; const auto& b = result->rows[row];
            result->structure_changed = a.name != b.name || a.format != b.format || a.from_wallet_dir != b.from_wallet_dir;
        }
        if (previous && !result->structure_changed) for (size_t row = 0; row < result->rows.size(); ++row) {
            const auto& a = previous->rows[row]; const auto& b = result->rows[row]; unsigned fields{0};
            if (a.display_name != b.display_name) fields |= DisplayName;
            if (a.load_state != b.load_state) fields |= Load;
            if (a.section != b.section) fields |= Section;
            if (a.error != b.error) fields |= Error;
            if (a.operation != b.operation) fields |= Operation;
            if (a.facade_revision != b.facade_revision) fields |= OpenWallet;
            if (a.balance != b.balance || a.balance_known != b.balance_known || a.key_scheme != b.key_scheme) fields |= walletcatalog::Information;
            else if (a.balance_text != b.balance_text) fields |= BalanceText;
            if (fields) result->notices.emplace_back(static_cast<int>(row), fields);
        }
        return result;
    }
};

std::map<QString, QString> ReadAliases(const Catalog& catalog)
{
    std::map<QString, QString> aliases;
    QSettings settings;
    const auto read = [&](const QString& name) {
        aliases[name] = walletidentity::ReadAlias(settings, name);
    };
    for (const auto& entry : catalog.directory) read(QString::fromStdString(entry.first));
    for (const auto& name : catalog.open | catalog.observed_external) read(name);
    if (settings.status() != QSettings::NoError) throw std::runtime_error("Wallet display names could not be read");
    return aliases;
}
} // namespace

struct Reader::Data {
    // Declared first, destroyed last: terminal publication follows release of
    // pending commands and catalog storage on the reserved Cleanup lane.
    struct Retirement {
        std::function<void()> released;
        ~Retirement() { if (released) released(); }
    } retirement;
    std::deque<Input> pending;
    std::optional<Input> discovery;
    Catalog catalog;
    bool hasPending() const { return !pending.empty() || discovery.has_value(); }
};
struct Reader::Control {
    mutable std::mutex mutex;
    Observation observation;
    quint64 desired_directory{0};
    bool active{false}, started{false}, closed{false}, notification_pending{false};
    std::shared_ptr<const Snapshot> latest, published;
    void notify(const Notify& wake)
    {
        {
            std::lock_guard lock{mutex};
            if (std::exchange(notification_pending, true)) return;
        }
        try { wake(); }
        catch (...) { std::lock_guard lock{mutex}; notification_pending = false; }
    }
};
struct Reader::Attempt { bool started{false}, completed{false}; };

Reader::Reader(std::shared_ptr<execution::Scheduler> scheduler, Read read, Notify notify)
    : m_scheduler(std::move(scheduler)), m_key(m_scheduler->allocateKey()), m_read(std::move(read)),
      m_notify(std::move(notify)), m_control(std::make_shared<Control>()),
      m_data(execution::RetainSnapshot<Data>(m_scheduler, [control = m_control, wake = m_notify] {
          auto data = std::make_unique<Data>();
          data->retirement.released = [control, wake] {
              {
                  std::lock_guard lock{control->mutex};
                  if (!control->closed) return;
                  control->observation.phase = Phase::Stopped;
              }
              control->notify(wake);
          };
          return data;
      }))
{
    if (!m_data) {
        m_control->observation.phase = Phase::Failed;
        m_control->observation.error = CatalogError("Wallet catalog is unavailable.");
    }
}

void Reader::request(Input input)
{
    std::shared_ptr<Data> data;
    bool start{false};
    {
        std::lock_guard lock{m_control->mutex};
        if (m_control->closed || !m_data) return;
        data = m_data;
        input.revision = ++m_control->observation.revision;
        if (input.kind == Input::Kind::Directory || input.kind == Input::Kind::Availability) {
            input.directory_revision = ++m_control->desired_directory;
            m_control->observation.fresh = false;
        }
        if (input.kind == Input::Kind::Availability) m_control->observation.availability = input.availability;
        // Discovery has no ordering effect on registry observations. Its one
        // replaceable slot bounds a burst without scanning on the GUI thread.
        if (input.kind == Input::Kind::Directory) data->discovery = std::move(input);
        else data->pending.push_back(std::move(input));
        if (!m_control->active) {
            m_control->active = true; m_control->started = false; start = true;
            m_control->observation.phase = Phase::Scheduled;
        } else if (m_control->started) m_control->observation.phase = Phase::ReadingWithPendingWork;
    }
    if (start) submit(std::move(data));
}

void Reader::submit(std::shared_ptr<Data> data)
{
    const auto self = shared_from_this();
    const auto attempt = std::make_shared<Attempt>();
    execution::SubmitResult submitted;
    try {
        submitted = execution::Submit(*m_scheduler,
            [self, data, attempt] { return self->read(data, attempt); },
            [self, data, attempt](std::shared_ptr<const Snapshot> snapshot) { self->complete(data, attempt, std::move(snapshot), false); },
            [self, data, attempt](std::exception_ptr) { self->complete(data, attempt, {}, true); },
            {execution::Lane::Core, m_key, execution::WorkClass::Observation});
    } catch (...) { rejected(attempt); return; }
    if (!submitted) rejected(attempt);
}

void Reader::rejected(const std::shared_ptr<Attempt>& attempt)
{
    {
        std::lock_guard lock{m_control->mutex};
        if (std::exchange(attempt->completed, true)) return;
        m_control->active = false; m_control->started = false;
        if (!m_control->closed) {
            m_control->observation.phase = Phase::Failed;
            m_control->observation.fresh = false;
            m_control->observation.error = CatalogError("Wallet catalog could not be refreshed.");
        }
    }
    // No effect was admitted, so no callback or automatic retry is awaited.
    // Pending discovery and ordered observations belong to the next request.
    notify();
}

std::shared_ptr<const Snapshot> Reader::read(const std::shared_ptr<Data>& data, const std::shared_ptr<Attempt>& attempt)
{
    std::deque<Input> pending;
    std::optional<Input> directory;
    std::shared_ptr<const Snapshot> published;
    {
        std::lock_guard lock{m_control->mutex};
        if (m_control->closed) throw execution::NotStarted{execution::NotStarted::Cause::Cancelled};
        pending.swap(data->pending);
        directory.swap(data->discovery);
        published = m_control->published;
        attempt->started = true; m_control->started = true; m_control->observation.phase = Phase::Reading;
    }
    notify();
    quint64 discovery{0};
    for (const auto& input : pending) {
        data->catalog.apply(input);
        discovery = std::max(discovery, input.directory_revision);
    }
    if (directory) { data->catalog.apply(*directory); discovery = std::max(discovery, directory->directory_revision); }
    if (discovery && data->catalog.availability == Availability::Available) {
        try {
            auto entries = m_read();
            data->catalog.directory = std::move(entries);
            data->catalog.loaded = true; ++data->catalog.directory_epoch;
            data->catalog.directory_revision = discovery; data->catalog.read_error.clear();
        } catch (...) { data->catalog.read_error = CatalogError("Wallet directory could not be read."); }
    }
    {
        std::lock_guard lock{m_control->mutex};
        if (!m_control->closed) m_control->observation.phase = data->hasPending() ? Phase::ReadingWithPendingWork : Phase::Applying;
    }
    const auto aliases = ReadAliases(data->catalog);
    auto snapshot = execution::RetainSnapshot<Snapshot>(m_scheduler, [&] { return data->catalog.project(published, aliases); });
    if (!snapshot) throw std::runtime_error("Wallet catalog projection is unavailable");
    return snapshot;
}

void Reader::complete(const std::shared_ptr<Data>& data, const std::shared_ptr<Attempt>& attempt,
                      std::shared_ptr<const Snapshot> snapshot, bool failed)
{
    bool again{false};
    {
        std::lock_guard lock{m_control->mutex};
        if (std::exchange(attempt->completed, true)) return;
        m_control->active = false; m_control->started = false;
        if (!m_control->closed) {
            // Rejection before Core starts retains the batch for an explicit
            // retry. A failed admitted read still drains newer required work.
            again = data->hasPending() && attempt->started;
            const bool current = snapshot && snapshot->directory_revision == m_control->desired_directory;
            // A superseded read failure cannot replace a newer discovery's
            // error. Its coherent registry observations may still be shown.
            if (snapshot && (current || !again)) m_control->observation.error = snapshot->error;
            else if (failed && !again) m_control->observation.error = CatalogError("Wallet catalog could not be refreshed.");
            m_control->observation.fresh = current && snapshot->loaded && snapshot->error.isEmpty()
                && m_control->observation.availability == Availability::Available;
            if (snapshot) m_control->latest = std::move(snapshot);
            if (again) {
                m_control->active = true; m_control->observation.phase = Phase::Scheduled;
            } else m_control->observation.phase = failed || !m_control->observation.error.isEmpty() ? Phase::Failed :
                m_control->observation.fresh ? Phase::Current : Phase::Idle;
        }
    }
    notify();
    if (again) submit(data);
}

void Reader::notify()
{
    m_control->notify(m_notify);
}
Observation Reader::observation() const
{
    std::lock_guard lock{m_control->mutex};
    return m_control->observation;
}
Update Reader::take()
{
    std::lock_guard lock{m_control->mutex};
    m_control->notification_pending = false;
    return {m_control->observation, std::exchange(m_control->latest, {})};
}
void Reader::published(std::shared_ptr<const Snapshot> snapshot)
{
    std::lock_guard lock{m_control->mutex};
    if (!m_control->closed) m_control->published = std::move(snapshot);
}
void Reader::close()
{
    std::shared_ptr<Data> retired;
    {
        std::lock_guard lock{m_control->mutex};
        if (std::exchange(m_control->closed, true)) return;
        m_control->observation.phase = m_data ? Phase::Stopping : Phase::Stopped;
        m_control->observation.fresh = false;
        m_control->latest.reset(); m_control->published.reset(); retired.swap(m_data);
    }
    // The cleanup lease releases pending inputs and domain rows off GUI.
    retired.reset();
    notify();
}
} // namespace walletcatalog
