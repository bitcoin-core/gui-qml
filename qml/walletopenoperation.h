// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_WALLETOPENOPERATION_H
#define BITCOIN_QML_WALLETOPENOPERATION_H

#include <qml/models/walletqmlmodel.h>

#include <QObject>
#include <QPointer>
#include <QString>

#include <functional>

/** One wallet open the wallet list ran, or refused: its target, how it ended,
 * its error and warnings, and the wallet it opened. While it is the list's
 * current open it follows the command; a later open freezes it. */
class WalletOpenOperation : public QObject
{
    Q_OBJECT
public:
    enum class State { WalletOperationIdle, WalletOperationPending, WalletOperationSucceeded, WalletOperationIncomplete, WalletOperationFailed, WalletOperationUncertain, WalletOperationNeedsMigration, WalletOperationNeedsPassphrase, WalletOperationClosed };
    Q_ENUM(State)
    enum class Adoption { AdoptionNone, AdoptionPending, AdoptionAvailable, AdoptionClosed, AdoptionUnavailable };
    Q_ENUM(Adoption)
    struct Snapshot {
        State state{State::WalletOperationIdle};
        QString target, error, warnings, adoption_error;
        QPointer<WalletQmlModel> result;
        bool can_open{false};
        Adoption adoption{Adoption::AdoptionNone};
    };

    //! Reads one field of the command while it is current.
    struct Live {
        std::function<bool()> current;
        std::function<State()> state;
        std::function<QString()> target, error, warnings, adoption_error;
        std::function<WalletQmlModel*()> result;
        std::function<bool()> can_open;
        std::function<Adoption()> adoption;
    };

    WalletOpenOperation(quint64 id, Live live, QObject* parent);
    //! A refused open: it never ran.
    WalletOpenOperation(QString target, QString error, QObject* parent);

    quint64 operationId() const { return m_id; }
    State state() const { return following() ? m_live.state() : m_frozen.state; }
    bool busy() const { return state() == State::WalletOperationPending; }
    QString target() const { return following() ? m_live.target() : m_frozen.target; }
    QString error() const { return following() ? m_live.error() : m_frozen.error; }
    QString warnings() const { return following() ? m_live.warnings() : m_frozen.warnings; }
    WalletQmlModel* resultWallet() const { return following() ? m_live.result() : m_frozen.result.data(); }
    bool canOpenResult() const { return following() ? m_live.can_open() : m_frozen.can_open; }
    Adoption adoptionState() const { return following() ? m_live.adoption() : m_frozen.adoption; }
    QString adoptionError() const { return following() ? m_live.adoption_error() : m_frozen.adoption_error; }

    //! Keeps the current outcome; the operation no longer follows its command.
    void freeze();

Q_SIGNALS:
    void changed();

private:
    bool following() const { return m_following && m_live.current(); }
    const quint64 m_id;
    Live m_live;
    bool m_following{false};
    Snapshot m_frozen;
};

#endif // BITCOIN_QML_WALLETOPENOPERATION_H
