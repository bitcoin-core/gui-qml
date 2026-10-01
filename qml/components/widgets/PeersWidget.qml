// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Layouts 1.15
import "../../controls"

DashboardWidget {
    id: root
    objectName: "peersWidget"
    property var peerModel: typeof peerTableModel !== "undefined" ? peerTableModel : null
    readonly property var snapshot: peerModel && peerModel.summary ? peerModel.summary : ({ready: false})
    readonly property bool ready: snapshot.ready === true
    readonly property int total: ready ? snapshot.total : 0
    readonly property int inbound: ready ? snapshot.inbound : 0
    readonly property int outbound: ready ? snapshot.outbound : 0
    readonly property real inboundFraction: ready && total > 0 ? inbound / total : 0
    readonly property var groups: ready ? (snapshot.groups || []).map(function(group) {
        return {id: group.id, value: group.count, label: root.groupLabel(group.id), color: root.groupColor(group.id)}
    }) : []
    activityTarget: peerModel
    activityProperty: "widgetActive"
    //: Accessible summary of the Peers dashboard widget. The placeholders are total, inbound and outbound peer counts.
    Accessible.description: ready
        ? qsTr("%1 connected peers: %2 inbound and %3 outbound.")
            .arg(number(total)).arg(number(inbound)).arg(number(outbound))
        //: Accessible status of the Peers widget when the node cannot provide peer statistics.
        : qsTr("Peer information unavailable.")

    function number(value) { return Number(value).toLocaleString(Qt.locale(), 'f', 0) }
    function groupLabel(id) {
        switch (id) {
        //: Network name in the Peers widget chart legend. Keep the technical name IPv4.
        case "ipv4": return qsTr("IPv4")
        //: Network name in the Peers widget chart legend. Keep the technical name IPv6.
        case "ipv6": return qsTr("IPv6")
        //: Network name in the Peers widget chart legend. Keep the technical name Tor.
        case "tor": return qsTr("Tor")
        //: Network name in the Peers widget chart legend. Keep the technical name I2P.
        case "i2p": return qsTr("I2P")
        //: Network name in the Peers widget chart legend. Keep the technical name CJDNS.
        case "cjdns": return qsTr("CJDNS")
        //: Legend category for connections outside the named networks, including local or unroutable addresses.
        default: return qsTr("Other")
        }
    }
    function groupColor(id) {
        switch (id) {
        case "ipv4": return Theme.color.orange
        case "ipv6": return Theme.color.purple
        case "tor": return Theme.color.red
        case "i2p": return Theme.color.neutral9
        case "cjdns": return Theme.color.amber
        default: return Theme.color.lavender
        }
    }

    function directionText(index) {
        //: Peers widget direction count. %1 is the number of incoming connections.
        if (index === 0) return qsTr("%1 inbound").arg(root.ready ? root.number(root.inbound) : "—")
        //: Peers widget direction count. %1 is the number of outgoing connections.
        return qsTr("%1 outbound").arg(root.ready ? root.number(root.outbound) : "—")
    }
    readonly property bool stackedDirections: compact
        && width - 10 < footerMetrics.advanceWidth(directionText(0)) + footerMetrics.advanceWidth(directionText(1)) + 6
    FontMetrics { id: footerMetrics; font: root.footerLabelFont }

    WidgetTitle {
        id: title
        objectName: "peersWidgetTitle"
        width: parent.width
        //: Title of the dashboard widget showing connections to other Bitcoin nodes.
        text: qsTr("Peers")
    }
    Item {
        id: body
        anchors.top: title.bottom
        anchors.topMargin: root.stackedDirections ? 4 : 6
        anchors.bottom: parent.bottom
        width: parent.width
        readonly property real leftWidth: root.compact ? width : (root.expanded ? width * 0.45 : width * 0.6 - 12)
        readonly property real metricHeight: Math.max(0, height - footer.height - (root.stackedDirections ? 4 : 6))
        ColumnLayout {
            id: metric
            width: body.leftWidth
            height: Math.min(implicitHeight, body.metricHeight)
            y: root.compact ? Math.max(0, (body.metricHeight - height) / 2) : 0
            spacing: 2
            CoreText {
                id: peerTotal
                objectName: "peersWidgetTotal"
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: 0
                text: root.ready ? root.number(root.total) : "—"
                font: root.primaryValueFont
                horizontalAlignment: Text.AlignLeft
                wrap: false
                fontSizeMode: Text.Fit
                minimumPixelSize: root.scaledPixelSize(root.secondaryValueStyle)
            }
            CoreText {
                objectName: "peersWidgetTotalLabel"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                Layout.minimumHeight: 0
                Layout.preferredHeight: root.stackedDirections ? footerMetrics.height : implicitHeight
                //: Label below the total number of connected Bitcoin nodes in the Peers widget.
                text: qsTr("Connected peers")
                font: root.primaryLabelFont
                color: Theme.color.neutral6
                horizontalAlignment: Text.AlignLeft
                wrap: false
                fontSizeMode: Text.Fit
                minimumPixelSize: root.footerLabelStyle.pixelSize
            }
        }
        WidgetSection {
            id: footer
            objectName: "peersWidgetDirectionSection"
            anchors.bottom: parent.bottom
            width: root.expanded ? body.leftWidth : body.width
            padding: root.stackedDirections ? 3 : root.compact ? 5 : root.expanded ? 12 : 8
            contentItem: ColumnLayout {
                spacing: root.stackedDirections ? 3 : root.expanded ? 6 : 5
                Item {
                    id: directions
                    objectName: "peersWidgetDirections"
                    Layout.fillWidth: true
                    implicitHeight: root.expanded ? detailedDirections.implicitHeight : compactDirections.implicitHeight
                    GridLayout {
                        id: compactDirections
                        visible: !root.expanded
                        anchors.fill: parent
                        columns: root.stackedDirections ? 1 : 2
                        rowSpacing: 0
                        columnSpacing: 6
                        Repeater {
                            model: 2
                            CoreText {
                                required property int index
                                objectName: root.expanded ? "" : root.compact
                                    ? (index === 0 ? "peersWidgetCompactInbound" : "peersWidgetCompactOutbound")
                                    : (index === 0 ? "peersWidgetInbound" : "peersWidgetOutbound")
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                text: root.directionText(index)
                                font: root.footerLabelFont
                                color: index === 0 ? Theme.color.green : Theme.color.blue
                                horizontalAlignment: index === 0 ? Text.AlignLeft : Text.AlignRight
                                wrap: false
                                fontSizeMode: Text.Fit
                                minimumPixelSize: root.footerLabelStyle.pixelSize
                            }
                        }
                    }
                    GridLayout {
                        id: detailedDirections
                        visible: root.expanded
                        width: parent.width
                        columns: 1
                        rowSpacing: 8
                        columnSpacing: 16
                        Repeater {
                            model: 2
                            RowLayout {
                                required property int index
                                readonly property color directionColor: index === 0 ? Theme.color.green : Theme.color.blue
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                spacing: 6
                                CoreText {
                                    objectName: index === 0 ? "peersWidgetInboundLabel" : "peersWidgetOutboundLabel"
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: 0
                                    //: Direction of connections in the Peers widget: other nodes connect to us, or we connect to other nodes.
                                    text: index === 0 ? qsTr("Inbound") : qsTr("Outbound")
                                    font: root.footerLabelFont
                                    color: parent.directionColor
                                    horizontalAlignment: Text.AlignLeft
                                    wrap: false
                                    elide: Text.ElideRight
                                }
                                CoreText {
                                    objectName: root.expanded ? (index === 0 ? "peersWidgetInbound" : "peersWidgetOutbound") : ""
                                    text: root.ready ? root.number(index === 0 ? root.inbound : root.outbound) : "—"
                                    font: root.expanded ? root.secondaryValueFont : root.footerValueFont
                                    color: parent.directionColor
                                    horizontalAlignment: Text.AlignRight
                                    wrap: false
                                }
                            }
                        }
                    }
                }
                Rectangle {
                    id: split
                    objectName: "peersWidgetSplit"
                    Layout.fillWidth: true
                    Layout.preferredHeight: 4 * root.fontScale
                    radius: height / 2
                    color: Theme.color.neutral2
                    clip: true
                    Rectangle {
                        anchors.fill: parent
                        visible: root.ready && root.total > 0
                        color: Theme.color.blue
                    }
                    Rectangle { width: parent.width * root.inboundFraction; height: parent.height; color: Theme.color.green }
                }
            }
        }
        Item {
            id: types
            objectName: "peersWidgetTypes"
            visible: !root.compact
            anchors.right: parent.right
            width: root.expanded ? Math.max(0, body.width - body.leftWidth - 20) : body.width * 0.4
            anchors.top: parent.top
            height: root.expanded ? body.height : body.metricHeight
            Item {
                id: typesColumn
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.top: root.expanded ? parent.top : undefined
                anchors.verticalCenter: root.expanded ? undefined : parent.verticalCenter
                width: parent.width
                height: root.expanded ? parent.height : legendHeight + statusHeight + statusSpacing
                readonly property real chartSpacing: 16
                readonly property real legendSpacing: 12
                readonly property real statusHeight: typesStatus.visible ? typesStatus.implicitHeight : 0
                readonly property real statusSpacing: typesStatus.visible ? 6 : 0
                readonly property real legendHeight: legend.implicitHeight
                CoreText {
                    id: typesTitle
                    objectName: "peersWidgetTypesTitle"
                    visible: root.expanded
                    anchors.top: parent.top
                    width: parent.width
                    //: Title above the donut chart of peer network types in the large Peers widget.
                    text: qsTr("Connection types")
                    font: root.footerLabelFont
                    color: Theme.color.neutral7
                    wrap: false
                    elide: Text.ElideRight
                }
                Item {
                    id: chartArea
                    visible: root.expanded
                    anchors.top: typesTitle.bottom
                    anchors.topMargin: typesColumn.chartSpacing
                    width: parent.width
                    // Reserve room for every label before sizing the chart. The
                    // square is independent of the layout's implicit dimensions.
                    height: Math.max(0, Math.min(root.height * 0.4, width,
                        types.height - typesTitle.height - typesColumn.chartSpacing
                        - typesColumn.legendSpacing - typesColumn.legendHeight
                        - typesColumn.statusHeight - typesColumn.statusSpacing))
                    PieChart {
                        id: chart
                        objectName: "peersWidgetChart"
                        anchors.centerIn: parent
                        width: parent.height
                        height: width
                        style: PieChart.Donut
                        slices: root.groups
                        active: root.renderingActive && root.expanded
                    }
                }
                GridLayout {
                    id: legend
                    objectName: "peersWidgetLegend"
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.top: root.expanded ? chartArea.bottom : parent.top
                    anchors.topMargin: root.expanded ? typesColumn.legendSpacing : 0
                    width: root.expanded ? chart.width : types.width
                    columns: root.expanded ? (root.groups.length > 4 ? 2 : 1)
                        : (root.groups.length > 4 ? 3 : root.groups.length > 2 ? 2 : 1)
                    rowSpacing: 2
                    columnSpacing: root.expanded ? 10 : 6
                    Repeater {
                        model: root.groups
                        RowLayout {
                            required property var modelData
                            objectName: "peersWidgetGroup_" + modelData.id
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            Layout.preferredWidth: (legend.width - (legend.columns - 1) * legend.columnSpacing) / legend.columns
                            spacing: root.expanded ? 5 : 3
                            Rectangle {
                                objectName: "peersWidgetGroupDot_" + modelData.id
                                visible: root.expanded || modelData.id !== "ipv4"
                                Layout.preferredWidth: root.expanded ? 6 : 4
                                Layout.preferredHeight: width
                                radius: width / 2
                                color: modelData.color
                            }
                            CoreText {
                                objectName: "peersWidgetGroupLabel_" + modelData.id
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                text: modelData.label
                                font: root.expanded ? root.footerLabelFont : root.primaryLabelFont
                                color: Theme.color.neutral7
                                horizontalAlignment: Text.AlignLeft
                                wrap: false
                                elide: Text.ElideRight
                            }
                            CoreText {
                                objectName: "peersWidgetGroupCount_" + modelData.id
                                text: root.number(modelData.value)
                                font: root.expanded ? root.footerValueFont : root.secondaryValueFont
                                wrap: false
                            }
                        }
                    }
                }
                CoreText {
                    id: typesStatus
                    objectName: "peersWidgetStatus"
                    visible: !root.ready || root.total === 0
                    anchors.top: legend.bottom
                    anchors.topMargin: 6
                    width: parent.width
                    //: Status in the Peers widget chart: zero connected peers, or no statistics from the node.
                    text: root.ready ? qsTr("No connections") : qsTr("Unavailable")
                    font: root.footerLabelFont
                    color: Theme.color.neutral6
                    wrap: false
                    elide: Text.ElideRight
                }
            }
        }
    }
}
