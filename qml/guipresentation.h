// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_GUIPRESENTATION_H
#define BITCOIN_QML_GUIPRESENTATION_H

#include <QObject>
#include <QCoreApplication>
#include <QEvent>
#include <QThread>
#include <QtCore/qlogging.h>

#include <functional>
#include <memory>
#include <utility>
#include <atomic>

namespace presentation {
using Post = std::function<void(std::function<void()>)>;
/** Create and own a parentless GUI object. A final Native reference transfers
 * ownership to a queued GUI retirement, never calls deleteLater on Native.
 * Qt's deleteLater flag shares a bitfield with ordinary GUI notification reads.
 */
inline std::shared_ptr<QObject> MakeGuiObject()
{
    auto* const gui = QThread::currentThread();
    return {new QObject, [gui](QObject* object) {
        if (QThread::currentThread() == gui) { delete object; return; }
        // The last shared owner is exclusive and parentless. The queued event
        // keeps this raw receiver alive until its GUI handoff requests deletion.
        if (!QMetaObject::invokeMethod(object, [object] { object->deleteLater(); }, Qt::QueuedConnection))
            qFatal("GUI object retirement could not be queued");
    }};
}
/** Application/test lifecycle only, after quiet admission and Native producer
 * drain. A drained registry may not enter an event loop, so consume retirement
 * handoffs before flushing their deferred deletes and destroying QApplication.
 * Never use this to wait inside a model getter, command or destructor.
 */
inline void FlushRetirements()
{
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}
/** Create on GUI. Native callbacks own this bridge independently of any facade;
 * only the queued GUI callback may inspect a QPointer to the observing facade.
 * The executor owner drains all accepted producers before application exit. */
inline Post QueuedPost()
{
    auto bridge = MakeGuiObject();
    return [bridge](std::function<void()> callback) {
        QMetaObject::invokeMethod(bridge.get(), [bridge, callback = std::move(callback)] { callback(); }, Qt::QueuedConnection);
    };
}
/** Keep one independent GUI bridge before admission. If an injected primary
 * transport rejects by throwing, deliver the same outcome through the bridge;
 * a primary that enqueues then throws cannot duplicate publication. */
inline Post ReliablePost(Post primary)
{
    const auto fallback=QueuedPost();
    return [primary=std::move(primary),fallback](std::function<void()> callback) {
        const auto once=std::make_shared<std::atomic<bool>>(false);
        const auto retained=std::make_shared<std::function<void()>>(std::move(callback));
        const auto deliver=[once,retained]{if(!once->exchange(true))(*retained)();};
        if(primary){try{primary(deliver);return;}catch(...){}}
        fallback(deliver);
    };
}

} // namespace presentation

#endif // BITCOIN_QML_GUIPRESENTATION_H
