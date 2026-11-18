// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/signerdiscovery.h>
#include <QCoreApplication>
#include <QRegularExpression>
#include <QSet>
#include <exception>
#include <stdexcept>
#include <utility>
namespace signerdiscovery {
Result Discover(const Ports& ports)
{
    Configuration config;
    try {
        if (!ports.available || !ports.configuration || !ports.enumerate) return Failure(Error::Unavailable, Text("Signer discovery is unavailable in this build."));
        config = ports.configuration();
        if (config.source == QStringLiteral("UnavailableCommandLine")) return Failure(Error::Unavailable, Text("The effective command-line signer is unavailable through this configuration adapter."), config);
        if (const auto error = PathError(config.command); !error.isEmpty()) return Failure(Error::InvalidPath, error, config);
        if (config.command.isEmpty()) return Failure(Error::Unconfigured, config.editable ? Text("Set an external signer path in Wallet settings first.") : Text("The signer is disabled by command line. Restart without the signer override to configure it here."), config);
        auto devices = ports.enumerate(config);
        const auto actual = ports.configuration();
        if (actual != config) return Failure(Error::ConfigurationChanged, Text("The signer configuration changed during discovery. Check again."), actual);
        return {config, std::move(devices), Error::None, {}};
    } catch (const std::invalid_argument& error) { return Failure(Error::InvalidResponse, QString::fromUtf8(error.what()).left(4096), config); }
    catch (const std::length_error& error) { return Failure(Error::InvalidResponse, QString::fromUtf8(error.what()).left(4096), config); }
    catch (const std::exception& error) { return Failure(Error::Backend, QString::fromUtf8(error.what()).left(4096), config); }
    catch (...) { return Failure(Error::Backend, Text("The signer check failed with an unknown error."), config); }
}
Reader::Drain::~Drain() { if (notify) notify(); }
Reader::Reader(Submit submit, std::function<Result()> read, std::function<void()> notify,
    std::function<void()> drained)
    : m_submit(std::move(submit)),
      m_read(std::move(read)), m_notify(std::move(notify)),
      m_drain(std::make_shared<Drain>(std::move(drained))) {}
execution::SubmitResult Reader::request(quint64 revision)
{
    {
        std::lock_guard lock{m_mutex};
        if (m_closed) return {{}, execution::Rejection::Closed};
        if (revision <= m_desired) return {{}, execution::Rejection::Invalid};
        m_desired = revision;
    }
    return wake();
}
execution::SubmitResult Reader::wake()
{
    std::shared_ptr<Drain> drain;
    {
        std::lock_guard lock{m_mutex};
        if (m_closed) return {{}, execution::Rejection::Closed};
        if (std::exchange(m_wake, true)) return {};
        drain = m_drain;
    }
    const auto self = shared_from_this();
    const auto result = m_submit(execution::MakeTask([self, drain] { self->consume(); }, [drain] {},
        [self, drain](std::exception_ptr) { self->rejected(); }), execution::Lane::Domain);
    if (!result) rejected();
    return result;
}
void Reader::rejected()
{
    quint64 desired;
    {
        std::lock_guard lock{m_mutex};
        m_wake = false;
        if (m_closed) return;
        desired = m_desired;
    }
    publish({desired, 0, Phase::Failed, Failure(Error::Rejected, Text("The signer check could not start. Try again.")), false, m_active_flag.load()});
}
void Reader::consume()
{
    quint64 desired, expired;
    {
        std::lock_guard lock{m_mutex};
        m_wake = false;
        if (m_closed) return;
        desired = m_desired;
        expired = m_expired;
    }
    if (m_active) {
        auto previous = m_previous;
        if (expired == desired) { previous.error = Error::Timeout; previous.message = Text("The signer check timed out. The signer program is still running."); }
        publish({desired, m_source, expired == desired ? Phase::Failed : desired > *m_active ? Phase::ReadingWithPendingWork : Phase::Reading, std::move(previous), false, true}); return;
    }
    if (desired <= m_completed) return;
    if (expired == desired) { m_completed = desired; publish({desired, m_source, Phase::Failed, Failure(Error::Timeout, Text("The signer check timed out before it started.")), false, false}); return; }
    m_active = desired;
    m_active_flag = true;
    publish({desired, m_source, Phase::Reading, m_previous, false, true});
    const auto self = shared_from_this();
    std::shared_ptr<Drain> drain;
    {
        std::lock_guard lock{m_mutex};
        if (m_closed) {
            m_active.reset();
            m_active_flag = false;
            return;
        }
        drain = m_drain;
    }
    const auto submitted = m_submit(execution::MakeTask([self, drain] { return self->m_read(); },
        [self, drain, desired](Result result) { self->complete(desired, std::move(result)); },
        [self, drain, desired](std::exception_ptr) { self->complete(desired, Failure(Error::Backend, Text("The signer check failed."))); }),
        execution::Lane::Core);
    if (!submitted) { m_active.reset(); m_active_flag = false; m_completed = desired; publish({desired, m_source, Phase::Failed, Failure(Error::Rejected, Text("The signer program could not start. Try again.")), false, false}); }
}
void Reader::complete(quint64 revision, Result result)
{
    if (!m_active || *m_active != revision) return;
    m_active.reset();
    m_active_flag = false;
    m_completed = revision;
    quint64 desired, expired;
    {
        std::lock_guard lock{m_mutex};
        if (m_closed) return;
        desired = m_desired;
        expired = m_expired;
    }
    if (result.devices.size() > 16 || result.configuration.command.size() > 32769 || result.configuration.id.size() > 128 || result.configuration.source.size() > 64) {
        result.configuration.command = result.configuration.command.left(32769); result.configuration.id = result.configuration.id.left(128); result.configuration.source = result.configuration.source.left(64);
        result = Failure(Error::InvalidResponse, Text("The signer result exceeds its budget."), result.configuration);
    }
    QSet<QString> seen;
    for (const auto& device : result.devices) {
        if (device.id.isEmpty() || device.id.size() > 128 || device.name.size() > 4096 || device.fingerprint.size() != 8 || device.id != result.configuration.id + QLatin1Char(':') + device.fingerprint.toLower() || !QRegularExpression{QStringLiteral("^[0-9a-fA-F]{8}$")}.match(device.fingerprint).hasMatch() || seen.contains(device.id)) { result = Failure(Error::InvalidResponse, Text("The signer returned an invalid or duplicate fingerprint."), result.configuration); break; }
        seen.insert(device.id);
    }
    result.message = result.message.left(4096);
    if (revision != desired) { wake(); return; }
    if (expired == revision) { result.error = Error::Timeout; result.message = Text("The signer check exceeded its deadline. Check again to obtain a current result."); result.devices.clear(); }
    const bool success = result.error == Error::None;
    if (success) { m_previous = result; m_source = revision; }
    else if (!m_previous.devices.empty()) result.devices = m_previous.devices;
    publish({revision, m_source, success ? Phase::Current : Phase::Failed, std::move(result), success, false, true});
}
void Reader::expire(quint64 revision)
{
    {
        std::lock_guard lock{m_mutex};
        if (m_closed || revision != m_desired) return;
        m_expired = revision;
    }
    wake();
}
void Reader::publish(Publication value)
{
    bool notify;
    {
        std::lock_guard lock{m_mutex};
        if (m_closed || value.revision != m_desired) return;
        m_latest = std::move(value);
        notify = !std::exchange(m_notified, true);
    }
    if (notify) try { m_notify(); } catch (...) { std::lock_guard lock{m_mutex}; m_notified = false; }
}
std::optional<Publication> Reader::take()
{
    std::lock_guard lock{m_mutex};
    m_notified = false;
    if (m_closed) return {};
    return std::exchange(m_latest, {});
}
void Reader::close()
{
    std::shared_ptr<Drain> drain;
    {
        std::lock_guard lock{m_mutex};
        m_closed = true;
        m_latest.reset();
        drain = std::move(m_drain);
    }
    drain.reset();
}
} // namespace signerdiscovery
