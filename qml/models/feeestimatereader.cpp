// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/models/feeestimatereader.h>
#include <qml/executor.h>
#include <qml/retainedvalue.h>

#include <exception>
#include <utility>
namespace feeestimate {
namespace {
FeeReadResult ReadFailure(std::exception_ptr error)
{
    FeeReadResult failure;
    failure.error = Error::Backend;
    try { if (error) std::rethrow_exception(error); }
    catch (const std::exception& e) { failure.message = QString::fromUtf8(e.what()).left(4096); }
    catch (...) { failure.message = QStringLiteral("The fee estimate failed."); }
    return failure;
}
} // namespace

Reader::Drain::~Drain() { if (notify) notify(); }
Reader::Reader(Post core, Post outcomes, Read read, std::function<void()> notify, std::function<void()> drained)
    : m_drain(drained ? std::make_shared<Drain>(std::move(drained)) : nullptr),
      m_core(std::move(core)), m_outcomes(std::move(outcomes)), m_read(std::move(read)), m_notify(std::move(notify))
{
}

Reader::Reader(std::shared_ptr<execution::SerialQueue> core, std::shared_ptr<execution::SerialQueue> outcomes,
               Read read, std::function<void()> notify, std::function<void()> drained)
    : Reader([core, delivery = std::make_shared<execution::ValueCleanupQueue>(outcomes)](std::shared_ptr<execution::Task> task) {
          return delivery->postWithOutcome(*core, std::move(task));
      }, [outcomes](std::shared_ptr<execution::Task> task) {
          const bool accepted = outcomes->post([task = std::move(task)] {
              task->execute();
              task->deliver();
          });
          return execution::SubmitResult{{}, accepted ? execution::Rejection::None : execution::Rejection::Transport};
      }, std::move(read), std::move(notify), std::move(drained))
{
}

execution::SubmitResult Reader::request(quint64 revision, FeeReadInput input)
{
    {
        std::lock_guard lock{m_mutex};
        if (m_closed) return {{}, execution::Rejection::Closed};
        if (m_desired && revision <= m_desired->revision) return {{}, execution::Rejection::Invalid};
        m_desired = Intent{revision, std::move(input)};
    }
    return queueWake();
}
execution::SubmitResult Reader::queueWake()
{
    std::shared_ptr<Drain> drain;
    {
        std::lock_guard lock{m_mutex};
        if (m_closed) return {{}, execution::Rejection::Closed};
        if (std::exchange(m_wake, true)) return {};
        drain = m_drain;
    }
    const auto self = shared_from_this();
    auto wake = [self, drain] { self->consume(); };
    const auto failed = [self, drain](std::exception_ptr) {
        self->rejected(QStringLiteral("The fee request could not be processed."));
    };
    const auto submitted = m_outcomes(execution::MakeTask(std::move(wake), [drain] {}, failed));
    if (!submitted) rejected(QStringLiteral("The fee could not be updated. Try again."));
    return submitted;
}
void Reader::rejected(QString message)
{
    std::optional<Intent> desired;
    {
        std::lock_guard lock{m_mutex};
        m_wake = false;
        if (m_closed) return;
        desired = m_desired;
    }
    if (!desired) return;
    FeeReadResult failure;
    failure.error = Error::Rejected;
    failure.message = std::move(message);
    publish({desired->revision, desired->input.dependencies, Phase::Failed, std::move(failure), false});
}
void Reader::consume()
{
    std::optional<Intent> desired;
    {
        std::lock_guard lock{m_mutex};
        m_wake = false;
        if (m_closed) return;
        desired = m_desired;
    }
    if (!desired || desired->revision <= m_completed) return;
    if (m_active) {
        if (desired->revision > m_active->revision)
            publish({desired->revision, desired->input.dependencies, Phase::ReadingWithPendingWork, m_retained_result.value_or(FeeReadResult{}), false, m_retained_dependencies, m_retained_revision});
        return;
    }
    if (!desired->input.eligible) {
        publish({desired->revision, desired->input.dependencies, desired->input.scheduled ? Phase::Scheduled : Phase::Idle, m_retained_result.value_or(FeeReadResult{}), false, m_retained_dependencies, m_retained_revision}); return;
    }
    m_active = desired;
    publish({desired->revision, desired->input.dependencies, Phase::Reading, m_retained_result.value_or(FeeReadResult{}), false, m_retained_dependencies, m_retained_revision});
    const auto self = shared_from_this();
    const auto intent = *desired;
    std::shared_ptr<Drain> drain;
    {
        std::lock_guard lock{m_mutex};
        if (m_closed) {
            m_active.reset();
            return;
        }
        drain = m_drain;
    }
    auto read = [self, drain, input = intent.input]() mutable {
        // Core admission owns this captured observation independently of the
        // facade. Closing prevents later work/publications; accepted work and
        // its actual outcome still drain through the original owner.
        if (input.draft) {
            const auto maximum = input.draft->positions.find(input.maximum_token);
            input.remainder_index = maximum == input.draft->positions.end() ? -1 : static_cast<int>(maximum->second);
            input.recipients.reserve(input.draft->recipients.size());
            for (const auto& recipient : input.draft->recipients) input.recipients.push_back({recipient.address.toStdString(), recipient.amount});
        }
        if (input.selected) for (const auto& outpoint : *input.selected) input.coin_control.Select(outpoint);
        coinselection::ApplyInputPolicy(input.coin_control, input.input_mode);
        return self->m_read(input);
    };
    auto deliver = [self, drain, intent = intent](FeeReadResult result) mutable {
        self->complete(std::move(intent), std::move(result));
    };
    auto failed = [deliver](std::exception_ptr error) mutable { deliver(ReadFailure(error)); };
    const auto accepted = m_core(execution::MakeTask(std::move(read), std::move(deliver), std::move(failed)));
    if (!accepted) {
        m_active.reset();
        m_completed = intent.revision;
        FeeReadResult failure; failure.error = Error::Rejected; failure.message = QStringLiteral("The fee estimate could not be updated. Try again.");
        publish({intent.revision, intent.input.dependencies, Phase::Failed, std::move(failure), false});
    }
}
void Reader::complete(Intent intent, FeeReadResult result)
{
    if (!m_active || m_active->revision != intent.revision) return;
    m_active.reset();
    m_completed = intent.revision;
    std::optional<Intent> desired;
    {
        std::lock_guard lock{m_mutex};
        if (m_closed) return;
        desired = m_desired;
    }
    if (!desired) return;
    result.message = result.message.left(4096);
    result.custom_error = result.custom_error.left(4096);
    if (result.estimates.size() > 3 || result.errors.size() > 3) {
        result.estimates.clear();
        result.errors.clear();
        result.custom.reset();
        result.error = Error::Invalid; result.message = QStringLiteral("The fee estimate exceeds the supported result limits.");
    }
    for (auto it = result.errors.begin(); it != result.errors.end(); ++it) it.value() = it.value().left(4096);
    const auto valid = [](const SendFeePreview& value) {
        return MoneyRange(value.fee) && value.inputs >= 0 && value.rate_per_kvb >= 0 &&
            (!value.maximum_amount || MoneyRange(*value.maximum_amount));
    };
    bool invalid{false};
    for (auto it = result.estimates.begin(); it != result.estimates.end();) {
        if (!valid(*it)) { result.errors.insert(it.key(), QStringLiteral("The fee estimate contains invalid amounts.")); it = result.estimates.erase(it); invalid = true; }
        else ++it;
    }
    if (result.custom && !valid(*result.custom)) { result.custom.reset(); result.custom_error = QStringLiteral("The fee estimate contains invalid amounts."); invalid = true; }
    if (invalid && result.estimates.isEmpty() && !result.custom) { result.error = Error::Invalid; result.message = QStringLiteral("The fee estimate contains invalid amounts."); }
    const bool selected = intent.input.custom_fee_enabled ? result.custom.has_value()
        : result.estimates.contains(intent.input.coin_control.m_confirm_target.value_or(6));
    if (result.error == Error::None && !selected) {
        result.error = Error::Unavailable;
        if (result.message.isEmpty()) result.message = intent.input.custom_fee_enabled ? result.custom_error
            : result.errors.value(intent.input.coin_control.m_confirm_target.value_or(6));
        if (result.message.isEmpty()) result.message = QStringLiteral("Fee estimation is unavailable. Choose Custom to enter a rate.");
    }
    if (desired->revision != intent.revision || desired->input.dependencies != intent.input.dependencies) {
        // Preserve independently valid progress with its original provenance,
        // explicitly stale. Unknown external wallet changes prevent promoting
        // an earlier observation to fresh authority for a new request.
        if (result.error == Error::None) {
            m_retained_result = result;
            m_retained_dependencies = intent.input.dependencies;
            m_retained_revision = intent.revision;
            auto previous = result;
            previous.errors.clear();
            previous.custom_error.clear();
            previous.message.clear();
            publish({desired->revision, desired->input.dependencies, Phase::ReadingWithPendingWork,
                std::move(previous), false, intent.input.dependencies, intent.revision});
        }
        queueWake();
        return;
    }
    const bool success = result.error == Error::None;
    if (success) {
        m_retained_result = result;
        m_retained_dependencies = intent.input.dependencies;
        m_retained_revision = intent.revision;
    }
    publish({intent.revision, intent.input.dependencies, success ? Phase::Current : Phase::Failed, std::move(result), success, intent.input.dependencies, intent.revision});
}
void Reader::publish(Publication value)
{
    bool notify;
    {
        std::lock_guard lock{m_mutex};
        if (m_closed || !m_desired || value.revision != m_desired->revision || value.dependencies != m_desired->input.dependencies) return;
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
void Reader::invalidate(quint64 revision, bool scheduled)
{
    FeeReadInput input; input.eligible = false; input.scheduled = scheduled;
    request(revision, std::move(input));
}
void Reader::close()
{
    std::shared_ptr<Drain> drain;
    {
        std::lock_guard lock{m_mutex};
        m_closed = true;
        m_latest.reset();
        m_desired.reset();
        drain = std::move(m_drain);
    }
    drain.reset(); // Notification may reenter; never release it under the mailbox mutex.
}
}
