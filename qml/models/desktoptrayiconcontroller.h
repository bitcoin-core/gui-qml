// Copyright (c) 2021-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_DESKTOPTRAYICONCONTROLLER_H
#define BITCOIN_QML_MODELS_DESKTOPTRAYICONCONTROLLER_H

#include <QObject>
#include <QPixmap>
#include <QRect>
#include <QString>
#include <QSystemTrayIcon>

class QAction;
class QWindow;

class DesktopTrayIconController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool supported READ supported NOTIFY supportedChanged)
    Q_PROPERTY(bool visible READ visible WRITE setVisible NOTIFY visibleChanged)
    Q_PROPERTY(bool windowVisible READ windowVisible WRITE setWindowVisible NOTIFY windowVisibleChanged)

public:
    explicit DesktopTrayIconController(QObject* parent = nullptr);

    bool supported() const;
    bool visible() const;
    void setVisible(bool visible);
    void setBasePixmap(const QPixmap& pixmap);
    void setToolTip(const QString& tip);
    QString toolTip() const;
    bool windowVisible() const;
    void setWindowVisible(bool visible);

    void setMainWindow(QWindow* window);

    Q_INVOKABLE void hideMainWindow();
    Q_INVOKABLE void showMainWindow();
    void activateMainWindow();

Q_SIGNALS:
    void visibleChanged(bool visible);
    void supportedChanged(bool supported);
    void showRequested();
    void hideRequested();
    void quitRequested();
    void windowVisibleChanged(bool visible);

private:
    void updateIcon();
    void toggleWindow();

    QSystemTrayIcon* m_tray_icon{nullptr};
    QAction* m_show_action{nullptr};
    QWindow* m_main_window{nullptr};
    bool m_window_visible{true};
    QPixmap m_base_pixmap;
    // Geometry captured at hideMainWindow() time and re-applied on the next
    // showMainWindow(), since the window manager does not reliably preserve it
    // across a hide/show. Invalid (and ignored) outside a hide→show cycle.
    QRect m_saved_geometry;
};

#endif // BITCOIN_QML_MODELS_DESKTOPTRAYICONCONTROLLER_H
