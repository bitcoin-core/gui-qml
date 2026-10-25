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

    // Dark alphas are strong enough to show on background (black) as well as on cards.
    property color surfaceHover: foreground(isDark ? 0.1 : 0.045)
    property color surfacePressed: foreground(isDark ? 0.16 : 0.0813)
    // Between hover and pressed, so a hovered row still differs from the selected one.
    property color surfaceSelected: foreground(isDark ? 0.13 : 0.065)
    property color surfaceDivider: foreground(isDark ? 0.1 : 0.0813)
    property color menuBorder: neutral2
    property color menuDivider: menuBorder
    property color modalOverlay: Qt.rgba(0, 0, 0, 0.5)

    // Semantic roles. Each palette sets the required ones; the rest derive from them.
    // Text and icons use the neutral ramp directly: neutral9 primary,
    // neutral8 menu items and labels, neutral7 secondary, neutral6 tertiary.
    required property color accent
    property color accentHover: Qt.lighter(accent, 1.1)
    property color accentPressed: Qt.darker(accent, 1.1)
    // Orange text and links; may be darker than accent to stay readable.
    property color accentText: accent
    // Content on accent, danger and success fills.
    property color accentForeground: "#FFFFFF"
    required property color focus
    required property color danger
    property color dangerHover: Qt.lighter(danger, 1.1)
    property color dangerPressed: Qt.darker(danger, 1.1)
    property color dangerText: danger
    required property color success
    property color successText: success
    required property color info
    property color infoText: info
    required property color warning
    property color warningText: warning
    property color warningForeground: neutral0
    // Network traffic and peer byte counts.
    required property color trafficSent
    required property color trafficReceived
    // Wallet activity: money in and out, transfers within the wallet, payment requests.
    property color txIncoming: successText
    property color txOutgoing: accent
    required property color txInternal
    required property color paymentRequest
    property color paymentRequestText: paymentRequest
    // A control that is switched on, such as an applied filter. Neutral; accent fills are for primary actions.
    property color activeFill: neutral9
    property color activeForeground: background
    property color placeholder: neutral6
    property color disabledContent: neutral4
    // Checkbox and field borders, slider thumb rims, the off track of a switch.
    property color controlBorder: neutral5
    property color sliderTrack: controlBorder
    property color selection: Qt.tint(background, Qt.rgba(accent.r, accent.g, accent.b, 0.35))
    property color selectedText: neutral9
    property color syntaxKey: success
    // QR codes stay dark on light in every palette, so scanners read them.
    property color qrBackground: "#FFFFFF"
    property color qrForeground: "#000000"
    // Onboarding illustration: the signal pulse and the newest block.
    property color illustrationSignal: "#F9D63C"
    property color illustrationLatestBlock: confirmationColors[1]

    // Tints: a role at one alpha, for pills, banners and icon discs.
    // Text on a tint uses the matching *Text role.
    property real tintAlpha: 0.16
    function tint(color) {
        return Qt.rgba(color.r, color.g, color.b, tintAlpha)
    }
    // The text role for a hue role, for components that take a tint color.
    function textFor(color) {
        const pairs = [[accent, accentText], [danger, dangerText], [success, successText],
                       [info, infoText], [warning, warningText], [paymentRequest, paymentRequestText]]
        for (const pair of pairs) {
            if (Qt.colorEqual(color, pair[0])) return pair[1]
        }
        return color
    }
    property color accentTint: tint(accent)
    property color dangerTint: tint(danger)
    property color successTint: tint(success)
    property color infoTint: tint(info)
    property color warningTint: tint(warning)
    property color paymentRequestTint: tint(paymentRequest)

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
}
