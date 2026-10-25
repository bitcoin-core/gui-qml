// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15

Palette {
    paletteId: "core-light"
    displayName: qsTr("Light")
    isDark: false

    neutral0: "#FFFFFF"
    neutral1: "#F6F6F6"
    neutral2: "#EEEEEE"
    neutral3: "#E2E2E2"
    neutral4: "#C4C4C4"
    neutral5: "#BBBBBB"
    neutral6: "#999999"
    neutral7: "#6B6B6B"
    neutral8: "#404040"
    neutral9: "#000000"

    accent: "#F7931A"
    focus: "#B763D7"
    danger: "#EB5757"
    success: "#27AE60"
    info: "#2D9CDB"
    warning: "#726700"
    trafficSent: "#B763D7"
    trafficReceived: "#2D9CDB"
    txInternal: "#B763D7"
    paymentRequest: "#6877FD"

    accentHover: "#FFAD4A"
    accentPressed: "#DE8417"
    // Darker variants for text: 4.5:1 on background and surface1, and on
    // their tint over both (accentText only over background).
    accentText: "#A15B06"
    dangerText: "#C51717"
    successText: "#1A7440"
    infoText: "#1A6B99"
    warningText: "#6D6200"
    paymentRequestText: "#364AFC"
    // neutral6 is under 3:1 on surface2 in light.
    placeholder: neutral7
    controlBorder: neutral7
    // neutral2 is barely distinguishable from the neutral1 menu surface in light.
    menuBorder: neutral3
    syntaxKey: "#3A7D2C"
    surface1BorderGradient: ["#F5F5F5", "#E6E6E6"]
    surface2BorderGradient: ["#EDEDED", "#DEDEDE"]
    accentBorderGradient: ["#FFB33A", "#FF9C23"]
    dangerBorderGradient: ["#FF7474", "#F36262"]
}
