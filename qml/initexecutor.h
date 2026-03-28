// Copyright (c) 2014-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_INITEXECUTOR_H
#define BITCOIN_QML_INITEXECUTOR_H

#include <interfaces/node.h>

#include <exception>

#include <QObject>
#include <QThread>

QT_BEGIN_NAMESPACE
class QString;
QT_END_NAMESPACE

/** Runs app initialization and shutdown work off the GUI thread. */
class QmlInitExecutor : public QObject
{
    Q_OBJECT
public:
    explicit QmlInitExecutor(interfaces::Node& node);
    ~QmlInitExecutor();

public Q_SLOTS:
    void initialize();
    void shutdown();

Q_SIGNALS:
    void initializeResult(bool success, interfaces::BlockAndHeaderTipInfo tip_info);
    void shutdownResult();
    void runawayException(const QString& message);

private:
    void handleRunawayException(const std::exception* e);

    interfaces::Node& m_node;
    QObject m_context;
    QThread m_thread;
    bool m_initialized{false};
};

#endif // BITCOIN_QML_INITEXECUTOR_H
