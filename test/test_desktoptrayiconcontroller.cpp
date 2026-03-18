// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QtTest/QtTest>

#include <qml/models/desktoptrayiconcontroller.h>

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTextStream>

class DesktopTrayIconControllerTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // setVisible() signal timing
    void setVisibleFalse_isImmediate();
    void setVisibleTrue_deferredUntilShowConfirms();
    void cancelPendingShow_preventsTrueEmission();

#if defined(Q_OS_LINUX)
    // index.theme and icon-name correctness (Linux-only; AppIndicator path)
    void indexTheme_createsValidFileWhenMissing();
    void indexTheme_updatesDirectoriesLineInExistingFile();
    void indexTheme_preservesExistingDirectoriesEntries();
    void setIcon_setsNamedIcon();
#endif
};

// ── setVisible() signal timing ────────────────────────────────────────────────

void DesktopTrayIconControllerTests::setVisibleFalse_isImmediate()
{
    DesktopTrayIconController ctrl;
    QSignalSpy spy(&ctrl, &DesktopTrayIconController::visibleChanged);

    ctrl.setVisible(false);

    // Calling setVisible(false) on a fresh controller (already false) is a no-op.
    QCOMPARE(ctrl.visible(), false);
    QCOMPARE(spy.count(), 0);
}

void DesktopTrayIconControllerTests::setVisibleTrue_deferredUntilShowConfirms()
{
    DesktopTrayIconController ctrl;
    QSignalSpy spy(&ctrl, &DesktopTrayIconController::visibleChanged);

    ctrl.setVisible(true);

    // Immediately after setVisible(true), visible() must still be false — the
    // show() has been deferred to the first event-loop iteration (Fix 3).
    QCOMPARE(ctrl.visible(), false);

    // Let the deferred show() run. On the offscreen platform,
    // QSystemTrayIcon::isVisible() returns false, so we expect visibleChanged
    // to eventually be emitted (either true on a real DE, or false after retries
    // exhaust on offscreen). We just verify the signal IS eventually emitted.
    QTRY_VERIFY_WITH_TIMEOUT(spy.count() >= 1, 1500);
}

void DesktopTrayIconControllerTests::cancelPendingShow_preventsTrueEmission()
{
    DesktopTrayIconController ctrl;
    QSignalSpy spy(&ctrl, &DesktopTrayIconController::visibleChanged);

    // Queue a deferred show, then immediately cancel it before the event loop runs.
    ctrl.setVisible(true);
    ctrl.setVisible(false);

    QCOMPARE(ctrl.visible(), false);

    // visibleChanged(false) was emitted by the cancelling setVisible(false) call
    // only if visible was true. Since it wasn't, the count is 0 here.
    // The key property: process the event loop and confirm no stale true emission.
    QTest::qWait(50);
    for (int i = 0; i < spy.count(); ++i) {
        const bool emitted_value = spy.at(i).at(0).toBool();
        QVERIFY2(!emitted_value,
                 "visibleChanged(true) must not be emitted after cancellation");
    }
    QCOMPARE(ctrl.visible(), false);
}

// ── index.theme and icon-name correctness (Linux-only) ───────────────────────

#if defined(Q_OS_LINUX)

// Helper: redirect GenericDataLocation to a temp dir for the duration of a test.
// QStandardPaths on Linux honours $XDG_DATA_HOME; Qt does not cache the result.
struct XdgDataHomeOverride {
    QByteArray old_val;
    bool was_set;

    explicit XdgDataHomeOverride(const QString& path)
    {
        old_val = qgetenv("XDG_DATA_HOME");
        was_set = !old_val.isNull();
        qputenv("XDG_DATA_HOME", path.toLocal8Bit());
    }
    ~XdgDataHomeOverride()
    {
        if (was_set) {
            qputenv("XDG_DATA_HOME", old_val);
        } else {
            qunsetenv("XDG_DATA_HOME");
        }
    }
};

void DesktopTrayIconControllerTests::indexTheme_createsValidFileWhenMissing()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    XdgDataHomeOverride env_guard(tmp.path());

    DesktopTrayIconController ctrl;
    QPixmap pixmap(48, 48);
    pixmap.fill(Qt::red);
    ctrl.setIcon(QIcon(pixmap));

    const QString theme_path =
        tmp.path() + QStringLiteral("/icons/hicolor/index.theme");
    QVERIFY2(QFile::exists(theme_path),
             "index.theme must be created when missing");

    QFile f(theme_path);
    QVERIFY(f.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString content = QTextStream(&f).readAll();

    QVERIFY2(content.contains(QStringLiteral("Directories=")),
             "index.theme must contain a Directories= line");
    QVERIFY2(content.contains(QStringLiteral("48x48/apps")),
             "index.theme Directories= must include 48x48/apps");
    QVERIFY2(content.contains(QStringLiteral("[48x48/apps]")),
             "index.theme must contain a [48x48/apps] stanza");
}

void DesktopTrayIconControllerTests::indexTheme_updatesDirectoriesLineInExistingFile()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    XdgDataHomeOverride env_guard(tmp.path());

    // Pre-create an index.theme that has Directories= but NOT 48x48/apps.
    const QString icons_dir = tmp.path() + QStringLiteral("/icons/hicolor");
    QVERIFY(QDir().mkpath(icons_dir));
    const QString theme_path = icons_dir + QStringLiteral("/index.theme");
    {
        QFile f(theme_path);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream(&f) << "[Icon Theme]\nName=hicolor\nDirectories=32x32/apps\n\n"
                        << "[32x32/apps]\nSize=32\nContext=Applications\nType=Fixed\n";
    }

    DesktopTrayIconController ctrl;
    QPixmap pixmap(48, 48);
    pixmap.fill(Qt::blue);
    ctrl.setIcon(QIcon(pixmap));

    QFile f(theme_path);
    QVERIFY(f.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString content = QTextStream(&f).readAll();

    QVERIFY2(content.contains(QStringLiteral("48x48/apps")),
             "After setIcon(), 48x48/apps must appear in index.theme");
    QVERIFY2(content.contains(QStringLiteral("[48x48/apps]")),
             "A [48x48/apps] stanza must be appended");

    // Find the Directories= line and verify 48x48/apps is on it.
    bool dirs_line_has_48 = false;
    for (const QString& line : content.split(QLatin1Char('\n'))) {
        if (line.startsWith(QStringLiteral("Directories="))) {
            dirs_line_has_48 = line.contains(QStringLiteral("48x48/apps"));
            break;
        }
    }
    QVERIFY2(dirs_line_has_48,
             "The Directories= line itself must list 48x48/apps");
}

void DesktopTrayIconControllerTests::indexTheme_preservesExistingDirectoriesEntries()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    XdgDataHomeOverride env_guard(tmp.path());

    const QString icons_dir = tmp.path() + QStringLiteral("/icons/hicolor");
    QVERIFY(QDir().mkpath(icons_dir));
    const QString theme_path = icons_dir + QStringLiteral("/index.theme");
    {
        QFile f(theme_path);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream(&f) << "[Icon Theme]\nName=hicolor\nDirectories=32x32/apps,22x22/apps\n\n"
                        << "[32x32/apps]\nSize=32\nContext=Applications\nType=Fixed\n"
                        << "[22x22/apps]\nSize=22\nContext=Applications\nType=Fixed\n";
    }

    DesktopTrayIconController ctrl;
    QPixmap pixmap(48, 48);
    pixmap.fill(Qt::green);
    ctrl.setIcon(QIcon(pixmap));

    QFile f(theme_path);
    QVERIFY(f.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString content = QTextStream(&f).readAll();

    // Existing entries must be preserved.
    bool dirs_has_all = false;
    for (const QString& line : content.split(QLatin1Char('\n'))) {
        if (line.startsWith(QStringLiteral("Directories="))) {
            dirs_has_all = line.contains(QStringLiteral("32x32/apps"))
                        && line.contains(QStringLiteral("22x22/apps"))
                        && line.contains(QStringLiteral("48x48/apps"));
            break;
        }
    }
    QVERIFY2(dirs_has_all,
             "Directories= must retain existing entries and add 48x48/apps");
}

void DesktopTrayIconControllerTests::setIcon_setsNamedIcon()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    XdgDataHomeOverride env_guard(tmp.path());

    DesktopTrayIconController ctrl;
    QPixmap pixmap(48, 48);
    pixmap.fill(Qt::cyan);
    ctrl.setIcon(QIcon(pixmap));

    // Both the main icon and its '-panel' monochrome variant (used by AppIndicator
    // for panel theming) must be written to the expected XDG location.
    const QString apps_dir =
        tmp.path() + QStringLiteral("/icons/hicolor/48x48/apps");
    QVERIFY2(QFile::exists(apps_dir + QStringLiteral("/bitcoin-core.png")),
             "bitcoin-core.png must be written to ~/.local/share/icons/hicolor/48x48/apps/");
    QVERIFY2(QFile::exists(apps_dir + QStringLiteral("/bitcoin-core-panel.png")),
             "bitcoin-core-panel.png must be written to ~/.local/share/icons/hicolor/48x48/apps/");

    // Verify the written PNGs are non-empty valid images.
    QPixmap loaded;
    QVERIFY2(loaded.load(apps_dir + QStringLiteral("/bitcoin-core.png")),
             "Written bitcoin-core.png must be a valid PNG");
    QVERIFY(!loaded.isNull());
}

#endif // Q_OS_LINUX

int RunDesktopTrayIconControllerTests(int argc, char* argv[])
{
    DesktopTrayIconControllerTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#ifndef BITCOINQML_NO_TEST_MAIN
QTEST_MAIN(DesktopTrayIconControllerTests)
#endif
#include "test_desktoptrayiconcontroller.moc"
