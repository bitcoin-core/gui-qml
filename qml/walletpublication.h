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
#include <optional>
#include <string>
#include <utility>
#include <vector>

class WalletQmlModel;
namespace interfaces { class Wallet; }

namespace walletpublication {
// Claim the wallet only after the registry accepts a new opening.
using TakeWallet = std::function<walletopening::Source(std::unique_ptr<interfaces::Handler>&)>;

/** Owns a load notification until the registry accepts it. */
class ObservedLoad final {
public:
    struct Facts {
        std::string name;
        bool external{false}, watch_only{false};
    };
    struct Receipt {
        const std::shared_ptr<std::atomic<uint64_t>> unloaded;
        explicit Receipt(std::shared_ptr<std::atomic<uint64_t>> flag) : unloaded(std::move(flag)) {}
        std::optional<Facts> publishedFacts() const;
    private:
        friend class ObservedLoad;
        mutable std::mutex mutex;
        std::optional<Facts> facts;
        bool claimed{false};
    };

    ObservedLoad(std::unique_ptr<interfaces::Wallet> wallet, std::unique_ptr<interfaces::Handler> handler,
                 std::shared_ptr<std::atomic<uint64_t>> unloaded, bool read_keys);
    ~ObservedLoad();
    std::shared_ptr<const Receipt> receipt() const { return m_receipt; }
    Facts describe(const std::function<std::string(interfaces::Wallet&)>& read_name);
    std::unique_ptr<interfaces::Wallet> take(std::unique_ptr<interfaces::Handler>& handler);

private:
    std::unique_ptr<interfaces::Wallet> m_wallet;
    std::unique_ptr<interfaces::Handler> m_handler;
    const std::shared_ptr<Receipt> m_receipt;
    const bool m_read_keys;
    std::mutex m_describe_mutex;
};
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
    bool capture(const std::shared_ptr<ObservedLoad>& wallet, uint64_t ingress);
    struct Observed {
        std::shared_ptr<ObservedLoad> wallet;
        walletopening::NativeEvents events;
        ObservedLoad::Facts facts;
    };
    // Native-only recovery of the original notification handle after a known
    // late Core persistence error, never a name-based getter or GUI call.
    Observed find(const QString& id, const std::function<QString(interfaces::Wallet&)>& read_name) const;
    walletopening::NativeEvents resolve(const QString& id, const std::function<QString(interfaces::Wallet&)>& read_name) const;
    void fail() { m_failed = true; }
    bool failed() const { return m_failed.load(); }
private:
    struct Entry { std::weak_ptr<ObservedLoad> wallet; std::shared_ptr<const ObservedLoad::Receipt> receipt; uint64_t ingress; };
    const std::shared_ptr<walletopening::NativeEventClock> m_clock;
    mutable std::mutex m_mutex;
    std::vector<Entry> m_entries;
    std::atomic_bool m_failed{false};
};
struct CatalogReservation {
    explicit CatalogReservation(QString value, std::shared_ptr<Release> token, std::shared_ptr<NativeObservation> observed) : operation_id(std::move(value)), backend_release(std::move(token)), observation(std::move(observed)) {}
    virtual ~CatalogReservation() = default;
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
    virtual std::shared_ptr<CatalogReservation> reserveCreation(const QString& operation_id) = 0;
    virtual Result publishCreation(const std::shared_ptr<CatalogReservation>& ticket,
        TakeWallet take, const QString& wallet_id, walletopening::NativeEvents observation) = 0;
    virtual Result publishWithLifetime(const std::shared_ptr<CatalogReservation>& ticket,
        TakeWallet take, const QString& id, walletopening::NativeEvents observation, std::shared_ptr<Release>) { return publishCreation(ticket, std::move(take), id, std::move(observation)); }
    virtual void finishCreation(const std::shared_ptr<CatalogReservation>& ticket) = 0;
    virtual bool containsExact(WalletQmlModel* wallet) const = 0;
};
} // namespace walletpublication
#endif // BITCOIN_QML_WALLETPUBLICATION_H
