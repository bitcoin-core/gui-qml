// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Shapes 1.15
import "../controls"
import "TransactionFlowLayout.js" as FlowLayout

ShapePath {
    id: root
    required property var ribbon
    property color sourceColor: "transparent"
    property color targetColor: "transparent"
    strokeWidth: ribbon.proportional || ribbon.inferredWidth ? -1 : (ribbon.thickness || 0)
    strokeColor: ribbon.proportional || ribbon.inferredWidth ? "transparent"
        : ribbon.targetKind === "data" ? Theme.color.neutral2 : Theme.color.neutral3
    strokeStyle: ShapePath.SolidLine
    capStyle: ShapePath.RoundCap
    // The geometry renderer culls transparent fills even with a gradient.
    // Inferred input bands retain their node color across the entire band.
    fillColor: ribbon.proportional || ribbon.inferredWidth ? sourceColor : "transparent"
    fillGradient: ribbon.proportional ? gradient : null

    property LinearGradient gradient: LinearGradient {
        x1: root.ribbon.startX
        y1: 0
        x2: root.ribbon.endX
        y2: 0
        GradientStop { position: 0; color: root.sourceColor }
        GradientStop { position: 1; color: root.targetColor }
    }
    PathSvg { path: FlowLayout.path(root.ribbon) }
}
