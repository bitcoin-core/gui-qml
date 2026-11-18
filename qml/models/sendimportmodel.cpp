// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/sendimportmodel.h>

#include <utility>

namespace {
QString ImportedFrom(SendImportModel::Source source)
{
    switch (source) {
    case SendImportModel::Source::Clipboard: return SendImportModel::tr("Payment request imported from clipboard");
    case SendImportModel::Source::ManualEntry: return SendImportModel::tr("Payment request imported from manual entry");
    case SendImportModel::Source::File: return SendImportModel::tr("Payment request imported from file");
    case SendImportModel::Source::DragAndDrop: return SendImportModel::tr("Payment request imported from drag and drop");
    case SendImportModel::Source::Desktop: return SendImportModel::tr("Payment request imported from desktop");
    }
    return {};
}
} // namespace

SendImportModel::SendImportModel(QObject* parent) : QObject(parent) {}

void SendImportModel::setTarget(ImportTarget* target)
{
    if (m_target == target) return;
    if (m_target) disconnect(m_target, nullptr, this, nullptr);
    cancelAll();
    m_admission_error.clear();
    m_target = target;
    ++m_context;
    if (target) {
        connect(target, &ImportTarget::availableChanged, this, [this] {
            ++m_context;
            if (m_target && !m_target->available()) cancelAll();
            Q_EMIT changed();
        });
        connect(target, &ImportTarget::importPendingChanged, this, [this] {
            if (m_target && !m_target->importPending()) checkClipboard();
        });
        connect(target, &ImportTarget::currentRecipientChanged, this, [this] {
            ++m_context;
            cancelImport();
            m_admission_error.clear();
            checkClipboard();
        });
        connect(target, &ImportTarget::recipientEdited, this, [this] {
            if (m_target && m_target->recipientAddressEmpty()) m_admission_error.clear();
            checkClipboard();
        });
        connect(target, &ImportTarget::importedReviewChanged, this, &SendImportModel::presentReview);
        connect(target, &QObject::destroyed, this, [this] { cancelAll(); Q_EMIT targetChanged(); });
    }
    Q_EMIT targetChanged();
    checkClipboard();
}

void SendImportModel::setActive(bool active)
{
    if (m_active == active) return;
    m_active = active;
    ++m_context;
    Q_EMIT activeChanged();
    if (active) checkClipboard();
    else cancelAll();
}

void SendImportModel::setClipboardText(const QString& text)
{
    if (m_clipboard_text == text) return;
    m_clipboard_text = text;
    Q_EMIT clipboardTextChanged();
    checkClipboard();
}

bool SendImportModel::canAccept() const
{
    return m_active && m_target && m_target->available() && !m_target->importPending();
}

QString SendImportModel::status() const
{
    if (!m_operation) return m_admission_error;
    if (const auto error = m_operation->error(); !error.isEmpty()) return error;
    return m_operation->applied() ? ImportedFrom(m_source) : QString{};
}

bool SendImportModel::statusIsError() const
{
    return m_operation ? !m_operation->error().isEmpty() : !m_admission_error.isEmpty();
}

QString SendImportModel::message() const
{
    return m_operation && m_operation->applied() ? m_operation->message() : QString{};
}

bool SendImportModel::clipboardOffered() const
{
    return m_active && m_target && m_target->available() && m_clipboard_check && m_clipboard_check->offersPayment();
}

void SendImportModel::importPaste(int field, const QString& text, QObject* recipient)
{
    cancelImport();
    if (!m_active || !m_target || !m_target->available()) return;
    start(m_target->importPaste(text, field, recipient, this), Source::Clipboard, /*use=*/true, /*report_unavailable=*/true);
}

void SendImportModel::importText(const QString& text, Source source)
{
    cancelImport();
    start(m_target ? m_target->importText(text, this) : nullptr, source, /*use=*/true, /*report_unavailable=*/true);
}

void SendImportModel::importFile(const QString& path)
{
    cancelImport();
    start(m_target ? m_target->importFile(path, this) : nullptr, Source::File, /*use=*/true, /*report_unavailable=*/true);
}

void SendImportModel::importDrop(int urlCount, const QUrl& firstUrl, const QString& text)
{
    cancelImport();
    start(m_target ? m_target->importDrop(urlCount, firstUrl, text, this) : nullptr, Source::DragAndDrop,
          /*use=*/true, /*report_unavailable=*/false);
}

bool SendImportModel::beginFileChoice()
{
    cancelImport();
    auto* choice = m_target ? m_target->captureFile(this) : nullptr;
    start(choice, Source::File, /*use=*/false, /*report_unavailable=*/false);
    return choice != nullptr;
}

void SendImportModel::chooseFile(ImportOperation* choice, const QUrl& file)
{
    if (choice && choice == m_operation && choice->requestFile(file)) choice->use();
}

bool SendImportModel::acceptImport(ImportOperation* operation, quint64 context)
{
    if (!operation) return false;
    cancelImport();
    if (context != m_context || !canAccept()) {
        operation->cancel();
        return false;
    }
    operation->setParent(this);
    start(operation, Source::Desktop, /*use=*/false, /*report_unavailable=*/false);
    if (!m_target->adopt(operation)) {
        cancelImport();
        return false;
    }
    operation->use();
    return true;
}

void SendImportModel::replace()
{
    if (m_operation && m_operation->promptToken() != 0) m_operation->replace(m_operation->promptToken());
}

void SendImportModel::cancel()
{
    cancelImport();
}

void SendImportModel::draftChanged()
{
    if (!(m_target && m_target->importPending())) cancelAll();
    checkClipboard();
}

void SendImportModel::clearStatus()
{
    if (m_admission_error.isEmpty()) return;
    m_admission_error.clear();
    Q_EMIT changed();
}

void SendImportModel::start(ImportOperation* operation, Source source, bool use, bool report_unavailable)
{
    if (!operation) {
        if (report_unavailable) m_admission_error = tr("Payment URI services are unavailable. Try again.");
        Q_EMIT changed();
        return;
    }
    m_operation = operation;
    m_source = source;
    watch(operation);
    Q_EMIT changed();
    if (use && m_operation == operation) operation->use();
}

void SendImportModel::watch(ImportOperation* operation)
{
    connect(operation, &ImportOperation::changed, this, [this, operation] {
        if (operation != m_operation) return;
        presentReview();
        Q_EMIT changed();
    });
}

void SendImportModel::cancelImport()
{
    m_presented_result.clear();
    if (const auto operation = std::exchange(m_operation, nullptr)) {
        disconnect(operation, nullptr, this, nullptr);
        operation->cancel();
    }
    Q_EMIT changed();
}

void SendImportModel::cancelAll()
{
    cancelImport();
    if (const auto check = std::exchange(m_clipboard_check, nullptr)) {
        disconnect(check, nullptr, this, nullptr);
        check->cancel();
    }
    Q_EMIT changed();
}

void SendImportModel::checkClipboard()
{
    if (const auto check = std::exchange(m_clipboard_check, nullptr)) {
        disconnect(check, nullptr, this, nullptr);
        check->cancel();
    }
    if (m_active && m_target && m_target->available() && !m_target->importPending() && !m_clipboard_text.trimmed().isEmpty()) {
        m_offered_text = m_clipboard_text;
        m_clipboard_check = m_target->importText(m_offered_text, this);
        if (m_clipboard_check) connect(m_clipboard_check, &ImportOperation::changed, this, &SendImportModel::changed);
    }
    Q_EMIT changed();
}

void SendImportModel::presentReview()
{
    if (!m_operation || !m_operation->reviewOpened() || !m_target || !m_target->available()) return;
    const auto token = m_operation->resultToken();
    if (token.isEmpty() || token == m_presented_result || m_target->importedReviewToken() != token) return;
    m_presented_result = token;
    Q_EMIT reviewReady();
}
