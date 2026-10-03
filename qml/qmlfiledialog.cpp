// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/qmlfiledialog.h>

#include <QDir>
#include <QFileDialog>
#include <QGuiApplication>
#include <QWindow>

QmlFileDialog::QmlFileDialog(QObject* parent)
    : QObject(parent)
{
}

QmlFileDialog::~QmlFileDialog()
{
    if (m_dialog) {
        m_dialog->deleteLater();
    }
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

bool QmlFileDialog::isVisible() const
{
    return m_dialog && m_dialog->isVisible();
}

QFileDialog* QmlFileDialog::dialog()
{
    if (m_dialog) return m_dialog;

    m_dialog = new QFileDialog(nullptr);
    connect(m_dialog, &QFileDialog::accepted, this, [this] {
        setSelectedFile(m_dialog->selectedUrls().value(0));
        Q_EMIT accepted();
    });
    connect(m_dialog, &QFileDialog::rejected, this, [this] { Q_EMIT rejected(); });
    connect(m_dialog, &QFileDialog::finished, this, [this] { Q_EMIT visibleChanged(); });
    return m_dialog;
}

void QmlFileDialog::open()
{
    QFileDialog* dlg = dialog();
    dlg->setWindowTitle(m_title);
    dlg->setNameFilters(m_name_filters);
    dlg->setDefaultSuffix(m_default_suffix);

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

    dlg->open();
    Q_EMIT visibleChanged();
}

void QmlFileDialog::close()
{
    if (m_dialog) {
        m_dialog->close();
    }
}
