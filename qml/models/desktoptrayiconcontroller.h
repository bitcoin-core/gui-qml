// Copyright (c) 2021-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_DESKTOPTRAYICONCONTROLLER_H
#define BITCOIN_QML_MODELS_DESKTOPTRAYICONCONTROLLER_H

#include <QObject>
#include <QPixmap>
#include <QString>
#include <QSystemTrayIcon>

class DesktopTrayIconController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool supported READ supported NOTIFY supportedChanged)
    Q_PROPERTY(bool visible READ visible WRITE setVisible NOTIFY visibleChanged)
    Q_PROPERTY(bool isDark READ isDark WRITE setIsDark NOTIFY isDarkChanged)

public:
    explicit DesktopTrayIconController(QObject* parent = nullptr);

    bool supported() const;
    bool visible() const;
    void setVisible(bool visible);
    void setIcon(const QIcon& icon);
    void setBasePixmap(const QPixmap& pixmap);
    QString iconName() const;
    bool isDark() const;
    void setIsDark(bool dark);

Q_SIGNALS:
    void visibleChanged(bool visible);
    void supportedChanged(bool supported);
    void restoreRequested();
    void contextMenuRequested();
    void quitRequested();
    void isDarkChanged(bool dark);

private:
    void attemptShow();
    void updateIcon();

    QSystemTrayIcon* m_tray_icon{nullptr};
    bool m_visible{false};
    // True while a deferred show() is in flight; cleared on success, failure,
    // or when setVisible(false) cancels the attempt.
    bool m_show_requested{false};
    int m_show_attempts{0};
    static constexpr int kMaxShowAttempts{5};
    bool m_is_dark{true};
    QPixmap m_base_pixmap;
};

#endif // BITCOIN_QML_MODELS_DESKTOPTRAYICONCONTROLLER_H
