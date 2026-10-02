// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_COMPONENTS_WIDGETS_ASYNCSETTINGSWRITER_H
#define BITCOIN_QML_COMPONENTS_WIDGETS_ASYNCSETTINGSWRITER_H

#include <QCoreApplication>
#include <QMap>
#include <QMutex>
#include <QMutexLocker>
#include <QObject>
#include <QSettings>
#include <QThread>

#include <functional>
#include <memory>

/** Coalesces per-key immutable snapshots and performs serialization and disk I/O
 * on a worker. Destruction flushes remaining snapshots before stopping it. */
class AsyncSettingsWriter : public QObject
{
public:
    using SerializeFn = std::function<QByteArray()>;
    using ResultFn = std::function<void(bool)>;
    explicit AsyncSettingsWriter(const QString& file = {}) : m_file(file), m_worker(new QObject)
    {
        m_worker->moveToThread(&m_thread);
        connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
        m_thread.setObjectName("qml-settings");
        m_thread.start();
    }
    ~AsyncSettingsWriter() override
    {
        // Only final destruction waits; interactive saves never wait for disk.
        QMetaObject::invokeMethod(m_worker, [] {}, Qt::BlockingQueuedConnection);
        m_thread.quit();
        m_thread.wait();
    }
    bool pending() const
    {
        QMutexLocker lock(&m_mutex);
        return m_scheduled;
    }
    void save(const QString& key, SerializeFn serialize, ResultFn result = {})
    {
        QMutexLocker lock(&m_mutex);
        // Capture the network-specific application name before crossing threads.
        QString organization = QCoreApplication::organizationName();
#ifdef Q_OS_DARWIN
        // The default QSettings constructor prefers the domain on macOS,
        // including when tests redirect IniFormat to a temporary directory.
        if (!QCoreApplication::organizationDomain().isEmpty()) organization = QCoreApplication::organizationDomain();
#endif
        m_tasks.insert(key, Task{std::move(serialize), std::move(result),
                                organization, QCoreApplication::applicationName(), QSettings::defaultFormat()});
        if (m_scheduled) return;
        m_scheduled = true;
        QMetaObject::invokeMethod(m_worker, [this] {
            for (;;) {
                QMap<QString, Task> tasks;
                {
                    QMutexLocker lock(&m_mutex);
                    if (m_tasks.isEmpty()) { m_scheduled = false; return; }
                    tasks.swap(m_tasks);
                }
                for (auto it = tasks.begin(); it != tasks.end(); ++it) {
                    const auto& task = it.value();
                    const auto settings = m_file.isEmpty()
                        ? std::make_unique<QSettings>(task.format, QSettings::UserScope, task.organization, task.application)
                        : std::make_unique<QSettings>(m_file, QSettings::IniFormat);
                    settings->setValue(it.key(), task.serialize());
                    settings->sync();
                    if (task.result) {
                        QMetaObject::invokeMethod(this, [result = task.result, success = settings->status() == QSettings::NoError] {
                            result(success);
                        }, Qt::QueuedConnection);
                    }
                }
            }
        }, Qt::QueuedConnection);
    }
private:
    struct Task { SerializeFn serialize; ResultFn result; QString organization; QString application; QSettings::Format format; };
    const QString m_file;
    QThread m_thread;
    QObject* m_worker;
    mutable QMutex m_mutex;
    QMap<QString, Task> m_tasks;
    bool m_scheduled{false};
};

#endif // BITCOIN_QML_COMPONENTS_WIDGETS_ASYNCSETTINGSWRITER_H
