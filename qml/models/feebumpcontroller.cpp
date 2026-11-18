// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/feebumpcontroller.h>

FeeBumpController::FeeBumpController(QObject* parent) : WalletPageController(parent) {}

FeeBumpController::~FeeBumpController()
{
    if (!m_operation) return;
    disconnect(m_operation, nullptr, this, nullptr);
    if (m_fee_bumps) m_fee_bumps->release(m_operation);
}

void FeeBumpController::setFeeBumps(FeeBumpInterface* fee_bumps)
{
    if (m_fee_bumps == fee_bumps || !bindWallet(fee_bumps)) return;
    m_fee_bumps = fee_bumps;
    Q_EMIT feeBumpsChanged();
}

bool FeeBumpController::open(const QString& txid)
{
    // Opening comes from the overlay's own show, before it reports visible;
    // only the wallet's availability decides.
    if (!boundWallet() || !boundWallet()->available() || !m_fee_bumps) return false;
    let(nullptr);
    const QPointer<FeeBumpController> guard{this};
    auto* operation = m_fee_bumps->openFeeBump(txid, 1);
    if (!guard) return false;
    if (!operation) return false;
    m_txid = txid;
    m_operation = operation;
    m_shown_final = false;
    const auto observe = [this] {
        if (!m_operation || !available()) return;
        const auto state = m_operation->state();
        if (state == FeeBumpOperation::Succeeded || state == FeeBumpOperation::Failed) m_shown_final = true;
    };
    observe();
    connect(operation, &FeeBumpOperation::stateChanged, this, observe);
    connect(operation, &FeeBumpOperation::stateChanged, this, &FeeBumpController::changed);
    connect(operation, &FeeBumpOperation::resultChanged, this, &FeeBumpController::changed);
    connect(operation, &FeeBumpOperation::needsUnlockChanged, this, &FeeBumpController::changed);
    connect(operation, &QObject::destroyed, this, &FeeBumpController::changed);
    Q_EMIT changed();
    return true;
}

bool FeeBumpController::proceed()
{
    if (!m_operation || !boundWallet()) return false;
    if (uncertainOutcome()) return m_operation->canRecheckOutcome() && m_operation->recheck();
    if (readyToPrepareAgain()) return m_operation->prepareAgain(m_txid, 1);
    if (readyToConfirm()) return m_operation->confirm();
    return false;
}

bool FeeBumpController::unlock(const QString& passphrase)
{
    if (!m_operation || !boundWallet() || m_operation->busy()) return false;
    return m_operation->confirmWithPassphrase(passphrase);
}

void FeeBumpController::dismiss()
{
    if (!m_operation) return;
    const auto state = m_operation->state();
    if (state == FeeBumpOperation::Succeeded || (m_operation->committed() && state == FeeBumpOperation::Failed)) {
        if (m_fee_bumps) m_fee_bumps->presented(m_operation);
    }
    let(nullptr);
}

void FeeBumpController::stop()
{
    // A final result the page showed counts as presented: the next open
    // starts a fresh review instead of showing it again.
    if (m_operation && m_shown_final && m_fee_bumps) m_fee_bumps->presented(m_operation);
    let(nullptr);
}

void FeeBumpController::let(FeeBumpOperation* operation)
{
    if (m_operation == operation) return;
    const QPointer<FeeBumpOperation> previous{m_operation};
    m_operation = operation;
    if (previous) {
        disconnect(previous, nullptr, this, nullptr);
        if (m_fee_bumps) m_fee_bumps->release(previous);
    }
    Q_EMIT changed();
}
