// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_QML_WALLETCREATION_H
#define BITCOIN_QML_WALLETCREATION_H

#include <qml/execution.h>
#include <qml/guipresentation.h>
#include <qml/retainedvalue.h>
#include <qml/secretinput.h>
#include <qml/snapshotquery.h>
#include <qml/walletpublication.h>

#include <QString>
#include <QStringList>
#include <QObject>

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <vector>

namespace interfaces { class Node; class Wallet; }
namespace execution { class SerialQueue; }
struct bilingual_str;
struct WalletNameRegistry;

struct WalletOperationError {
    Q_GADGET
    Q_PROPERTY(QString code MEMBER code)
    Q_PROPERTY(QString message MEMBER message)
    Q_PROPERTY(QString field MEMBER field)
    Q_PROPERTY(QString recovery MEMBER recovery)
public:
    QString code, message, field, recovery;
    bool empty() const { return code.isEmpty() && message.isEmpty(); }
    friend bool operator==(const WalletOperationError&, const WalletOperationError&) = default;
};
Q_DECLARE_METATYPE(WalletOperationError)

struct WalletCreationReceipt {
    Q_GADGET
    Q_PROPERTY(QString commandId MEMBER command_id)
    Q_PROPERTY(bool acceptedForDelivery MEMBER accepted)
    Q_PROPERTY(WalletOperationError error MEMBER error)
public:
    QString command_id;
    bool accepted{false};
    WalletOperationError error;
};
Q_DECLARE_METATYPE(WalletCreationReceipt)

struct WalletOperationResult {
    Q_GADGET
    Q_PROPERTY(QString resultId MEMBER result_id)
    Q_PROPERTY(QString primaryWalletId MEMBER wallet_id)
    Q_PROPERTY(QStringList auxiliaryWalletIds MEMBER auxiliary_wallet_ids)
    Q_PROPERTY(QStringList warnings MEMBER warnings)
    Q_PROPERTY(QStringList recoveryPaths MEMBER recovery_paths)
    Q_PROPERTY(Effect effect MEMBER effect)
    Q_PROPERTY(bool outcomeKnown MEMBER outcome_known)
    Q_PROPERTY(bool warningsTruncated MEMBER warnings_truncated)
public:
    enum Effect { NoEffect, WalletCreated, SetupIncomplete, PublicationUnavailable, PossibleEffect, WalletRestored, WalletMigrated, BackupCopied, WalletOpened };
    Q_ENUM(Effect)
    QString result_id, wallet_id;
    QStringList auxiliary_wallet_ids, warnings, recovery_paths;
    Effect effect{NoEffect};
    bool outcome_known{true}, warnings_truncated{false};
};
Q_DECLARE_METATYPE(WalletOperationResult)

namespace walletcreation {
Q_NAMESPACE
enum class Kind { Software, WatchOnly, ExternalSigner };
Q_ENUM_NS(Kind)
enum class Phase { Requested, Validating, Creating, SettingUp, Publishing, Succeeded, Failed, Partial, Uncertain, Rejected, Cancelled, AwaitingConfirmation, AwaitingPassphrase };
Q_ENUM_NS(Phase)
bool Busy(Phase phase);
/** Translated, non-empty warnings, one per line. */
QString JoinWarnings(const std::vector<bilingual_str>& warnings);
bool Terminal(Phase phase);

// Private transport/admission identity, shared with the legacy adapters while
// load/import/migration are migrated separately. Name reservations never mutate
// the registry's set of currently open wallets.
class Admission final {
public:
    quint64 allocate();
    bool request(quint64 id);
    bool reserveName(quint64 id, const QString& name);
    void releaseName(quint64 id);
    void presentationConsumed(quint64 id);
    bool creationPending() const { return m_presentation.load() != 0; }
    bool legacyBusy() const { return m_legacy.load(); }
    void setLegacyBusy(bool value) { m_legacy.store(value); }
private:
    std::atomic<quint64> m_sequence{0}, m_presentation{0};
    std::atomic_bool m_legacy{false};
    std::mutex m_mutex;
    std::map<quint64, QString> m_names;
};
struct SignerSnapshot {
    bool pending{false}, configured{false};
    int count{0};
    QString name, error, id, configuration_id;
    std::shared_ptr<const std::atomic<quint64>> version;
    quint64 admitted_version{0};
};
struct Request {
    quint64 id{0}, editor_revision{0};
    Kind kind{Kind::Software};
    QString submitted_name, extended_public_key, signer_id;
    bool encrypt{true}, legacy_normalize{false};
    SignerSnapshot signer;
    WalletOperationError transport_error;
    secretinput::Captured passphrase, confirmation;
};
struct Facts {
    QString wallet_directory;
    std::vector<std::pair<QString, QString>> existing;
    bool credentials_match{true}, credentials_present{false};
    bool signer_current{true};
};
struct Decision {
    QString name, wallet_directory, recovery_path;
    uint64_t flags{0};
    WalletOperationError error;
};
struct NativeResult {
    QString name, key_scheme, warnings, error, recovery_path;
    walletopening::NativeEvents observation;
    bool effect_started{false}, created{false}, incomplete{false}, uncertain{false};
};
struct Backend {
    walletopening::Source wallet;
    std::shared_ptr<walletpublication::Release> release;
    std::shared_ptr<walletpublication::ObservedLoad> notification;
    ~Backend();

    interfaces::Wallet* get() const;
    bool canPublish() const { return transferred || notification || get(); }
    walletopening::Source take(std::unique_ptr<interfaces::Handler>& handler);
    void recover(std::shared_ptr<walletpublication::ObservedLoad> source) { notification = std::move(source); transferred = true; }
private:
    bool transferred{false};
};
struct Frame {
    quint64 revision{0};
    Phase phase{Phase::Requested};
    QString target_id, key_scheme, error_text, warning_text;
    QString prompt_id, error_title, error_description, rejected_command;
    WalletOperationError command_error;
    WalletOperationError error;
    WalletOperationResult result;
    bool publication_requested{false}, outcome_known{false};
};
Facts ReadFacts(interfaces::Node& node, const Request& request, const std::shared_ptr<WalletNameRegistry>& open_names);
Decision Validate(const Request& request, const Facts& facts);
NativeResult Create(interfaces::Node& node, const Request& request, const Decision& decision, std::unique_ptr<interfaces::Wallet>& created_wallet);
QString Setup(interfaces::Wallet& wallet, const Request& request);

struct Services {
    interfaces::Node* node{nullptr};
    presentation::Post gui_post;
    std::shared_ptr<Admission> admission;
    std::shared_ptr<WalletNameRegistry> open_names;
    std::shared_ptr<std::atomic_bool> initialized, shutdown;
    std::shared_ptr<execution::SerialQueue> core{}, outcomes{};
    execution::ValueCleanup value_cleanup{};

    secretinput::CaptureResult capture(const QString& text) const
    {
        return secretinput::Capture(value_cleanup, text);
    }
    template <typename T, typename Factory>
    std::shared_ptr<T> retain(Factory factory) const
    {
        if (!value_cleanup) return {};
        return execution::RetainValues<T>(value_cleanup, std::move(factory));
    }

    execution::SubmitResult post(execution::Lane lane, std::shared_ptr<execution::Task> task) const;
    execution::SubmitResult post(execution::Lane lane, std::function<void()> work) const
    {
        return post(lane, execution::MakeTask(std::move(work), [] {}, {}));
    }
    template <typename Value, typename Work>
    execution::SubmitResult start(execution::OwnedCommand<Value>& command, Work work, execution::Lane lane = execution::Lane::Core) const
    {
        return command.startWith([this, lane, work = std::move(work)](auto done, auto failed) mutable {
            return this->post(lane, execution::MakeTask(std::move(work), std::move(done), std::move(failed)));
        });
    }
};
// Common value/effect contract. Creation and restore/migration remain separate
// Native owners; a GUI projection cannot execute or inspect their Core work.
class OperationService {
public:
    virtual ~OperationService() = default;
    virtual void start() = 0;
    virtual void cancel() = 0;
    virtual void detach() = 0;
    virtual Frame snapshot() const = 0;
    virtual QString errorText() const = 0;
    virtual QString warningText() const = 0;
    virtual walletopening::NativeEvents observationForPublication() const = 0;
    virtual walletpublication::TakeWallet walletForPublication() const = 0;
    virtual void publicationConsumed(walletpublication::Availability availability, QString error) = 0;
    virtual void reconcilePublication() = 0;
    virtual bool reconcileNeedsTicket() const { return false; }
    virtual void reconcileWithPorts(std::shared_ptr<walletpublication::Release>, std::shared_ptr<walletpublication::NativeObservation>) { reconcilePublication(); }
    virtual size_t publicationCount() const { return 1; }
    virtual QString publicationName(size_t) const { return snapshot().target_id; }
    virtual bool publicationPrimary(size_t index) const { return index == 0; }
    virtual walletpublication::TakeWallet publicationWallet(size_t) const { return walletForPublication(); }
    virtual walletopening::NativeEvents publicationObservation(size_t) const { return observationForPublication(); }
    virtual std::shared_ptr<walletpublication::Release> publicationRelease(size_t) const { return {}; }
    virtual void publicationsConsumed(std::vector<walletpublication::Availability> values, QString error) { publicationConsumed(values.empty() ? walletpublication::Availability::Unavailable : values.front(), std::move(error)); }
    virtual bool reconcileAvailable() const { const auto events = observationForPublication(); return bool(walletForPublication()) && !(events.unloaded && events.unloaded->load()); }
    virtual void answer(QString, QString, std::shared_ptr<walletpublication::Release>, std::shared_ptr<walletpublication::NativeObservation>, secretinput::Captured) {}
};
// Domain/Core owner; no GUI object, registry, publisher or selection dependency.
class Service final : public OperationService, public std::enable_shared_from_this<Service> {
public:
    using Notify = std::function<void()>;
    static std::shared_ptr<Service> Make(Services services, Request request, Notify notify, std::shared_ptr<walletpublication::Release> release, std::shared_ptr<walletpublication::NativeObservation> observation);
    ~Service() override;
    void start() override;
    void preparePublication(std::shared_ptr<walletpublication::Release> release);
    void publicationConsumed(walletpublication::Availability availability, QString error) override;
    void cancel() override;
    void detach() override;
    Frame snapshot() const override;
    QString errorText() const override;
    QString warningText() const override;
    walletopening::NativeEvents observationForPublication() const override;
    void reconcilePublication() override;
    walletpublication::TakeWallet walletForPublication() const override;
    quint64 id() const { return m_request.id; }
    bool encrypted() const { return m_request.kind == Kind::Software && m_request.encrypt; }
    Kind kind() const { return m_request.kind; }
private:
    Service(Services services, Request request, Notify notify, std::shared_ptr<walletpublication::Release> release, std::shared_ptr<walletpublication::NativeObservation> observation);
    void factsReady();
    void created();
    void setupReady();
    void readyToPublish();
    void complete(Phase phase, WalletOperationError error = {});
    void update(Phase phase);
    void notify();
    const Services m_services;
    const Request m_request;
    const Notify m_notify;
    const std::shared_ptr<walletpublication::Release> m_release;
    const std::shared_ptr<walletpublication::NativeObservation> m_observation;
    mutable std::mutex m_mutex;
    Frame m_frame;
    std::shared_ptr<Service> m_keep_alive;
    std::shared_ptr<execution::OwnedCommand<Facts>> m_facts;
    std::shared_ptr<execution::OwnedCommand<NativeResult>> m_create;
    std::shared_ptr<execution::OwnedCommand<QString>> m_setup;
    std::shared_ptr<Backend> m_backend;
    Decision m_decision;
    NativeResult m_actual;
    std::atomic_bool m_cancelled{false}, m_detached{false};
    bool m_started{false}, m_backend_retained{false}, m_native_created{false}, m_publication_awaited{false}, m_reconciled{false};
};
} // namespace walletcreation
#endif // BITCOIN_QML_WALLETCREATION_H
