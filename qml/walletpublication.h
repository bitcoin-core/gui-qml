// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_QML_WALLETPUBLICATION_H
#define BITCOIN_QML_WALLETPUBLICATION_H

#include <qml/walletopening.h>
#include <QPointer>
#include <QString>

#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

class WalletQmlModel;
namespace interfaces { class Wallet; }

namespace walletpublication {
// An exact Native backend release receipt, independent of retained text facts.
class Release final {
public:
    void whenReleased(std::function<void()> callback);
    void release();
private:
    std::mutex m_mutex;
    bool m_released{false};
    std::list<std::function<void()>> m_notifications;
};
// A bounded receipt from the original subscription, never a second catalog.
// Weak handles are resolved on Core; no GUI/Core call or extra backend owner.
class NativeObservation final {
public:
    explicit NativeObservation(std::shared_ptr<walletopening::NativeEventClock> clock) : m_clock(std::move(clock)) { m_entries.reserve(64); }
    bool capture(std::weak_ptr<interfaces::Wallet> wallet, std::shared_ptr<std::atomic<uint64_t>> unloaded, std::shared_ptr<const void> observation, uint64_t ingress);
    struct Observed { std::shared_ptr<interfaces::Wallet> wallet; walletopening::NativeEvents events; };
    // Native-only recovery of the original notification handle after a known
    // late Core persistence error, never a name-based getter or GUI call.
    Observed find(const QString& id, const std::function<QString(interfaces::Wallet&)>& read_name) const;
    walletopening::NativeEvents resolve(const QString& id, const std::function<QString(interfaces::Wallet&)>& read_name) const;
    void fail() { m_failed = true; }
    bool failed() const { return m_failed.load(); }
private:
    struct Entry { std::weak_ptr<interfaces::Wallet> wallet; std::shared_ptr<std::atomic<uint64_t>> unloaded; std::shared_ptr<const void> observation; uint64_t ingress; };
    const std::shared_ptr<walletopening::NativeEventClock> m_clock;
    mutable std::mutex m_mutex;
    std::vector<Entry> m_entries;
    std::atomic_bool m_failed{false};
};
struct Ticket {
    explicit Ticket(QString value, std::shared_ptr<Release> token, std::shared_ptr<NativeObservation> observed) : operation_id(std::move(value)), backend_release(std::move(token)), observation(std::move(observed)) {}
    virtual ~Ticket() = default;
    const QString operation_id;
    const std::shared_ptr<Release> backend_release;
    const std::shared_ptr<NativeObservation> observation;
};
enum class Availability { Available, Closed, Unavailable };
struct Result {
    QPointer<WalletQmlModel> wallet;
    Availability availability{Availability::Unavailable};
    QString error;
};
// GUI-only bounded publication port. Neither the Core effect nor its Domain
// owner includes or calls a concrete controller/registry or touches a QObject.
class WalletCatalogPublisher {
public:
    virtual ~WalletCatalogPublisher() = default;
    virtual std::shared_ptr<Ticket> reserveCreation(const QString& operation_id) = 0;
    virtual Result publishCreation(const std::shared_ptr<Ticket>& ticket,
        std::shared_ptr<interfaces::Wallet> wallet, const QString& wallet_id, walletopening::NativeEvents observation) = 0;
    virtual Result publishWithLifetime(const std::shared_ptr<Ticket>& ticket,
        std::shared_ptr<interfaces::Wallet> wallet, const QString& id, walletopening::NativeEvents observation, std::shared_ptr<Release>) { return publishCreation(ticket, std::move(wallet), id, std::move(observation)); }
    virtual void finishCreation(const std::shared_ptr<Ticket>& ticket) = 0;
    virtual bool containsExact(WalletQmlModel* wallet) const = 0;
};
} // namespace walletpublication
#endif // BITCOIN_QML_WALLETPUBLICATION_H
