// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/presentationservices.h>

#include <QPointer>
#include <QTimer>

#include <utility>

PresentationServices::PresentationServices(Ports ports, QObject* parent)
    : QObject(parent), m_ports(std::move(ports))
{
}

void PresentationServices::close()
{
    if (std::exchange(m_closed, true)) return;
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
    // The callback is owned independently of any view. Retry turns do no Core
    // work and never wait synchronously on the GUI event transport.
    auto turn = std::make_shared<std::function<void()>>();
    const std::weak_ptr<std::function<void()>> weak{turn};
    *turn = [guard, self, done = std::move(done), weak] {
        if (!guard) return;
        if (!self || (self->retryRetirements() && self->pendingRetirements() == 0)) { done(); return; }
        if (const auto next = weak.lock()) {
            const auto retry = [guard, next] { if (guard) (*next)(); };
            if (self->m_ports.retry) self->m_ports.retry(retry);
            else QTimer::singleShot(1, guard, retry);
        }
    };
    QTimer::singleShot(0, receiver, [turn] { (*turn)(); });
}
