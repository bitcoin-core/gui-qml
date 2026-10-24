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

    orange: "#F7931A"
    red: "#EB5757"
    green: "#27AE60"
    blue: "#2D9CDB"
    amber: "#726700"
    purple: "#B763D7"
    lavender: "#6877FD"

    accentHover: "#FFAD4A"
    accentPressed: "#FFBF72"
    // neutral2 is barely distinguishable from the neutral1 menu surface in light.
    menuBorder: neutral3
    syntaxKey: "#3A7D2C"
    surface1BorderGradient: ["#F5F5F5", "#E6E6E6"]
    surface2BorderGradient: ["#EDEDED", "#DEDEDE"]
    accentBorderGradient: ["#FFB33A", "#FF9C23"]
    dangerBorderGradient: ["#FF7474", "#F36262"]
}
