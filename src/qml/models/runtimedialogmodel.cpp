// Copyright (c) 2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/runtimedialogmodel.h>
#include <interfaces/node.h>
#include <node/interface_ui.h>
#include <util/translation.h>
#include <QEventLoop>
#include <QThread>
#include <algorithm>
#include <cassert>

namespace {
QStringList SplitWarnings(const QString& warnings)
{
    QString normalized{warnings};
    normalized.replace(QStringLiteral("<hr/>"), QStringLiteral("<hr />"), Qt::CaseInsensitive);
    normalized.replace(QStringLiteral("<hr>"), QStringLiteral("<hr />"), Qt::CaseInsensitive);

    QStringList result;
    for (const QString& warning : normalized.split(QStringLiteral("<hr />"), Qt::SkipEmptyParts)) {
        const QString trimmed{warning.trimmed()};
        if (!trimmed.isEmpty()) {
            result.push_back(trimmed);
        }
    }
    return result;
}

QString RuntimeDialogTitle(unsigned int style)
{
    if (style & CClientUIInterface::ICON_ERROR) {
        return QObject::tr("Error");
    }
    if (style & CClientUIInterface::ICON_WARNING) {
        return QObject::tr("Warning");
    }
    return QObject::tr("Information");
}

QString RuntimeDialogIcon(unsigned int style)
{
    if (style & CClientUIInterface::ICON_ERROR) {
        return QStringLiteral("image://images/error");
    }
    if (style & CClientUIInterface::ICON_WARNING) {
        return QStringLiteral("image://images/alert-filled");
    }
    return QStringLiteral("image://images/info-filled");
}

unsigned int RuntimeDialogButtons(unsigned int style)
{
    const unsigned int buttons{style & CClientUIInterface::BTN_MASK};
    return buttons ? buttons : CClientUIInterface::BTN_OK;
}

bool HasMultipleRuntimeDialogButtons(unsigned int buttons)
{
    return (buttons & (buttons - 1)) != 0;
}

} // namespace

RuntimeDialogModel::RuntimeDialogModel(interfaces::Node& node) : m_node{node}
{
    refreshWarnings();
    ConnectToAlertChangedSignal();
    ConnectToRuntimeDialogSignals();
}

RuntimeDialogModel::~RuntimeDialogModel()
{
    stop();
    m_handler_notify_alert_changed.reset();
    m_handler_message_box.reset();
    m_handler_question.reset();
}

void RuntimeDialogModel::completeInitialization(bool success)
{
    if (m_stopping) return;
    refreshWarnings();
    if (success) {
        m_startup_error_messages.clear();
        m_runtime_dialogs_enabled = true;
        showStartupWarnings();
    }
}

QString RuntimeDialogModel::startupErrorSummary() const
{
    QString error{m_startup_error_messages.isEmpty() ? tr("Node initialization failed.") : m_startup_error_messages.join(QStringLiteral("\n\n"))};
    if (!m_startup_warning_messages.isEmpty()) {
        error.prepend(tr("Startup warnings:") + QStringLiteral("\n") + m_startup_warning_messages.join(QStringLiteral("\n\n")) + QStringLiteral("\n\n"));
    }
    return error;
}

void RuntimeDialogModel::finishRequest(const std::shared_ptr<RuntimeDialogRequest>& request, bool answer)
{
    {
        std::lock_guard lock{request->mutex};
        request->answered = true;
        request->answer = answer;
    }
    request->completed.notify_all();
    if (request->loop) request->loop->quit();
}

void RuntimeDialogModel::stop()
{
    m_stopping = true;
    // Includes callbacks whose queued GUI delivery has not run yet.
    {
        std::lock_guard lock{m_pending_mutex};
        for (auto& pending : m_pending) {
            if (auto request = pending.lock()) finishRequest(request, false);
        }
        m_pending.clear();
    }
    m_runtime_dialog_active.reset();
    m_runtime_dialog_queue.clear();
    if (m_runtime_dialog_visible) {
        m_runtime_dialog_visible = false;
        Q_EMIT runtimeDialogChanged();
    }
}

void RuntimeDialogModel::showRuntimeMessageBox(const QString& message, unsigned int style)
{
    submitDialog(message, style, false);
}

bool RuntimeDialogModel::showRuntimeQuestion(const QString& message, unsigned int style)
{
    return submitDialog(message, style, true);
}

bool RuntimeDialogModel::showRuntimeDialogOnGuiThread(const QString& message, unsigned int style, bool question)
{
    return submitDialog(message, style, question);
}

bool RuntimeDialogModel::submitDialog(const QString& message, unsigned int style, bool question)
{
    auto request{std::make_shared<RuntimeDialogRequest>()};
    request->message = message;
    request->style = style;
    request->question = question;
    const bool blocking{question || (style & CClientUIInterface::MODAL)};
    {
        std::lock_guard lock{m_pending_mutex};
        if (m_stopping) return false;
        std::erase_if(m_pending, [](const auto& pending) { return pending.expired(); });
        m_pending.push_back(request);
    }
    if (QThread::currentThread() == thread()) {
        QEventLoop loop;
        if (blocking) request->loop = &loop;
        enqueueRequest(request);
        if (blocking && !request->answered) loop.exec();
        request->loop = nullptr;
    } else {
        QMetaObject::invokeMethod(this, [this, request] { enqueueRequest(request); }, Qt::QueuedConnection);
        if (blocking) {
            std::unique_lock lock{request->mutex};
            request->completed.wait(lock, [&] { return request->answered; });
        }
    }
    std::lock_guard lock{request->mutex};
    return request->answer;
}

void RuntimeDialogModel::enqueueRequest(const std::shared_ptr<RuntimeDialogRequest>& request)
{
    if (m_stopping) {
        finishRequest(request, false);
        return;
    }
    if (!m_runtime_dialogs_enabled && !request->question) {
        if (request->style & CClientUIInterface::ICON_WARNING) {
            recordStartupWarningMessage(request->message);
            finishRequest(request, false);
            return;
        }
        if (!(request->style & CClientUIInterface::MODAL)) {
            if (request->style & CClientUIInterface::ICON_ERROR) recordStartupErrorMessage(request->message);
            finishRequest(request, false);
            return;
        }
    }
    if (!m_runtime_dialogs_enabled && (request->question || (request->style & CClientUIInterface::ICON_ERROR))) {
        m_startup_failure_dialog_shown = true;
    }
    if (m_runtime_dialog_active) m_runtime_dialog_queue.push_back(request);
    else showRuntimeDialogRequest(request);
}

void RuntimeDialogModel::answerRuntimeDialog(unsigned int button)
{
    if (!m_runtime_dialog_active || !(button & m_runtime_dialog_buttons) || (button & (button - 1))) return;
    auto answered{std::move(m_runtime_dialog_active)};
    finishRequest(answered, button == CClientUIInterface::BTN_OK);
    if (!m_runtime_dialog_queue.empty()) {
        auto next{m_runtime_dialog_queue.front()};
        m_runtime_dialog_queue.pop_front();
        showRuntimeDialogRequest(next);
    } else {
        m_runtime_dialog_visible = false;
        Q_EMIT runtimeDialogChanged();
    }
}

void RuntimeDialogModel::addStartupWarnings(const QStringList& warnings)
{
    for (const QString& warning : warnings) {
        recordStartupWarningMessage(warning);
    }
}

void RuntimeDialogModel::setWarnings(const QString& warnings)
{
    const QStringList warning_list{SplitWarnings(warnings)};
    if (m_warnings == warnings && m_warning_list == warning_list) {
        return;
    }
    m_warnings = warnings;
    m_warning_list = warning_list;
    Q_EMIT warningsChanged();
}

void RuntimeDialogModel::refreshWarnings()
{
    if (m_stopping) return;
    // Keep "current warnings" tied to Core's active warning set.
    setWarnings(QString::fromStdString(m_node.getWarnings().translated));
}

void RuntimeDialogModel::showStartupWarnings()
{
    if (m_startup_warning_messages.isEmpty()) {
        return;
    }

    const QString warnings{m_startup_warning_messages.join(QStringLiteral("\n\n"))};
    m_startup_warning_messages.clear();
    // MSG_WARNING is modal; startup notices should be shown once without blocking initialization.
    showRuntimeDialogOnGuiThread(warnings, CClientUIInterface::ICON_WARNING, /*question=*/false);
}

void RuntimeDialogModel::recordStartupErrorMessage(const QString& message)
{
    const QString error{message.trimmed()};
    if (error.isEmpty() || m_startup_error_messages.contains(error)) {
        return;
    }
    m_startup_error_messages.push_back(error);
}

void RuntimeDialogModel::recordStartupWarningMessage(const QString& message)
{
    const QString warning{message.trimmed()};
    if (warning.isEmpty() || m_startup_warning_messages.contains(warning)) {
        return;
    }
    m_startup_warning_messages.push_back(warning);
}

void RuntimeDialogModel::ConnectToAlertChangedSignal()
{
    assert(!m_handler_notify_alert_changed);

    m_handler_notify_alert_changed = m_node.handleNotifyAlertChanged([this]() {
        QMetaObject::invokeMethod(this, [this] {
            refreshWarnings();
        }, Qt::QueuedConnection);
    });
}

void RuntimeDialogModel::ConnectToRuntimeDialogSignals()
{
    assert(!m_handler_message_box);
    assert(!m_handler_question);

    m_handler_message_box = m_node.handleMessageBox(
        [this](const bilingual_str& message, unsigned int style) {
            showRuntimeMessageBox(
                QString::fromStdString(message.translated),
                style);
        });
    m_handler_question = m_node.handleQuestion(
        [this](const bilingual_str& message, [[maybe_unused]] const std::string& non_interactive_message, unsigned int style) {
            return showRuntimeQuestion(
                QString::fromStdString(message.translated),
                style);
        });
}

void RuntimeDialogModel::showRuntimeDialogRequest(const std::shared_ptr<RuntimeDialogRequest>& request)
{
    m_runtime_dialog_active = request;
    m_runtime_dialog_title = RuntimeDialogTitle(request->style);
    m_runtime_dialog_message = request->message;
    m_runtime_dialog_icon = RuntimeDialogIcon(request->style);
    m_runtime_dialog_buttons = RuntimeDialogButtons(request->style);
    m_runtime_dialog_question = request->question || HasMultipleRuntimeDialogButtons(m_runtime_dialog_buttons);
    m_runtime_dialog_visible = true;
    Q_EMIT runtimeDialogChanged();
}
