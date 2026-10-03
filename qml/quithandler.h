// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_QUITHANDLER_H
#define BITCOIN_QML_QUITHANDLER_H

#include <QCoreApplication>
#include <QEvent>
#include <QObject>

/** Defer native Quit until bootstrap or coordinated shutdown can finish.
 * Install before entering any event loop and keep alive through the final
 * worker drain. Letting Qt accept Quit before QApplication::exec() sets its
 * quit flag, preventing subsequent bootstrap/drain loops from dispatching.
 */
class QmlQuitHandler : public QObject
{
    Q_OBJECT
public:
    QmlQuitHandler() { QCoreApplication::instance()->installEventFilter(this); }
    bool isQuitRequested() const { return m_requested; }

Q_SIGNALS:
    void quitRequested();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (watched != QCoreApplication::instance() || event->type() != QEvent::Quit) {
            return QObject::eventFilter(watched, event);
        }
        event->ignore();
        m_requested = true;
        // A later Quit can acknowledge a fatal exception during shutdown.
        Q_EMIT quitRequested();
        return true;
    }

private:
    bool m_requested{false};
};

#endif // BITCOIN_QML_QUITHANDLER_H
