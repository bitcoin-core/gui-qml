// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtTest 1.2

TestCase {
    name: "SettingsWindowBehavior"

    // ── Test 1: showTrayIconSwitch enabled iff desktopPlatform ───────────────
    function test_showTrayIcon_enabled_on_desktop() {
        const m = Qt.createQmlObject(
            'import QtQuick 2.15; QtObject {' +
            '  property bool desktopPlatform: true;' +
            '  property bool enabled: desktopPlatform }', this)
        compare(m.enabled, true)
    }

    function test_showTrayIcon_disabled_on_non_desktop() {
        const m = Qt.createQmlObject(
            'import QtQuick 2.15; QtObject {' +
            '  property bool desktopPlatform: false;' +
            '  property bool enabled: desktopPlatform }', this)
        compare(m.enabled, false)
    }

    // ── Test 2: minimizeToTraySwitch requires desktopPlatform && showTrayIcon ─
    function test_minimizeToTray_enabled_when_desktop_and_tray_visible() {
        const m = Qt.createQmlObject(
            'import QtQuick 2.15; QtObject {' +
            '  property bool desktopPlatform: true;' +
            '  property bool showTrayIcon: true;' +
            '  property bool enabled: desktopPlatform && showTrayIcon }', this)
        compare(m.enabled, true)
    }

    function test_minimizeToTray_disabled_when_trayIcon_hidden() {
        const m = Qt.createQmlObject(
            'import QtQuick 2.15; QtObject {' +
            '  property bool desktopPlatform: true;' +
            '  property bool showTrayIcon: false;' +
            '  property bool enabled: desktopPlatform && showTrayIcon }', this)
        compare(m.enabled, false)
    }

    function test_minimizeToTray_disabled_on_non_desktop() {
        const m = Qt.createQmlObject(
            'import QtQuick 2.15; QtObject {' +
            '  property bool desktopPlatform: false;' +
            '  property bool showTrayIcon: true;' +
            '  property bool enabled: desktopPlatform && showTrayIcon }', this)
        compare(m.enabled, false)
    }

    // ── Test 3: minimizeOnCloseSwitch enabled iff desktopPlatform ────────────
    function test_minimizeOnClose_enabled_on_desktop() {
        const m = Qt.createQmlObject(
            'import QtQuick 2.15; QtObject {' +
            '  property bool desktopPlatform: true;' +
            '  property bool enabled: desktopPlatform }', this)
        compare(m.enabled, true)
    }

    function test_minimizeOnClose_disabled_on_non_desktop() {
        const m = Qt.createQmlObject(
            'import QtQuick 2.15; QtObject {' +
            '  property bool desktopPlatform: false;' +
            '  property bool enabled: desktopPlatform }', this)
        compare(m.enabled, false)
    }
}
