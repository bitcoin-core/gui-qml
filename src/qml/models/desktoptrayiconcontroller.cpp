// Copyright (c) 2021-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/desktoptrayiconcontroller.h>
#include <qml/applicationrouter.h>

#include <QGuiApplication>
#include <QMenu>
#include <QWindow>

#if defined(Q_OS_MACOS)
#include <qml/models/macdockiconhandler.h>
#endif

// Synchronous hit-test for whether another window covers @w, ported from
// Bitcoin Core's GUIUtil::isObscured(). QGuiApplication::topLevelAt() only
// reports windows owned by this application, so a corner covered by another
// application reads as "not ours" and the window is treated as obscured.
// This works under X11 only: on Wayland the compositor forbids cross-app
// window queries, so topLevelAt() always returns our own window and obscured
// detection is unavailable (the Qt Widgets GUI has the same limitation via
// QApplication::widgetAt()). See bitcoin/bitcoin#19950.
static bool checkPoint(const QPoint &p, const QWindow *w)
{
    QWindow *atW = QGuiApplication::topLevelAt(w->mapToGlobal(p));
    return atW == w;
}

static bool isObscured(const QWindow *w)
{
    return !(checkPoint(QPoint(0, 0), w)
        && checkPoint(QPoint(w->width() - 1, 0), w)
        && checkPoint(QPoint(0, w->height() - 1), w)
        && checkPoint(QPoint(w->width() - 1, w->height() - 1), w)
        && checkPoint(QPoint(w->width() / 2, w->height() / 2), w));
}

DesktopTrayIconController::DesktopTrayIconController(QObject* parent)
    : QObject(parent)
    , m_tray_icon(new QSystemTrayIcon(this))
{
    // Linux SNI/AppIndicator hosts require a native QMenu registered via
    // setContextMenu() — the menu is exported over D-Bus and displayed by
    // the DE natively. Without this, right-click does nothing on GNOME/KDE.
    m_menu = std::make_unique<QMenu>();
    rebuildMenu();
    m_tray_icon->setContextMenu(m_menu.get());

    connect(m_tray_icon, &QSystemTrayIcon::activated, this,
        [this](QSystemTrayIcon::ActivationReason reason) {
            if (reason == QSystemTrayIcon::Trigger) {
                toggleWindow();
            }
        });

#if defined(Q_OS_MACOS)
    MacDockIconHandler* dockHandler = MacDockIconHandler::instance();
    connect(dockHandler, &MacDockIconHandler::dockIconClicked, this,
        [this] { Q_EMIT showRequested(); });
#endif
}

DesktopTrayIconController::~DesktopTrayIconController()
{
    m_tray_icon->setContextMenu(nullptr);
}

void DesktopTrayIconController::setRouter(ApplicationRouter& router)
{
    if (m_router) disconnect(m_router, nullptr, this, nullptr);
    m_router = &router;
    // Do not delete the triggering QAction while its callback is on the stack.
    connect(&router, &ApplicationRouter::destinationsChanged, this, &DesktopTrayIconController::rebuildMenu, Qt::QueuedConnection);
    connect(&router, &ApplicationRouter::currentChanged, this, &DesktopTrayIconController::rebuildMenu, Qt::QueuedConnection);
    rebuildMenu();
}

void DesktopTrayIconController::retranslate() { rebuildMenu(); }

void DesktopTrayIconController::rebuildMenu()
{
    m_menu->clear();
    m_show_action = m_menu->addAction(m_window_visible ? tr("Hide") : tr("Show"));
    connect(m_show_action, &QAction::triggered, this, [this] {
        if (m_window_visible) Q_EMIT hideRequested();
        else Q_EMIT showRequested();
    });
    if (m_router) {
        m_menu->addSeparator();
        for (const auto& destination : m_router->destinations()) {
            if (!destination.in_menu || !destination.enabled) continue;
            auto* action = m_menu->addAction(ApplicationRouter::title(destination));
            action->setObjectName(QStringLiteral("native_navigate_") + destination.id);
            action->setCheckable(true);
            action->setChecked(m_router->currentSelection() == destination.id);
            action->setEnabled(!m_router->shuttingDown());
            connect(action, &QAction::triggered, this, [this, id = destination.id] {
                if (m_router && m_router->navigate(id)) Q_EMIT showRequested();
            });
        }
    }
    m_menu->addSeparator();
    auto* quit_action = m_menu->addAction(tr("Quit"));
    quit_action->setObjectName(QStringLiteral("native_quit"));
    quit_action->setEnabled(!m_router || !m_router->shuttingDown());
    connect(quit_action, &QAction::triggered, this, &DesktopTrayIconController::quitRequested);
}

void DesktopTrayIconController::toggleWindow()
{
    if (!m_main_window) return;

    bool hidden = !m_main_window->isVisible();
    bool minimized = m_main_window->windowStates() & Qt::WindowMinimized;

    if (!hidden && !minimized && !isObscured(m_main_window)) {
        Q_EMIT hideRequested();
    } else {
        Q_EMIT showRequested();
    }
}

void DesktopTrayIconController::setMainWindow(QWindow* window)
{
    m_main_window = window;
}

void DesktopTrayIconController::hideMainWindow()
{
    if (!m_main_window) return;
    // Capture geometry before hiding so showMainWindow() can restore it; the
    // window manager does not reliably preserve size/position across an
    // unmap/map cycle.
    m_saved_geometry = m_main_window->geometry();
    m_main_window->hide();
}

void DesktopTrayIconController::showMainWindow()
{
    if (!m_main_window) return;
    // hide() withdraws the window, so the WM forgets its geometry and re-maps it
    // fresh, unlike minimize, which keeps the window mapped and restores it
    // natively. Re-impose the saved geometry around the re-map: set the full
    // geometry before show() (best chance the WM honors the position on map), and
    // re-assert the size after show(), since a WM that ignores a programmatic
    // position will often still honor a size request. On Wayland the compositor
    // controls placement and does not reliably honor either, so restoration there
    // is best-effort.
    const QRect target = m_saved_geometry;
    m_saved_geometry = QRect();
    if (target.isValid()) {
        m_main_window->setGeometry(target);
    }
    m_main_window->show();
    if (target.isValid()) {
        m_main_window->resize(target.size());
    }
    m_main_window->raise();
    m_main_window->requestActivate();
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

void DesktopTrayIconController::setBasePixmap(const QPixmap& pixmap)
{
    m_base_pixmap = pixmap;
    updateIcon();
}

void DesktopTrayIconController::setToolTip(const QString& tip)
{
    m_tray_icon->setToolTip(tip);
}

QString DesktopTrayIconController::toolTip() const
{
    return m_tray_icon->toolTip();
}

bool DesktopTrayIconController::windowVisible() const
{
    return m_window_visible;
}

void DesktopTrayIconController::setWindowVisible(bool visible)
{
    if (m_window_visible == visible) return;
    m_window_visible = visible;
    if (m_show_action) {
        m_show_action->setText(visible ? tr("Hide") : tr("Show"));
    }
    Q_EMIT windowVisibleChanged(visible);
}

void DesktopTrayIconController::updateIcon()
{
    if (m_base_pixmap.isNull()) return;
    // Match Bitcoin Core's Qt Widgets GUI: show the network-colored icon as-is,
    // with no theme inversion and no macOS template mask (a mask would discard
    // the per-network color). See src/qt/bitcoingui.cpp.
    m_tray_icon->setIcon(QIcon(m_base_pixmap));
}
