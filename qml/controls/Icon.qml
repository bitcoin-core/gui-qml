// Copyright (c) 2023-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15

Button {
    id: root
    width: icon.width
    height: icon.height
    required property color color
    required property url source
    property int size: 32
    focusPolicy: Qt.NoFocus
    padding: 0
    icon.source: root.source
    icon.color: root.color
    icon.height: root.size
    icon.width: root.size
    enabled: false
    background: null
    // Decorative: the control that shows the icon carries the name.
    Accessible.ignored: true

    // A fade that runs while the image changes leaves the new image at an
    // in-between color on Qt 6.4, so a new image takes its color at once.
    // Color-only changes, such as hover, still fade.
    property bool _sourceChanging: false
    onSourceChanged: {
        _sourceChanging = true
        // The color may have changed just before the image, which started a
        // fade: stop it and apply the color again, now without a fade.
        colorFade.stop()
        root.icon.color = Qt.binding(function() { return root.color })
        Qt.callLater(function() { root._sourceChanging = false })
    }

    Behavior on icon.color {
        enabled: Theme.motion.animated && !root._sourceChanging
        ColorAnimation {
            id: colorFade
            duration: Theme.motion.normal
        }
    }
}
