// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_QMLFILEDIALOG_H
#define BITCOIN_QML_QMLFILEDIALOG_H

#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QUrl>

QT_BEGIN_NAMESPACE
class QFileDialog;
QT_END_NAMESPACE

/**
 * File and directory chooser backed by QFileDialog.
 *
 * It replaces QtQuick.Dialogs.FileDialog, whose non-native implementation is
 * QML that imports Qt.labs.folderlistmodel. Where that module is absent the
 * implementation cannot be instantiated, and from Qt 6.3 to 6.9.2 opening a
 * dialog then dereferences it. QFileDialog still hands over to the platform
 * dialog when one exists and otherwise draws its own, which needs no QML module
 * and honours the save mode the QML fallback ignores.
 */
class QmlFileDialog : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString title READ title WRITE setTitle NOTIFY titleChanged)
    Q_PROPERTY(FileMode fileMode READ fileMode WRITE setFileMode NOTIFY fileModeChanged)
    Q_PROPERTY(QString defaultSuffix READ defaultSuffix WRITE setDefaultSuffix NOTIFY defaultSuffixChanged)
    Q_PROPERTY(QStringList nameFilters READ nameFilters WRITE setNameFilters NOTIFY nameFiltersChanged)
    Q_PROPERTY(QUrl currentFolder READ currentFolder WRITE setCurrentFolder NOTIFY currentFolderChanged)
    Q_PROPERTY(QUrl currentFile READ currentFile WRITE setCurrentFile NOTIFY currentFileChanged)
    Q_PROPERTY(QUrl selectedFile READ selectedFile WRITE setSelectedFile NOTIFY selectedFileChanged)
    Q_PROPERTY(bool visible READ isVisible NOTIFY visibleChanged)

public:
    enum FileMode {
        OpenFile,
        SaveFile,
        Directory,
    };
    Q_ENUM(FileMode)

    explicit QmlFileDialog(QObject* parent = nullptr);
    ~QmlFileDialog();

    QString title() const { return m_title; }
    void setTitle(const QString& title);

    FileMode fileMode() const { return m_file_mode; }
    void setFileMode(FileMode mode);

    QString defaultSuffix() const { return m_default_suffix; }
    void setDefaultSuffix(const QString& suffix);

    QStringList nameFilters() const { return m_name_filters; }
    void setNameFilters(const QStringList& filters);

    QUrl currentFolder() const { return m_current_folder; }
    void setCurrentFolder(const QUrl& folder);

    QUrl currentFile() const { return m_current_file; }
    void setCurrentFile(const QUrl& file);

    QUrl selectedFile() const { return m_selected_file; }
    void setSelectedFile(const QUrl& file);

    bool isVisible() const;

    Q_INVOKABLE void open();
    Q_INVOKABLE void close();

Q_SIGNALS:
    void titleChanged();
    void fileModeChanged();
    void defaultSuffixChanged();
    void nameFiltersChanged();
    void currentFolderChanged();
    void currentFileChanged();
    void selectedFileChanged();
    void visibleChanged();
    void accepted();
    void rejected();

private:
    QFileDialog* dialog();

    QString m_title;
    FileMode m_file_mode{OpenFile};
    QString m_default_suffix;
    QStringList m_name_filters;
    QUrl m_current_folder;
    QUrl m_current_file;
    QUrl m_selected_file;
    QPointer<QFileDialog> m_dialog;
};

#endif // BITCOIN_QML_QMLFILEDIALOG_H
