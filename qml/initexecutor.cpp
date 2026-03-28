// Copyright (c) 2014-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/initexecutor.h>

#include <interfaces/node.h>
#include <util/exception.h>
#include <util/threadnames.h>

#include <QDebug>
#include <QMetaObject>
#include <QObject>
#include <QString>
#include <QThread>

QmlInitExecutor::QmlInitExecutor(interfaces::Node& node)
    : QObject(), m_node(node)
{
    m_context.moveToThread(&m_thread);
    m_thread.start();
}

QmlInitExecutor::~QmlInitExecutor()
{
    qDebug() << __func__ << ": Stopping thread";
    m_thread.quit();
    m_thread.wait();
    qDebug() << __func__ << ": Stopped thread";
}

void QmlInitExecutor::handleRunawayException(const std::exception* e)
{
    PrintExceptionContinue(e, "Runaway exception");
    Q_EMIT runawayException(QString::fromStdString(m_node.getWarnings().translated));
}

void QmlInitExecutor::initialize()
{
    // Called only from the GUI thread; m_initialized requires no atomic protection.
    if (m_initialized) return;
    m_initialized = true;
    QMetaObject::invokeMethod(&m_context, [this] {
        try {
            util::ThreadRename("qml-init");
            qDebug() << "Running initialization in thread";
            interfaces::BlockAndHeaderTipInfo tip_info;
            bool rv = m_node.appInitMain(&tip_info);
            Q_EMIT initializeResult(rv, tip_info);
        } catch (const std::exception& e) {
            handleRunawayException(&e);
        } catch (...) {
            handleRunawayException(nullptr);
        }
    });
}

void QmlInitExecutor::shutdown()
{
    QMetaObject::invokeMethod(&m_context, [this] {
        try {
            qDebug() << "Running shutdown in thread";
            m_node.appShutdown();
            qDebug() << "Shutdown finished";
            Q_EMIT shutdownResult();
        } catch (const std::exception& e) {
            handleRunawayException(&e);
        } catch (...) {
            handleRunawayException(nullptr);
        }
    });
}
