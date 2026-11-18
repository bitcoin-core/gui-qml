// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/imageexportoperation.h>

#include <QDir>
#include <QPointer>
#include <QSaveFile>

namespace imageexport {
Error WritePng(const QImage& image, const QUrl& path)
{
    if (!path.isLocalFile()) return Error::Invalid;
    const auto name = path.toLocalFile();
    if (!QDir::isAbsolutePath(name)) return Error::Invalid;
    QSaveFile file{name};
    if (!file.open(QIODevice::WriteOnly) || !image.save(&file, "PNG") || !file.commit()) return Error::Write;
    return Error::None;
}

Operation::Operation(QImage image, QUrl path, Identity identity, Services services, std::function<void()> notify)
    : m_image(std::move(image)), m_path(std::move(path)), m_identity(std::move(identity)),
      m_services(std::move(services)), m_notify(std::move(notify)) {}

void Operation::start()
{
    if (std::exchange(m_started, true)) return;
    // Capture is bounded on GUI. Encoding, path conversion and filesystem work
    // occur only on Native. A QImage copy retains its immutable pixel storage.
    constexpr qsizetype MAX_BYTES{3 * 1024 * 1024};
    if (m_image.isNull() || m_image.width() > 736 || m_image.height() > 736 ||
        m_image.sizeInBytes() > MAX_BYTES || m_path.toString().size() > 32'768 ||
        m_identity.payload.size() > 65'536 || m_identity.request_id.size() > 64 ||
        !m_identity.opening || m_identity.request_id.isEmpty() || m_identity.payload.isEmpty()) {
        complete(Error::Invalid); return;
    }
    if ((!m_services.scheduler && !m_services.submit) || !m_services.closed) { complete(Error::Rejected); return; }
    auto self = shared_from_this();
    const std::function<Error()> work = [self] {
        if (self->m_services.closed->load()) return Error::Closed;
        auto expected = Admission::Open;
        if (!self->m_admission.compare_exchange_strong(expected, Admission::Writing)) return Error::Retired;
        return self->m_services.write ? self->m_services.write(self->m_image, self->m_path) : WritePng(self->m_image, self->m_path);
    };
    const std::function<void(Error)> done = [self](Error error) { self->complete(error); };
    const std::function<void(std::exception_ptr)> failed = [self](std::exception_ptr error) {
        try { std::rethrow_exception(error); }
        catch (const execution::NotStarted&) { self->complete(Error::Rejected); }
        catch (...) { self->complete(Error::Write); }
    };
    const auto admitted = m_services.submit ? m_services.submit(work, done, failed)
        : execution::Submit(*m_services.scheduler, work, done, failed,
            {execution::Lane::Core, m_services.scheduler->allocateKey(), execution::WorkClass::Action});
    if (!admitted) complete(admitted.rejection == execution::Rejection::Closed ? Error::Closed : Error::Rejected);
}

void Operation::retire()
{
    auto expected = Admission::Open;
    m_admission.compare_exchange_strong(expected, Admission::Retired);
}

void Operation::complete(Error error)
{
    const Result result{m_identity, m_path, error, error == Error::None};
    {
        std::lock_guard lock{m_mutex};
        if (m_result) return;
        m_result = result;
    }
    // Queue ownership, not a view or GUI acknowledgement, retains the pixels
    // until Native and its Domain outcome have both completed.
    m_image = {};
    try { if (m_services.outcome) m_services.outcome(result); } catch (...) { }
    try { if (m_notify) m_notify(); } catch (...) { }
}

std::optional<Result> Operation::result() const
{
    std::lock_guard lock{m_mutex};
    return m_result;
}
} // namespace imageexport

ImageExportOperation::ImageExportOperation(QImage image, QUrl path, imageexport::Identity identity, imageexport::Services services,
                                         std::function<void(std::function<void()>)> post, QObject* parent)
    : QObject(parent), m_path(std::move(path))
{
    const QPointer<ImageExportOperation> observer{this};
    m_operation = std::make_shared<imageexport::Operation>(std::move(image), m_path, std::move(identity), std::move(services),
        [post = std::move(post), observer] { post([observer] { if (observer) observer->consume(); }); });
}

ImageExportOperation::~ImageExportOperation() { retire(); }
void ImageExportOperation::start() { m_operation->start(); }
void ImageExportOperation::consume()
{
    if (m_result) return;
    m_result = m_operation->result();
    if (m_result) Q_EMIT changed();
}

QString ImageExportOperation::errorText() const
{
    if (!m_result) return {};
    switch (m_result->error) {
    case imageexport::Error::None: return {};
    case imageexport::Error::Closed: return tr("This wallet opening has closed.");
    case imageexport::Error::Retired: return tr("The payment request changed before the QR code was saved.");
    case imageexport::Error::Rejected: return tr("The action could not start. Try again.");
    case imageexport::Error::Invalid:
    case imageexport::Error::Write: return tr("The QR code could not be saved. Please try again.");
    }
    return {};
}
