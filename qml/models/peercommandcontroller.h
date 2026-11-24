// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_PEERCOMMANDCONTROLLER_H
#define BITCOIN_QML_MODELS_PEERCOMMANDCONTROLLER_H

#include <qml/models/peercommandoperation.h>
#include <qml/models/peeractionsinterface.h>

#include <QObject>
#include <QPointer>
#include <QString>

/** A peers page's disconnect and ban commands. The page has one command at a
 * time: it asks for confirmation, runs, and reports its result. Declared by
 * Peers and PeerDetails. */
class PeerCommandController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(PeerCommandOperation* operation READ operation NOTIFY changed)
    //! The confirmation the command waits for.
    Q_PROPERTY(Prompt prompt READ prompt NOTIFY changed)
    //! A command waits for its confirmation or runs; no other can start.
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString errorText READ errorText NOTIFY changed)
    //! How long the command bans the peer, as the menu names it.
    Q_PROPERTY(QString banDurationText READ banDurationText NOTIFY changed)
    //! A finished command failed and the error was not dismissed.
    Q_PROPERTY(bool errorShown READ errorShown NOTIFY changed)
    //! The page still shows a confirmation, such as during its exit
    //! transition; no command starts until it is gone.
    Q_PROPERTY(bool presenting READ presenting WRITE setPresenting NOTIFY presentingChanged)
public:
    enum class Prompt { None, Disconnect, Ban };
    Q_ENUM(Prompt)

    explicit PeerCommandController(QObject* parent = nullptr);
    ~PeerCommandController() override;

    PeerCommandOperation* operation() const { return m_operation; }
    Prompt prompt() const;
    bool busy() const;
    QString errorText() const;
    QString banDurationText() const;
    bool errorShown() const { return m_error_shown; }
    bool presenting() const { return m_presenting; }
    void setPresenting(bool presenting);

    //! Asks to disconnect \p peer.
    Q_INVOKABLE bool disconnectPeer(PeerActionsInterface* peer);
    //! Asks to ban \p peer for \p duration.
    Q_INVOKABLE bool banPeer(PeerActionsInterface* peer, PeerCommandOperation::BanDuration duration);
    Q_INVOKABLE void confirm();
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void reconcile();
    Q_INVOKABLE void dismissError();

Q_SIGNALS:
    void changed();
    void presentingChanged();

private:
    bool start(PeerCommandOperation* operation, Prompt kind);
    void follow();
    //! Retires \p operation and returns it to QML, which may still hold it.
    void release(PeerCommandOperation* operation);

    QPointer<PeerCommandOperation> m_operation;
    Prompt m_kind{Prompt::None};
    bool m_error_shown{false};
    bool m_presenting{false};
};

#endif // BITCOIN_QML_MODELS_PEERCOMMANDCONTROLLER_H
