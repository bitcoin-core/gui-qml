// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_QML_WALLETREGISTRY_H
#define BITCOIN_QML_WALLETREGISTRY_H

#include <qml/guipresentation.h>
#include <qml/retainedvalue.h>
#include <qml/walletnames.h>
#include <qml/walletpublication.h>
#include <qml/models/walletmodel.h>

#include <QAbstractListModel>
#include <QMutex>
#include <QPointer>

#include <atomic>
#include <functional>
#include <limits>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <utility>

class WalletRegistry;

class OpenWalletListModel final : public QAbstractListModel
{
    Q_OBJECT
public:
    OpenWalletListModel(const WalletRegistry& registry, QObject* parent, presentation::Post post);
    ~OpenWalletListModel() override;
    int rowCount(const QModelIndex& parent = {}) const override;
    QModelIndex index(int row, int column = 0, const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;
    bool addWallet(WalletModel* wallet);
    void removeWallet(WalletModel* wallet);
    int rowForWallet(WalletModel* wallet) const;
    bool valid() const { return bool(m_state); }
    std::shared_ptr<WalletNameRegistry> nameRegistry() const;
    void close();
private:
    struct State;
    std::shared_ptr<State> m_state;
    presentation::Post m_post;
    bool removeWalletRow(WalletModel* wallet);
    void reconcile();
};

/** One GUI-side registry writer for the existing opening protocol. Native
 * subscriptions and Session remain the backend owners; this adapter retains
 * exact opening/origin identities and the original bounded retirement escrow.
 * It never selects a wallet or infers a new opening from a name alone.
 */
class WalletRegistry : public std::enable_shared_from_this<WalletRegistry>, public walletpublication::WalletCatalogPublisher
{
public:
    struct WalletInformation { QString name; int key_scheme; };
    enum class Notice { Inventory, CloseTarget, Loaded, Found, AdoptionAvailable, PublicationsReleased, NativeDrained, Retired, LoadedChanged };
    enum class Information { Subscribe, ApplyAlias, Initialize };
    /** Bounded GUI observation ports. Callbacks are invoked only while owner is
     * alive, and their caller rechecks it after every presentation callback.
     * This is the compatibility boundary; registry never inspects a controller.
     */
    struct Host {
        QPointer<QObject> owner;
        std::function<bool()> stopping;
        std::function<quint64()> publicationId;
        std::function<bool(quint64)> publicationCurrent;
        std::function<void(Notice)> notice;
        std::function<bool(bool)> commitLoaded;
        std::function<void(bool)> adoptionUnavailable;
        std::function<WalletModel*(walletopening::Source, const QString&, walletopening::NativeEvents, std::unique_ptr<interfaces::Handler>)> createFacade;
        std::function<void(WalletModel*, Information)> information;
        std::function<void(const QString&, bool)> loadState;
        std::function<void(const QString&)> loadError;
        std::function<bool(WalletModel*)> forgetCloseTarget;
        std::function<void(WalletModel*)> closeQuestionChanged;
        std::function<void(WalletModel*)> prepareForget;
        std::function<void(WalletModel*, WalletModel*)> forget;
        std::function<void()> contextChanged;
    };
    struct Services {
        std::shared_ptr<QObject> bridge;
        WalletModel* empty{nullptr};
        presentation::Post post;
        std::function<void(std::function<void()>)> shutdown;
        std::shared_ptr<execution::SerialQueue> value_cleanup{};
    };
    struct Form {
        QPointer<QObject> owner;
        std::function<void()> close;
    };

    /** Acknowledges the reserved Native release of one command's backend cell.
     * Metadata may remain observable without extending that backend lifetime.
     */
    using ResultLifetime = walletpublication::Release;
    struct OpeningAdoption {
        QPointer<WalletModel> facade;
        walletopening::Opening identity;
        bool closed{false};
        std::set<std::shared_ptr<ResultLifetime>, std::owner_less<std::shared_ptr<ResultLifetime>>> pending_results;
        std::function<void()> retirement_ready;
        std::shared_ptr<std::atomic<uint64_t>> unloaded{std::make_shared<std::atomic<uint64_t>>(0)};
    };
    struct AdoptionOrigin {
        struct Binding { QString name; std::shared_ptr<OpeningAdoption> opening; uint64_t notification_sequence{0}; };
        std::map<QString, std::shared_ptr<Binding>> bindings;
        bool unavailable{false};
        uint64_t sequence{0}; // GUI origin lifetime only.
        uint64_t admission_event{0}; // Observed Native-event clock, independent domain.
        std::shared_ptr<ResultLifetime> result_lifetime;
        std::function<void()> retired;
        ~AdoptionOrigin() { if (retired) { try { retired(); } catch (...) {} } }
    };
    struct Retirement : std::enable_shared_from_this<Retirement> {
        struct ClosedFence { QString name; uint64_t origin_cutoff, event_cutoff; std::shared_ptr<OpeningAdoption> opening; };
        struct Values {
            std::list<WalletModel*> pending;
            std::map<QObject*, Form> forms;
            std::map<WalletModel*, QPointer<WalletModel>> alive;
            std::map<QString, WalletModel*> names;
            std::map<WalletModel*, std::list<WalletModel*>::iterator> positions;
            std::map<WalletModel*, WalletInformation> information;
            std::set<WalletModel*> retiring;
            std::multiset<QString> retiring_names;
            std::map<WalletModel*, std::shared_ptr<OpeningAdoption>> adoptions;
            std::set<uint64_t> origins;
            uint64_t admitting_origin{std::numeric_limits<uint64_t>::max()};
            std::map<QString, std::shared_ptr<ClosedFence>> closed;
            std::map<std::pair<uint64_t, uint64_t>, std::shared_ptr<ClosedFence>> close_order;
        };
        std::shared_ptr<Values> values;
        std::shared_ptr<QObject> bridge;
        QPointer<QObject> observer;
        std::shared_ptr<Host> host;
        QPointer<WalletModel> empty;
        std::shared_ptr<Retirement> keep_alive;
        std::function<void(std::function<void()>)> post;
        std::function<void(std::function<void()>)> shutdown;
        std::function<void()> join_when_drained;
        std::shared_ptr<execution::ValueCleanupQueue> value_cleanup;
        bool started{false}, pumping{false}, batches_done{false}, native_done{false}, completed{false};
        size_t native_backends{0};
        bool retained{true};

        void pruneAdoptions();
        void watch(WalletModel* model);
        bool adopt(WalletModel* model, bool remove);
        void enqueue();
        void pump();
        void finish();
        void notifyRetired();
        void drainValues(std::function<void()> done);
    };
    WalletRegistry(std::shared_ptr<Host> host, Services services);
    ~WalletRegistry() override;
    OpenWalletListModel* openWalletModel() const { return m_open_wallet_model; }
    std::shared_ptr<WalletNameRegistry> nameRegistry() const { return m_names_registry; }
    execution::ValueCleanup valueCleanup() const;
    bool containsExact(WalletModel* wallet) const override;
    std::shared_ptr<walletpublication::CatalogReservation> reserveCreation(const QString& operation_id) override;
    walletpublication::Result publishCreation(const std::shared_ptr<walletpublication::CatalogReservation>& ticket,
        walletpublication::TakeWallet take, const QString& wallet_id, walletopening::NativeEvents observation) override;
    walletpublication::Result publishWithLifetime(const std::shared_ptr<walletpublication::CatalogReservation>& ticket, walletpublication::TakeWallet take,
        const QString& wallet_id, walletopening::NativeEvents observation, std::shared_ptr<walletpublication::Release> cell) override;
    void finishCreation(const std::shared_ptr<walletpublication::CatalogReservation>& ticket) override;
    WalletModel* walletForName(const QString& name) const;
    WalletModel* firstAvailable(WalletModel* except = nullptr) const;
    QList<QObject*> openWallets() const;
    bool empty() const { return m_wallets.empty(); }
    const auto& names() const { return m_wallet_names; }
    const auto& positions() const { return m_wallet_positions; }
    const auto& wallets() const { return m_wallets; }
    const auto& information() const { return m_wallet_information; }
    void retainResult(WalletModel* wallet, const std::shared_ptr<ResultLifetime>& lifetime);
    void rememberInformation(WalletModel* wallet, WalletInformation information);
    void insert(WalletModel* wallet, const QString& name);
    void close();
    bool adoptRetirement(WalletModel* model, bool remove);
    void detachPresentation();
    void detachController();
    void beginRetirement(std::shared_ptr<execution::Executor> executor,
                         std::function<void(std::function<void()>)> notification_retirement);
    void registerForm(QObject* form, std::function<void()> close);
    void forgetForm(QObject* form);
    bool adoptionActive() const { return m_adoption_depth != 0; }
    bool retained() const { return m_retirement->retained; }
    std::shared_ptr<Retirement> retirement() const { return m_retirement; }
    std::shared_ptr<AdoptionOrigin> newOrigin();
    void closeOpening(WalletModel* wallet);
    bool registerModel(WalletModel* wallet, std::shared_ptr<OpeningAdoption> adoption);
    void removeModel(WalletModel* wallet, bool remove);
    WalletModel* adoptWallet(walletpublication::TakeWallet take,
        const std::optional<QString>& captured_name, std::shared_ptr<AdoptionOrigin> origin, uint64_t ingress_sequence, walletopening::NativeEvents observation = {});
    WalletModel* adoptWallet(std::unique_ptr<interfaces::Wallet>& wallet, const QString& name);
    void observeOpening(const std::shared_ptr<AdoptionOrigin>& source, const std::shared_ptr<AdoptionOrigin>& intent,
                        const QString& name, WalletModel* facade, uint64_t notification_sequence, uint64_t ingress_sequence);
    std::shared_ptr<walletopening::NativeEventClock> eventClock() const { return m_adoption_clock; }
    std::shared_ptr<AdoptionOrigin> notificationOrigin() const { return m_notification_origin; }
    std::shared_ptr<AdoptionOrigin> commandOrigin() const { return m_command_origin; }
    void setNotificationOrigin(std::shared_ptr<AdoptionOrigin> origin) { m_notification_origin = std::move(origin); }
    void setCommandOrigin(std::shared_ptr<AdoptionOrigin> origin) { m_command_origin = std::move(origin); }
    std::shared_ptr<AdoptionOrigin> exchangePublishingOrigin(std::shared_ptr<AdoptionOrigin> origin) { return std::exchange(m_publishing_origin, std::move(origin)); }
    void clearCommandOrigin() { m_command_origin.reset(); }
    void clearOrigins() { m_command_origin.reset(); m_notification_origin.reset(); m_publishing_origin.reset(); }
    template <typename T, typename Factory>
    std::shared_ptr<T> retain(Factory factory) const
    {
        try { return execution::RetainValues<T>(valueCleanup(), std::move(factory)); }
        catch (...) { return {}; }
    }
private:
    const std::shared_ptr<Host> m_host;
    const Services m_services;
    size_t m_adoption_depth{0};
    std::map<QObject*, Form> m_forms;
    mutable QMutex m_wallets_mutex;
    std::list<WalletModel*> m_wallets;
    std::map<QString, WalletModel*> m_wallet_names;
    std::map<WalletModel*, std::list<WalletModel*>::iterator> m_wallet_positions;
    std::map<WalletModel*, WalletInformation> m_wallet_information;
    std::shared_ptr<WalletNameRegistry> m_names_registry;
    OpenWalletListModel* m_open_wallet_model{nullptr};
    std::shared_ptr<Retirement> m_retirement;
    std::shared_ptr<AdoptionOrigin> m_notification_origin, m_command_origin, m_publishing_origin;
    const std::shared_ptr<walletopening::NativeEventClock> m_adoption_clock{std::make_shared<walletopening::NativeEventClock>()};
    uint64_t m_origin_sequence{0}, m_close_fence_sequence{0}, m_adoption_unavailable_event_cutoff{0};
};

#endif // BITCOIN_QML_WALLETREGISTRY_H
