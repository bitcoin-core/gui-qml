// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/models/receiptreconciliationmodel.h>

#include <utility>

ReceiptReconciliationModel::ReceiptReconciliationModel(Ports ports, QObject* parent)
    : QObject(parent), m_ports(std::move(ports))
{
    m_poll.setInterval(1000);
    connect(&m_poll, &QTimer::timeout, this, &ReceiptReconciliationModel::poll);
}
ReceiptReconciliationModel::~ReceiptReconciliationModel() { close(); }
bool ReceiptReconciliationModel::fresh() const
{
    return !m_closed && m_phase == Ready && m_snapshot && m_ports.current && m_ports.current()
        && m_ports.nativeRevision && m_snapshot->native_revision == m_ports.nativeRevision();
}
QString ReceiptReconciliationModel::errorText() const
{
    return m_phase == Failed || m_phase == Rejected ? tr("Payments could not be checked. Try again.") : QString{};
}
CAmount ReceiptReconciliationModel::amount(const QString& address) const
{
    if (!m_snapshot || address.isEmpty()) return 0;
    const auto it = m_snapshot->totals.find(address);
    return it == m_snapshot->totals.end() ? 0 : it->second;
}
bool ReceiptReconciliationModel::observed(const QString& address) const
{
    return m_snapshot && !address.isEmpty() && m_snapshot->observed.contains(address);
}
void ReceiptReconciliationModel::consume()
{
    if (m_closed) return;
    if (!configured()) {
        if (m_phase != Rejected) {
            m_phase = Rejected;
            const QPointer<ReceiptReconciliationModel> guard{this};
            Q_EMIT changed(); if (guard) Q_EMIT committed();
        }
        return;
    }
    if (m_applying) { m_again = true; return; }
    auto update = m_ports.take();
    if (!update) return;
    if (update->snapshot && update->snapshot->opening != m_ports.opening) {
        if (m_ports.retire) m_ports.retire(std::move(update->snapshot));
        return;
    }
    if (update->status == walletreceipts::Status::Closed) {
        if (update->snapshot && m_ports.retire) m_ports.retire(std::move(update->snapshot));
        close(); return;
    }
    m_applying = true;
    m_revision = update->revision;
    switch (update->status) {
    case walletreceipts::Status::Ready: m_phase = Ready; break;
    case walletreceipts::Status::Pending: m_phase = Refreshing; break;
    case walletreceipts::Status::Failed: m_phase = Failed; break;
    case walletreceipts::Status::Rejected: m_phase = Rejected; break;
    case walletreceipts::Status::Closed: m_phase = Closed; break;
    }
    if (update->snapshot) {
        auto previous = std::exchange(m_snapshot, std::move(update->snapshot));
        if (previous && m_ports.retire) m_ports.retire(std::move(previous));
    }
    if (update->needs_poll) m_poll.start(); else m_poll.stop();
    if (fresh() && m_ports.acknowledge) m_ports.acknowledge();
    const QPointer<ReceiptReconciliationModel> guard{this};
    Q_EMIT changed(); if (!guard || m_closed) return;
    // The compatibility adapter owns its bounded string formatting. Keeping
    // this value callback local also permits reentrant owner destruction.
    const auto transactions = m_ports.transactions;
    if (transactions && !update->transactions.empty()) transactions(update->transactions);
    if (!guard || m_closed) return;
    if (update->rescan) { Q_EMIT rescanRequested(); if (!guard || m_closed) return; }
    m_applying = false;
    Q_EMIT committed(); if (!guard || m_closed) return;
    if (std::exchange(m_again, false)) QTimer::singleShot(0, this, &ReceiptReconciliationModel::consume);
}
void ReceiptReconciliationModel::invalidate()
{
    if (m_closed) return;
    if (!fresh() && m_phase != Failed && m_phase != Rejected) m_phase = Refreshing;
    if (fresh() && m_ports.acknowledge) m_ports.acknowledge();
    const QPointer<ReceiptReconciliationModel> guard{this};
    Q_EMIT changed(); if (guard) Q_EMIT committed();
}
void ReceiptReconciliationModel::poll() { if (!m_closed && configured() && m_ports.poll) m_ports.poll(); }
void ReceiptReconciliationModel::retry()
{
    if (m_closed || !configured() || !m_ports.retry) return;
    m_ports.retry();
    invalidate();
}
void ReceiptReconciliationModel::close()
{
    if (std::exchange(m_closed, true)) return;
    m_poll.stop(); m_phase = Closed; m_applying = m_again = false;
    if (m_snapshot && m_ports.retire) m_ports.retire(std::move(m_snapshot));
    m_ports = {};
    const QPointer<ReceiptReconciliationModel> guard{this};
    Q_EMIT changed(); if (guard) Q_EMIT committed();
}

void RequestReceiptModel::setReader(ReceiptReconciliationModel* reader)
{
    if (m_reader == reader) return;
    // A facade belongs to its original opening, including after that owner
    // disappears. A later wallet with the same request/address cannot adopt it.
    if (m_bound) return;
    disconnect(m_connection); disconnect(m_destroyed); m_reader = reader;
    if (reader) {
        m_bound = true;
        m_connection = connect(reader, &ReceiptReconciliationModel::changed, this, &RequestReceiptModel::refresh);
        m_destroyed = connect(reader, &QObject::destroyed, this, [this] { m_reader_closed = true; refresh(); });
    }
    refresh();
}
void RequestReceiptModel::setSource(QString request, QString address, bool used, bool available)
{
    if (request != m_request || address != m_address) m_used = false;
    m_request = std::move(request); m_address = std::move(address);
    m_source_used = used; m_available = available && !m_address.isEmpty();
    refresh();
}
void RequestReceiptModel::refresh()
{
    m_amount = m_reader ? m_reader->amount(m_address) : 0;
    m_used = m_used || m_source_used || (m_reader && m_reader->observed(m_address));
    Q_EMIT changed();
}
