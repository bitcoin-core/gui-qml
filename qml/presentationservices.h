// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_PRESENTATIONSERVICES_H
#define BITCOIN_QML_PRESENTATIONSERVICES_H

#include <qml/execution.h>

#include <QList>
#include <QObject>

#include <cstddef>
#include <functional>
#include <memory>

/** Background services for presentation objects that QML creates, such as the
 * block clock dial and monospace output. The application owns one instance and
 * QML binds it as ApplicationModels.presentationServices.
 *
 * close() ends the users' background work at shutdown: each user closes itself
 * when closed() is emitted, or immediately if it receives a closed instance.
 * Values released on another thread are tracked here until they are gone, so
 * shutdown can wait for them with whenRetired().
 */
class PresentationServices : public QObject
{
    Q_OBJECT

public:
    struct Ports {
        std::shared_ptr<execution::Scheduler> scheduler;
        // Runs a turn on the GUI thread.
        std::function<void(std::function<void()>)> post;
        // Enqueues value cleanup on another thread. False means not accepted.
        std::function<bool(std::function<void()>)> retire;
        // Optional manual turn for retrying retirement (tests).
        std::function<void(std::function<void()>)> retry;
    };
    /** Values waiting to be released on another thread. */
    class Retirement
    {
    public:
        virtual ~Retirement() = default;
        /** Ask again for cleanup of retained values. False if not accepted. */
        virtual bool retry() = 0;
        virtual size_t pending() = 0;
    };

    explicit PresentationServices(Ports ports, QObject* parent = nullptr);

    const Ports& ports() const { return m_ports; }
    bool isClosed() const { return m_closed; }
    /** End all users' background work. Emits closed() once. */
    void close();

    /** Keep a retirement until it has no pending values and no other owner. */
    void track(std::shared_ptr<Retirement> retirement);
    bool retryRetirements();
    size_t pendingRetirements();
    /** Call done on receiver's thread once every tracked value is released. */
    void whenRetired(QObject* receiver, std::function<void()> done);

Q_SIGNALS:
    void closed();

private:
    Ports m_ports;
    bool m_closed{false};
    QList<std::shared_ptr<Retirement>> m_retirements;
};

#endif // BITCOIN_QML_PRESENTATIONSERVICES_H
