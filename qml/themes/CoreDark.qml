// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15

Palette {
    paletteId: "core-dark"
    displayName: qsTr("Dark")
    isDark: true

    neutral0: "#000000"
    neutral1: "#121212"
    neutral2: "#222222"
    neutral3: "#383838"
    neutral4: "#5C5C5C"
    neutral5: "#787878"
    neutral6: "#949494"
    neutral7: "#B0B0B0"
    neutral8: "#CCCCCC"
    neutral9: "#FFFFFF"

    accent: "#F89B2A"
    focus: "#C075DC"
    danger: "#EC6363"
    success: "#36B46B"
    info: "#3CA3DE"
    warning: "#C9B500"
    trafficSent: "#C075DC"
    trafficReceived: "#3CA3DE"
    txInternal: "#C075DC"
    paymentRequest: "#6877FD"

    accentHover: "#FFAD4A"
    accentPressed: "#DF8C26"
    // paymentRequest is under 4.5:1 on its tint over surface1.
    paymentRequestText: "#7280FD"
    syntaxKey: "#98C379"
    surface1BorderGradient: ["#272727", "#1B1B1B"]
    surface2BorderGradient: ["#373737", "#2B2B2B"]
    accentBorderGradient: ["#FFBA4A", "#FFA433"]
    dangerBorderGradient: ["#FF8080", "#F36D6D"]
}
