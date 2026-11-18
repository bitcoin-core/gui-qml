// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/walletpublication.h>

#include <utility>

namespace walletpublication {
ObservedLoad::ObservedLoad(std::unique_ptr<interfaces::Wallet> wallet, std::unique_ptr<interfaces::Handler> handler,
                           std::shared_ptr<std::atomic<uint64_t>> unloaded, bool read_keys)
    : m_wallet(std::move(wallet)), m_handler(std::move(handler)),
      m_receipt(std::make_shared<Receipt>(std::move(unloaded))), m_read_keys(read_keys)
{
}

ObservedLoad::~ObservedLoad()
{
    if (m_handler) m_handler->disconnect();
}

std::optional<ObservedLoad::Facts> ObservedLoad::Receipt::publishedFacts() const
{
    std::lock_guard lock{mutex};
    return claimed ? facts : std::nullopt;
}

ObservedLoad::Facts ObservedLoad::describe(const std::function<std::string(interfaces::Wallet&)>& read_name)
{
    std::lock_guard describing{m_describe_mutex};
    interfaces::Wallet* wallet;
    {
        std::lock_guard lock{m_receipt->mutex};
        if (m_receipt->facts) return *m_receipt->facts;
        wallet = m_wallet.get();
    }
    // Only Core describes a wallet, and it cannot be claimed before this
    // description is published. Do not hold the GUI handoff lock across Core.
    Facts facts{read_name(*wallet)};
    if (m_read_keys) {
        facts.external = wallet->hasExternalSigner();
        facts.watch_only = !facts.external && wallet->privateKeysDisabled();
    }
    {
        std::lock_guard lock{m_receipt->mutex};
        m_receipt->facts = facts;
    }
    return facts;
}

std::unique_ptr<interfaces::Wallet> ObservedLoad::take(std::unique_ptr<interfaces::Handler>& handler)
{
    std::lock_guard lock{m_receipt->mutex};
    if (!m_receipt->facts || !m_wallet) return {};
    m_receipt->claimed = true;
    handler = std::move(m_handler);
    return std::move(m_wallet);
}

bool NativeObservation::capture(const std::shared_ptr<ObservedLoad>& wallet, uint64_t ingress)
{
    std::lock_guard lock{m_mutex};
    if (m_entries.size() == 64) { m_failed = true; return false; }
    m_entries.push_back({wallet, wallet->receipt(), ingress});
    return true;
}

NativeObservation::Observed NativeObservation::find(const QString& id, const std::function<QString(interfaces::Wallet&)>& read_name) const
{
    std::vector<Entry> entries;
    { std::lock_guard lock{m_mutex}; entries = m_entries; }
    Observed earliest;
    uint64_t earliest_ingress{0};
    for (const auto& entry : entries) {
        if (earliest.events.unloaded && entry.ingress >= earliest_ingress) continue;
        auto wallet = entry.wallet.lock();
        const auto facts = wallet ? wallet->describe([&](interfaces::Wallet& value) { return read_name(value).toStdString(); })
                                  : entry.receipt->publishedFacts();
        if (facts && QString::fromStdString(facts->name) == id) {
            earliest_ingress = entry.ingress;
            earliest = {std::move(wallet), {m_clock, entry.receipt->unloaded}, *facts};
        }
    }
    return earliest;
}
walletopening::NativeEvents NativeObservation::resolve(const QString& id, const std::function<QString(interfaces::Wallet&)>& read_name) const
{
    return find(id, read_name).events;
}
void Release::whenReleased(std::function<void()> callback)
{
    {
        std::lock_guard lock{m_mutex};
        if (!m_released) { m_notifications.push_back(std::move(callback)); return; }
    }
    callback();
}
void Release::release()
{
    std::list<std::function<void()>> callbacks;
    { std::lock_guard lock{m_mutex}; if (std::exchange(m_released, true)) return; callbacks.swap(m_notifications); }
    for (auto& callback : callbacks) callback(); // Enqueue GUI only; never wait.
}
} // namespace walletpublication
