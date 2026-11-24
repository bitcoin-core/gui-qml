// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_QML_PEERCOMMANDINTERFACE_H
#define BITCOIN_QML_PEERCOMMANDINTERFACE_H
#include <qml/models/nodepeercommandoperation.h>

/** GUI-only bounded intent/publication port. Detail/list readers depend on no
 * concrete NodeModel, command implementation, executor or Core mutation API. */
class PeerCommandInterface {
public:
    virtual ~PeerCommandInterface() = default;
    virtual QObject* observerObject() = 0;
    virtual bool canStart() const = 0;
    virtual PeerCommandOperation* prepare(const PeerConnection& target, peercommands::Kind kind, int64_t duration) = 0;
    virtual QMetaObject::Connection observe(QObject* receiver, std::function<void()> changed) = 0;
};
#endif // BITCOIN_QML_PEERCOMMANDINTERFACE_H
