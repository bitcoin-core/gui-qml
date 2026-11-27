// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_QML_WALLETRESTORATION_H
#define BITCOIN_QML_WALLETRESTORATION_H

#include <qml/walletcreation.h>
#include <QUrl>
#include <array>

namespace walletrestoration {
enum class Kind { Restore, Migrate };
enum class Format { Unknown, SQLite, Berkeley };
enum class Failure { None, Missing, TargetExists, InvalidFormat, Pruned, CredentialsRequired, Credentials, Backend, Settings, Exception, Changed };
struct Request {
    quint64 id{0};
    Kind kind{Kind::Restore};
    QString source;
    bool confirmed{false};
    secretinput::Captured passphrase;
    WalletOperationError transport_error;
};
struct FileStamp {
    QString path;
    qint64 size{0}, modified{0};
    QByteArray header;
    friend bool operator==(const FileStamp&, const FileStamp&) = default;
};
struct Facts {
    QString source_path, target, wallet_directory, target_path;
    Format format{Format::Unknown};
    FileStamp stamp;
    bool encrypted{false}, exists{false}, target_exists{false};
    std::vector<std::pair<QString, QString>> wallets;
    Failure failure{Failure::None};
    QString error;
};
struct NativeResult {
    Failure failure{Failure::None};
    QString error, warnings, primary, key_scheme;
    QStringList auxiliary, recovery;
    bool copied{false}, migrated{false}, effect_started{false}, uncertain{false};
    FileStamp target_stamp;
    size_t count{0}, primary_index{3};
    std::array<QString, 3> names;
    std::array<walletopening::NativeEvents, 3> observations;
};

// Bounded on-disk format hint only. Core still verifies the actual wallet.
FileStamp ReadStamp(const QString& path);
Format Hint(const FileStamp& stamp);
QString RestoreName(const QString& source, Format hint);
Facts Inspect(interfaces::Node& node, const Request& request, const QString& captured_target, bool copied);
walletcreation::Frame FailureFrame(Failure reason, const QString& error, const QString& target, bool effect);

class Service final : public walletcreation::OperationService, public std::enable_shared_from_this<Service> {
public:
    using Notify = std::function<void()>;
    static std::shared_ptr<Service> Make(walletcreation::Services services, Request request, Notify notify,
        std::shared_ptr<walletpublication::Release> release, std::shared_ptr<walletpublication::NativeObservation> observation);
    ~Service() override;
    void start() override;
    void cancel() override;
    void detach() override;
    walletcreation::Frame snapshot() const override;
    QString errorText() const override;
    QString warningText() const override;
    walletopening::NativeEvents observationForPublication() const override;
    walletpublication::TakeWallet walletForPublication() const override;
    void publicationConsumed(walletpublication::Availability availability, QString error) override;
    void publicationsConsumed(std::vector<walletpublication::Availability> values, QString error) override;
    size_t publicationCount() const override;
    QString publicationName(size_t index) const override;
    bool publicationPrimary(size_t index) const override;
    walletpublication::TakeWallet publicationWallet(size_t index) const override;
    std::shared_ptr<walletpublication::Release> publicationRelease(size_t index) const override;
    walletopening::NativeEvents publicationObservation(size_t index) const override;
    bool reconcileAvailable() const override;
    void reconcilePublication() override;
    bool reconcileNeedsTicket() const override;
    void reconcileWithPorts(std::shared_ptr<walletpublication::Release> release, std::shared_ptr<walletpublication::NativeObservation> observation) override;
    void answer(QString command_id, QString expected_prompt, std::shared_ptr<walletpublication::Release> release,
        std::shared_ptr<walletpublication::NativeObservation> observation, secretinput::Captured secret) override;
private:
    Service(walletcreation::Services services, Request request, Notify notify,
        std::shared_ptr<walletpublication::Release> release, std::shared_ptr<walletpublication::NativeObservation> observation, secretinput::Captured secret);
    void inspect();
    void inspected();
    void perform();
    void performed();
    void prompt(walletcreation::Phase phase, QString error = {}, Failure reason = Failure::Credentials);
    void publishReady();
    void complete(walletcreation::Phase phase, Failure reason = Failure::None, QString error = {});
    void update(walletcreation::Phase phase);
    void notify();
    NativeResult effect();
    NativeResult performEffect();
    void loadResult(NativeResult& out, const QString& name, size_t index);
    const walletcreation::Services m_services;
    const Request m_request;
    const Notify m_notify;
    mutable std::mutex m_mutex;
    walletcreation::Frame m_frame;
    NativeResult m_actual;
    std::vector<NativeResult> m_history; // Immutable Native attempt receipts, no backend handles.
    Facts m_facts;
    QString m_target;
    secretinput::Captured m_secret;
    std::shared_ptr<walletpublication::Release> m_release;
    std::shared_ptr<walletpublication::NativeObservation> m_observation;
    std::array<std::shared_ptr<walletcreation::Backend>, 3> m_backends;
    std::shared_ptr<Service> m_keep_alive;
    std::shared_ptr<execution::OwnedCommand<Facts>> m_inspect;
    std::shared_ptr<execution::OwnedCommand<NativeResult>> m_effect;
    std::atomic_bool m_cancelled{false}, m_detached{false};
    quint64 m_prompt_sequence{0};
    bool m_backend_retained{false};
    bool m_reload_only{false};
    bool m_confirmed{false}, m_started{false}, m_copied{false}, m_migrated{false}, m_reconciled{false}, m_awaited{false};
};
} // namespace walletrestoration
#endif // BITCOIN_QML_WALLETRESTORATION_H
