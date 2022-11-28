import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtGraphicalEffects 1.0

import "../components"

Item {
    id: root

    Layout.alignment: Qt.AlignCenter
    width: size
    height: size

    property real ringProgress: 0
    property string header
    property string subText
    property bool synced
    property bool pause
    property bool conns

    property int headerSize: 32

    property int size: 200
    property real arcBegin: 0
    property real arcEnd: ringProgress * 360
    property real lineWidth: 4
    property string colorCircle: "#f1d54a"
    property string colorBackground: Theme.color.neutral2

    property variant blockList: []
    property variant colorList: ["#EC5445", "#ED6E46", "#EE8847", "#EFA148", "#F0BB49", "#F1D54A"]

    property alias beginAnimation: animationArcBegin.enabled
    property alias endAnimation: animationArcEnd.enabled

    property int animationDuration: 250

    onArcBeginChanged: canvas.requestPaint()
    onArcEndChanged: canvas.requestPaint()
    onSyncedChanged: canvas.requestPaint()
    onBlockListChanged: canvas.requestPaint()

    Behavior on arcBegin {
       id: animationArcBegin
       enabled: true
       NumberAnimation {
           duration: root.animationDuration
           easing.type: Easing.InOutCubic
       }
    }

    Behavior on arcEnd {
       id: animationArcEnd
       enabled: true
       NumberAnimation {
            easing.type: Easing.Bezier
            easing.bezierCurve: [0.5, 0.0, 0.2, 1, 1, 1]
            duration: root.animationDuration
        }
    }

    Canvas {
        id: canvas
        anchors.fill: parent
        rotation: -90

        onPaint: {
            var ctx = getContext("2d")
            var x = width / 2
            var y = height / 2

            ctx.reset()

            // Paint background
            ctx.beginPath();
            ctx.arc(x, y, (width / 2) - parent.lineWidth / 2, 0, Math.PI * 2, false)
            ctx.lineWidth = root.lineWidth
            ctx.strokeStyle = root.colorBackground
            ctx.stroke()

            if (!synced) {
                var start = Math.PI * (parent.arcBegin / 180)
                var end = Math.PI * (parent.arcEnd / 180)
                // Paint foreground arc
                ctx.beginPath();
                ctx.arc(x, y, (width / 2) - parent.lineWidth / 2, start, end, false)
                ctx.lineWidth = root.lineWidth
                ctx.strokeStyle = root.colorCircle
                ctx.stroke()
            }

            else {
                var del = 0.0025
                // Paint Block time points
                for (var i=1; i<parent.blockList.length - 1; i++) {
                    var starts = Math.PI * ((parent.blockList[i]) * 360 / 180)
                    var ends = Math.PI * ((parent.blockList[i + 1]) * 360 / 180)
                    var conf = blockList.length - i - 1
                    ctx.beginPath();
                    ctx.arc(x, y, (width / 2) - parent.lineWidth / 2, starts, ends, false)
                    ctx.lineWidth = root.lineWidth
                    ctx.strokeStyle = conf > 5 ? colorList[5] : colorList[conf]
                    ctx.stroke()

                    // Paint dark segments
                    var start = Math.PI * ((parent.blockList[i + 1] - del) * 360 / 180)
                    ctx.beginPath();
                    ctx.arc(x, y, (width / 2) - parent.lineWidth/2, start, ends, false)
                    ctx.lineWidth = 4
                    ctx.strokeStyle = root.colorBackground
                    ctx.stroke();
                }

                // Print last segment
                var starts = Math.PI * ((parent.blockList[parent.blockList.length - 1]) * 360 / 180)
                var ends = Math.PI * (ringProgress * 360 / 180)

                ctx.beginPath();
                ctx.arc(x, y, (width / 2) - parent.lineWidth / 2, starts, ends, false)
                ctx.lineWidth = root.lineWidth
                ctx.strokeStyle = colorList[0];
                ctx.stroke()
            }
        }
    }

    ColumnLayout {
        anchors.centerIn: root
        Image {
            Layout.alignment: Qt.AlignCenter
            source: "image://images/bitcoin-circle"
            sourceSize.width: 40
            sourceSize.height: 40
            Layout.bottomMargin: 7
            ColorOverlay {
                anchors.fill: parent
                source: parent
                color: Theme.color.neutral9
            }
        }
        Header {
            Layout.fillWidth: true
            header: root.header
            headerSize: root.headerSize
            headerBold: true
            description: root.subText
            descriptionMargin: 2
            descriptionColor: Theme.color.neutral4
            descriptionBold: true
            Layout.bottomMargin: 14
        }
        // TODO: Replace following with PeersIndicator{} once it is incorporated.
        RowLayout {
            Layout.alignment: Qt.AlignCenter
            spacing: 5
            Repeater {
                model: 5
                Rectangle {
                    width: 3
                    height: width
                    radius: width/2
                    color: Theme.color.neutral9
                }
            }
        }
    }

    MouseArea {
        anchors.fill: canvas
        onClicked: {
            nodeModel.pause = !pause
        }
    }
}
