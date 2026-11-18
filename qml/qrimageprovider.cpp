// Copyright (c) 2025 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/qrimageprovider.h>

#include <bitcoin-build-config.h> // IWYU pragma: keep

#ifdef USE_QRCODE
#include <qrencode.h>
#endif

#include <QCoreApplication>
#include <QImage>
#include <QMetaObject>
#include <QQuickImageResponse>
#include <QQuickTextureFactory>
#include <QString>
#include <QUrl>
#include <QUrlQuery>

#include <atomic>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace {
constexpr qsizetype MAX_SOURCE_CHARS{32 * 1024};
class Response final : public QQuickImageResponse
{
    struct State {
        std::atomic_bool cancelled{false};
        std::mutex mutex;
        QImage image;
        QString error;
    };
    mutable State m_state;
    const std::shared_ptr<execution::SerialQueue> m_queue;
    bool m_finished{false}, m_engine_retired{false};
public:
    Response(std::shared_ptr<execution::SerialQueue> queue, QRImageProvider::Render render, QString id, QObject* engine_lifetime)
        : m_queue(std::move(queue))
    {
        // Qt's per-engine image thread can stop before an outstanding response
        // completes. Use the application's presentation loop for this bounded
        // native handoff, and explicitly release orphaned responses only after
        // the engine has destroyed its reader and all rendering has finished.
        moveToThread(QCoreApplication::instance()->thread());
        if (engine_lifetime) QObject::connect(engine_lifetime, &QObject::destroyed, this, [this] {
            m_engine_retired = true;
            if (m_finished) deleteLater();
        });
        if (id.size() > MAX_SOURCE_CHARS) {
            complete({}, QStringLiteral("QR request exceeds the rendering limit")); return;
        }
        const bool accepted = m_queue && m_queue->post([this, render = std::move(render), id = std::move(id)] {
            try {
                complete(m_state.cancelled.load() ? QImage{} : render(id), {});
            } catch (...) {
                complete({}, QStringLiteral("QR rendering did not complete"));
            }
        });
        if (!accepted) complete({}, QStringLiteral("QR rendering is unavailable"));
    }
    void cancel() override { m_state.cancelled.store(true); }
    QQuickTextureFactory* textureFactory() const override
    {
        std::lock_guard lock{m_state.mutex};
        return m_state.image.isNull() ? nullptr : QQuickTextureFactory::textureFactoryForImage(m_state.image);
    }
    QString errorString() const override
    {
        std::lock_guard lock{m_state.mutex};
        return m_state.error;
    }
private:
    void complete(QImage image, QString error)
    {
        // Keep the image private until Qt receives the completion. Even a
        // rejected request must return its response before emitting finished.
        QMetaObject::invokeMethod(this, [this, image = std::move(image), error = std::move(error)]() mutable {
            {
                std::lock_guard lock{m_state.mutex};
                if (m_state.cancelled.load()) error = QStringLiteral("QR request cancelled");
                else m_state.image = std::move(image);
                m_state.error = std::move(error);
            }
            m_finished = true;
            if (m_engine_retired) deleteLater();
            Q_EMIT finished();
        }, Qt::QueuedConnection);
    }
};
} // namespace

QRImageProvider::QRImageProvider(std::shared_ptr<execution::SerialQueue> queue, Render render, QObject* engine_lifetime)
    : m_queue(std::move(queue)), m_render(std::move(render)), m_engine_lifetime(engine_lifetime)
{
}

QQuickImageResponse* QRImageProvider::requestImageResponse(const QString& id, const QSize& requested_size)
{
    Q_UNUSED(requested_size); // Preserve the QR module grid; QML controls display scaling.
    return new Response{m_queue, m_render, id, m_engine_lifetime};
}

QImage QRImageProvider::RenderImage(const QString& id)
{
    if (id.size() > MAX_SOURCE_CHARS) throw std::length_error("QR source too large");
#ifdef USE_QRCODE
    const QUrl url{"image:///" + id};
    const QUrlQuery query{url};
    const QString data{url.path().mid(1)};
    const QColor fg{query.queryItemValue("fg")};
    const QColor bg{query.queryItemValue("bg")};

    QRcode* code = QRcode_encodeString(data.toUtf8().constData(), 0, QR_ECLEVEL_L, QR_MODE_8, 1);

    if (code) {
        QImage image{code->width, code->width, QImage::Format_ARGB32};
        unsigned char* p = code->data;
        for (int y = 0; y < code->width; ++y) {
            for (int x = 0; x < code->width; ++x) {
                image.setPixelColor(x, y, (*p & 1) ? fg : bg);
                ++p;
            }
        }
        QRcode_free(code);
        return image;
    }
#endif // USE_QRCODE
    QImage pixel{1, 1, QImage::Format_ARGB32};
    pixel.setPixelColor(0, 0, QColorConstants::Transparent);
    return pixel;
}
