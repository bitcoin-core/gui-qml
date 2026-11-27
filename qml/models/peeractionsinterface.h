// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_PEERACTIONSINTERFACE_H
#define BITCOIN_QML_MODELS_PEERACTIONSINTERFACE_H

#include <qml/models/peercommandoperation.h>

#include <QObject>

/** What a PeerCommandController needs from a peer: whether it can be
 * disconnected or banned, and the commands that do. PeerDetailsModel
 * implements it; QML tests use a fake. */
class PeerActionsInterface : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
    virtual bool canDisconnect() const = 0;
    virtual bool canBan() const = 0;
    //! A disconnect waiting for its confirmation, or null. The caller owns it.
    virtual PeerCommandOperation* prepareDisconnect() = 0;
    //! A ban waiting for its confirmation, or null. The caller owns it.
    virtual PeerCommandOperation* prepareBan(PeerCommandOperation::BanDuration duration) = 0;
};

#endif // BITCOIN_QML_MODELS_PEERACTIONSINTERFACE_H
