// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_QML_WALLETREGISTRY_H
#define BITCOIN_QML_WALLETREGISTRY_H

#include <qml/backendexecutor.h>
#include <qml/guipresentation.h>
#include <qml/walletnames.h>
#include <qml/walletpublication.h>
#include <qml/models/walletqmlmodel.h>

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

class OpenWalletListModel final : public QAbstractListModel
{
    Q_OBJECT
public:
    explicit OpenWalletListModel(std::shared_ptr<execution::Scheduler> scheduler, QObject* parent = nullptr, presentation::Post post = {});
    ~OpenWalletListModel() override;
    int rowCount(const QModelIndex& parent = {}) const override;
    QModelIndex index(int row, int column = 0, const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;
    bool addWallet(WalletQmlModel* wallet);
    void removeWallet(WalletQmlModel* wallet);
    int rowForWallet(WalletQmlModel* wallet) const;
    bool valid() const { return bool(m_state); }
    std::shared_ptr<WalletNameRegistry> nameRegistry() const;
    void close();
private:
    struct State;
    std::shared_ptr<State> m_state;
    presentation::Post m_post;
    bool removeWalletRow(WalletQmlModel* wallet);
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
        std::function<WalletQmlModel*(std::shared_ptr<interfaces::Wallet>, const QString&, walletopening::NativeEvents)> createFacade;
        std::function<void(WalletQmlModel*, Information)> information;
        std::function<void(const QString&, bool)> loadState;
        std::function<void(const QString&)> loadError;
        std::function<bool(WalletQmlModel*)> forgetCloseTarget;
        std::function<void(WalletQmlModel*)> closeQuestionChanged;
        std::function<void(WalletQmlModel*)> prepareForget;
        std::function<void(WalletQmlModel*, WalletQmlModel*)> forget;
        std::function<void()> contextChanged;
    };
    struct Services {
        std::shared_ptr<execution::Scheduler> scheduler;
        std::shared_ptr<BackendExecutor> executor;
        std::shared_ptr<QObject> bridge;
        WalletQmlModel* empty{nullptr};
        presentation::Post post;
        std::function<void(std::function<void()>)> shutdown;
    };
    struct Form {
        QPointer<QObject> owner;
        std::function<void()> close;
    };

    struct ImportErrorFrame {
        struct Budget { std::atomic_size_t frames{0}, bytes{0}; };
        std::shared_ptr<Budget> budget;
        QString error;
        quint64 revision{0};
        size_t bytes{0};
        ImportErrorFrame(std::shared_ptr<Budget> value, const QString& text, quint64 version)
            : budget(std::move(value)), error(text), revision(version), bytes(size_t(text.size()) * sizeof(QChar)) {}
        ~ImportErrorFrame() { error.clear(); budget->bytes -= bytes; --budget->frames; }
    };
    struct ImportErrorInput {
        std::mutex mutex;
        std::shared_ptr<ImportErrorFrame::Budget> budget{std::make_shared<ImportErrorFrame::Budget>()};
        std::shared_ptr<const ImportErrorFrame> current, notified;
        std::map<const ImportErrorFrame*, std::shared_ptr<const ImportErrorFrame>> retired;
    };
    /** Acknowledges the reserved Native release of one command's backend cell.
     * Metadata may remain observable without extending that backend lifetime.
     */
    using ResultLifetime = walletpublication::Release;
    struct OpeningAdoption {
        QPointer<WalletQmlModel> facade;
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
            std::shared_ptr<ImportErrorInput> import_errors{std::make_shared<ImportErrorInput>()};
            std::list<WalletQmlModel*> pending;
            std::map<QObject*, Form> forms;
            std::map<WalletQmlModel*, QPointer<WalletQmlModel>> alive;
            std::map<QString, WalletQmlModel*> names;
            std::map<WalletQmlModel*, std::list<WalletQmlModel*>::iterator> positions;
            std::map<WalletQmlModel*, WalletInformation> information;
            std::set<WalletQmlModel*> retiring;
            std::multiset<QString> retiring_names;
            std::map<WalletQmlModel*, std::shared_ptr<OpeningAdoption>> adoptions;
            std::set<uint64_t> origins;
            uint64_t admitting_origin{std::numeric_limits<uint64_t>::max()};
            std::map<QString, std::shared_ptr<ClosedFence>> closed;
            std::map<std::pair<uint64_t, uint64_t>, std::shared_ptr<ClosedFence>> close_order;
        };
        std::shared_ptr<Values> values;
        std::shared_ptr<BackendExecutor> executor;
        std::shared_ptr<QObject> bridge;
        QPointer<QObject> observer;
        std::shared_ptr<Host> host;
        QPointer<WalletQmlModel> empty;
        std::shared_ptr<Retirement> keep_alive;
        std::function<void(std::function<void()>)> post;
        std::function<void(std::function<void()>)> shutdown;
        bool started{false}, pumping{false}, batches_done{false}, native_done{false}, completed{false};
        size_t native_backends{0};
        bool retained{true};

        void pruneAdoptions();
        void watch(WalletQmlModel* model);
        bool adopt(WalletQmlModel* model, bool remove);
        void enqueue();
        void pump();
        void finish();
    };
    WalletRegistry(std::shared_ptr<Host> host, Services services);
    OpenWalletListModel* openWalletModel() const { return m_open_wallet_model; }
    std::shared_ptr<WalletNameRegistry> nameRegistry() const { return m_names_registry; }
    bool containsExact(WalletQmlModel* wallet) const override;
    std::shared_ptr<walletpublication::Ticket> reserveCreation(const QString& operation_id) override;
    walletpublication::Result publishCreation(const std::shared_ptr<walletpublication::Ticket>& ticket,
        std::shared_ptr<interfaces::Wallet> wallet, const QString& wallet_id, walletopening::NativeEvents observation) override;
    walletpublication::Result publishWithLifetime(const std::shared_ptr<walletpublication::Ticket>& ticket, std::shared_ptr<interfaces::Wallet> wallet,
        const QString& wallet_id, walletopening::NativeEvents observation, std::shared_ptr<walletpublication::Release> cell) override;
    void finishCreation(const std::shared_ptr<walletpublication::Ticket>& ticket) override;
    WalletQmlModel* walletForName(const QString& name) const;
    WalletQmlModel* firstAvailable(WalletQmlModel* except = nullptr) const;
    QList<QObject*> openWallets() const;
    bool empty() const { return m_wallets.empty(); }
    const auto& names() const { return m_wallet_names; }
    const auto& positions() const { return m_wallet_positions; }
    const auto& wallets() const { return m_wallets; }
    const auto& information() const { return m_wallet_information; }
    void retainResult(WalletQmlModel* wallet, const std::shared_ptr<ResultLifetime>& lifetime);
    void rememberInformation(WalletQmlModel* wallet, WalletInformation information);
    void insert(WalletQmlModel* wallet, const QString& name);
    void close();
    bool adoptRetirement(WalletQmlModel* model, bool remove);
    void detachPresentation();
    void detachController();
    void beginRetirement();
    void registerForm(QObject* form, std::function<void()> close);
    void forgetForm(QObject* form);
    bool adoptionActive() const { return m_adoption_depth != 0; }
    bool retained() const { return m_retirement->retained; }
    std::shared_ptr<Retirement> retirement() const { return m_retirement; }
    std::shared_ptr<ImportErrorInput> importErrors() const;
    std::shared_ptr<AdoptionOrigin> newOrigin();
    void closeOpening(WalletQmlModel* wallet);
    bool registerModel(WalletQmlModel* wallet, std::shared_ptr<OpeningAdoption> adoption);
    void removeModel(WalletQmlModel* wallet, bool remove);
    WalletQmlModel* adoptWallet(std::shared_ptr<interfaces::Wallet> wallet,
        const std::optional<QString>& captured_name, std::shared_ptr<AdoptionOrigin> origin, uint64_t ingress_sequence, walletopening::NativeEvents observation = {});
    void observeOpening(const std::shared_ptr<AdoptionOrigin>& source, const std::shared_ptr<AdoptionOrigin>& intent,
                        const QString& name, WalletQmlModel* facade, uint64_t notification_sequence, uint64_t ingress_sequence);
    std::shared_ptr<walletopening::NativeEventClock> eventClock() const { return m_adoption_clock; }
    std::shared_ptr<AdoptionOrigin> notificationOrigin() const { return m_notification_origin; }
    std::shared_ptr<AdoptionOrigin> commandOrigin() const { return m_command_origin; }
    void setNotificationOrigin(std::shared_ptr<AdoptionOrigin> origin) { m_notification_origin = std::move(origin); }
    void setCommandOrigin(std::shared_ptr<AdoptionOrigin> origin) { m_command_origin = std::move(origin); }
    std::shared_ptr<AdoptionOrigin> exchangePublishingOrigin(std::shared_ptr<AdoptionOrigin> origin) { return std::exchange(m_publishing_origin, std::move(origin)); }
    void clearCommandOrigin() { m_command_origin.reset(); }
    void clearOrigins() { m_command_origin.reset(); m_notification_origin.reset(); m_publishing_origin.reset(); }
    template <typename T, typename Factory>
    static std::shared_ptr<T> retain(const std::shared_ptr<execution::Scheduler>& scheduler, Factory factory) {
        return RetainValue<T>(scheduler, std::move(factory));
    }
private:
    template <typename T, typename Factory>
    static std::shared_ptr<T> RetainValue(const std::shared_ptr<execution::Scheduler>& scheduler, Factory factory)
    {
        struct Storage { std::unique_ptr<T> value; };
        const auto storage = std::make_shared<Storage>();
        const auto key = scheduler->allocateKey();
        const auto cleanup = execution::Submit(*scheduler, [storage] { storage->value.reset(); }, [] {}, {},
            {execution::Lane::Cleanup, key, execution::WorkClass::Action}, true);
        if (!cleanup) return {};
        auto held = scheduler->borrow(key);
        scheduler->activateCleanup(cleanup.id);
        if (!held) return {};
        const auto lease = std::make_shared<execution::ResourceLease>(std::move(*held.lease));
        try { storage->value = factory(); }
        catch (...) { return {}; }
        return std::shared_ptr<T>{lease, storage->value.get()};
    }
    const std::shared_ptr<Host> m_host;
    const Services m_services;
    size_t m_adoption_depth{0};
    std::map<QObject*, Form> m_forms;
    mutable QMutex m_wallets_mutex;
    std::list<WalletQmlModel*> m_wallets;
    std::map<QString, WalletQmlModel*> m_wallet_names;
    std::map<WalletQmlModel*, std::list<WalletQmlModel*>::iterator> m_wallet_positions;
    std::map<WalletQmlModel*, WalletInformation> m_wallet_information;
    std::shared_ptr<WalletNameRegistry> m_names_registry;
    OpenWalletListModel* m_open_wallet_model{nullptr};
    std::shared_ptr<Retirement> m_retirement;
    std::shared_ptr<AdoptionOrigin> m_notification_origin, m_command_origin, m_publishing_origin;
    const std::shared_ptr<walletopening::NativeEventClock> m_adoption_clock{std::make_shared<walletopening::NativeEventClock>()};
    uint64_t m_origin_sequence{0}, m_close_fence_sequence{0}, m_adoption_unavailable_event_cutoff{0};
};

#endif // BITCOIN_QML_WALLETREGISTRY_H
