// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_QML_MODELS_PEERCOMMANDOPERATION_H
#define BITCOIN_QML_MODELS_PEERCOMMANDOPERATION_H

#include <QMetaType>
#include <QObject>
#include <QString>

class PeerCommandResult;
/** One prepared peer command: a confirmation, then one mutation and its
 * result. Once finished it no longer changes. NodePeerCommandOperation runs
 * it against the node; QML tests use a fake. */
class PeerCommandOperation : public QObject {
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
    virtual QString operationId() const = 0;
    virtual QString connectionId() const = 0;
    virtual QString nodeIdText() const = 0;
    virtual QString address() const = 0;
    //! Seconds of a ban; 0 for a disconnect.
    virtual int banDuration() const = 0;
    virtual bool confirmationRequired() const = 0;
    virtual bool pending() const = 0;
    virtual bool queryPending() const = 0;
    virtual bool resultPresent() const = 0;
    virtual bool outcomeKnown() const = 0;
    virtual QString errorText() const = 0;
    virtual QString queryError() const = 0;
    virtual PeerCommandResult result() const = 0;
    //! Runs the confirmed command.
    Q_INVOKABLE virtual bool confirm() = 0;
    //! Drops a command that still waits for its confirmation.
    Q_INVOKABLE virtual bool cancel() = 0;
    //! Checks the node again after an uncertain outcome.
    Q_INVOKABLE virtual bool reconcile() = 0;
    Q_INVOKABLE virtual void refresh() = 0;
    //! Ends this projection's interest; an unconfirmed command is cancelled.
    Q_INVOKABLE virtual void retire() = 0;
Q_SIGNALS:
    void changed();
protected:
    explicit PeerCommandOperation(QObject* parent) : QObject(parent) {}
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
};
Q_DECLARE_METATYPE(PeerCommandResult)
#endif // BITCOIN_QML_MODELS_PEERCOMMANDOPERATION_H
