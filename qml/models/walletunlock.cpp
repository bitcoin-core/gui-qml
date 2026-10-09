// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/walletunlock.h>

#include <interfaces/wallet.h>
#include <qml/util.h>
#include <wallet/wallet.h>

#include <utility>

WalletSecurityState ReadSecurityState(interfaces::Wallet& wallet)
{
    if (auto* local = wallet.wallet()) {
        LOCK(local->cs_wallet);
        return {local->HasEncryptionKeys(), local->IsLocked()};
    }
    return {wallet.isCrypted(), wallet.isLocked()};
}

WalletUnlockResult TryUnlockWithPassphrase(interfaces::Wallet& wallet,
                                           SecureString& passphrase)
{
    const auto security = ReadSecurityState(wallet);
    if (!security.encrypted || !security.locked) {
        QmlUtil::ClearSecureString(passphrase);
        return WalletUnlockResult::AlreadyUnlocked;
    }
    const bool unlocked{wallet.unlock(passphrase)};
    QmlUtil::ClearSecureString(passphrase);
    return unlocked ? WalletUnlockResult::UnlockedNowRelockRequired
                    : WalletUnlockResult::IncorrectPassphrase;
}

WalletRelockGuard::WalletRelockGuard(interfaces::Wallet& wallet,
                                     std::function<void()> refresh_security_state,
                                     bool active)
    : m_wallet{wallet}, m_refresh_security_state{std::move(refresh_security_state)}, m_active{active}
{
}

WalletRelockGuard::~WalletRelockGuard()
{
    relock();
}

void WalletRelockGuard::relock()
{
    if (!m_active) {
        return;
    }
    m_wallet.lock();
    m_refresh_security_state();
    m_active = false;
}
