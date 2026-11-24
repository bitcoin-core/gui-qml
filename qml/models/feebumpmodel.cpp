// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/feebumpmodel.h>

FeeBumpModel::FeeBumpModel(std::function<bool()> available, Create create, QObject* parent)
    : FeeBumpInterface(parent), m_available(std::move(available)), m_create(std::move(create))
{
}

FeeBumpOperation* FeeBumpModel::attemptFor(const QString& txid)
{
    if (!available()) return nullptr;
    auto* attempt = m_create ? m_create(this) : nullptr;
    if (!attempt) return nullptr;
    attempt->setDisplayUnit(m_display_unit);
    m_attempts.append(attempt);
    m_txids.insert(attempt, txid);
    connect(attempt, &FeeBumpOperation::stateChanged, this, [this, attempt] {
        if (attempt->committed() && !m_kept.contains(m_txids.value(attempt))) m_kept.insert(m_txids.value(attempt), attempt);
    });
    ++m_holds[attempt];
    return attempt;
}

FeeBumpOperation* FeeBumpModel::openFeeBump(const QString& txid, unsigned target_blocks)
{
    if (!available() || txid.isEmpty()) return nullptr;
    // An attempt past its commit point is shown again, never a second commit.
    if (auto* kept = m_kept.value(txid).data()) {
        ++m_holds[kept];
        return kept;
    }
    auto* attempt = attemptFor(txid);
    if (!attempt) return nullptr;
    const QPointer<FeeBumpOperation> guard{attempt};
    if (!attempt->review(txid, target_blocks)) {
        if (guard) forget(attempt);
        return nullptr;
    }
    return guard;
}

void FeeBumpModel::presented(FeeBumpOperation* attempt)
{
    if (!attempt || !m_txids.contains(attempt)) return;
    const auto txid = m_txids.value(attempt);
    if (m_kept.value(txid) == attempt) m_kept.remove(txid);
}

void FeeBumpModel::release(FeeBumpOperation* attempt)
{
    if (!attempt || !m_holds.contains(attempt)) return;
    if (--m_holds[attempt] > 0) return;
    m_holds.remove(attempt);
    // Past its commit point the attempt stays until a page shows its result.
    if (m_kept.value(m_txids.value(attempt)) == attempt) return;
    attempt->discard();
    forget(attempt);
}

void FeeBumpModel::forget(FeeBumpOperation* attempt)
{
    m_holds.remove(attempt);
    const auto txid = m_txids.take(attempt);
    if (m_kept.value(txid) == attempt) m_kept.remove(txid);
    m_attempts.removeAll(attempt);
    if (!m_closed) attempt->close(); // close() already closed every attempt.
    attempt->deleteLater();
}

void FeeBumpModel::setDisplayUnit(int unit)
{
    m_display_unit = unit;
    for (const auto& attempt : m_attempts) if (attempt) attempt->setDisplayUnit(unit);
}

void FeeBumpModel::retryPublications()
{
    const auto attempts = m_attempts;
    for (const auto& attempt : attempts) if (attempt) attempt->retryPublications();
}

void FeeBumpModel::close()
{
    if (std::exchange(m_closed, true)) return;
    const auto attempts = m_attempts;
    for (const auto& attempt : attempts) if (attempt) attempt->close();
    Q_EMIT availableChanged();
}
