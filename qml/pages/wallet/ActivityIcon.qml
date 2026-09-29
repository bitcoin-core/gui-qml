// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import org.bitcoincore.qt 1.0
import "../../controls"

Rectangle {
    id: root
    property int activityType: TransactionActivityModel.Other
    property bool incoming: false
    property bool pending: false
    property bool inactive: false
    property bool paymentRequest: false
    property bool solid: false
    property int iconSize: 24
    readonly property bool multiple: !paymentRequest && activityType === TransactionActivityModel.Multiple
    readonly property bool dashed: (pending || paymentRequest) && !inactive && !solid
    readonly property bool neutral: !paymentRequest && activityType === TransactionActivityModel.Other
    readonly property color accent: {
        if (inactive) return Theme.color.neutral6
        if (paymentRequest) return Theme.color.lavender
        if (activityType === TransactionActivityModel.Consolidation
            || activityType === TransactionActivityModel.Split
            || activityType === TransactionActivityModel.InternalTransfer) return Theme.color.purple
        if (neutral) return Theme.color.neutral7
        return incoming ? Theme.color.green : Theme.color.orange
    }
    readonly property url iconSource: {
        if (paymentRequest) return "qrc:/icons/activity-payment-request.svg"
        switch (activityType) {
        case TransactionActivityModel.Consolidation: return "qrc:/icons/activity-consolidation.svg"
        case TransactionActivityModel.Split: return "qrc:/icons/activity-split.svg"
        case TransactionActivityModel.InternalTransfer: return "qrc:/icons/activity-internal.svg"
        case TransactionActivityModel.Mined: return "qrc:/icons/coinbase.svg"
        case TransactionActivityModel.Other: return "qrc:/icons/file.svg"
        default: return "qrc:/icons/activity-" + (incoming ? "receive" : "send") + ".svg"
        }
    }
    implicitWidth: 44
    implicitHeight: 44
    radius: width / 2
    color: inactive ? Theme.color.neutral2 : dashed ? "transparent"
        : neutral ? Theme.color.neutral2
        : Qt.rgba(accent.r, accent.g, accent.b,
            activityType === TransactionActivityModel.Send || (multiple && !incoming) ? 79 / 255 : 64 / 255)

    Canvas {
        id: pendingRing
        objectName: "activityPendingRing"
        anchors.fill: parent
        visible: root.dashed
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            const radius = (Math.min(width, height) - 1) / 2
            if (radius <= 0) return
            ctx.strokeStyle = root.accent
            ctx.lineWidth = 1
            ctx.lineCap = "round"
            // Equal dash/gap lengths keep the 22 dashes evenly spaced at every size.
            const step = 2 * Math.PI / 22
            for (let i = 0; i < 22; ++i) {
                const start = -Math.PI / 2 + i * step
                ctx.beginPath()
                ctx.arc(width / 2, height / 2, radius, start, start + step / 2)
                ctx.stroke()
            }
        }
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        onVisibleChanged: if (visible) requestPaint()
        Connections {
            target: root
            function onAccentChanged() { pendingRing.requestPaint() }
        }
    }

    Icon {
        anchors.centerIn: parent
        source: root.iconSource
        color: root.accent
        size: root.iconSize
    }
}
