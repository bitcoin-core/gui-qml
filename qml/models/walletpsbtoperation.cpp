// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/walletpsbtoperation.h>

#include <qml/models/walletmodel.h>

WalletPsbtOperation::WalletPsbtOperation(WalletModel& wallet, Kind kind, QObject* parent)
    : PsbtOperation(kind, parent), m_wallet{&wallet}
{
    connect(&wallet, &QObject::destroyed, this, [this] { publish(State::Cancelled); });
}

bool WalletPsbtOperation::start(const QString& path, const QString& signer_result)
{
    WalletModel* wallet = m_wallet.data();
    if (!wallet) return false;
    // Follow before starting: the wallet can report the outcome at once.
    connect(wallet, &WalletModel::psbtActionChanged, this, &WalletPsbtOperation::follow);
    switch (kind()) {
    case Kind::Save:
        m_action = signer_result.isEmpty() ? wallet->requestSaveCurrentTransactionAsPsbt(path)
                                           : wallet->requestSaveUnverifiedSignerResult(signer_result, path);
        break;
    case Kind::Broadcast: m_action = wallet->requestBroadcastCurrentTransaction(); break;
    case Kind::Recheck: m_action = wallet->requestRecheckCurrentTransaction(); break;
    case Kind::Import: m_action = wallet->requestImportPsbtFromFile(path); break;
    }
    if (!m_action) {
        disconnect(wallet, nullptr, this, nullptr);
        return false;
    }
    follow();
    return true;
}

void WalletPsbtOperation::cancel()
{
    // A save that still waits for its destination ends with the operation.
    if (kind() == Kind::Save && m_wallet && m_action) m_wallet->retirePsbtFileExport(m_action);
    if (m_wallet) disconnect(m_wallet, &WalletModel::psbtActionChanged, this, &WalletPsbtOperation::follow);
    publish(State::Cancelled);
}

void WalletPsbtOperation::follow()
{
    WalletModel* wallet = m_wallet.data();
    if (!wallet || finished() || !m_action) return;
    // The wallet started another action; this one's outcome is not known here.
    if (wallet->psbtActionId() != m_action) {
        publish(State::Stale);
        return;
    }
    using A = WalletModel::PsbtActionState;
    switch (wallet->psbtActionState()) {
    case A::PsbtIdle:
    case A::PsbtPending: return;
    case A::PsbtImported: publish(State::Imported, {}, {}, {}, int(wallet->psbtImportResult())); return;
    case A::PsbtKnown: publish(State::Known, {}, {}, {}, int(wallet->psbtImportResult())); return;
    case A::PsbtSaved: publish(State::Saved, {}, wallet->psbtSavedPath()); return;
    case A::PsbtSubmitted: publish(State::Submitted, {}, {}, wallet->psbtSubmittedTxid()); return;
    case A::PsbtFailed: publish(State::Failed, wallet->psbtActionError(), {}, {}, int(wallet->psbtImportResult())); return;
    case A::PsbtUncertain: publish(State::Uncertain, wallet->psbtActionError()); return;
    case A::PsbtStale:
    case A::PsbtClosed: publish(State::Stale); return;
    }
}
