// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/walletsenddraft.h>

#include <qml/models/coinslistmodel.h>
#include <qml/models/sendrecipientslistmodel.h>
#include <qml/models/walletpsbtoperation.h>
#include <qml/models/walletmodel.h>

#include <memory>

using State = PreparationOperation::State;

WalletPreparationOperation::WalletPreparationOperation(WalletModel& wallet, QObject* parent)
    : PreparationOperation(parent), m_wallet{&wallet}
{
    connect(&wallet, &QObject::destroyed, this, [this] { publish(State::Cancelled); });
}

bool WalletPreparationOperation::start(std::optional<QString> passphrase)
{
    WalletModel* wallet = m_wallet.data();
    if (!wallet) return false;
    disconnect(wallet, &WalletModel::preparationChanged, this, &WalletPreparationOperation::follow);
    // Follow before starting: the wallet can report the outcome at once. Its
    // notice of the new preparation arrives before the id does, so the
    // previous id is forgotten first.
    connect(wallet, &WalletModel::preparationChanged, this, &WalletPreparationOperation::follow);
    m_preparation = 0;
    const auto started = passphrase ? wallet->requestPrepareTransactionWithPassphrase(*passphrase) : wallet->requestPrepareTransaction();
    if (!started) {
        disconnect(wallet, &WalletModel::preparationChanged, this, &WalletPreparationOperation::follow);
        return false;
    }
    m_preparation = started;
    if (passphrase) markPasswordRequested();
    publish(State::Pending);
    follow();
    return true;
}

void WalletPreparationOperation::unlock(const QString& passphrase)
{
    // A refused retry leaves the prompt as it is.
    if (state() == State::NeedsUnlock) start(passphrase);
}

void WalletPreparationOperation::cancel()
{
    if (m_wallet) disconnect(m_wallet, &WalletModel::preparationChanged, this, &WalletPreparationOperation::follow);
    publish(State::Cancelled);
}

void WalletPreparationOperation::follow()
{
    WalletModel* wallet = m_wallet.data();
    if (!wallet || finished() || !m_preparation) return;
    // The wallet prepares another draft now; this one has no outcome here.
    if (wallet->preparationId() != m_preparation) {
        publish(State::Cancelled);
        return;
    }
    if (wallet->preparationBusy()) return;
    using P = WalletModel::PreparationState;
    switch (wallet->preparationState()) {
    case P::PreparationIdle:
    case P::PreparationPending: return;
    case P::PreparationReady: publish(State::Ready); return;
    case P::PreparationNeedsUnlock: publish(State::NeedsUnlock, wallet->transactionError()); return;
    case P::PreparationStale: publish(State::Stale); return;
    case P::PreparationClosed: publish(State::Cancelled); return;
    case P::PreparationFailed:
    case P::PreparationUncertain: publish(State::Failed, wallet->transactionError()); return;
    }
}

WalletSendDraft::WalletSendDraft(WalletModel& wallet)
    : SendDraftInterface(&wallet), m_wallet{&wallet}
{
    connect(&wallet, &WalletModel::availableChanged, this, &SendDraftInterface::availableChanged);
    connect(&wallet, &WalletModel::feeEstimatePendingChanged, this, &SendDraftInterface::draftReadyChanged);
    connect(&wallet, &WalletModel::preparationChanged, this, &SendDraftInterface::draftReadyChanged);
    connect(&wallet, &WalletModel::customFeeTargetPendingChanged, this, &SendDraftInterface::draftReadyChanged);
    connect(&wallet, &WalletModel::coinSelectionChanged, this, &SendDraftInterface::draftReadyChanged);
    connect(&wallet, &WalletModel::sendAmountExhaustsBalanceChanged, this, &SendDraftInterface::draftReadyChanged);
    connect(&wallet, &WalletModel::maximumRecipientChanged, this, &SendDraftInterface::draftReadyChanged);
    connect(&wallet, &WalletModel::customFeeEnabledChanged, this, &SendDraftInterface::draftReadyChanged);
    connect(&wallet, &WalletModel::customFeeRateValidChanged, this, &SendDraftInterface::draftReadyChanged);
    connect(&wallet, &WalletModel::psbtActionChanged, this, &SendDraftInterface::draftReadyChanged);
    if (auto* fees = wallet.fees()) {
        connect(fees, &FeeSelectionModel::policyChanged, this, &SendDraftInterface::draftReadyChanged);
        connect(fees, &FeeSelectionModel::estimateChanged, this, &SendDraftInterface::draftReadyChanged);
    }
    if (auto* recipients = wallet.sendRecipientList()) {
        connect(recipients, &SendRecipientsListModel::revisionChanged, this, &SendDraftInterface::draftChanged);
        connect(recipients, &SendRecipientsListModel::validationChanged, this, &SendDraftInterface::draftReadyChanged);
    }
    if (auto* coins = wallet.coinsListModel()) {
        connect(coins, &CoinsListModel::selectionSnapshotChanged, this, &SendDraftInterface::draftChanged);
    }
}

bool WalletSendDraft::available() const
{
    return m_wallet && m_wallet->available();
}

QString WalletSendDraft::draftRevision() const
{
    if (!m_wallet) return {};
    const auto* recipients = m_wallet->sendRecipientList();
    const auto* coins = m_wallet->coinsListModel();
    return (recipients ? recipients->revision() : QString{}) + QLatin1Char(':') + (coins ? coins->selectionToken() : QString{});
}

bool WalletSendDraft::selectionPending() const
{
    return m_wallet && m_wallet->coinsListModel() && m_wallet->coinsListModel()->selectionPending();
}

bool WalletSendDraft::draftReady() const
{
    const auto* wallet = m_wallet.data();
    if (!wallet) return false;
    const auto* recipients = wallet->sendRecipientList();
    const auto* fees = wallet->fees();
    return recipients && recipients->allValid() && fees && !fees->policyPending() &&
        !wallet->feeEstimatePending() && !wallet->preparationBusy() &&
        !wallet->customFeeTargetPending() && !wallet->coinSelectionPending() && !wallet->coinSelectionBlocked() &&
        !wallet->sendAmountExhaustsBalance() && (!wallet->maximumRecipient() || fees->errorText().isEmpty()) &&
        (!wallet->customFeeEnabled() || wallet->customFeeRateValid());
}

bool WalletSendDraft::sweepsWallet() const
{
    return m_wallet && m_wallet->sendDraftSweepsWallet();
}

void WalletSendDraft::resetDraft()
{
    if (!available() || m_wallet->preparationBusy() || m_wallet->psbtActionBusy()) return;
    const QPointer<WalletModel> wallet{m_wallet};
    wallet->sendRecipientList()->clear();
    if (!wallet) return;
    auto* coins = wallet->coinsListModel();
    coins->setManualInputSelection(false);
    if (wallet) coins->update();
}

PreparationOperation* WalletSendDraft::startPreparation(std::optional<QString> passphrase)
{
    if (!m_wallet) return nullptr;
    auto operation = std::make_unique<WalletPreparationOperation>(*m_wallet);
    if (!operation->start(std::move(passphrase))) return nullptr;
    return operation.release();
}

PsbtOperation* WalletSendDraft::startImport(const QString& path)
{
    if (!m_wallet) return nullptr;
    auto operation = std::make_unique<WalletPsbtOperation>(*m_wallet, PsbtOperation::Kind::Import);
    if (!operation->start(path, {})) return nullptr;
    return operation.release();
}

SendDraftInterface* WalletModel::sendDraft() const
{
    if (!m_send_draft) m_send_draft = new WalletSendDraft(*const_cast<WalletModel*>(this));
    return m_send_draft;
}
