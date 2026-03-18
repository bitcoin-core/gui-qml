// Copyright (c) 2021-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/desktoptrayiconcontroller.h>

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QIcon>
#include <QImage>
#include <QMenu>
#include <QStandardPaths>
#include <QStringList>
#include <QTextStream>
#include <QTimer>

DesktopTrayIconController::DesktopTrayIconController(QObject* parent)
    : QObject(parent)
    , m_tray_icon(new QSystemTrayIcon(this))
{
    // Tooltip is required by the StatusNotifierItem/AppIndicator protocol —
    // some GNOME AppIndicator versions silently drop icons with no title set.
    m_tray_icon->setToolTip(QStringLiteral("Bitcoin Core"));

    // Fix 5: Register a native QMenu before any show() call.
    // Some AppIndicator/SNI hosts (Ubuntu Unity era and gnome-shell-extension-
    // appindicator) take direct ownership of all click interactions when a menu
    // is registered, bypassing QSystemTrayIcon::activated entirely. The menu
    // must therefore contain real actions so users can restore and quit the app.
    auto* nativeMenu = new QMenu();
    auto* showAction = nativeMenu->addAction(tr("Show"));
    connect(showAction, &QAction::triggered, this, [this] { Q_EMIT restoreRequested(); });
    nativeMenu->addSeparator();
    auto* quitAction = nativeMenu->addAction(tr("Quit"));
    connect(quitAction, &QAction::triggered, this, [this] { Q_EMIT quitRequested(); });
    m_tray_icon->setContextMenu(nativeMenu);

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
    return m_visible;
}

void DesktopTrayIconController::setVisible(bool visible)
{
    if (visible) {
        // Idempotent: already showing, or a show attempt is already in flight.
        if (m_visible || m_show_requested) return;

        m_show_requested = true;
        m_show_attempts = 0;

        // Fix 3: Defer show() to the first event-loop iteration so the D-Bus
        // connection is ready on SNI systems. Do NOT set m_visible or emit
        // visibleChanged(true) yet — only do so after show() is confirmed.
        QTimer::singleShot(0, this, [this] { attemptShow(); });
    } else {
        // Cancel any in-flight show attempt.
        m_show_requested = false;
        if (!m_visible) return;
        m_visible = false;
        m_tray_icon->hide();
        Q_EMIT visibleChanged(false);
    }
}

void DesktopTrayIconController::attemptShow()
{
    // Fix 3+4: Abort if setVisible(false) cancelled the request.
    if (!m_show_requested) return;

    m_tray_icon->show();

    if (m_tray_icon->isVisible()) {
        m_show_requested = false;
        m_visible = true;
        qInfo() << "[tray] System tray icon registered and visible";
        Q_EMIT visibleChanged(true);
        Q_EMIT supportedChanged(true);
        return;
    }

    // Fix 4: Retry with increasing delay to handle slow SNI host init.
    ++m_show_attempts;
    if (m_show_attempts < kMaxShowAttempts) {
        const int delay_ms = 100 * m_show_attempts; // 100, 200, 300, 400 ms
        QTimer::singleShot(delay_ms, this, [this] { attemptShow(); });
        return;
    }

    // All retries exhausted.
    m_show_requested = false;
    Q_EMIT supportedChanged(false);
}

QString DesktopTrayIconController::iconName() const
{
    return m_tray_icon->icon().name();
}

void DesktopTrayIconController::setIcon(const QIcon& icon)
{
#if defined(Q_OS_LINUX)
    // gnome-shell-extension-appindicator looks up icons by IconName in the default
    // GTK icon theme. It appends '-panel' to try a monochrome panel variant first.
    // We install both variants to the standard XDG user icon directory
    // (~/.local/share/icons/hicolor/48x48/apps/) so GTK finds them without
    // requiring a custom IconThemePath — which the extension often ignores.
    // This block is Linux-only: on macOS/Windows GenericDataLocation resolves to
    // different paths and AppIndicator does not exist.
    const QPixmap pixmap = icon.pixmap(48, 48);
    if (!pixmap.isNull()) {
        const QString data_dir =
            QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
        const QString apps_dir =
            data_dir + QStringLiteral("/icons/hicolor/48x48/apps");
        if (QDir().mkpath(apps_dir)) {
            const bool ok =
                pixmap.save(apps_dir + QStringLiteral("/bitcoin-core.png")) &&
                pixmap.save(apps_dir + QStringLiteral("/bitcoin-core-panel.png"));
            if (ok) {
                const QString theme_path =
                    data_dir + QStringLiteral("/icons/hicolor/index.theme");

                // Fix 1: Ensure 48x48/apps appears in BOTH the Directories= line
                // AND has its own stanza. Qt's QIconLoader and GTK both use the
                // Directories= value as the authoritative list of subdirs to scan;
                // a stanza without a Directories= entry is silently ignored.
                bool has_48x48_apps = false;
                QString existing_content;
                if (QFile::exists(theme_path)) {
                    QFile rf(theme_path);
                    if (rf.open(QIODevice::ReadOnly | QIODevice::Text)) {
                        existing_content = QTextStream(&rf).readAll();
                        has_48x48_apps =
                            existing_content.contains(QStringLiteral("48x48/apps"));
                    }
                }

                if (!has_48x48_apps) {
                    const QString dirs_key = QStringLiteral("Directories=");
                    const int dirs_pos = existing_content.indexOf(dirs_key);
                    if (dirs_pos >= 0) {
                        // Existing index.theme has a Directories= line but it
                        // doesn't include 48x48/apps. Append the directory to that
                        // line, then add the required stanza.
                        const int eol = existing_content.indexOf(
                            QLatin1Char('\n'), dirs_pos);
                        const int insert_at =
                            (eol >= 0) ? eol : existing_content.size();
                        existing_content.insert(
                            insert_at, QStringLiteral(",48x48/apps"));
                        existing_content +=
                            QStringLiteral("\n[48x48/apps]\n"
                                           "Size=48\n"
                                           "Context=Applications\n"
                                           "Type=Fixed\n");
                        QFile wf(theme_path);
                        if (wf.open(QIODevice::WriteOnly |
                                    QIODevice::Truncate |
                                    QIODevice::Text)) {
                            QTextStream(&wf) << existing_content;
                        } else {
                            qWarning() << "[tray] Failed to update index.theme:" << wf.errorString();
                        }
                    } else {
                        // No Directories= line (malformed or new file): write a
                        // fresh minimal hicolor theme from scratch.
                        QFile wf(theme_path);
                        if (wf.open(QIODevice::WriteOnly |
                                    QIODevice::Truncate |
                                    QIODevice::Text)) {
                            QTextStream s(&wf);
                            s << "[Icon Theme]\n"
                              << "Name=hicolor\n"
                              << "Comment=Fallback Theme\n"
                              << "Directories=48x48/apps\n\n"
                              << "[48x48/apps]\n"
                              << "Size=48\n"
                              << "Context=Applications\n"
                              << "Type=Fixed\n";
                        } else {
                            qWarning() << "[tray] Failed to write index.theme:" << wf.errorString();
                        }
                    }
                }

                // Use the icon directly via IconPixmap (StatusNotifierItem spec)
                // rather than QIcon::fromTheme(). GNOME AppIndicator forces
                // all named/themed icons to monochrome, which strips colour.
                // Passing the QIcon directly uses the IconPixmap D-Bus property
                // and preserves the full-colour pixmap.
                m_tray_icon->setIcon(icon);
                return;
            }
        }
    }
#endif // Q_OS_LINUX
    m_tray_icon->setIcon(icon);
}

void DesktopTrayIconController::setBasePixmap(const QPixmap& pixmap)
{
    m_base_pixmap = pixmap;
    updateIcon();
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

void DesktopTrayIconController::updateIcon()
{
    if (m_base_pixmap.isNull()) return;
    QIcon icon;
#if defined(Q_OS_MACOS)
    // macOS NSStatusBar handles tray icon theming automatically via template
    // images; manual inversion is not needed and could double-invert.
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
    setIcon(icon);
}
