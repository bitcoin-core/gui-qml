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
    const std::shared_ptr<State> m_state{std::make_shared<State>()};
    const std::shared_ptr<execution::Scheduler> m_scheduler;
    execution::TaskId m_task{};
    bool m_finished{false}, m_engine_retired{false};
public:
    Response(std::shared_ptr<execution::Scheduler> scheduler, QRImageProvider::Render render, QString id, QObject* engine_lifetime)
        : m_scheduler(std::move(scheduler))
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
        if (!m_scheduler || id.size() > MAX_SOURCE_CHARS) {
            complete({}, QStringLiteral("QR request exceeds the rendering limit")); return;
        }
        const auto state = m_state;
        const auto submitted = execution::Submit(*m_scheduler,
            [state, render = std::move(render), id = std::move(id)] {
                if (state->cancelled.load()) return QImage{};
                return render(id);
            },
            [this](QImage image) { complete(std::move(image), {}); },
            [this](std::exception_ptr) { complete({}, QStringLiteral("QR rendering did not complete")); },
            {execution::Lane::Domain, {}, execution::WorkClass::Observation});
        m_task = submitted.id;
        if (!submitted) complete({}, QStringLiteral("QR rendering is unavailable"));
    }
    void cancel() override
    {
        m_state->cancelled.store(true);
        if (m_scheduler && m_task.value) m_scheduler->cancelQueued(m_task);
    }
    QQuickTextureFactory* textureFactory() const override
    {
        std::lock_guard lock{m_state->mutex};
        return m_state->image.isNull() ? nullptr : QQuickTextureFactory::textureFactoryForImage(m_state->image);
    }
    QString errorString() const override
    {
        std::lock_guard lock{m_state->mutex};
        return m_state->error;
    }
private:
    void complete(QImage image, QString error)
    {
        {
            std::lock_guard lock{m_state->mutex};
            if (m_state->cancelled.load()) error = QStringLiteral("QR request cancelled");
            else { m_state->image = std::move(image); }
            m_state->error = std::move(error);
        }
        // Required native response handoff, independent of domain decisions.
        // Never finish inline: Qt must receive the response before completion.
        QMetaObject::invokeMethod(this, [this] {
            m_finished = true;
            if (m_engine_retired) deleteLater();
            Q_EMIT finished();
        }, Qt::QueuedConnection);
    }
};
} // namespace

QRImageProvider::QRImageProvider(std::shared_ptr<execution::Scheduler> scheduler, Render render, QObject* engine_lifetime)
    : m_scheduler(std::move(scheduler)), m_render(std::move(render)), m_engine_lifetime(engine_lifetime)
{
}

QQuickImageResponse* QRImageProvider::requestImageResponse(const QString& id, const QSize& requested_size)
{
    Q_UNUSED(requested_size); // Preserve the QR module grid; QML controls display scaling.
    return new Response{m_scheduler, m_render, id, m_engine_lifetime};
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
