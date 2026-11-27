// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/requestsharing.h>
#include <qml/executor.h>
#include <bitcoin-build-config.h> // IWYU pragma: keep
#ifdef USE_QRCODE
#include <qrencode.h>
#endif

#include <QCoreApplication>
#include <QPainter>

#include <algorithm>
#include <limits>

requestshare::Scope requestshare::WorkScope(const Services& services, bool retain)
{
    const auto lease = retain && services.hold ? services.hold() : nullptr;
    if (retain && !lease) return {};
    if (services.utility) {
        return [queue = services.utility, lease](std::shared_ptr<execution::Task> task, execution::Lane) {
            const bool accepted = queue->post([task = std::move(task), lease] {
                task->execute();
                task->deliver();
            });
            return execution::SubmitResult{{}, accepted ? execution::Rejection::None : execution::Rejection::Transport};
        };
    }
    if (!services.submit) return {};
    return [submit = services.submit, lease](std::shared_ptr<execution::Task> task, execution::Lane lane) {
        return submit(std::move(task), lane);
    };
}

bool requestshare::View::active(uint64_t expected, std::optional<uint64_t> input_revision) const
{
    return interest.load() == Interest::Active && token.load() == expected && (!input || input->load()) &&
        (!input_epoch || (!input_epoch->closed.load() && (!input_revision || input_epoch->current(*input_revision))));
}
bool requestshare::View::setActive(bool enabled)
{
    if (interest.load() == Interest::Closed) return false;
    const auto next = enabled ? Interest::Active : Interest::Inactive;
    if (interest.load() == next) return true;
    const auto previous = token.load();
    if (previous == std::numeric_limits<uint64_t>::max()) { close(); return false; }
    token.store(previous + 1);
    interest.store(next);
    return true;
}
void requestshare::View::close()
{
    interest.store(Interest::Closed);
}
requestshare::Grant::Grant(Source value, std::shared_ptr<View> view, uint64_t token, uint64_t input_revision)
    : source(std::move(value)), m_view(std::move(view)), m_token(token), m_input_revision(input_revision) {}
bool requestshare::Grant::admit()
{
    std::lock_guard lock{m_mutex};
    if (m_state != State::Open) return false;
    bool valid{false};
    try { valid = m_view && m_view->active(m_token, m_input_revision) && source.opening && source.request_id > 0 && source.record_revision > 0 &&
        !source.payload.isEmpty() && source.error == Error::None && source.current && source.current(); } catch (...) { }
    if (!valid) {
        m_state = State::Revoked;
        return false;
    }
    m_state = State::Admitted;
    return true;
}
bool requestshare::Grant::current() const
{
    std::lock_guard lock{m_mutex};
    try { return m_state == State::Open && m_view && m_view->active(m_token, m_input_revision) && source.opening && source.request_id > 0 &&
        source.record_revision > 0 && !source.payload.isEmpty() && source.error == Error::None && source.current && source.current(); }
    catch (...) { return false; }
}
void requestshare::Grant::revoke()
{
    std::lock_guard lock{m_mutex};
    if (m_state == State::Open) m_state = State::Revoked;
}
bool requestshare::Grant::admitted() const
{
    std::lock_guard lock{m_mutex};
    return m_state == State::Admitted;
}
QString requestshare::ErrorText(Error error)
{
    switch (error) {
    case Error::None: return {};
    case Error::Missing: return QCoreApplication::translate("RequestSharingModel", "This saved request no longer exists.");
    case Error::Changed: return QCoreApplication::translate("RequestSharingModel", "This request changed. Review it before sharing.");
    case Error::Used: return QCoreApplication::translate("RequestSharingModel", "This receiving address has already been used.");
    case Error::Closed: return QCoreApplication::translate("RequestSharingModel", "This wallet opening is closed.");
    case Error::Inactive: return QCoreApplication::translate("RequestSharingModel", "Open this saved request before sharing it.");
    case Error::Rejected: return QCoreApplication::translate("RequestSharingModel", "Sharing could not start. Try again.");
    case Error::Oversize: return QCoreApplication::translate("RequestSharingModel", "This payment request is too large to share.");
    case Error::QrEncoding: return QCoreApplication::translate("RequestSharingModel", "This payment request cannot be encoded as a QR image. Shorten its public message and save it again.");
    case Error::Native: return QCoreApplication::translate("RequestSharingModel", "The sharing action failed.");
    case Error::Unknown: return QCoreApplication::translate("RequestSharingModel", "Sharing may not have finished. Check the destination before trying again.");
    case Error::Cancelled: return QCoreApplication::translate("RequestSharingModel", "This sharing action was cancelled.");
    }
    return {};
}
QImage requestshare::RenderQr(const QString& payload)
{
    if (payload.isEmpty() || payload.size() > 16'384 || payload.contains(QChar{0})) return {};
#ifndef USE_QRCODE
    return {};
#else
    const auto bytes = payload.toUtf8();
    const std::unique_ptr<QRcode, decltype(&QRcode_free)> code{
        QRcode_encodeData(static_cast<int>(bytes.size()), reinterpret_cast<const unsigned char*>(bytes.constData()), 0, QR_ECLEVEL_L), QRcode_free};
    if (!code || code->width <= 1 || code->width > 177) return {};
    QImage modules{code->width, code->width, QImage::Format_RGB32};
    for (int y{0}; y < code->width; ++y) for (int x{0}; x < code->width; ++x)
        modules.setPixel(x, y, code->data[y * code->width + x] & 1 ? qRgb(0, 0, 0) : qRgb(255, 255, 255));
    constexpr int QUIET_MODULES{4};
    const int extent = modules.width() + QUIET_MODULES * 2;
    const int scale = std::max(1, 736 / extent);
    QImage image(extent * scale, extent * scale, QImage::Format_RGB32);
    image.fill(Qt::white);
    QPainter painter{&image};
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    painter.drawImage(QRect(QUIET_MODULES * scale, QUIET_MODULES * scale, modules.width() * scale, modules.height() * scale), modules);
    return image;
#endif
}
