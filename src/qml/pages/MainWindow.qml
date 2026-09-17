// Copyright (c) 2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0
import "../components"
import "../controls"
import "./node"

ApplicationWindow {
    id: appWindow
    objectName: "mainWindow"
    readonly property string nodeStatus: nodeLifecycleModel.statusText
    title: qsTr("Bitcoin Core")
    minimumWidth: 800
    minimumHeight: 665
    color: Theme.color.background
    palette.window: Theme.color.neutral1 // Context menu background
    palette.windowText: Theme.color.neutral8 // Menu item text and icons
    palette.dark: Theme.dark ? Theme.color.neutral2 : Theme.color.neutral3 // Menu border
    palette.mid: Theme.dark ? Theme.color.neutral2 : Theme.color.neutral3 // Menu separators
    palette.light: Theme.color.neutral3 // Highlighted menu item background
    palette.midlight: Theme.color.neutral3 // Pressed menu item background
    palette.disabled.windowText: Theme.color.neutral4 // Disabled item text and icons
    palette.disabled.light: Theme.color.neutral1 // Disabled item highlight background
    palette.disabled.midlight: Theme.color.neutral1 // Disabled item pressed background

    // The window starts hidden and is shown at the end of Component.onCompleted,
    // after the saved size has been applied (below). Applying the size while
    // hidden, before the first expose, lays the scene out exactly once at the
    // final size. That avoids the Qt 6.4.x Quick Layouts crash from resizing an
    // already-laid-out scene, and it avoids a live size binding pinned to
    // minimumWidth during construction (which loops popup layout on Qt 6.4).
    visible: false
    width: minimumWidth
    height: minimumHeight
    AppSettings {
        id: windowSettings
        property real windowX
        property real windowY
        property real windowWidth
        property real windowHeight
    }

    onXChanged: if (visibility === Window.Windowed) windowSettings.windowX = x
    onYChanged: if (visibility === Window.Windowed) windowSettings.windowY = y
    onWidthChanged: if (visibility === Window.Windowed) windowSettings.windowWidth = width
    onHeightChanged: if (visibility === Window.Windowed) windowSettings.windowHeight = height

    Behavior on color {
        ColorAnimation { duration: 150 }
    }

    // Tracks the previous visibility state to distinguish a user-initiated minimize
    // (Windowed → Minimized) from a WM restore transition (Hidden → Minimized).
    // The latter occurs when showNormal() calls setVisible(true) before
    // setWindowState(NoState); without this guard the onVisibilityChanged handler
    // would immediately re-hide the window, making tray-icon restore a no-op.
    property int m_prevVisibility: Window.AutomaticVisibility

    onClosing: (close) => {
        close.accepted = false
        if (desktopWindowBehaviorModel.shouldMinimizeWindowOnClose()) {
            showMinimized()
        } else {
            nodeLifecycleModel.requestShutdown()
        }
    }

    onVisibilityChanged: function(visibility) {
        const prev = m_prevVisibility
        m_prevVisibility = visibility

        // Capture geometry once the window reaches its normal (Windowed) state.
        // The onWidthChanged/onHeightChanged writes only fire on a *change*, and
        // the size is set before the window is Windowed (the onCompleted self-
        // assign that breaks the size binding doesn't re-trigger them), so without
        // this the initial size is never saved (windowWidth/Height stay 0).
        if (visibility === Window.Windowed) {
            windowSettings.windowX = x
            windowSettings.windowY = y
            windowSettings.windowWidth = width
            windowSettings.windowHeight = height
        }

        if (visibility === Window.Minimized &&
                prev !== Window.Hidden &&
                desktopWindowBehaviorModel.shouldHideToTrayOnMinimize()) {
            desktopTrayIconController.hideMainWindow()
        }
    }

    Connections {
        target: desktopWindowBehaviorModel
        function onShowTrayIconChanged(show) {
            desktopTrayIconController.visible = AppMode.isDesktop && show
        }
    }

    Binding {
        target: desktopTrayIconController
        property: "windowVisible"
        value: appWindow.visible &&
               appWindow.visibility !== Window.Hidden &&
               appWindow.visibility !== Window.Minimized
    }

    Connections {
        target: desktopTrayIconController
        function onShowRequested() {
            desktopTrayIconController.showMainWindow()
        }
        function onHideRequested() {
            desktopTrayIconController.hideMainWindow()
        }
        function onQuitRequested() {
            nodeLifecycleModel.requestShutdown()
        }
    }

    ApplicationShell {
        anchors.fill: parent
    }

    NodeRuntimeDialog {
        parent: Overlay.overlay
    }

    NodeFatalErrorPopup {
        parent: Overlay.overlay
    }

    Component.onCompleted: {
        // Apply the saved geometry once, while the window is still hidden, then
        // show it. Sizing before the first expose lays the scene out a single
        // time at the final size: there is no live size binding to oscillate and
        // no resize of an already-laid-out scene (which crashes Qt 6.4 Quick
        // Layouts). Position is restored imperatively; moving a window does not
        // relayout its contents.
        if (windowSettings.windowWidth > 0) {
            width = windowSettings.windowWidth
        }
        if (windowSettings.windowHeight > 0) {
            height = windowSettings.windowHeight
        }
        if (windowSettings.windowWidth > 0 && windowSettings.windowHeight > 0) {
            x = windowSettings.windowX
            y = windowSettings.windowY
        }
        visible = true
    }

}
