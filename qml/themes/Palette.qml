// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15

QtObject {
    required property string paletteId
    required property string displayName
    required property bool isDark

    // Base scale, from background (neutral0) to foreground (neutral9)
    required property color neutral0
    required property color neutral1
    required property color neutral2
    required property color neutral3
    required property color neutral4
    required property color neutral5
    required property color neutral6
    required property color neutral7
    required property color neutral8
    required property color neutral9

    // Base hues
    required property color orange
    required property color red
    required property color green
    required property color blue
    required property color amber
    required property color purple
    required property color lavender

    // Surfaces, numbered by nesting depth
    property color background: neutral0
    property color surface1: neutral1
    property color surface2: neutral2
    property color surface3: neutral3

    function surface(level) {
        return [background, surface1, surface2, surface3][Math.max(0, Math.min(level, 3))]
    }

    // States and dividers are translucent foreground, so they read on any surface.
    // Qt.rgba instead of Qt.alpha(), which is newer than Qt 6.2.
    function foreground(alpha) {
        return Qt.rgba(neutral9.r, neutral9.g, neutral9.b, alpha)
    }

    property color surfaceHover: foreground(isDark ? 0.0675 : 0.0325)
    property color surfacePressed: foreground(isDark ? 0.16 : 0.0813)
    property color surfaceSelected: surfaceHover
    property color surfaceDivider: foreground(isDark ? 0.0675 : 0.0813)
    property color menuBorder: neutral2
    property color menuDivider: menuBorder
    property color modalOverlay: Qt.rgba(0, 0, 0, 0.5)

    // Semantic roles
    property color accent: orange
    property color accentHover: Qt.lighter(accent, 1.1)
    property color accentPressed: Qt.darker(accent, 1.1)
    property color accentText: accent
    property color accentForeground: "#FFFFFF"
    property color focus: purple
    property color danger: red
    property color success: green
    property color info: blue
    property color warning: amber
    property color txSent: purple
    property color txReceived: blue
    property color placeholder: neutral6
    property color disabledContent: neutral4
    property color selection: accent
    property color selectedText: accentForeground
    property color syntaxKey: success

    // Border gradients, top to bottom
    property var surface1BorderGradient: [Qt.tint(surface1, foreground(isDark ? 0.09 : 0.004)),
                                          Qt.tint(surface1, foreground(isDark ? 0.04 : 0.066))]
    property var surface2BorderGradient: [Qt.tint(surface2, foreground(isDark ? 0.09 : 0.004)),
                                          Qt.tint(surface2, foreground(isDark ? 0.04 : 0.066))]
    property var accentBorderGradient: [Qt.lighter(accent, 1.15), Qt.lighter(accent, 1.07)]
    property var dangerBorderGradient: [Qt.lighter(danger, 1.15), Qt.lighter(danger, 1.07)]

    property var confirmationColors: [
        "#FF1C1C", // red
        "#ED6E46",
        "#EE8847",
        "#EFA148",
        "#F0BB49",
        "#F1D54A", // yellow
    ]

    // Names from before the semantic roles; call sites move to the roles above.
    readonly property color white: accentForeground
    readonly property color orangeLight1: accentHover
    readonly property color orangeLight2: accentPressed
    readonly property var neutral1SurfaceBorderGradient: surface1BorderGradient
    readonly property var neutral2SurfaceBorderGradient: surface2BorderGradient
    readonly property var primaryButtonBorderGradient: accentBorderGradient
    readonly property var destructiveButtonBorderGradient: dangerBorderGradient
}
