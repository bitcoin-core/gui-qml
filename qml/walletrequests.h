// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_WALLETREQUESTS_H
#define BITCOIN_QML_WALLETREQUESTS_H

#include <qml/walletopening.h>
#include <qml/walletrequestdata.h>
#include <qml/walletrequesterror.h>
#include <qml/walletreservationdata.h>
#include <qml/observationretirement.h>
#include <qml/secretinput.h>
#include <support/allocators/secure.h>

#include <QMap>

#include <deque>
#include <set>

namespace walletreceipts { struct Snapshot; }

namespace walletrequests {
enum class Kind { Reload, Create, Edit, Erase, Label, ReserveAddress, ResumeAddress, SelectAddress, ForgetAddress, SentLabels };

struct Command {
    Kind kind{Kind::Reload};
    int64_t id{0};
    uint64_t revision{0};
    Draft draft;
    QString address, label;
    // App editor precondition, checked again through the public Wallet API
    // immediately before its write. This is not atomic with external RPC.
    std::optional<QString> expected_label;
    bool preserve_current_note{false};
    Txid sent_transaction;
    QMap<QString, QString> sent_labels;
    bool require_unused{false};
    bool consume_address{false};
    uint64_t view{1}, view_revision{0};
    bool next{false}, explicit_type{false}, clear_reservation{false}, remember_type{false};
    std::shared_ptr<const AddressIntent> resume, reservation;
    std::shared_ptr<SecureString> secret;
    secretinput::Captured captured_secret;
    secretinput::Error secret_capture_error{secretinput::Error::None};
};
struct Result {
    Error error{Error::None};
    int64_t id{0};
    bool request_saved{false}, request_loaded{false}, erased{false}, label_saved{false};
    std::optional<QString> current_label;
    bool reservation_released{false};
    bool publication_failed{false};
    bool preference_requested{false}, preference_saved{false};
    size_t records_saved{0}, records_total{0};
    std::shared_ptr<const Record> record;
    std::optional<ReservedAddress> address;
    QString paid_address;
    Txid sent_transaction;
    size_t notes_total{0}, notes_saved{0}, notes_unchanged{0}, notes_failed{0};
};
/** Retained terminal outcome of one original-opening command. GUI lifetime is
 * not required for consuming a Native result or updating authoritative state. */
class Operation
{
public:
    Operation(walletopening::Opening opening, uint64_t token, Kind kind, std::function<void()> notify,
              std::shared_ptr<const AddressIntent> intent = {}, std::function<void(const Result&)> result_notify = {})
        : opening(std::move(opening)), token(token), kind(kind), address_intent(std::move(intent)), m_notify(std::move(notify)), m_result_notify(std::move(result_notify)) {}
    const walletopening::Opening opening;
    const uint64_t token;
    const Kind kind;
    const std::shared_ptr<const AddressIntent> address_intent;
    std::optional<Result> result() const;
private:
    friend class Store;
    void complete(Result result);
    const std::function<void()> m_notify;
    const std::function<void(const Result&)> m_result_notify;
    mutable std::mutex m_mutex;
    std::optional<Result> m_result;
};
struct Admission {
    std::shared_ptr<Operation> operation;
    execution::Rejection rejection{execution::Rejection::None};
    explicit operator bool() const { return operation && rejection == execution::Rejection::None; }
};
struct Publication {
    std::shared_ptr<const Snapshot> snapshot;
    Error error{Error::None};
    bool loaded{false}, busy{false};
};
struct Inspection {
    Error error{Error::None};
    SharedRecord record;
    std::shared_ptr<const AddressIntent> reservation;
    std::optional<ReservedAddress> address;
    std::optional<QString> current_label;
    bool loaded{false}, current{false}, closed{false}, used{false};
    uint64_t revision{0}, receipt_epoch{0}, native_revision{0};
    std::function<bool()> permission;
};

/** The sole local writer of receive-request records for an opening. Commands
 * are reduced on Domain and public Wallet effects run on the opening's Core
 * key. A later command is never derived before the previous result is consumed.
 * This serializes app writers; it does not implement Core/RPC conditional writes.
 */
class Store : public std::enable_shared_from_this<Store>
{
public:
    using Notify = std::function<void()>;
    using Now = std::function<QDateTime()>;
    using Decoder = std::function<std::optional<QmlRecentRequestEntry>(std::string_view)>;
    using Prepare = std::function<SharedRecord(Record)>; // Pure immutable row preparation on Domain
    using Encoder = std::function<std::string(const QmlRecentRequestEntry&)>; // Domain value effect, injectable for work accounting
    using Watch = std::function<void(QSet<QString>)>; // Domain -> retained receipt owner
    using Ready = std::function<bool()>; // Bounded cached readiness, also safe for presentation queries
    using Release = std::function<bool(interfaces::Wallet&, const QString&)>; // Native, after confirmed create
    Store(std::shared_ptr<walletopening::Session> opening, Notify notify, Watch watch = {},
          Now now = QDateTime::currentDateTimeUtc, Ready ready = [] { return true; }, Release release = {},
          ReservationStorage storage = {}, Encoder encoder = Encode, Decoder decoder = DecodeOne, Prepare prepare = FreezeRecord);
    ~Store();
    Admission submit(Command command, Notify complete = {});
    Admission submitResult(Command command, std::function<void(const Result&)> complete, std::shared_ptr<const walletopening::Session::Action> action = {});
    // An already-admitted original-opening capability, captured before the
    // transaction commit, lets its metadata drain even if that opening closes.
    Admission submitSentLabels(std::shared_ptr<const walletopening::Session::Action> action, const Txid& transaction,
                               QMap<QString, QString> labels, Notify complete = {});
    // Complete, monotonic Reader snapshots for this opening, delivered on
    // Domain independently of GUI. Pending inputs coalesce; facts never regress.
    void observed(const std::shared_ptr<const walletreceipts::Snapshot>& snapshot);
    execution::SubmitResult interest(uint64_t view, QString address);
    void close();
    std::optional<Publication> take();
    // Domain-only inspection of the same authoritative writer state. No GUI
    // reader consumes another view's publication or scans Core collections.
    execution::SubmitResult inspect(int64_t id, uint64_t view, std::function<void(Inspection)> done);
    uint64_t revision() const { return m_revision.load(); }
    bool presentationCurrent() const {
        return !m_closed.load() && !m_mutating.load() && !m_uncertain.load() && m_receipts_ready.load() && m_ready() && !m_observation_pending.load() && m_snapshot_ready.load() &&
            m_published_revision.load() == m_revision.load() && m_checked_revision.load() == m_opening->transactionRevision();
    }
    void retire(std::shared_ptr<const Snapshot> old_snapshot);
private:
    struct Queued {
        Command command;
        std::shared_ptr<Operation> operation;
        std::shared_ptr<const walletopening::Session::Action> action;
        std::optional<ReservedAddress> prepared_address{};
    };
    struct Effect;
    struct AddressWork;
    struct Loading;
    struct Observation;
    struct SentLabels;
    Admission submitImpl(Command command, Notify complete, std::shared_ptr<const walletopening::Session::Action> action, std::function<void(const Result&)> result_notify = {});
    void advanceSentLabels(const std::shared_ptr<SentLabels>& work);
    void scheduleSentLabels(const std::shared_ptr<SentLabels>& work);
    execution::SubmitResult schedule(const std::shared_ptr<const walletopening::Session::Action>& action);
    void plan();
    void planMutation(Queued queued);
    void reserve(Queued queued);
    void scanAddress(const std::shared_ptr<AddressWork>& work);
    void allocateAddress(const std::shared_ptr<AddressWork>& work, bool reuse);
    void addressReady(const std::shared_ptr<AddressWork>& work, ReservedAddress address, Error error);
    bool currentAddressIntent(const std::shared_ptr<const AddressIntent>& intent);
    static void clearSecret(const std::shared_ptr<SecureString>& secret);
    void loaded(std::vector<std::string> blobs, std::vector<interfaces::WalletAddress> addresses,
                const std::shared_ptr<Operation>& operation,
                const std::shared_ptr<const walletopening::Session::Action>& action);
    void advanceLoad();
    void stopLoad(Error error);
    void execute(std::shared_ptr<Effect> effect);
    void advanceLabel(const std::shared_ptr<Effect>& effect);
    void scheduleLabel(const std::shared_ptr<Effect>& effect);
    void finish(const std::shared_ptr<Operation>& operation, Result result);
    void fail(const std::shared_ptr<Operation>& operation, Error error);
    void publish(Error error = Error::None);
    void publishMetadata(Error error);
    void afterPreparation(std::function<void()> work);
    void advancePreparation();
    void stopPreparation(Error error);
    void finishPrepared(const std::shared_ptr<Operation>& operation, Result result);
    void updateWatch();
    void advanceObservation();
    void stopObservation(Error error);
    void flushPaid();
    void noteNativePayment(const QString& address);

    const std::shared_ptr<walletopening::Session> m_opening;
    const Notify m_notify;
    const Watch m_watch;
    const Now m_now;
    const Encoder m_encode;
    const Decoder m_decode;
    const Prepare m_prepare_record;
    const Ready m_ready;
    const Release m_release;
    const ReservationStorage m_storage;
    const execution::ObservationRetirement<Snapshot> m_retirement;
    const execution::ObservationRetirement<SnapshotPreparation> m_preparation_retirement;
    const execution::ObservationRetirement<Loading> m_loading_retirement;
    const execution::ObservationRetirement<walletreceipts::Snapshot> m_observation_retirement;
    std::atomic_bool m_closed{false};
    const std::shared_ptr<ChangeCounter> m_source_epoch{std::make_shared<ChangeCounter>()};
    std::atomic<uint64_t> m_checked_revision{0};
    std::atomic<uint64_t> m_receipt_epoch{0};
    std::mutex m_mutex; // bounded ingress/publication only; never held across effects
    std::deque<Queued> m_queue;
    uint64_t m_next_operation{1};
    bool m_scheduled{false};
    std::map<uint64_t, uint64_t> m_latest_address_intent;
    std::optional<Publication> m_latest;
    bool m_notice_pending{false};

    // Domain-only authority below.
    bool m_active{false}, m_loaded{false}, m_previous_paid_batch{false};
    std::atomic<uint64_t> m_revision{0}, m_published_revision{0};
    std::atomic_bool m_snapshot_ready{false}, m_mutating{false}, m_receipts_ready{false}, m_observation_pending{false}, m_uncertain{false};
    std::shared_ptr<SnapshotPreparation> m_preparing;
    std::shared_ptr<Loading> m_loading;
    std::shared_ptr<Operation> m_loading_operation;
    std::shared_ptr<const walletopening::Session::Action> m_active_action, m_prepare_action;
    std::deque<std::function<void()>> m_after_preparation;
    std::optional<uint64_t> m_preparation_failed_revision;
    int64_t m_highest_id{0};
    size_t m_malformed{0};
    SharedRecords m_records;
    std::map<QString, std::set<int64_t>> m_records_by_address;
    std::map<QString, QString> m_labels;
    // A confirmed single-use create stays consumed even after its row is
    // deleted or the GUI that requested it disappears. Explicit sibling
    // requests do not consume a receiving reservation.
    std::map<QString, int64_t> m_consumed_addresses;
    struct Reservation { std::shared_ptr<const AddressIntent> intent; ReservedAddress address; };
    std::map<uint64_t, Reservation> m_reservations;
    std::map<uint64_t, std::shared_ptr<const AddressIntent>> m_address_questions;
    std::set<uint64_t> m_fresh_address_views;
    std::set<QString> m_observed, m_native_paid_repairs;
    std::unique_ptr<Observation> m_observing;
    std::shared_ptr<const walletreceipts::Snapshot> m_pending_observation;
    std::shared_ptr<const walletopening::Session::Action> m_observation_action;
    std::optional<QString> m_incomplete_paid_address;
    size_t m_accepted_observed_size{0}, m_reconciled_observed_size{0};
    uint64_t m_observation_native_revision{0};
    Error m_observation_error{Error::None};
    std::set<int64_t> m_paid_dirty;
    std::shared_ptr<const walletopening::Session::Action> m_paid_action;
    Error m_persistence_error{Error::None}, m_preparation_error{Error::None};
    std::shared_ptr<const Snapshot> m_current;
    QSet<QString> m_watched;
    std::map<uint64_t, QString> m_interests;
    std::weak_ptr<const Snapshot> m_watch_snapshot;
    bool m_watch_started{false}, m_interests_dirty{true}, m_reload_needs_receipts{false};
};
} // namespace walletrequests

#endif // BITCOIN_QML_WALLETREQUESTS_H
