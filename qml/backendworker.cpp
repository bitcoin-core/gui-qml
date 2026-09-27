// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/backendworker.h>

BackendWorker::BackendWorker(QObject* parent) : QObject(parent), m_context{new QObject}
{
    m_context->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_context, &QObject::deleteLater);
    connect(&m_thread, &QThread::finished, this, &BackendWorker::drained);
    m_thread.start();
}

BackendWorker::~BackendWorker()
{
    // Fallback for early initialization failures and standalone model owners.
    // The application normally calls drain() while its event loop is running.
    m_thread.quit();
    m_thread.wait();
}

void BackendWorker::requireOwnerThread() const
{
    RequireModelThread(this);
}

void BackendWorker::post(std::function<void()> work)
{
    requireOwnerThread();
    if (!m_stopping) QMetaObject::invokeMethod(m_context, std::move(work), Qt::QueuedConnection);
}

void BackendWorker::drain()
{
    requireOwnerThread();
    if (m_stopping) return;
    m_stopping = true;
    // quit() is ordered behind accepted work, including tasks not yet started.
    QMetaObject::invokeMethod(m_context, [this] { m_thread.quit(); }, Qt::QueuedConnection);
}
