// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_PSBTOPERATION_H
#define BITCOIN_QML_MODELS_PSBTOPERATION_H

#include <QObject>
#include <QString>

/** One PSBT action on a wallet: saving the reviewed transaction (or an
 * unverified signer result) as a file, broadcasting it, checking it again, or
 * importing a PSBT file. A wallet runs one action at a time; the controller
 * that asked for this one owns it. Once finished it no longer changes. */
class PsbtOperation : public QObject
{
    Q_OBJECT
    Q_PROPERTY(PsbtOperation::Kind kind READ kind CONSTANT)
    Q_PROPERTY(PsbtOperation::State state READ state NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QString savedPath READ savedPath NOTIFY changed)
    Q_PROPERTY(QString txid READ txid NOTIFY changed)
    //! For an import: WalletQmlModel::PsbtImportResult.
    Q_PROPERTY(int importResult READ importResult NOTIFY changed)

public:
    enum class Kind { Save, Broadcast, Recheck, Import };
    Q_ENUM(Kind)
    //! None is never an operation's state: a controller reports it when it
    //! has no operation. Imported and Known are import and recheck outcomes.
    enum class State { None, Pending, Saved, Submitted, Imported, Known, Failed, Stale, Uncertain, Cancelled };
    Q_ENUM(State)

    Kind kind() const { return m_kind; }
    State state() const { return m_state; }
    bool pending() const { return m_state == State::Pending; }
    bool finished() const { return m_state != State::None && m_state != State::Pending; }
    QString error() const { return m_error; }
    QString savedPath() const { return m_saved_path; }
    QString txid() const { return m_txid; }
    int importResult() const { return m_import_result; }

    //! Stops following the action. Work that already started finishes.
    virtual void cancel() = 0;

Q_SIGNALS:
    void changed();

protected:
    PsbtOperation(Kind kind, QObject* parent) : QObject(parent), m_kind{kind} {}
    //! Publishes where the action is; a final state is kept.
    void publish(State state, QString error = {}, QString saved_path = {}, QString txid = {}, int import_result = 0)
    {
        if (finished() || state == State::None) return;
        m_import_result = import_result;
        m_state = state;
        m_error = std::move(error);
        m_saved_path = std::move(saved_path);
        m_txid = std::move(txid);
        Q_EMIT changed();
    }

private:
    const Kind m_kind;
    State m_state{State::Pending};
    QString m_error, m_saved_path, m_txid;
    int m_import_result{0};
};

#endif // BITCOIN_QML_MODELS_PSBTOPERATION_H
