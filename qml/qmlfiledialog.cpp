// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/qmlfiledialog.h>

#include <QDir>
#include <QFileDialog>
#include <QGuiApplication>
#include <QScopeGuard>
#include <QWindow>
#include <limits>

namespace {
// Native choosers are application-modal. A second request cannot steal an
// existing chooser or create an overlapping native modal session.
QPointer<QmlFileDialog> ActiveChooser;
}

QmlFileDialog::QmlFileDialog(QObject* parent)
    : QObject(parent)
{
}

QmlFileDialog::~QmlFileDialog()
{
    dismiss(false);
}

void QmlFileDialog::setContextObject(QObject* context)
{
    if (m_context == context) return;
    disconnect(m_context_destroyed);
    m_context = context;
    if (context) m_context_destroyed = connect(context, &QObject::destroyed, this, [this] {
        const QPointer<QmlFileDialog> guard{this};
        dismiss(true);
        if (guard) Q_EMIT contextObjectChanged();
    });
    const QPointer<QmlFileDialog> guard{this};
    dismiss(true);
    if (guard) Q_EMIT contextObjectChanged();
}

void QmlFileDialog::setEnabled(bool enabled)
{
    if (m_enabled == enabled) return;
    m_enabled = enabled;
    const QPointer<QmlFileDialog> guard{this};
    if (!enabled) dismiss(true);
    if (guard) Q_EMIT enabledChanged();
}

void QmlFileDialog::setTitle(const QString& title)
{
    if (m_title == title) return;
    m_title = title;
    Q_EMIT titleChanged();
}

void QmlFileDialog::setFileMode(FileMode mode)
{
    if (m_file_mode == mode) return;
    m_file_mode = mode;
    Q_EMIT fileModeChanged();
}

void QmlFileDialog::setDefaultSuffix(const QString& suffix)
{
    if (m_default_suffix == suffix) return;
    m_default_suffix = suffix;
    Q_EMIT defaultSuffixChanged();
}

void QmlFileDialog::setNameFilters(const QStringList& filters)
{
    if (m_name_filters == filters) return;
    m_name_filters = filters;
    Q_EMIT nameFiltersChanged();
}

void QmlFileDialog::setCurrentFolder(const QUrl& folder)
{
    if (m_current_folder == folder) return;
    m_current_folder = folder;
    Q_EMIT currentFolderChanged();
}

void QmlFileDialog::setCurrentFile(const QUrl& file)
{
    if (m_current_file == file) return;
    m_current_file = file;
    Q_EMIT currentFileChanged();
}

void QmlFileDialog::setSelectedFile(const QUrl& file)
{
    if (m_selected_file == file) return;
    m_selected_file = file;
    Q_EMIT selectedFileChanged();
}

void QmlFileDialog::setSuggestedName(const QString& name)
{
    if (name.size() > 32768 || m_suggested_name == name) return;
    m_suggested_name = name;
    Q_EMIT suggestedNameChanged();
}

void QmlFileDialog::setConfirmOverwrite(bool confirm)
{
    if (m_confirm_overwrite == confirm) return;
    m_confirm_overwrite = confirm;
    Q_EMIT confirmOverwriteChanged();
}

bool QmlFileDialog::isVisible() const
{
    return m_active;
}

QFileDialog* QmlFileDialog::dialog(quint64 ticket)
{
    m_dialog = new QFileDialog(nullptr);
    // This native handoff must not become an application window whose close
    // requests node shutdown (notably with mixed Widgets/Quick on Qt 6.2).
    m_dialog->setAttribute(Qt::WA_QuitOnClose, false);
    const auto native = m_dialog;
    connect(native, &QFileDialog::accepted, this, [this, ticket, native] { finish(ticket, native, true); });
    connect(native, &QFileDialog::rejected, this, [this, ticket, native] { finish(ticket, native, false); });
    return m_dialog;
}

bool QmlFileDialog::open()
{
    if (!m_enabled || m_active || m_finishing || ActiveChooser || m_ticket == std::numeric_limits<quint64>::max()) return false;
    m_active = true;
    m_request_context = m_context;
    ActiveChooser = this;
    QFileDialog* dlg = dialog(++m_ticket);
    dlg->setWindowTitle(m_title);
    dlg->setNameFilters(m_name_filters);
    dlg->setDefaultSuffix(m_default_suffix);
    dlg->setOption(QFileDialog::DontConfirmOverwrite, !m_confirm_overwrite);

    switch (m_file_mode) {
    case OpenFile:
        dlg->setAcceptMode(QFileDialog::AcceptOpen);
        dlg->setFileMode(QFileDialog::ExistingFile);
        dlg->setOption(QFileDialog::ShowDirsOnly, false);
        dlg->setFilter(dlg->filter() & ~QDir::Hidden);
        break;
    case SaveFile:
        dlg->setAcceptMode(QFileDialog::AcceptSave);
        dlg->setFileMode(QFileDialog::AnyFile);
        dlg->setOption(QFileDialog::ShowDirsOnly, false);
        dlg->setFilter(dlg->filter() & ~QDir::Hidden);
        break;
    case Directory:
        dlg->setAcceptMode(QFileDialog::AcceptOpen);
        dlg->setFileMode(QFileDialog::Directory);
        dlg->setOption(QFileDialog::ShowDirsOnly, true);
        // Data directories such as ~/.bitcoin are hidden by convention, and
        // the folder picker is where users look for an existing one.
        dlg->setFilter(dlg->filter() | QDir::Hidden);
        break;
    }

    if (m_current_folder.isValid() && !m_current_folder.isEmpty()) {
        dlg->setDirectoryUrl(m_current_folder);
    }
    if (m_current_file.isValid() && !m_current_file.isEmpty()) {
        dlg->selectUrl(m_current_file);
    } else if (!m_suggested_name.isEmpty()) {
        dlg->selectFile(m_suggested_name);
    }

    // Keep the chooser tied to the window that asked for it, so a compositor
    // that cannot place unparented dialogs still stacks it over the app.
    dlg->setModal(true);
    if (QWindow* active = QGuiApplication::focusWindow()) {
        dlg->winId();
        if (QWindow* handle = dlg->windowHandle()) {
            handle->setTransientParent(active);
        }
    }

    const QPointer<QmlFileDialog> guard{this};
    dlg->open();
    if (guard && m_active) Q_EMIT visibleChanged();
    return true;
}

void QmlFileDialog::close()
{
    dismiss(true);
}

void QmlFileDialog::dismiss(bool notify)
{
    if (m_finishing) m_cancelled = true;
    if (!m_active) return;
    const QPointer<QmlFileDialog> guard{this};
    const bool previous_finishing = m_finishing;
    m_finishing = true;
    const auto restore = qScopeGuard([guard, identity = this, previous_finishing] {
        if (ActiveChooser == identity) ActiveChooser.clear();
        if (guard) guard->m_finishing = previous_finishing;
    });
    m_active = false;
    const auto native = m_dialog;
    m_dialog.clear();
    if (native) { native->close(); native->deleteLater(); }
    if (notify) {
        Q_EMIT visibleChanged();
        if (guard) Q_EMIT rejected();
    }
}

void QmlFileDialog::finish(quint64 ticket, const QPointer<QFileDialog>& native, bool accepted)
{
    if (!m_active || ticket != m_ticket || native != m_dialog) return;
    const QPointer<QmlFileDialog> guard{this};
    m_finishing = true;
    m_cancelled = false;
    const auto restore = qScopeGuard([guard, identity = this] {
        if (ActiveChooser == identity) ActiveChooser.clear();
        if (guard) guard->m_finishing = false;
    });
    if (accepted) {
        setSelectedFile(native->selectedUrls().value(0));
        // A binding may close the view, replace its context or destroy this
        // object during selectedFileChanged. That retires this same request.
        if (!guard || !m_active || ticket != m_ticket || native != m_dialog) return;
    }
    m_active = false;
    m_dialog.clear();
    if (native) native->deleteLater();
    Q_EMIT visibleChanged();
    if (!guard) return;
    if (accepted && !m_cancelled && m_enabled && !m_selected_file.isEmpty()) {
        const auto file = m_selected_file;
        const auto context = m_request_context;
        Q_EMIT chosen(context, file);
        if (guard && !m_cancelled && m_enabled) Q_EMIT this->accepted();
    } else Q_EMIT rejected();
}
