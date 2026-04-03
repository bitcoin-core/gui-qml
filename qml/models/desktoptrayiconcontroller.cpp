// Copyright (c) 2021-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/desktoptrayiconcontroller.h>

#include <QImage>
#include <QMenu>

DesktopTrayIconController::DesktopTrayIconController(QObject* parent)
    : QObject(parent)
    , m_tray_icon(new QSystemTrayIcon(this))
{
    m_tray_icon->setToolTip(QStringLiteral("Bitcoin Core"));

    auto* menu = new QMenu();
    auto* showAction = menu->addAction(tr("Show"));
    connect(showAction, &QAction::triggered, this, [this] { Q_EMIT restoreRequested(); });
    menu->addSeparator();
    auto* quitAction = menu->addAction(tr("Quit"));
    connect(quitAction, &QAction::triggered, this, [this] { Q_EMIT quitRequested(); });
    m_tray_icon->setContextMenu(menu);

    connect(m_tray_icon, &QSystemTrayIcon::activated, this,
        [this](QSystemTrayIcon::ActivationReason reason) {
            if (reason == QSystemTrayIcon::Trigger ||
                    reason == QSystemTrayIcon::DoubleClick) {
                Q_EMIT restoreRequested();
            } else if (reason == QSystemTrayIcon::Context) {
                Q_EMIT contextMenuRequested();
            }
        });
}

bool DesktopTrayIconController::supported() const
{
    return QSystemTrayIcon::isSystemTrayAvailable();
}

bool DesktopTrayIconController::visible() const
{
    return m_tray_icon->isVisible();
}

void DesktopTrayIconController::setVisible(bool visible)
{
    if (visible == m_tray_icon->isVisible()) return;

    if (visible) {
        m_tray_icon->show();
        if (!m_tray_icon->isVisible()) {
            Q_EMIT supportedChanged(false);
            return;
        }
    } else {
        m_tray_icon->hide();
    }
    Q_EMIT visibleChanged(m_tray_icon->isVisible());
}

bool DesktopTrayIconController::isDark() const
{
    return m_is_dark;
}

void DesktopTrayIconController::setIsDark(bool dark)
{
    if (m_is_dark == dark) return;
    m_is_dark = dark;
    updateIcon();
    Q_EMIT isDarkChanged(m_is_dark);
}

void DesktopTrayIconController::setBasePixmap(const QPixmap& pixmap)
{
    m_base_pixmap = pixmap;
    updateIcon();
}

void DesktopTrayIconController::updateIcon()
{
    if (m_base_pixmap.isNull()) return;
    QIcon icon;
#if defined(Q_OS_MACOS)
    icon = QIcon(m_base_pixmap);
    icon.setIsMask(true);
#else
    if (m_is_dark) {
        QImage img = m_base_pixmap.toImage();
        img.invertPixels(QImage::InvertRgb);
        icon = QIcon(QPixmap::fromImage(img));
    } else {
        icon = QIcon(m_base_pixmap);
    }
#endif
    m_tray_icon->setIcon(icon);
}
