// Copyright (c) 2014-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/initexecutor.h>

#include <interfaces/node.h>
#include <util/exception.h>
#include <util/threadnames.h>

#include <QMetaObject>
#include <QString>

QmlInitExecutor::QmlInitExecutor(interfaces::Node& node)
    : m_node{node}
{
    m_context.moveToThread(&m_thread);
    m_thread.start();
}

QmlInitExecutor::~QmlInitExecutor()
{
    m_thread.quit();
    m_thread.wait();
}

void QmlInitExecutor::handleRunawayException(const std::exception* exception)
{
    PrintExceptionContinue(exception, "Runaway exception");
    Q_EMIT runawayException(exception ? QString::fromUtf8(exception->what()) : tr("Unknown exception"));
}

void QmlInitExecutor::initialize()
{
    QMetaObject::invokeMethod(&m_context, [this] {
        try {
            util::ThreadRename("qml-init");
            interfaces::BlockAndHeaderTipInfo tip_info;
            const bool success{m_node.appInitMain(&tip_info)};
            Q_EMIT initializeResult(success, tip_info);
        } catch (const std::exception& exception) {
            handleRunawayException(&exception);
        } catch (...) {
            handleRunawayException(nullptr);
        }
    });
}

void QmlInitExecutor::shutdown()
{
    QMetaObject::invokeMethod(&m_context, [this] {
        try {
            m_node.appShutdown();
            Q_EMIT shutdownResult();
        } catch (const std::exception& exception) {
            handleRunawayException(&exception);
        } catch (...) {
            handleRunawayException(nullptr);
        }
    });
}
