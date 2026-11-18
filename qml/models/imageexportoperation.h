// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_IMAGEEXPORTOPERATION_H
#define BITCOIN_QML_MODELS_IMAGEEXPORTOPERATION_H

#include <qml/execution.h>

#include <QImage>
#include <QObject>
#include <QUrl>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>

namespace imageexport {
enum class Error { None, Invalid, Closed, Retired, Rejected, Write };
struct Identity {
    std::shared_ptr<const void> opening;
    QString request_id, payload;
    quint64 generation{0}, revision{0};
};
struct Result {
    Identity identity;
    QUrl path;
    Error error{Error::None};
    bool saved{false};
};
struct Services {
    using Submit = std::function<execution::Admission(std::function<Error()>, std::function<void(Error)>,
                                                     std::function<void(std::exception_ptr)>)>;
    std::shared_ptr<execution::Scheduler> scheduler;
    std::shared_ptr<std::atomic_bool> closed;
    std::function<Error(const QImage&, const QUrl&)> write;
    // Consumed on Domain, independently of any GUI observer.
    std::function<void(const Result&)> outcome;
    Submit submit{};
};
Error WritePng(const QImage& image, const QUrl& path);

/** One bounded image and original target. Retiring an interest before the
 * Native write commits admission prevents its effect. An admitted write owns
 * its outcome and finishes even if the view disappears. */
class Operation : public std::enable_shared_from_this<Operation>
{
public:
    Operation(QImage image, QUrl path, Identity identity, Services services, std::function<void()> notify);
    void start();
    void retire();
    std::optional<Result> result() const;
private:
    enum class Admission { Open, Writing, Retired };
    void complete(Error error);
    QImage m_image;
    const QUrl m_path;
    const Identity m_identity;
    const Services m_services;
    const std::function<void()> m_notify;
    std::atomic<Admission> m_admission{Admission::Open};
    bool m_started{false};
    mutable std::mutex m_mutex;
    std::optional<Result> m_result;
};
} // namespace imageexport

class ImageExportOperation : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool pending READ pending NOTIFY changed)
    Q_PROPERTY(bool saved READ saved NOTIFY changed)
    Q_PROPERTY(QString errorText READ errorText NOTIFY changed)
    Q_PROPERTY(QUrl path READ path CONSTANT)
public:
    ImageExportOperation(QImage image, QUrl path, imageexport::Identity identity, imageexport::Services services,
                         std::function<void(std::function<void()>)> post, QObject* parent = nullptr);
    ~ImageExportOperation() override;
    void start();
    bool pending() const { return !m_result; }
    bool saved() const { return m_result && m_result->saved; }
    QString errorText() const;
    QUrl path() const { return m_path; }
    Q_INVOKABLE void retire() { m_operation->retire(); }
    const std::shared_ptr<imageexport::Operation>& operation() const { return m_operation; }
Q_SIGNALS:
    void changed();
private:
    void consume();
    const QUrl m_path;
    std::shared_ptr<imageexport::Operation> m_operation;
    std::optional<imageexport::Result> m_result;
};

#endif // BITCOIN_QML_MODELS_IMAGEEXPORTOPERATION_H
