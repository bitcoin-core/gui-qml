// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_QML_MODELS_PEERCOMMANDOPERATION_H
#define BITCOIN_QML_MODELS_PEERCOMMANDOPERATION_H

#include <qml/peercommands.h>
#include <QObject>

class PeerCommandResult;
/** One captured connection/parameter prompt, then one original mutation/result.
 * Returned projections are QML-owned; native callers delete their projections.
 * Destruction cannot cancel or erase an admitted Native outcome. */
class PeerCommandOperation final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString operationId READ operationId CONSTANT)
    Q_PROPERTY(QString connectionId READ connectionId CONSTANT)
    Q_PROPERTY(QString nodeIdText READ nodeIdText CONSTANT)
    Q_PROPERTY(QString address READ address CONSTANT)
    Q_PROPERTY(int banDuration READ banDuration CONSTANT)
    Q_PROPERTY(bool confirmationRequired READ confirmationRequired NOTIFY changed)
    Q_PROPERTY(bool pending READ pending NOTIFY changed)
    Q_PROPERTY(bool queryPending READ queryPending NOTIFY changed)
    Q_PROPERTY(bool resultPresent READ resultPresent NOTIFY changed)
    Q_PROPERTY(bool outcomeKnown READ outcomeKnown NOTIFY changed)
    Q_PROPERTY(QString errorText READ errorText NOTIFY changed)
    Q_PROPERTY(QString queryError READ queryError NOTIFY changed)
    Q_PROPERTY(PeerCommandResult result READ result NOTIFY changed)
public:
    enum class Outcome { Succeeded, Rejected, PartiallyApplied, Uncertain, Cancelled };
    Q_ENUM(Outcome)
    enum class BanDuration { Hour = 3600, Day = 86400, Week = 604800, Year = 31536000 };
    Q_ENUM(BanDuration)
    PeerCommandOperation(std::shared_ptr<peercommands::Store> store, peercommands::Request request,
                         execution::SnapshotServices services, std::function<bool()> ready,
                         std::function<void()> admitted, std::function<bool()> interested = {}, QObject* parent = nullptr);
    QString operationId() const { return m_operation->request.id; }
    QString connectionId() const { return m_operation->request.target.key; }
    QString nodeIdText() const { return QString::number(m_operation->request.target.node_id); }
    QString address() const { return m_operation->request.target.display_address; }
    int banDuration() const { return static_cast<int>(m_operation->request.duration); }
    bool confirmationRequired() const { return !m_confirmed && !m_retired; }
    bool pending() const { return m_pending; }
    bool queryPending() const { return m_query_pending; }
    bool resultPresent() const { return m_result.has_value(); }
    bool outcomeKnown() const { return m_result && m_result->outcome_known; }
    QString errorText() const { return m_result ? m_result->error : QString{}; }
    QString queryError() const { return m_result ? m_result->query_error : QString{}; }
    PeerCommandResult result() const;
    Q_INVOKABLE bool confirm();
    Q_INVOKABLE bool cancel();
    Q_INVOKABLE bool reconcile();
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void retire();
Q_SIGNALS:
    void changed();
private:
    bool interested() const { return !m_retired && (!m_interested || m_interested()); }
    std::shared_ptr<peercommands::Store> m_store;
    std::shared_ptr<peercommands::Operation> m_operation;
    const std::function<bool()> m_ready;
    const std::function<void()> m_admitted;
    const std::function<bool()> m_interested;
    std::optional<peercommands::Result> m_result;
    bool m_confirmed{false}, m_retired{false}, m_pending{false}, m_query_pending{false};
};
class PeerCommandResult {
    Q_GADGET
    Q_PROPERTY(QString operationId MEMBER operationId)
    Q_PROPERTY(QString connectionId MEMBER connectionId)
    Q_PROPERTY(QString nodeIdText MEMBER nodeIdText)
    Q_PROPERTY(QString address MEMBER address)
    Q_PROPERTY(QString errorText MEMBER errorText)
    Q_PROPERTY(QString queryError MEMBER queryError)
    Q_PROPERTY(PeerCommandOperation::Outcome outcome MEMBER outcome)
    Q_PROPERTY(bool present MEMBER present)
    Q_PROPERTY(bool outcomeKnown MEMBER outcomeKnown)
    Q_PROPERTY(bool mutationAttempted MEMBER mutationAttempted)
    Q_PROPERTY(bool banApplied MEMBER banApplied)
    Q_PROPERTY(bool disconnectConfirmed MEMBER disconnectConfirmed)
    Q_PROPERTY(bool observationAvailable MEMBER observationAvailable)
    Q_PROPERTY(bool targetConnected MEMBER targetConnected)
    Q_PROPERTY(bool banObservationAvailable MEMBER banObservationAvailable)
    Q_PROPERTY(bool targetBanned MEMBER targetBanned)
public:
    QString operationId, connectionId, nodeIdText, address, errorText, queryError;
    PeerCommandOperation::Outcome outcome{PeerCommandOperation::Outcome::Rejected};
    bool present{false}, outcomeKnown{false}, mutationAttempted{false}, banApplied{false}, disconnectConfirmed{false};
    bool observationAvailable{false}, targetConnected{false}, banObservationAvailable{false}, targetBanned{false};
    static PeerCommandResult Project(const peercommands::Result& value);
};
Q_DECLARE_METATYPE(PeerCommandResult)
#endif // BITCOIN_QML_MODELS_PEERCOMMANDOPERATION_H
