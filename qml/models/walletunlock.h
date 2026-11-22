// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_WALLETUNLOCK_H
#define BITCOIN_QML_MODELS_WALLETUNLOCK_H

#include <support/allocators/secure.h>

#include <functional>
#include <mutex>

namespace interfaces {
class Wallet;
} // namespace interfaces

enum class WalletUnlockResult {
    AlreadyUnlocked,
    UnlockedNowRelockRequired,
    IncorrectPassphrase,
};

struct WalletSecurityState {
    bool encrypted;
    bool locked;
};

//! Read the encryption and lock state under cs_wallet, which EncryptWallet() holds while adding the master keys.
WalletSecurityState ReadSecurityState(interfaces::Wallet& wallet);

//! Try to unlock `wallet` with `passphrase`. `passphrase` is wiped on every return path.
WalletUnlockResult TryUnlockWithPassphrase(interfaces::Wallet& wallet,
                                           SecureString& passphrase);

//! Hold from checking the lock state until relocking; take it before cs_wallet.
class WalletUnlockSession
{
public:
    std::unique_lock<std::mutex> acquire() { return std::unique_lock{m_mutex}; }
    std::unique_lock<std::mutex> tryAcquire() { return std::unique_lock{m_mutex, std::try_to_lock}; }

private:
    std::mutex m_mutex;
};

class WalletRelockGuard
{
public:
    WalletRelockGuard(interfaces::Wallet& wallet,
                      std::function<void()> refresh_security_state,
                      bool active);
    ~WalletRelockGuard();

    WalletRelockGuard(const WalletRelockGuard&) = delete;
    WalletRelockGuard& operator=(const WalletRelockGuard&) = delete;

    void relock();

private:
    interfaces::Wallet& m_wallet;
    std::function<void()> m_refresh_security_state;
    bool m_active;
};

#endif // BITCOIN_QML_MODELS_WALLETUNLOCK_H
