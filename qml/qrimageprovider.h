// Copyright (c) 2025-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_QRIMAGEPROVIDER_H
#define BITCOIN_QML_QRIMAGEPROVIDER_H

#include <qml/execution.h>

#include <QImage>
#include <QQuickAsyncImageProvider>
#include <QString>

#include <functional>
#include <memory>

class QRImageProvider : public QQuickAsyncImageProvider
{
public:
    // Pure value rendering on the injected Domain lane. Qt owns each response
    // until its finished signal, including cancelled responses.
    using Render = std::function<QImage(const QString&)>;
    explicit QRImageProvider(std::shared_ptr<execution::Scheduler> scheduler, Render render = RenderImage, QObject* engine_lifetime = nullptr);
    QQuickImageResponse* requestImageResponse(const QString& id, const QSize& requested_size) override;
    static QImage RenderImage(const QString& id);
private:
    const std::shared_ptr<execution::Scheduler> m_scheduler;
    const Render m_render;
    QObject* const m_engine_lifetime;
};

#endif // BITCOIN_QML_QRIMAGEPROVIDER_H
