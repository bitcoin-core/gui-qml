// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick
import QtTest

TestCase {
    name: "MainWindow"
    when: windowShown
    width: 320
    height: 200

    property var mainWindow
    property var mainWindowComponent

    function initTestCase() {
        mainWindowComponent = Qt.createComponent("qrc:///qml/pages/MainWindow.qml")
        compare(mainWindowComponent.status, Component.Ready, mainWindowComponent.errorString())
        mainWindow = mainWindowComponent.createObject(null)
        verify(mainWindow, mainWindowComponent.errorString())
    }

    function cleanupTestCase() {
        mainWindow.destroy()
    }

    function test_defaults() {
        compare(mainWindow.objectName, "mainWindow")
        compare(mainWindow.title, "Bitcoin Core")
        compare(mainWindow.nodeStatus, "Not connected")
    }
}
