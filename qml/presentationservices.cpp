// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/presentationservices.h>
#include <qml/executor.h>

#include <QPointer>
#include <QTimer>

#include <atomic>
#include <mutex>
#include <utility>

struct PresentationServices::RetiredValues final : Retirement, std::enable_shared_from_this<RetiredValues> {
    explicit RetiredValues(std::function<bool(std::function<void()>)> post) : post(std::move(post)) {}

    void retire(std::shared_ptr<const void> value)
    {
        if (!value) return;
        {
            std::lock_guard lock{mutex};
            values.append(std::move(value));
        }
        retry();
    }

    size_t pending() override
    {
        std::lock_guard lock{mutex};
        return values.size() + size_t(scheduled);
    }

    bool retry() override
    {
        {
            std::lock_guard lock{mutex};
            if (scheduled || values.empty()) return true;
            scheduled = true;
        }
        const auto self = shared_from_this();
        try {
            if (post([self] {
                QList<std::shared_ptr<const void>> batch;
                {
                    std::lock_guard lock{self->mutex};
                    batch.swap(self->values);
                }
                batch.clear();
                {
                    std::lock_guard lock{self->mutex};
                    self->scheduled = false;
                }
                self->retry(); // Releasing a frame may release its source.
            })) return true;
        } catch (...) {}
        std::lock_guard lock{mutex};
        scheduled = false;
        return false;
    }

    const std::function<bool(std::function<void()>)> post;
    std::mutex mutex;
    QList<std::shared_ptr<const void>> values;
    bool scheduled{false};
};

PresentationServices::PresentationServices(Ports ports, QObject* parent)
    : QObject(parent), m_ports(std::move(ports))
{
    if (m_ports.utility && m_ports.retire) {
        m_values = std::make_shared<RetiredValues>(m_ports.retire);
        track(m_values);
    }
}

std::function<execution::SubmitResult(std::shared_ptr<execution::Task>)> PresentationServices::observationPost() const
{
    if (!m_ports.utility) return {};
    return [utility = m_ports.utility](std::shared_ptr<execution::Task> packet) {
        const bool accepted = utility->post([packet = std::move(packet)] {
            packet->execute();
            packet->deliver();
        });
        return execution::SubmitResult{{}, accepted ? execution::Rejection::None : execution::Rejection::Transport};
    };
}

PresentationServices::RetireValue PresentationServices::valueRetirement() const
{
    if (!m_values) return {};
    return [weak = std::weak_ptr<RetiredValues>{m_values}](std::shared_ptr<const void> value) {
        if (auto values = weak.lock()) values->retire(std::move(value));
    };
}

void PresentationServices::close()
{
    if (std::exchange(m_closed, true)) return;
    if (m_ports.utility) m_ports.utility->close();
    Q_EMIT closed();
}

void PresentationServices::track(std::shared_ptr<Retirement> retirement)
{
    if (retirement && !m_retirements.contains(retirement)) m_retirements.append(std::move(retirement));
}

bool PresentationServices::retryRetirements()
{
    bool admitted{true};
    for (int i = m_retirements.size() - 1; i >= 0; --i) {
        const auto retirement = m_retirements[i];
        if (!retirement->retry()) admitted = false;
        // The list and this loop hold the only references: its owner is gone.
        if (retirement->pending() == 0 && retirement.use_count() == 2) m_retirements.removeAt(i);
    }
    return admitted;
}

size_t PresentationServices::pendingRetirements()
{
    size_t pending{0};
    for (const auto& retirement : std::as_const(m_retirements)) pending += retirement->pending();
    return pending;
}

void PresentationServices::whenRetired(QObject* receiver, std::function<void()> done)
{
    const QPointer<QObject> guard{receiver};
    const QPointer<PresentationServices> self{this};
    const bool wait_for_work = m_closed && m_ports.utility;
    const auto work_drained = std::make_shared<std::atomic_bool>(!wait_for_work);
    if (wait_for_work) m_ports.utility->whenDrained([work_drained] { work_drained->store(true); });
    // The callback is owned independently of any view. Retry turns do no Core
    // work and never wait synchronously on the GUI event transport.
    auto turn = std::make_shared<std::function<void()>>();
    const std::weak_ptr<std::function<void()>> weak{turn};
    *turn = [guard, self, done = std::move(done), weak, work_drained] {
        if (!guard) return;
        if (!self || (self->retryRetirements() && self->pendingRetirements() == 0 && work_drained->load())) { done(); return; }
        if (const auto next = weak.lock()) {
            const auto retry = [guard, next] { if (guard) (*next)(); };
            if (self->m_ports.retry) self->m_ports.retry(retry);
            else QTimer::singleShot(1, guard, retry);
        }
    };
    QTimer::singleShot(0, receiver, [turn] { (*turn)(); });
}
