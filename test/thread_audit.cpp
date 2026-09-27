// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/thread_audit.h>

#include <coins.h>
#include <interfaces/handler.h>
#include <net_processing.h>
#include <node/types.h>
#include <policy/feerate.h>
#include <policy/rbf.h>
#include <univalue.h>
#include <wallet/wallet.h>

#include <QDebug>

#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <utility>

namespace qmlintegration {
void ThreadAudit::check(const char* method, ThreadPolicy policy) const
{
    const auto phase = m_phase.load();
    if (policy != ThreadPolicy::Forbidden &&
        (QThread::currentThread() != m_gui_thread ||
         (m_local && (policy == ThreadPolicy::LocalShutdown || policy == ThreadPolicy::LocalAccessor ||
                      (policy == ThreadPolicy::Bootstrap && phase == TestPhase::Bootstrap))))) {
        std::function<void(const char*)> observer;
        {
            std::lock_guard lock{m_observer_mutex};
            observer = m_observer;
        }
        if (observer) observer(method);
        return;
    }
    const char* phase_name = phase == TestPhase::Bootstrap ? "bootstrap" : phase == TestPhase::Running ? "running" : "shutdown";
    // The application installs a Qt handler that writes to debug.log. Always
    // retain this diagnostic on stderr as well, including before normal exit.
    std::fprintf(stderr, "Core thread policy violation: %s (phase=%s, %s)\n", method, phase_name,
                 policy == ThreadPolicy::Forbidden ? "unwrapped backend access" : "GUI thread");
    std::fflush(stderr);
    std::abort();
}

void ThreadAudit::setObserver(std::function<void(const char*)> observer)
{
    std::lock_guard lock{m_observer_mutex};
    m_observer = std::move(observer);
}

namespace {
// The generated forwarding code has a separate, explicit policy for every
// method, including inherited default methods. CTest checks it against the
// pinned interface headers so adding a default method cannot open a bypass.
#include <test/thread_audit_generated.inc>
} // namespace

std::unique_ptr<interfaces::Node> CheckNode(std::unique_ptr<interfaces::Node> backend, std::shared_ptr<ThreadAudit> audit)
{
    return std::make_unique<CheckedNode>(std::move(backend), std::move(audit));
}

std::unique_ptr<interfaces::Chain> CheckChain(std::unique_ptr<interfaces::Chain> backend, std::shared_ptr<ThreadAudit> audit)
{
    return std::make_unique<CheckedChain>(std::move(backend), std::move(audit));
}

std::unique_ptr<interfaces::Wallet> CheckWallet(std::unique_ptr<interfaces::Wallet> backend, std::shared_ptr<ThreadAudit> audit)
{
    return backend ? std::make_unique<CheckedWallet>(std::move(backend), std::move(audit)) : nullptr;
}

std::unique_ptr<interfaces::WalletLoader> CheckWalletLoader(std::unique_ptr<interfaces::WalletLoader> backend, std::shared_ptr<ThreadAudit> audit)
{
    return std::make_unique<CheckedWalletLoader>(std::move(backend), std::move(audit));
}
} // namespace qmlintegration
